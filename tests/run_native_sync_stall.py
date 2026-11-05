#!/usr/bin/env python3
"""Exercise real synchronization and stall helpers without network or LLM calls.

Each fixture runs in a separate process and directory. All build artifacts,
compiler temporary files, fixtures, and logs stay under the GPTFuzz workspace.
The default run includes AddressSanitizer and UndefinedBehaviorSanitizer.
"""

import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent.resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "tests/out/native-sync-stall")
    parser.add_argument("--no-sanitizers", action="store_true",
                        help="Run the ordinary native build only")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(WORKSPACE):
        parser.error("--output must remain inside the GPTFuzz workspace")
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    temporary = run / "tmp"
    temporary.mkdir()
    environment = dict(os.environ, TMPDIR=str(temporary), TMP=str(temporary),
                       TEMP=str(temporary))
    compiler = shlex.split(os.environ.get("CC", "clang"))
    common = ["-O1", "-g", "-ffunction-sections", "-fdata-sections",
              "-Wno-pointer-sign", "-Wno-unused-result",
              f'-DAFL_PATH="{ROOT}"', f'-DBIN_PATH="{ROOT}"',
              f'-DDOC_PATH="{ROOT / "docs"}"']
    libraries = ["-ldl", "-lgvc", "-lcgraph", "-lm", "-lcurl", "-ljson-c", "-lpcre2-8"]
    variants = [("native", [])]
    if not args.no_sanitizers:
        variants.append(("sanitized", ["-fsanitize=address,undefined",
                                        "-fno-omit-frame-pointer"]))
    results = {"run_directory": str(run), "variants": {}}
    commands = []
    failures = []

    def build(command, log):
        commands.append(command)
        with log.open("a") as stream:
            stream.write("$ " + shlex.join(command) + "\n")
            stream.flush()
            result = subprocess.run(command, cwd=run, env=environment,
                                    stdout=stream, stderr=subprocess.STDOUT,
                                    timeout=300)
        if result.returncode:
            raise RuntimeError(f"Native test build failed; see {log}")

    for variant, extra_flags in variants:
        directory = run / variant
        directory.mkdir()
        build_log = directory / "build.log"
        objects = []
        for source in [ROOT / "aflnet.c", ROOT / "chat-llm.c",
                       ROOT / "tests/native_sync_stall.c"]:
            obj = directory / (source.stem + ".o")
            build(compiler + common + extra_flags + ["-c", str(source), "-o", str(obj)],
                  build_log)
            objects.append(str(obj))
        binary = directory / "native_sync_stall"
        build(compiler + extra_flags + objects + ["-Wl,--gc-sections",
              "-Wl,--wrap=read", "-Wl,--wrap=chat_with_llm",
              *libraries, "-o", str(binary)], build_log)
        symbols = subprocess.run(["nm", "-a", str(binary)], cwd=run,
                                 env=environment, capture_output=True,
                                 text=True, timeout=15, check=True).stdout
        (directory / "symbols.log").write_text(symbols)
        for excluded in ["discarded_fuzzer_main", "run_target", "send_over_network", "chat_with_llm", "curl_easy_perform"]:
            if any(line.split()[-1:] == [excluded] for line in symbols.splitlines()):
                raise RuntimeError(f"Unneeded execution path retained in test binary: {excluded}")
        test_environment = dict(environment)
        if variant == "sanitized":
            test_environment["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
            test_environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        listing = subprocess.run([str(binary), "--list"], cwd=directory,
                                 env=test_environment, capture_output=True,
                                 text=True, timeout=15, check=True)
        cases = listing.stdout.splitlines()
        if not cases:
            raise RuntimeError("Native harness listed no tests")
        variant_results = []
        for name in cases:
            case_dir = directory / name
            case_dir.mkdir()
            command = [str(binary), name]
            commands.append(command)
            try:
                result = subprocess.run(command, cwd=case_dir, env=test_environment,
                                        capture_output=True, text=True, timeout=15)
                (case_dir / "stdout.log").write_text(result.stdout)
                (case_dir / "stderr.log").write_text(result.stderr)
                success = (result.returncode == 0 and
                           result.stdout.splitlines()[-1:] == [f"PASS {name}"])
                record = {"case": name, "status": "passed" if success else "failed",
                          "returncode": result.returncode}
            except subprocess.TimeoutExpired as error:
                success = False
                record = {"case": name, "status": "timed_out"}
                (case_dir / "stderr.log").write_text(str(error))
            variant_results.append(record)
            if not success:
                failures.append(f"{variant}/{name}")
                print(f"FAIL {variant}/{name}: {case_dir / 'stderr.log'}", flush=True)
        passed = sum(case["status"] == "passed" for case in variant_results)
        results["variants"][variant] = variant_results
        (run / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(f"{variant}: {passed}/{len(cases)} native sync/stall tests passed", flush=True)
    results["failures"] = failures
    (run / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    (run / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
    print(f"Evidence: {run / 'results.json'}", flush=True)
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
