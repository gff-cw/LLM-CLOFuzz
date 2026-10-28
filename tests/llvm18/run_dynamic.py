#!/usr/bin/env python3
"""Execute real pass-instrumented I/O and C++ operations against debug runtime."""
import os
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    out = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "tests/llvm18/out/dynamic"
    out.mkdir(parents=True, exist_ok=True)
    cc = shlex.split(os.environ.get("CC", "clang"))
    cxx = shlex.split(os.environ.get("CXX", "clang++"))
    wrapper = ROOT / "afl-clang-fast"
    build_log = out / "build.log"

    def command(args, extra_env=None):
        with build_log.open("a") as log:
            log.write("$ " + shlex.join(map(str, args)) + "\n")
            log.flush()
            environment = dict(os.environ, **(extra_env or {}))
            subprocess.run(list(map(str, args)), cwd=ROOT, stdout=log, env=environment,
                           stderr=subprocess.STDOUT, check=True, timeout=60)

    command(cc + ["-O1", "-g", "-DDEBUG_LOG_LEVEL", "-fPIC", "-c",
                  ROOT / "llvm_mode/afl-llvm-rt-state-tracer.o.c", "-o", out / "tracer.o"])
    libs = [ROOT / "llvm_mode/.build/containers.a", ROOT / "llvm_mode/.build/libtlsh.a",
            ROOT / "llvm_mode/.build/libmvptree.a"]
    results = {}
    for name, source, compiler, entry in [
        ("io", "io_fixture.c", cc, wrapper),
        ("cpp", "cpp_fixture.cpp", cxx, ROOT / "afl-clang-fast++"),
        ("memory", "memory_fixture.c", cc, wrapper),
        ("custom", "custom_fixture.c", cc, wrapper)
    ]:
        custom = {"TRACE_CUSTOM_RECEIVE": "custom_receive:1",
                  "TRACE_CUSTOM_SEND": "custom_send:1"} if name == "custom" else {}
        command([entry, "-O1", "-g", "-c", ROOT / "tests/llvm18" / source,
                 "-o", out / (name + ".o")], custom)
        command(compiler + [out / (name + ".o"), out / "tracer.o",
                            ROOT / "afl-llvm-rt.o", *libs, "-lstdc++", "-lm",
                            "-o", out / name])
        evidence = Path(tempfile.mkdtemp(prefix=name + "-logs-", dir=out))
        env = dict(os.environ, AFL_OUTDIR=str(evidence))
        result = subprocess.run([out / name], env=env, capture_output=True,
                                text=True, timeout=15)
        (evidence / "stdout.log").write_text(result.stdout)
        (evidence / "stderr.log").write_text(result.stderr)
        assert result.returncode == 0, (name, result.returncode, result.stderr)
        runtime = (evidence / "state-tracer-rt.log").read_text()
        if name == "io":
            expected = ["recv", "send", "recvfrom", "sendto", "recvmsg", "sendmsg",
                        "read", "write", "fgets", "fprintf", "fread", "fwrite",
                        "fclose", "plain-file", "close"]
            assert result.stdout.splitlines() == expected, result.stdout
            for event in ["TRACE READ", "TRACE WRITE", "TRACE FGETS", "TRACE FPRINTF",
                          "TRACE FREAD", "TRACE FWRITE", "TRACE FCLOSE", "TRACE CLOSE",
                          "FD IS NOT SOCKET", "TLSH HASH:", "TOTAL STATES:"]:
                assert event in runtime, event
            # Both message helpers must use the target msghdr size, not payload.
            assert len(re.findall(r"TRACE (?:RECV|SEND).*size = 56\)", runtime)) == 2
            for size in [32, 7, 48, 9, 16, 13, 6, 15]:
                assert re.search(r"TRACE (?:RECV|SEND).*size = " + str(size) + r"\)", runtime), size
            assert "SKIPPING IGNORED AREA INSIDE AREA" in runtime
        elif name == "cpp":
            allocations = re.findall(r"NEW HEAP ALLOC: (0x[0-9a-f]+) \((\d+) bytes\)", runtime)
            releases = re.findall(r"FREE HEAP ALLOC: (0x[0-9a-f]+)", runtime)
            assert sorted(size for _, size in allocations) == ["256", "256", "512"], allocations
            assert sorted(address for address, _ in allocations) == sorted(releases), (allocations, releases)
        elif name == "memory":
            assert "(256 bytes)" in runtime and "(3 elems, 768 bytes)" in runtime
            assert "TRACE REALLOC" in runtime
            assert runtime.count("FREE HEAP ALLOC:") == 2
        else:
            events = re.findall(r"TRACE (RECV|SEND) \(buf = (0x[0-9a-f]+), size = (\d+)\)", runtime)
            assert [(kind, size) for kind, _, size in events] == [("RECV", "256"), ("SEND", "256")], events
            assert "DETECTED I/O BUF AREA" in runtime and "TOTAL STATES: 2" in runtime
        results[name] = {"status": "passed", "evidence_directory": str(evidence),
                         "operations": result.stdout.splitlines()}
        print(name + ": real instrumented operations and runtime log assertions passed")
    (out / "results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
