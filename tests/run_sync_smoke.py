#!/usr/bin/env python3
"""Run one bounded local fuzzer against the fixed two-byte regression server.

The peer is a preloaded queue, not another process. AFL_IMPORT_FIRST exercises
the real dry-run -> sync -> save path before the first mutation. This runner
uses neither -P nor -E and makes no LLM requests. All artifacts stay in GPTFuzz.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import signal
import socket
import struct
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent.resolve()
PEER_NAME = "peer"
PEER_CASE = "id:000000,orig:sync-smoke"
MESSAGES = [b"A\n", b"B\n", b"C\n"]


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def snapshot(directory):
    return {str(path.relative_to(directory)): sha256(path)
            for path in sorted(directory.rglob("*")) if path.is_file()}


def pack_replay(messages):
    return b"".join(struct.pack("=I", len(message)) + message
                    for message in messages)


def unpack_replay(wire):
    messages = []
    offset = 0
    while offset < len(wire):
        if len(wire) - offset < 4:
            raise ValueError("Truncated replay header")
        length, = struct.unpack_from("=I", wire, offset)
        offset += 4
        if length > len(wire) - offset:
            raise ValueError("Truncated replay payload")
        messages.append(wire[offset:offset + length])
        offset += length
    return messages


def read_stats(path):
    try:
        lines = path.read_text().splitlines()
    except FileNotFoundError:
        return {}
    return {key.strip(): value.strip()
            for line in lines if ":" in line
            for key, value in [line.split(":", 1)]}


def imported_count(stats):
    try:
        return int(stats.get("paths_imported", "0"))
    except ValueError:
        return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "tests/out/sync-smoke-20261105")
    parser.add_argument("--timeout", type=float, default=45,
                        help="Maximum fuzzer runtime, 1 to 50 seconds (default 45)")
    parser.add_argument("--baseline-commit",
                        help="Label an expected-negative run using the archived baseline fuzzer binary")
    args = parser.parse_args()
    # Apply before any child is spawned, including CLI and compiler processes.
    # A failing fuzzer needs only its recorded status/log, never a core file.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if not 1 <= args.timeout <= 50:
        parser.error("--timeout must be between 1 and 50 seconds")
    if args.baseline_commit and (len(args.baseline_commit) != 40 or
                                any(c not in "0123456789abcdef" for c in args.baseline_commit)):
        parser.error("--baseline-commit must be a full lowercase commit hash")
    output = args.output.resolve()
    if not output.is_relative_to(WORKSPACE):
        parser.error("--output must remain within the GPTFuzz workspace")
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    temporary = run / "tmp"
    temporary.mkdir()
    environment = dict(os.environ, TMPDIR=str(temporary), TMP=str(temporary),
                       TEMP=str(temporary), AFL_PATH=str(ROOT), AFL_NO_UI="1",
                       AFL_NO_AFFINITY="1", AFL_SKIP_CPUFREQ="1",
                       AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES="1",
                       AFL_IMPORT_FIRST="1", GIT_OPTIONAL_LOCKS="0")
    commands = []
    report = {"passed": False, "scope": "single-process local synchronization smoke",
              "run_directory": str(run), "llm_requests": False,
              "protocol_option": False, "state_aware_option": False,
              "checks": {}, "timeout_seconds": args.timeout,
              "child_core_limit_bytes": 0}
    report["environment_overrides"] = {name: environment[name] for name in (
        "TMPDIR", "TMP", "TEMP", "AFL_PATH", "AFL_NO_UI", "AFL_NO_AFFINITY",
        "AFL_SKIP_CPUFREQ", "AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES", "AFL_IMPORT_FIRST")}
    source_names = ["afl-fuzz.c", "aflnet.c", "aflnet.h", "chat-llm.c",
                    "chat-llm.h", "alloc-inl.h", "config.h", "types.h",
                    "tests/llvm18/network_server.c", "tests/run_sync_smoke.py"]
    artifact_names = ["afl-fuzz", "afl-clang-fast", "afl-llvm-pass.so",
                      "afl-llvm-rt.o", "afl-llvm-rt-state-tracer.o",
                      "afl-llvm-rt-state-tracer.o.cpp.o"]

    def record_command(label, command):
        commands.append({"label": label, "argv": command, "cwd": str(run)})
        (run / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")

    def file_hashes(names):
        return {name: sha256(ROOT / name) for name in names}

    process = None
    forced_stop = False
    try:
        report["source_hashes_before"] = file_hashes(source_names)
        report["artifact_hashes_before"] = file_hashes(artifact_names)
        revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                  env=environment, capture_output=True, text=True,
                                  timeout=10, check=True)
        report["checkout_head"] = revision.stdout.strip()

        if args.baseline_commit:
            manifest_path = ROOT / "tests/evidence/recovery-integrity-20261103/build/environment-and-hashes.json"
            manifest = json.loads(manifest_path.read_text())
            committed_source = subprocess.run(
                ["git", "show", f"{args.baseline_commit}:afl-fuzz.c"], cwd=ROOT,
                env=environment, capture_output=True, timeout=10, check=True).stdout
            archived_hash = manifest["binary_sha256"]["afl-fuzz"]
            baseline = {
                "expected_negative_control": True,
                "binary_source_commit": args.baseline_commit,
                "archive_manifest": str(manifest_path.relative_to(ROOT)),
                "archive_manifest_sha256": sha256(manifest_path),
                "archived_fuzzer_sha256": archived_hash,
                "binary_matches_archive": report["artifact_hashes_before"]["afl-fuzz"] == archived_hash,
                "committed_fuzzer_source_matches_archive":
                    hashlib.sha256(committed_source).hexdigest() == manifest["source_sha256"]["afl-fuzz.c"],
                "source_hash_note": "Working-tree hashes below describe uncompiled fixes, not this baseline binary.",
            }
            report["baseline_control"] = baseline
            if not baseline["binary_matches_archive"] or not baseline["committed_fuzzer_source_matches_archive"]:
                raise RuntimeError("Expected baseline binary/source does not match the archived manifest")

        help_command = [str(ROOT / "afl-fuzz"), "--help"]
        record_command("fuzzer-cli", help_command)
        help_result = subprocess.run(help_command, cwd=run, env=environment,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     text=True, timeout=15)
        (run / "cli-help.log").write_text(help_result.stdout)
        report["cli_exit_code"] = help_result.returncode

        corpus = run / "corpus"
        corpus.mkdir()
        (corpus / "initial-A.replay").write_bytes(pack_replay([MESSAGES[0]]))
        shared = run / "sync"
        peer = shared / PEER_NAME
        (peer / "queue").mkdir(parents=True)
        (peer / "replayable-queue").mkdir()
        raw_peer = b"".join(MESSAGES)
        (peer / "queue" / PEER_CASE).write_bytes(raw_peer)
        (peer / "replayable-queue" / PEER_CASE).write_bytes(pack_replay(MESSAGES))
        peer_before = snapshot(peer)
        report["peer_hashes_before"] = peer_before

        server = run / "network_server"
        build_command = [str(ROOT / "afl-clang-fast"), "-O2", "-g",
                         str(ROOT / "tests/llvm18/network_server.c"), "-o", str(server)]
        record_command("build-fixed-local-server", build_command)
        with (run / "server-build.log").open("w") as log:
            built = subprocess.run(build_command, cwd=run, env=environment,
                                   stdout=log, stderr=subprocess.STDOUT, timeout=60)
        report["server_build_exit_code"] = built.returncode
        if built.returncode:
            raise RuntimeError("Local fixture build failed; see server-build.log")
        report["server_sha256"] = sha256(server)

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        report["loopback_port"] = port
        command = ["setarch", os.uname().machine, "-R", str(ROOT / "afl-fuzz"),
                   "-S", "local", "-i", str(corpus), "-I", "replay", "-o", str(shared),
                   "-N", f"tcp://127.0.0.1/{port}", "-D", "10000", "-W", "20",
                   "-r", "20", "-w", "10000", "-m", "none", "-t", "1000",
                   "--", str(server), str(port)]
        record_command("bounded-import-first-fuzzer", command)
        local = shared / "local"
        stats_path = local / "fuzzer_stats"
        started = time.monotonic()
        observed_import = False
        with (run / "fuzzer.log").open("w") as log:
            process = subprocess.Popen(command, cwd=run, env=environment, stdout=log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            deadline = started + args.timeout
            while process.poll() is None and time.monotonic() < deadline:
                if imported_count(read_stats(stats_path)) > 0:
                    observed_import = True
                    break
                time.sleep(0.1)
            if process.poll() is None:
                report["stop_reason"] = "import-observed" if observed_import else "runtime-limit"
                process.send_signal(signal.SIGINT)
            else:
                report["stop_reason"] = "fuzzer-exited-before-stop"
            try:
                status = process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                forced_stop = True
                os.killpg(process.pid, signal.SIGKILL)
                status = process.wait(timeout=5)
        report["elapsed_seconds"] = round(time.monotonic() - started, 2)
        report["fuzzer_exit_code"] = status
        report["forced_stop"] = forced_stop

        stats = read_stats(stats_path)
        imported = sorted(path for path in (local / "queue").glob("id:*")
                          if f"sync:{PEER_NAME},src:000000" in path.name)
        records = []
        for path in imported:
            replay = local / "replayable-queue" / path.name
            replay_messages = unpack_replay(replay.read_bytes()) if replay.is_file() else None
            records.append({"name": path.name, "raw_sha256": sha256(path),
                            "raw_matches_peer": path.read_bytes() == raw_peer,
                            "replay_messages_hex": [message.hex() for message in replay_messages]
                            if replay_messages is not None else None,
                            "replay_matches_peer": replay_messages == MESSAGES})
        checkpoint = local / ".synced" / PEER_NAME
        checkpoint_bytes = checkpoint.read_bytes() if checkpoint.is_file() else b""
        checkpoint_value = (struct.unpack("=I", checkpoint_bytes)[0]
                            if len(checkpoint_bytes) == 4 else None)
        report["stats"] = stats
        report["imported_cases"] = records
        report["checkpoint"] = checkpoint_value
        report["peer_hashes_after"] = snapshot(peer)
        report["source_hashes_after"] = file_hashes(source_names)
        report["artifact_hashes_after"] = file_hashes(artifact_names)
        fuzzer_log = (run / "fuzzer.log").read_text()
        report["checks"] = {
            "clean_bounded_exit": status == 0 and not forced_stop,
            "imported_statistic_positive": imported_count(stats) > 0,
            "exactly_one_peer_import": len(records) == 1,
            "imported_raw_matches_peer": bool(records) and all(record["raw_matches_peer"] for record in records),
            "three_replay_records_match_peer": bool(records) and all(record["replay_matches_peer"] for record in records),
            "sync_checkpoint_one": checkpoint_value == 1,
            "peer_unchanged": report["peer_hashes_after"] == peer_before,
            "effective_aslr_disabled": "ASLR disabled for this process (ADDR_NO_RANDOMIZE)." in fuzzer_log,
            "source_hashes_stable": report["source_hashes_before"] == report["source_hashes_after"],
            "artifact_hashes_stable": report["artifact_hashes_before"] == report["artifact_hashes_after"],
        }
        report["passed"] = all(report["checks"].values())
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
    finally:
        if process is not None:
            if process.poll() is None:
                forced_stop = True
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    report["cleanup_error"] = "Fuzzer did not terminate after SIGKILL"
            # A target/forkserver must not outlive this runner, including on a
            # startup error. The fuzzer has its own isolated process group.
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        report["forced_stop"] = forced_stop
        if forced_stop:
            report["passed"] = False
        (run / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        (run / "source-hashes.json").write_text(json.dumps({
            "checkout_head": report.get("checkout_head"),
            "sources": report.get("source_hashes_before", {}),
            "artifacts": report.get("artifact_hashes_before", {}),
            "local_server": report.get("server_sha256"),
        }, indent=2) + "\n")
    print(json.dumps({"passed": report["passed"], "checks": report["checks"],
                      "error": report.get("error"), "results": str(run / "results.json")},
                     indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
