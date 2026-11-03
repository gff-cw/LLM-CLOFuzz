#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir="$PWD/tests/llvm18/out/native-prompt"
mkdir -p "$build_dir/tmp"
export TMPDIR="$build_dir/tmp"
native_cc="${CC:-clang}"
sanitizer_flags=()
if [[ -n "${NATIVE_SANITIZE:-}" ]]; then
  sanitizer_flags=(-fsanitize="$NATIVE_SANITIZE" -fno-omit-frame-pointer)
fi
if [[ "${NATIVE_NDEBUG:-0}" == 1 ]]; then
  sanitizer_flags+=(-DNDEBUG)
fi
"$native_cc" -std=gnu11 -O1 -g -Wall -Wextra -Wno-pointer-sign \
  -Wno-unused-function -Wno-sign-compare -Wno-unused-parameter \
  -ffunction-sections -fdata-sections "${sanitizer_flags[@]}" -I. \
  tests/native_prompt.c chat-llm.c -Wl,--gc-sections \
  -Wl,--wrap=asprintf -Wl,--wrap=strdup \
  -Wl,--wrap=json_object_new_array -Wl,--wrap=json_object_new_object \
  -Wl,--wrap=json_object_new_string -Wl,--wrap=json_object_object_add \
  -Wl,--wrap=json_object_array_add -Wl,--wrap=json_object_put \
  -Wl,--wrap=json_object_to_json_string -ljson-c -o "$build_dir/native_prompt"
"$build_dir/native_prompt"
