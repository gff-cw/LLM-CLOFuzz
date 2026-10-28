#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
result_dir="${1:-tests/llvm18/out/run-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$result_dir"
result_dir="$(realpath "$result_dir")"
trap 'printf "Regression failed at line %s. Logs: %s\n" "$LINENO" "$result_dir" >&2' ERR
export AFL_PATH="$PWD"
regression_cc="${CC:-clang}"
regression_cxx="${CXX:-clang++}"
export CC="$regression_cc" CXX="$regression_cxx"

printf '%s\n' 'IR matrix'
python3 tests/llvm18/run_ir.py --output "$result_dir/ir" > "$result_dir/ir.log" 2>&1
printf '%s\n' 'Pass edges'
python3 tests/llvm18/run_pass_edges.py --output "$result_dir/pass-edges" > "$result_dir/pass-edges.log" 2>&1
printf '%s\n' 'Wrapper and coverage'
python3 tests/llvm18/toolchain_features.py --output "$result_dir/features" > "$result_dir/features.log" 2>&1

printf '%s\n' 'Runtime records'
mkdir -p "$result_dir/runtime-records/data"
"$regression_cc" -O1 -g -c tests/llvm18/runtime_records.c -o "$result_dir/runtime-records/records.o" > "$result_dir/runtime-records/build.log" 2>&1
"$regression_cxx" "$result_dir/runtime-records/records.o" llvm_mode/.build/containers.a llvm_mode/.build/libtlsh.a llvm_mode/.build/libmvptree.a -lm -o "$result_dir/runtime-records/records" >> "$result_dir/runtime-records/build.log" 2>&1
AFL_OUTDIR="$result_dir/runtime-records/data" "$result_dir/runtime-records/records" > "$result_dir/runtime-records/run.log" 2>&1

printf '%s\n' 'Instrumented I/O and allocations'
python3 tests/llvm18/run_dynamic.py "$result_dir/dynamic" > "$result_dir/dynamic.log" 2>&1
printf '%s\n' 'State SHM and calibration'
mkdir -p "$result_dir/state-feedback"
./afl-clang-fast -O1 -g tests/llvm18/state_fixture.c -o "$result_dir/state-feedback/fixture" > "$result_dir/state-feedback/build.log" 2>&1
python3 tests/llvm18/state_feedback.py "$result_dir/state-feedback/fixture" "$result_dir/state-feedback/data" > "$result_dir/state-feedback/run.log" 2>&1
printf '%s\n' 'Bounded network campaign'
python3 tests/llvm18/e2e.py --output "$result_dir/e2e" --seconds "${STATEAFL_TEST_SECONDS:-45}" > "$result_dir/e2e.log" 2>&1
printf 'All regression groups passed. Evidence: %s\n' "$result_dir"
