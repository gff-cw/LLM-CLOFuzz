#!/usr/bin/env python3
"""Compile real wrapper IR at O0-O3, with/without debug, and verify using opt18."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HELPERS = {"new_heap_alloc_record", "trace_calloc", "trace_realloc",
           "free_heap_alloc_record", "trace_receive", "trace_send", "trace_recvmsg",
           "trace_sendmsg", "trace_read",
           "trace_write", "trace_fprintf", "trace_fgets", "trace_fread",
           "trace_fwrite", "trace_close", "trace_fclose", "end_state_tracer"}


def run(command, log, env):
    result = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    log.write_text(result.stdout)
    if result.returncode:
        raise AssertionError(f"command failed ({result.returncode}), see {log}")


def calls(ir, name):
    return len(re.findall(r"\bcall\b[^\n]*@" + re.escape(name) + r"\(", ir))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--opt", default=shutil.which("opt") or "opt")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["AFL_PATH"] = str(ROOT)
    # Original syntax for recoverable local/global/GEP pointers and event-only calls.
    # Explicit size is the optional extension for opaque external return values.
    for direction in ("RECEIVE", "SEND"):
        prefix = "custom_" + direction.lower()
        env["TRACE_CUSTOM_" + direction] = ";".join(
            [f"{prefix}_{kind}:0" for kind in ("local", "global", "formal")] +
            [f"{prefix}_external:0:96", f"{prefix}_nobuf"])
    commands = []
    cases = []
    for language, wrapper, source in (("c", "afl-clang-fast", "ir_fixture.c"),
                                       ("cpp", "afl-clang-fast++", "ir_fixture.cpp")):
        for level in range(4):
            for debug in (False, True):
                stem = f"{language}-O{level}-{'debug' if debug else 'nodebug'}"
                target = output / (stem + ".ll")
                command = [str(ROOT / wrapper), f"-O{level}", "-g" if debug else "-g0",
                           "-fno-builtin", "-S", "-emit-llvm", "-c",
                           str(ROOT / "tests/llvm18" / source), "-o", str(target)]
                commands.append(command)
                case = {"name": stem, "checks": {}, "passed": False}
                try:
                    compile_env = env.copy()
                    for direction in ("RECEIVE", "SEND"):
                        compile_env["TRACE_CUSTOM_" + direction] += (
                            ";custom_" + direction.lower() + "_debug:0" +
                            ("" if debug else ":96"))
                    case["custom_environment"] = {key: compile_env[key] for key in
                                                  ("TRACE_CUSTOM_RECEIVE", "TRACE_CUSTOM_SEND")}
                    driver_command = command + ["-###"]
                    commands.append(driver_command)
                    driver_log = output / (stem + ".driver.log")
                    run(driver_command, driver_log, compile_env)
                    driver = driver_log.read_text()
                    run(command, output / (stem + ".compile.log"), compile_env)
                    ir = target.read_text()
                    verify = [args.opt, "-passes=verify", "-disable-output", str(target)]
                    commands.append(verify)
                    run(verify, output / (stem + ".verify.log"), env)
                    checks = {"ir_verified": True,
                              "coverage_globals": "@__afl_area_ptr" in ir and "@__afl_prev_loc" in ir,
                              "coverage_increment": bool(re.search(r"add i8", ir)),
                              "debug_information_matches_request": ("!DICompileUnit" in ir) == debug,
                              "optnone_matches_request": ("optnone" in ir) == (level == 0),
                              "driver_optimization_matches_request": re.findall(r'"-O[0-3]"', driver) == [f'"-O{level}"'],
                              "clang18_driver": bool(re.search(r"\bclang version 18\.", driver)),
                              "new_pm_plugin_option": "-fpass-plugin=" in driver}
                    expected = HELPERS if language == "c" else {
                        "new_heap_alloc_record", "free_heap_alloc_record"}
                    for helper in sorted(expected):
                        checks["calls_" + helper] = calls(ir, helper) > 0
                    # Clang emits lifetime markers in optimized IR. O0 is checked
                    # separately because Clang itself normally omits those markers.
                    if language == "c" and level:
                        checks["stack_start"] = calls(ir, "new_stack_alloc_record") > 0
                        checks["stack_end"] = calls(ir, "free_stack_alloc_record") > 0
                        checks["stack_large_tracked"] = bool(re.search(
                            r"\bcall\b[^\n]*@new_stack_alloc_record\([^\n]*, i(?:32|64) 128\)", ir))
                        checks["stack_small_excluded"] = not re.search(
                            r"\bcall\b[^\n]*@new_stack_alloc_record\([^\n]*, i(?:32|64) 32\)", ir)
                    if language == "c":
                        # Twelve custom hooks plus recv/recvfrom and send/sendto.
                        # msghdr has a separate native-ABI helper in each direction.
                        checks["receive_hook_count"] = calls(ir, "trace_receive") == 8
                        checks["send_hook_count"] = calls(ir, "trace_send") == 8
                        checks["recvmsg_hook_count"] = calls(ir, "trace_recvmsg") == 1
                        checks["sendmsg_hook_count"] = calls(ir, "trace_sendmsg") == 1
                        # No size-0 placeholder may be used for a typed custom buffer.
                        checks["custom_buffers_recovered"] = len(re.findall(
                            r"\bcall\b[^\n]*@trace_(?:receive|send)\(ptr[^\n]*, i(?:32|64) 96\)", ir)) == 10
                    checks["single_coverage_globals"] = len(re.findall(
                        r"^@__afl_area_ptr\s*=", ir, re.M)) == 1
                    case["checks"] = checks
                    case["helper_counts"] = {h: calls(ir, h) for h in sorted(expected)}
                    case["passed"] = all(checks.values())
                except (AssertionError, OSError) as error:
                    case["error"] = str(error)
                cases.append(case)
    # Re-loading a plugin or feeding already instrumented IR into a subsequent
    # build must not duplicate events. Run verification after every explicit pass.
    duplicate = {"name": "explicit-plugin-idempotency", "passed": False}
    try:
        original = output / "c-O1-nodebug.ll"
        target = output / "c-O1-twice.ll"
        command = [args.opt, "-load-pass-plugin", str(ROOT / "afl-llvm-pass.so"),
                   "-passes=stateafl,stateafl,verify", "-verify-each", "-S",
                   str(original), "-o", str(target)]
        commands.append(command)
        run(command, output / "idempotency.log", env)
        before, after = original.read_text(), target.read_text()
        duplicate["checks"] = {
            "helper_counts_unchanged": all(calls(before, h) == calls(after, h)
                                             for h in HELPERS | {"new_stack_alloc_record", "free_stack_alloc_record"}),
            "single_coverage_global": len(re.findall(r"^@__afl_area_ptr\s*=", after, re.M)) == 1,
        }
        duplicate["passed"] = all(duplicate["checks"].values())
    except (AssertionError, OSError) as error:
        duplicate["error"] = str(error)
    cases.append(duplicate)
    # Invalid custom syntax and out-of-range positions require explicit failures.
    # An untyped external return is intentionally unresolvable without a size.
    diagnostic_source = output / "diagnostic.c"
    diagnostic_source.write_text("struct payload { char bytes[96]; };\n"
                                 "extern void custom(void *);\n"
                                 "extern void *opaque(void);\n"
                                 "void probe(void) { custom(opaque()); }\n")
    for entry, diagnostic in (("custom:5", "out of range"),
                              ("custom:-2", "Invalid custom trace argument"),
                              ("custom:0:0", "Invalid explicit custom trace size"),
                              ("custom:0", "struct")):
        name = "diagnostic-" + entry.replace(":", "_")
        local_env = env.copy()
        local_env["TRACE_CUSTOM_RECEIVE"] = entry
        local_env.pop("TRACE_CUSTOM_SEND", None)
        command = [str(ROOT / "afl-clang-fast"), "-O1", "-g0", "-S", "-emit-llvm",
                   "-c", str(diagnostic_source), "-o", str(output / (name + ".ll"))]
        commands.append(command)
        result = subprocess.run(command, cwd=ROOT, env=local_env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        (output / (name + ".log")).write_text(result.stdout)
        cases.append({"name": name, "exit_code": result.returncode,
                      "passed": result.returncode != 0 and diagnostic.lower() in result.stdout.lower()})
    report = {"passed": all(c["passed"] for c in cases), "cases": cases,
              "custom_environment": {k: env[k] for k in ("TRACE_CUSTOM_RECEIVE", "TRACE_CUSTOM_SEND")}}
    (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
