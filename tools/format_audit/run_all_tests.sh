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
echo "[Step 1/4] Running Replay Codec Acceptance Tests (T01 - T18)..."
./test_replay_codec
CODEC_STATUS=$?

echo ""
echo "[Step 2/4] Running Region Consistency Acceptance Tests (T19 - T28)..."
./test_region_checker
REGION_STATUS=$?

echo ""
echo "[Step 3/4] Running Industrial Protocol Extraction Tests (T29 - T63)..."
./test_industrial_protocols
PROTO_STATUS=$?

echo ""
echo "[Step 4/4] Running LLM Prompt Adaptation Tests..."
./test_prompt_profiles
PROMPT_STATUS=$?

echo ""
echo "============================================================"
if [ $CODEC_STATUS -eq 0 ] && [ $REGION_STATUS -eq 0 ] && [ $PROTO_STATUS -eq 0 ] && [ $PROMPT_STATUS -eq 0 ]; then
  echo " [+] ALL AUDIT, REPLAY, PROTOCOL, AND PROMPT TESTS PASSED SUCCESSFULLY!"
  echo "============================================================"
  exit 0
else
  echo " [-] TESTS FAILED: Codec=$CODEC_STATUS, Region=$REGION_STATUS, Proto=$PROTO_STATUS, Prompt=$PROMPT_STATUS"
  echo "============================================================"
  exit 1
fi
