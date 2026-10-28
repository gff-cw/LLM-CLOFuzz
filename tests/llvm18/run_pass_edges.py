#!/usr/bin/env python3
"""Additional real LLVM18 regressions: target ABI, opaque types and O0 lifetime."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/llvm18"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--opt", default=shutil.which("opt") or "opt")
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    base_env = os.environ.copy()
    for name in ("TRACE_CUSTOM_RECEIVE", "TRACE_CUSTOM_SEND", "AFL_INST_RATIO"):
        base_env.pop(name, None)
    base_env.update(AFL_PATH=str(ROOT), AFL_DONT_OPTIMIZE="1")
    wrapper = str(ROOT / "afl-clang-fast")
    commands, cases = [], []

    def run(name, command, extra_env=None, diagnostic=None):
        env = base_env | (extra_env or {})
        commands.append({"name": name, "argv": list(map(str, command)),
                         "environment": extra_env or {}})
        result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=60)
        (output / (name + ".log")).write_text(result.stdout + result.stderr)
        if diagnostic:
            assert result.returncode != 0 and diagnostic in result.stdout + result.stderr, (
                name, result.returncode, "expected diagnostic", diagnostic)
        else:
            assert result.returncode == 0, (name, result.returncode)
        return result

    def compile_ir(name, source, flags=(), env=None, diagnostic=None):
        target = output / (name + ".ll")
        run(name, [wrapper, *flags, "-S", "-emit-llvm", "-c", source, "-o", target],
            env, diagnostic)
        if diagnostic:
            return None
        run(name + "-verify", [args.opt, "-passes=verify", "-disable-output", target])
        return target.read_text()

    def case(name, action):
        try:
            evidence = action()
            cases.append({"name": name, "passed": True, "evidence": evidence})
        except (AssertionError, OSError, subprocess.TimeoutExpired) as error:
            cases.append({"name": name, "passed": False, "error": str(error)})

    def debug_recovery():
        ir = compile_ir("custom-caller-debug", FIXTURES / "pass_edges.c", ["-O0", "-g"],
                        {"TRACE_CUSTOM_RECEIVE": "edge_external:0;edge_body:0"})
        sizes = re.findall(r"call void @trace_receive\(ptr[^\n]*, i64 (\d+)\)", ir)
        assert sizes == ["260"] * 5, sizes
        return {"recovered_buffer_sizes": sizes}
    case("caller-and-debug-type-recovery", debug_recovery)

    def explicit_and_legacy():
        ir = compile_ir("opaque-explicit", FIXTURES / "custom_opaque_edges.c",
                        ["-O0", "-g0"], {"TRACE_CUSTOM_RECEIVE": "edge_opaque:0:256"})
        assert re.search(r"call void @trace_receive\(ptr[^\n]*, i64 256\)", ir)
        ir = compile_ir("legacy-minus-one", FIXTURES / "custom_opaque_edges.c",
                        ["-O0", "-g0"], {"TRACE_CUSTOM_RECEIVE": "edge_opaque:-1"})
        assert "call void @trace_receive(ptr null, i64 0)" in ir
        return {"explicit_bytes": 256, "legacy_event_without_buffer": True}
    case("explicit-size-and-original-minus-one", explicit_and_legacy)

    for name, flags, entry, diagnostic in (
        ("pointer-to-pointer", ["-g", "-DDOUBLE_POINTER"], "edge_opaque:0", "Cannot recover struct type"),
        ("non-pointer", ["-g0", "-DNON_POINTER"], "edge_opaque:0", "is not a pointer"),
        ("invalid-index-text", ["-g0"], "edge_opaque:garbage", "Invalid custom trace argument"),
        ("size-overflow-32", ["-g0", "-m32"], "edge_opaque:0:4294967296", "exceeds target size_t"),
    ):
        def diagnostic_action(name=name, flags=flags, entry=entry, diagnostic=diagnostic):
            compile_ir(name, FIXTURES / "custom_opaque_edges.c", ["-O0", *flags],
                       {"TRACE_CUSTOM_RECEIVE": entry}, diagnostic)
            return {"expected_diagnostic": diagnostic}
        case(name, diagnostic_action)

    def original_stubs():
        ir = compile_ir("original-int-stubs", ROOT / "llvm_mode/test-state-tracer.c", ["-O0", "-g0"])
        # LLVM opaque pointer verification alone allows differing call signatures;
        # explicitly check both call and declaration use the actual runtime ABI.
        assert "declare void @trace_receive(ptr, i64)" in ir
        assert re.search(r"call void @trace_receive\(ptr[^\n]*, i64 100\)", ir)
        assert "call void @trace_send(ptr null, i64 0)" in ir
        return {"original_int_count_widened_to_i64": True}
    case("original-int-sized-recv-send-stubs", original_stubs)

    def target32():
        ir = compile_ir("abi32", FIXTURES / "abi_edges.c", ["-O1", "-g0", "-m32"])
        assert "declare void @new_heap_alloc_record(ptr, i64)" in ir
        assert "declare void @trace_receive(ptr, i32)" in ir
        assert "declare void @trace_calloc(ptr, i32, i32)" in ir
        return {"target_size_t_bits": 32, "heap_record_size_bits": 64,
                "scope": "IR only; does not claim a 32-bit state runtime is linked"}
    case("target32-helper-widths", target32)

    def too_wide():
        compile_ir("abi-too-wide", FIXTURES / "abi_edges.c",
                   ["-O0", "-g0", "-DCOUNT_TYPE=unsigned __int128"],
                   diagnostic="Unsupported argument layout")
        return {"oversized_byte_count_rejected": True}
    case("oversized-count-diagnostic", too_wide)

    def lifetime():
        target = output / "lifetime-O0.ll"
        run("lifetime-O0", [args.opt, "-load-pass-plugin", ROOT / "afl-llvm-pass.so",
                           "-passes=stateafl,stateafl,default<O0>,verify", "-verify-each",
                           "-S", FIXTURES / "lifetime_edges.ll", "-o", target])
        program = output / "lifetime-observer"
        run("lifetime-build", [*shlex.split(args.cc), "-O0", target,
                               FIXTURES / "lifetime_observer.c", "-o", program])
        run("lifetime-execute", [program])
        return {"threshold_bytes": 64, "executed_start_hooks": 1,
                "executed_end_hooks": 1, "explicit_passes": 2}
    case("O0-explicit-lifetime-threshold-and-idempotency", lifetime)

    def ratio_independence():
        state_calls = []
        for ratio in (1, 100):
            ir = compile_ir("ratio" + str(ratio), FIXTURES / "ir_fixture.c", ["-O0", "-g0"],
                            {"AFL_INST_RATIO": str(ratio)})
            state_calls.append(re.findall(r"call void @((?:trace_|new_|free_)[\w]+)\([^\n]*", ir))
        # Random coverage IDs and block instrumentation must never drop state events.
        assert state_calls[0] and state_calls[0] == state_calls[1], state_calls
        return {"state_calls_per_ratio": len(state_calls[0]), "ratios": [1, 100]}
    case("coverage-ratio-keeps-state-events", ratio_independence)

    report = {"passed": all(item["passed"] for item in cases), "cases": cases}
    (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
