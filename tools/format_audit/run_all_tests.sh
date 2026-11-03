#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "============================================================"
echo "    Independent codec/checker tests: 28 cases per build"
echo "    18 codec cases (T01-T18), 10 region cases (T17-T26)"
echo "    These tests do not establish native fuzzer coverage."
echo "============================================================"

mkdir -p "$SCRIPT_DIR/.build-tmp"
export TMPDIR="$SCRIPT_DIR/.build-tmp"
make clean
make all

echo ""
echo "[Step 1/2] Running 28 independent cases with the default build flags..."
make test

echo ""
echo "[Step 2/2] Running the same 28 independent cases with NDEBUG..."
make test-ndebug

echo ""
echo "============================================================"
echo " [+] All 28 independent cases passed in both builds (56 executions)."
echo "============================================================"
