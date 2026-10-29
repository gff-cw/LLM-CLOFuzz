#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "============================================================"
echo "    LLM-CLOFuzz Format & Region Audit Test Suite (T01-T24)"
echo "============================================================"

make clean >/dev/null 2>&1 || true
make all

echo ""
echo "[Step 1/2] Running Replay Codec Acceptance Tests (T01 - T16)..."
./test_replay_codec
CODEC_STATUS=$?

echo ""
echo "[Step 2/2] Running Region Consistency Acceptance Tests (T17 - T24)..."
./test_region_checker
REGION_STATUS=$?

echo ""
echo "============================================================"
if [ $CODEC_STATUS -eq 0 ] && [ $REGION_STATUS -eq 0 ]; then
  echo " [+] ALL 24 AUDIT AND ACCEPTANCE TESTS PASSED SUCCESSFULLY!"
  echo "============================================================"
  exit 0
else
  echo " [-] TESTS FAILED: Codec=$CODEC_STATUS, Region=$REGION_STATUS"
  echo "============================================================"
  exit 1
fi
