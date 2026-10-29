"""Unit tests for offline representation validation (validate_representation.py)."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

# Setup import path
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.offline_validation.common import ReprExitCode
from tools.offline_validation.validate_representation import (
    Region,
    RepresentationValidator,
    replay_to_raw,
    validate_replay_file,
)
from tests.offline_validation.generate_fixtures import (
    FIXTURES_DIR,
    REPLAY_CORRUPTED,
    REPLAY_NORMAL,
    generate_all_fixtures,
)


class TestValidateRepresentation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Ensure fixtures exist
        generate_all_fixtures()

    # ------------------ Normal Cases ------------------

    def test_single_message_normal(self):
        replay_path = REPLAY_NORMAL / "single_msg.replay"
        raw_path = REPLAY_NORMAL / "single_msg.raw"

        res, raw_bytes, regions = validate_replay_file(replay_path, raw_path=raw_path)
        self.assertTrue(res.valid)
        self.assertEqual(res.exit_code, ReprExitCode.SUCCESS)
        self.assertEqual(len(regions), 1)
        self.assertEqual(regions[0].index, 0)
        self.assertEqual(regions[0].start_byte, 0)
        self.assertEqual(regions[0].end_byte, len(raw_bytes) - 1)
        self.assertEqual(raw_bytes, raw_path.read_bytes())

    def test_multi_message_rtsp_normal(self):
        replay_path = REPLAY_NORMAL / "multi_msg_rtsp.replay"
        raw_path = REPLAY_NORMAL / "multi_msg_rtsp.raw"

        res, raw_bytes, regions = validate_replay_file(replay_path, raw_path=raw_path)
        self.assertTrue(res.valid)
        self.assertEqual(res.exit_code, ReprExitCode.SUCCESS)
        self.assertEqual(len(regions), 4)

        # Verify contiguous boundaries
        for i in range(len(regions)):
            self.assertEqual(regions[i].index, i)
            if i > 0:
                self.assertEqual(regions[i].start_byte, regions[i - 1].end_byte + 1)
        self.assertEqual(regions[-1].end_byte, len(raw_bytes) - 1)
        self.assertEqual(raw_bytes, raw_path.read_bytes())

    def test_binary_payloads_normal(self):
        replay_path = REPLAY_NORMAL / "binary_payloads.replay"
        raw_path = REPLAY_NORMAL / "binary_payloads.raw"

        res, raw_bytes, regions = validate_replay_file(replay_path, raw_path=raw_path)
        self.assertTrue(res.valid)
        self.assertEqual(res.exit_code, ReprExitCode.SUCCESS)
        self.assertEqual(len(regions), 3)
        self.assertEqual(raw_bytes, raw_path.read_bytes())

    def test_minimal_boundary_normal(self):
        replay_path = REPLAY_NORMAL / "minimal_boundary.replay"
        raw_path = REPLAY_NORMAL / "minimal_boundary.raw"

        res, raw_bytes, regions = validate_replay_file(replay_path, raw_path=raw_path)
        self.assertTrue(res.valid)
        self.assertEqual(res.exit_code, ReprExitCode.SUCCESS)
        self.assertEqual(len(regions), 3)
        self.assertEqual(raw_bytes, b"ABC")
        self.assertEqual(regions[0].length, 1)
        self.assertEqual(regions[1].length, 1)
        self.assertEqual(regions[2].length, 1)

    def test_existing_repository_live555_rtsp_replay(self):
        replay_file = REPO_ROOT.parent / "ChatAFL" / "benchmark" / "subjects" / "RTSP" / "Live555" / "in-rtsp-replay" / "rtsp_requests_aac.raw"
        raw_file = REPO_ROOT / "tutorials" / "live555" / "in-rtsp" / "rtsp_requests_aac.raw"

        if replay_file.is_file() and raw_file.is_file():
            res, raw_bytes, regions = validate_replay_file(replay_file, raw_path=raw_file)
            self.assertTrue(res.valid)
            self.assertEqual(res.exit_code, ReprExitCode.SUCCESS)
            self.assertEqual(len(regions), 3)
            self.assertEqual(len(raw_bytes), 515)

    # ------------------ Corrupted Cases ------------------

    def test_corrupted_truncated_header(self):
        replay_path = REPLAY_CORRUPTED / "truncated_header.replay"
        res, raw_bytes, regions = validate_replay_file(replay_path)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ReprExitCode.TRUNCATED_HEADER)
        self.assertEqual(res.error_type, "TRUNCATED_HEADER")
        self.assertIn("Incomplete 4-byte message length header", res.message)

    def test_corrupted_truncated_mid_header(self):
        replay_path = REPLAY_CORRUPTED / "truncated_mid_header.replay"
        res, raw_bytes, regions = validate_replay_file(replay_path)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ReprExitCode.TRUNCATED_HEADER)
        self.assertEqual(res.error_type, "TRUNCATED_HEADER")

    def test_corrupted_overflow_length(self):
        replay_path = REPLAY_CORRUPTED / "overflow_length.replay"
        res, raw_bytes, regions = validate_replay_file(replay_path)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ReprExitCode.OVERFLOW_LENGTH)
        self.assertEqual(res.error_type, "OVERFLOW_LENGTH")
        self.assertIn("declared length 500000", res.message)

    def test_corrupted_zero_length_message(self):
        replay_path = REPLAY_CORRUPTED / "zero_length_message.replay"
        res, raw_bytes, regions = validate_replay_file(replay_path)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ReprExitCode.ZERO_LENGTH_MESSAGE)
        self.assertEqual(res.error_type, "ZERO_LENGTH_MESSAGE")
        self.assertIn("Zero-length message declared", res.message)

    def test_corrupted_empty_file(self):
        replay_path = REPLAY_CORRUPTED / "empty_file.replay"
        res, raw_bytes, regions = validate_replay_file(replay_path, allow_empty=False)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ReprExitCode.EMPTY_FILE)
        self.assertEqual(res.error_type, "EMPTY_FILE")

    def test_empty_file_allowed(self):
        replay_path = REPLAY_CORRUPTED / "empty_file.replay"
        res, raw_bytes, regions = validate_replay_file(replay_path, allow_empty=True)
        self.assertTrue(res.valid)
        self.assertEqual(res.exit_code, ReprExitCode.SUCCESS)
        self.assertEqual(len(regions), 0)
        self.assertEqual(raw_bytes, b"")

    def test_corrupted_raw_mismatch_content(self):
        replay_path = REPLAY_CORRUPTED / "raw_mismatch_content.replay"
        raw_path = REPLAY_CORRUPTED / "raw_mismatch_content.raw"
        res, raw_bytes, regions = validate_replay_file(replay_path, raw_path=raw_path)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ReprExitCode.RAW_MISMATCH)
        self.assertEqual(res.error_type, "RAW_BYTE_MISMATCH")
        self.assertIn("mismatch at offset 5", res.message)

    def test_corrupted_raw_mismatch_length(self):
        replay_path = REPLAY_CORRUPTED / "raw_mismatch_length.replay"
        raw_path = REPLAY_CORRUPTED / "raw_mismatch_length.raw"
        res, raw_bytes, regions = validate_replay_file(replay_path, raw_path=raw_path)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ReprExitCode.RAW_MISMATCH)
        self.assertEqual(res.error_type, "RAW_LENGTH_MISMATCH")

    # ------------------ Output and Export Tests ------------------

    def test_output_raw_and_regions_files(self):
        replay_path = REPLAY_NORMAL / "multi_msg_rtsp.replay"
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_p = Path(tmp_dir)
            out_raw = tmp_p / "exported.raw"
            out_reg = tmp_p / "exported_regions.json"
            out_rep = tmp_p / "report.json"

            res, raw_bytes, regions = validate_replay_file(
                replay_path,
                output_raw_path=out_raw,
                output_regions_path=out_reg,
            )
            self.assertTrue(res.valid)
            self.assertTrue(out_raw.is_file())
            self.assertTrue(out_reg.is_file())
            self.assertEqual(out_raw.read_bytes(), raw_bytes)

            loaded_regions = json.loads(out_reg.read_text(encoding="utf-8"))
            self.assertEqual(len(loaded_regions), len(regions))
            self.assertEqual(loaded_regions[0]["start_byte"], 0)

    # ------------------ CLI Invocation Tests ------------------

    def test_cli_success(self):
        replay_path = str(REPLAY_NORMAL / "single_msg.replay")
        raw_path = str(REPLAY_NORMAL / "single_msg.raw")
        script = str(REPO_ROOT / "tools" / "offline_validation" / "validate_representation.py")

        cmd = [sys.executable, script, "--replay", replay_path, "--raw", raw_path]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(proc.returncode, int(ReprExitCode.SUCCESS))
        self.assertIn("[PASS]", proc.stdout)

    def test_cli_failure_exit_code(self):
        replay_path = str(REPLAY_CORRUPTED / "truncated_header.replay")
        script = str(REPO_ROOT / "tools" / "offline_validation" / "validate_representation.py")

        cmd = [sys.executable, script, "--replay", replay_path]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(proc.returncode, int(ReprExitCode.TRUNCATED_HEADER))
        self.assertIn("[FAIL]", proc.stdout)


if __name__ == "__main__":
    unittest.main()
