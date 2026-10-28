#!/usr/bin/env python3
"""Bounded real StateAFL network campaign, preserving all output as evidence."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import signal
import shutil
import socket
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


class State(ctypes.Structure):
    _fields_ = [("seq_len", ctypes.c_uint), ("seq", ctypes.c_uint * 100),
                ("iterations", ctypes.c_uint)]


def check_stream_framing(server, work, environment, disable_aslr):
    """The same TCP byte stream must produce identical replies and SHM states."""
    evidence = work / "stream-framing"
    evidence.mkdir()
    libc = ctypes.CDLL(None, use_errno=True)
    libc.shmget.argtypes = [ctypes.c_int, ctypes.c_size_t, ctypes.c_int]
    libc.shmget.restype = ctypes.c_int
    libc.shmat.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    libc.shmat.restype = ctypes.c_void_p
    libc.shmdt.argtypes = [ctypes.c_void_p]
    libc.shmctl.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_void_p]
    ident = libc.shmget(0, ctypes.sizeof(State), 0o1000 | 0o600)
    if ident < 0:
        raise OSError(ctypes.get_errno(), "framing SHM allocation failed")
    address = libc.shmat(ident, None, 0)
    if address == ctypes.c_void_p(-1).value:
        libc.shmctl(ident, 0, None)
        raise OSError(ctypes.get_errno(), "framing SHM attachment failed")
    state = State.from_address(address)
    active = environment.copy()
    for key in ("__AFL_SHM_ID", "MVP_CALIBRATION_SHM_ID", "MVP_RADIUS"):
        active.pop(key, None)
    active.update(STATE_SHM_ID=str(ident), AFL_OUTDIR=str(evidence))
    results = []
    try:
        for mode, chunks in (("coalesced", [b"A\nB\nC\n"]),
                             ("fragmented", [bytes([value]) for value in b"A\nB\nC\n"])):
            ctypes.memset(address, 0, ctypes.sizeof(State))
            state.seq_len, state.seq[0] = 1, 0
            with socket.socket() as probe:
                probe.bind(("127.0.0.1", 0))
                port = probe.getsockname()[1]
            command = [str(server), str(port)]
            if disable_aslr:
                command = ["setarch", os.uname().machine, "-R"] + command
            with (evidence / (mode + ".log")).open("w") as log:
                child = subprocess.Popen(command, env=active, cwd=evidence,
                                         stdout=log, stderr=subprocess.STDOUT)
                try:
                    for attempt in range(200):
                        try:
                            connection = socket.create_connection(("127.0.0.1", port), timeout=5)
                            break
                        except ConnectionRefusedError:
                            if child.poll() is not None:
                                raise AssertionError(f"{mode} server exited before accepting a connection")
                            time.sleep(.01)
                    else:
                        raise AssertionError(f"{mode} server did not start")
                    with connection:
                        for chunk in chunks:
                            connection.sendall(chunk)
                            if mode == "fragmented":
                                time.sleep(.005)
                        connection.shutdown(socket.SHUT_WR)
                        reply = b""
                        while True:
                            data = connection.recv(256)
                            if not data:
                                break
                            reply += data
                    status = child.wait(timeout=15)
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait()
            sequence = list(state.seq[:state.seq_len])
            result = {"mode": mode, "command": command, "reply_hex": reply.hex(),
                      "sequence": sequence, "iterations": state.iterations,
                      "exit_code": status}
            results.append(result)
            (evidence / "result.json").write_text(json.dumps(results, indent=2) + "\n")
            assert status == 0, result
            assert len(reply) == 12 and [reply[i:i+3] for i in (0, 4, 8)] == [b"OK1", b"OK2", b"OK4"], result
            assert state.iterations == 3 and state.seq_len == 4 and sequence[0] == 0, result
            assert all(value > 0 for value in sequence[1:]), result
        assert results[0]["reply_hex"] == results[1]["reply_hex"], results
        assert results[0]["sequence"] == results[1]["sequence"], results
        return {"passed": True, "runs": results, "evidence_directory": str(evidence)}
    finally:
        libc.shmdt(address)
        libc.shmctl(ident, 0, None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=45)
    parser.add_argument("--server", type=Path)
    args = parser.parse_args()
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=True)
    corpus = work / "in"
    corpus.mkdir(exist_ok=True)
    for name, messages in {"abc": [b"A\n", b"B\n", b"C\n"],
                           "bac": [b"B\n", b"A\n", b"C\n"]}.items():
        (corpus / name).write_bytes(b"".join(struct.pack("<I", len(m)) + m
                                              for m in messages))
    server = args.server.resolve() if args.server else work / "network_server"
    env = os.environ.copy()
    env.update(AFL_PATH=str(ROOT), AFL_NO_UI="1", AFL_NO_AFFINITY="1",
               AFL_SKIP_CPUFREQ="1", AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES="1")
    commands = []
    if not args.server:
        build = [str(ROOT / "afl-clang-fast"), "-O2", "-g",
                 str(ROOT / "tests/llvm18/network_server.c"), "-o", str(server)]
        commands.append(build)
        with (work / "server-build.log").open("w") as log:
            subprocess.run(build, env=env, stdout=log, stderr=subprocess.STDOUT,
                           check=True, cwd=ROOT)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    output = work / "out"
    if output.exists():
        raise SystemExit(f"Refusing to overwrite previous campaign: {output}")
    aslr = Path("/proc/sys/kernel/randomize_va_space")
    disable_aslr = aslr.exists() and aslr.read_text().strip() != "0"
    framing = check_stream_framing(server, work, env, disable_aslr)
    command = [str(ROOT / "afl-fuzz"), "-d", "-i", str(corpus), "-o", str(output),
               "-N", f"tcp://127.0.0.1/{port}", "-D", "10000", "-W", "20",
               "-r", "20", "-w", "10000", "-E", "-R", "-q", "3", "-s", "3",
               "-m", "none", "-t", "1000", "--", str(server), str(port)]
    if disable_aslr:
        # Disable ASLR for this campaign and its children without changing a
        # system-wide kernel setting. StateAFL checks this effective personality.
        command = ["setarch", os.uname().machine, "-R"] + command
    commands.append(command)
    (work / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
    started = time.monotonic()
    with (work / "fuzz.log").open("w") as log:
        proc = subprocess.Popen(command, env=env, stdout=log,
                                stderr=subprocess.STDOUT, cwd=work,
                                start_new_session=True)
        timed = False
        try:
            status = proc.wait(timeout=args.seconds)
        except subprocess.TimeoutExpired:
            timed = True
            proc.send_signal(signal.SIGINT)
            try:
                status = proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                status = proc.wait()
    stats = {}
    stats_file = output / "fuzzer_stats"
    if stats_file.exists():
        for line in stats_file.read_text().splitlines():
            if ":" in line:
                key, value = line.split(":", 1)
                stats[key.strip()] = value.strip()
    queue_count = len(list((output / "queue").glob("id*")))
    replay_count = len(list((output / "replayable-queue").glob("id*")))
    paths = list((output / "replayable-new-ipsm-paths").glob("id*"))
    state_sequences = []
    for path in paths:
        # Original state_sequence_to_string may leave one trailing '-' when it
        # elides a final third repeated state; that is valid original output.
        encoded = path.name.split(":", 2)[1].removesuffix("-")
        if re.fullmatch(r"[0-9]+(?:-[0-9]+)*", encoded):
            state_sequences.append([int(n) for n in encoded.split("-")])
    state_ids = {n for seq in state_sequences for n in seq}
    dot = output / "ipsm.dot"
    dot_text = dot.read_text() if dot.exists() else ""
    dot_valid = False
    if dot_text:
        if shutil.which("dot"):
            checked = subprocess.run(["dot", "-Tdot", str(dot), "-o", str(work / "ipsm-parsed.dot")],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            (work / "dot-validate.log").write_text(checked.stderr)
            dot_valid = checked.returncode == 0
        else:
            # libgraphviz-dev supplies the parser even without the optional CLI.
            graphviz = ctypes.CDLL("libcgraph.so")
            graphviz.agmemread.argtypes = [ctypes.c_char_p]
            graphviz.agmemread.restype = ctypes.c_void_p
            graphviz.agclose.argtypes = [ctypes.c_void_p]
            graph = graphviz.agmemread(dot_text.encode())
            dot_valid = bool(graph)
            if graph:
                graphviz.agclose(graph)
            (work / "dot-validate.log").write_text(
                f"Graphviz agmemread parsed {len(dot_text)} bytes: {dot_valid}\n")
    tlsh = output / "tlsh.log"
    radius_lines = tlsh.read_text().splitlines() if tlsh.exists() else []
    valid_radii = bool(radius_lines) and all(re.fullmatch(r"[0-9]+", line.strip()) and
                                           int(line.strip()) > 0 for line in radius_lines)
    graph_edges = {(int(a), int(b)) for a, b in re.findall(
        r"(?m)^\s*(\d+)\s*->\s*(\d+)\b", dot_text)}
    feedback_edges = {(a, b) for sequence in state_sequences
                      for a, b in zip(sequence, sequence[1:])}
    fuzz_log = (work / "fuzz.log").read_text()
    fuzzer_personality_confirmed = "ASLR disabled for this process (ADDR_NO_RANDOMIZE)." in fuzz_log
    checks = {
        "stream_framing_and_state_repeatability": framing["passed"],
        "effective_fuzzer_aslr_disabled": not disable_aslr or fuzzer_personality_confirmed,
        "clean_bounded_stop": timed and status == 0,
        "executions_continue": int(stats.get("execs_done", "0")) > 30,
        "coverage_nonzero": float(stats.get("bitmap_cvg", "0").rstrip("%")) > 0,
        "queue_expands": queue_count > 2,
        "queue_matches_final_statistics": queue_count == int(stats.get("paths_total", "0")),
        "replayable_queue_present": replay_count >= 2,
        "state_paths_present": len(paths) >= 2,
        "initial_state_zero": bool(state_sequences) and all(seq[0] == 0 for seq in state_sequences),
        "multiple_inferred_states": len(state_ids - {0}) >= 2,
        "different_state_sequences": len({tuple(seq) for seq in state_sequences}) >= 2,
        "new_state_path_during_fuzzing": any(path.name.endswith(":new") for path in paths),
        "state_graph_has_edges": "->" in dot_text and dot_valid,
        "state_graph_matches_feedback": bool(feedback_edges) and feedback_edges <= graph_edges,
        "calibration_present": valid_radii,
        "no_target_crashes": int(stats.get("unique_crashes", "0")) == 0,
        "no_target_hangs": int(stats.get("unique_hangs", "0")) == 0,
    }
    report = {"passed": all(checks.values()), "checks": checks,
              "exit_code": status, "time_limited": timed,
              "elapsed_seconds": round(time.monotonic() - started, 2),
              "stats": stats, "queue_count": queue_count,
              "replayable_queue_count": replay_count,
              "state_paths": [p.name for p in paths], "dot_bytes": len(dot_text)}
    report["state_sequences"] = state_sequences
    report["inferred_state_ids"] = sorted(state_ids)
    report["stream_framing"] = framing
    report["calibrated_radii"] = [int(line) for line in radius_lines] if valid_radii else []
    if aslr.exists():
        libc = ctypes.CDLL(None)
        libc.personality.argtypes = [ctypes.c_ulong]
        libc.personality.restype = ctypes.c_int
        report["aslr"] = {"global_sysctl": aslr.read_text().strip(),
                          "launcher_personality": libc.personality(0xffffffff),
                          "fuzzer_personality_confirmed_by_log": fuzzer_personality_confirmed,
                          "fuzzer_evidence": str(work / "fuzz.log")}
    (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
