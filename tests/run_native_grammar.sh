#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir="$PWD/tests/llvm18/out/native-grammar"
mkdir -p "$build_dir/tmp"
export TMPDIR="$build_dir/tmp"
native_cc="${CC:-clang}"
extra_flags=()
if [[ -n "${NATIVE_SANITIZE:-}" ]]; then
  extra_flags=(-fsanitize="$NATIVE_SANITIZE" -fno-omit-frame-pointer)
fi
if [[ "${NATIVE_NDEBUG:-0}" == 1 ]]; then
  extra_flags+=(-DNDEBUG)
fi
"$native_cc" -std=gnu11 -O1 -g -Wall -Wextra -Wno-pointer-sign \
  -Wno-unused-function -Wno-sign-compare -Wno-unused-parameter \
  -ffunction-sections -fdata-sections "${extra_flags[@]}" -I. \
  tests/native_grammar.c chat-llm.c -Wl,--gc-sections \
  -Wl,--wrap=pcre2_compile_8 -Wl,--wrap=pcre2_jit_compile_8 \
  -ljson-c -lpcre2-8 -o "$build_dir/native_grammar"
"$build_dir/native_grammar"
