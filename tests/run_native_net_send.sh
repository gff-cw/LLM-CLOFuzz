#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir="$PWD/tests/llvm18/out/native-net-send"
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
  tests/native_net_send.c aflnet.c -Wl,--gc-sections -Wl,--wrap=send \
  -o "$build_dir/native_net_send"
"$build_dir/native_net_send"
