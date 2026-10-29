"""Unit tests for offline saved response validation (validate_saved_responses.py)."""

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

from tools.offline_validation.common import ResponseExitCode
from tools.offline_validation.validate_saved_responses import (
    SavedResponseValidator,
    validate_saved_response_content,
    validate_saved_response_file,
)
from tests.offline_validation.generate_fixtures import (
    FIXTURES_DIR,
    RESPONSES_CORRUPTED,
    RESPONSES_NORMAL,
    generate_all_fixtures,
)


class TestValidateSavedResponses(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Ensure fixtures exist
        generate_all_fixtures()

    # ------------------ Normal Cases ------------------

    def test_chat_completion_rtsp_normal(self):
        f = RESPONSES_NORMAL / "chat_completion_rtsp.json"
        res = validate_saved_response_file(f, schema="api_chat")
        self.assertTrue(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SUCCESS)

        # Also test auto-detect
        res_auto = validate_saved_response_file(f, schema="auto")
        self.assertTrue(res_auto.valid)
        self.assertEqual(res_auto.exit_code, ResponseExitCode.SUCCESS)

    def test_completion_instruct_normal(self):
        f = RESPONSES_NORMAL / "completion_instruct.json"
        res = validate_saved_response_file(f, schema="api_completion")
        self.assertTrue(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SUCCESS)

        res_auto = validate_saved_response_file(f, schema="auto")
        self.assertTrue(res_auto.valid)
        self.assertEqual(res_auto.exit_code, ResponseExitCode.SUCCESS)

    def test_grammar_rtsp_array_normal(self):
        f = RESPONSES_NORMAL / "grammar_rtsp_array.json"
        res = validate_saved_response_file(f, schema="grammar")
        self.assertTrue(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SUCCESS)
        self.assertEqual(res.details.get("template_count"), 3)

        res_auto = validate_saved_response_file(f, schema="auto")
        self.assertTrue(res_auto.valid)

    def test_grammar_http_dict_normal(self):
        f = RESPONSES_NORMAL / "grammar_http_dict.json"
        res = validate_saved_response_file(f, schema="grammar")
        self.assertTrue(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SUCCESS)
        self.assertEqual(res.details.get("template_count"), 2)

    def test_message_types_list_normal(self):
        f = RESPONSES_NORMAL / "message_types_list.json"
        res = validate_saved_response_file(f, schema="message_types")
        self.assertTrue(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SUCCESS)
        self.assertEqual(res.details.get("type_count"), 6)

    def test_stagnation_intervention_normal(self):
        f = RESPONSES_NORMAL / "stagnation_intervention.json"
        res = validate_saved_response_file(f, schema="stagnation")
        self.assertTrue(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SUCCESS)

    # ------------------ Syntax & Encoding Corruptions ------------------

    def test_syntax_unclosed_array(self):
        f = RESPONSES_CORRUPTED / "syntax_unclosed_array.json"
        res = validate_saved_response_file(f)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.INVALID_JSON_SYNTAX)
        self.assertEqual(res.error_type, "JSON_SYNTAX_ERROR")

    def test_syntax_trailing_backslash(self):
        f = RESPONSES_CORRUPTED / "syntax_trailing_backslash.json"
        res = validate_saved_response_file(f)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.INVALID_JSON_SYNTAX)
        self.assertEqual(res.error_type, "INVALID_ESCAPE")

    def test_syntax_embedded_null_byte(self):
        f = RESPONSES_CORRUPTED / "syntax_embedded_null.json"
        res = validate_saved_response_file(f)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.INVALID_JSON_SYNTAX)
        self.assertEqual(res.error_type, "EMBEDDED_NULL_BYTE")

    def test_syntax_escaped_null_byte(self):
        f = RESPONSES_CORRUPTED / "syntax_escaped_null.json"
        res = validate_saved_response_file(f)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.INVALID_JSON_SYNTAX)
        self.assertEqual(res.error_type, "ESCAPED_NULL_BYTE")

    def test_syntax_invalid_utf8(self):
        f = RESPONSES_CORRUPTED / "syntax_invalid_utf8.json"
        res = validate_saved_response_file(f)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.INVALID_JSON_SYNTAX)
        self.assertEqual(res.error_type, "ENCODING_ERROR")

    # ------------------ Schema & Type Corruptions ------------------

    def test_schema_missing_choices(self):
        f = RESPONSES_CORRUPTED / "schema_missing_choices.json"
        res = validate_saved_response_file(f, schema="api_chat")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SCHEMA_VIOLATION)
        self.assertEqual(res.error_type, "MISSING_FIELD")

    def test_schema_choices_empty(self):
        f = RESPONSES_CORRUPTED / "schema_choices_empty.json"
        res = validate_saved_response_file(f, schema="api_chat")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.CONSTRAINT_VIOLATION)
        self.assertEqual(res.error_type, "EMPTY_ARRAY")

    def test_schema_choices_not_list(self):
        f = RESPONSES_CORRUPTED / "schema_choices_not_list.json"
        res = validate_saved_response_file(f, schema="api_chat")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SCHEMA_VIOLATION)
        self.assertEqual(res.error_type, "TYPE_MISMATCH")

    def test_schema_choice_not_dict(self):
        f = RESPONSES_CORRUPTED / "schema_choice_not_dict.json"
        res = validate_saved_response_file(f, schema="api_chat")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SCHEMA_VIOLATION)
        self.assertEqual(res.error_type, "TYPE_MISMATCH")

    def test_schema_missing_message(self):
        f = RESPONSES_CORRUPTED / "schema_missing_message.json"
        res = validate_saved_response_file(f, schema="api_chat")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SCHEMA_VIOLATION)
        self.assertEqual(res.error_type, "MISSING_FIELD")

    def test_null_message(self):
        f = RESPONSES_CORRUPTED / "null_message.json"
        res = validate_saved_response_file(f, schema="api_chat")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.NULL_VALUE_ERROR)
        self.assertEqual(res.error_type, "NULL_FIELD")

    def test_null_content(self):
        f = RESPONSES_CORRUPTED / "null_content.json"
        res = validate_saved_response_file(f, schema="api_chat")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.NULL_VALUE_ERROR)
        self.assertEqual(res.error_type, "NULL_FIELD")

    def test_wrong_type_content(self):
        f = RESPONSES_CORRUPTED / "wrong_type_content.json"
        res = validate_saved_response_file(f, schema="api_chat")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SCHEMA_VIOLATION)
        self.assertEqual(res.error_type, "TYPE_MISMATCH")

    # ------------------ Grammar Corruptions ------------------

    def test_grammar_empty_template(self):
        f = RESPONSES_CORRUPTED / "grammar_empty_template.json"
        res = validate_saved_response_file(f, schema="grammar")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.CONSTRAINT_VIOLATION)
        self.assertEqual(res.error_type, "EMPTY_TEMPLATE")

    def test_grammar_header_not_string(self):
        f = RESPONSES_CORRUPTED / "grammar_header_not_string.json"
        res = validate_saved_response_file(f, schema="grammar")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SCHEMA_VIOLATION)
        self.assertEqual(res.error_type, "TYPE_MISMATCH")

    def test_grammar_header_empty(self):
        f = RESPONSES_CORRUPTED / "grammar_header_empty.json"
        res = validate_saved_response_file(f, schema="grammar")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.CONSTRAINT_VIOLATION)
        self.assertEqual(res.error_type, "EMPTY_HEADER")

    def test_grammar_field_null(self):
        f = RESPONSES_CORRUPTED / "grammar_field_null.json"
        res = validate_saved_response_file(f, schema="grammar")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.NULL_VALUE_ERROR)
        self.assertEqual(res.error_type, "NULL_FIELD")

    def test_grammar_field_not_string(self):
        f = RESPONSES_CORRUPTED / "grammar_field_not_string.json"
        res = validate_saved_response_file(f, schema="grammar")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.SCHEMA_VIOLATION)
        self.assertEqual(res.error_type, "TYPE_MISMATCH")

    def test_grammar_mismatched_placeholder(self):
        f = RESPONSES_CORRUPTED / "grammar_mismatched_placeholder.json"
        res = validate_saved_response_file(f, schema="grammar")
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.CONSTRAINT_VIOLATION)
        self.assertEqual(res.error_type, "MALFORMED_PLACEHOLDER")

    # ------------------ Resource Bounds ------------------

    def test_resource_string_too_long(self):
        f = RESPONSES_CORRUPTED / "resource_string_too_long.json"
        res = validate_saved_response_file(f)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.RESOURCE_EXCEEDED)
        self.assertEqual(res.error_type, "STRING_TOO_LONG")

    def test_resource_deep_nesting(self):
        f = RESPONSES_CORRUPTED / "resource_deep_nesting.json"
        res = validate_saved_response_file(f)
        self.assertFalse(res.valid)
        self.assertEqual(res.exit_code, ResponseExitCode.RESOURCE_EXCEEDED)
        self.assertEqual(res.error_type, "MAX_DEPTH_EXCEEDED")

    # ------------------ CLI Invocation Tests ------------------

    def test_cli_saved_response_success(self):
        f = str(RESPONSES_NORMAL / "chat_completion_rtsp.json")
        script = str(REPO_ROOT / "tools" / "offline_validation" / "validate_saved_responses.py")

        cmd = [sys.executable, script, f, "--schema", "api_chat"]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(proc.returncode, int(ResponseExitCode.SUCCESS))
        self.assertIn("[PASS]", proc.stdout)

    def test_cli_saved_response_failure(self):
        f = str(RESPONSES_CORRUPTED / "null_content.json")
        script = str(REPO_ROOT / "tools" / "offline_validation" / "validate_saved_responses.py")

        cmd = [sys.executable, script, f, "--schema", "api_chat"]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(proc.returncode, int(ResponseExitCode.NULL_VALUE_ERROR))
        self.assertIn("[FAIL]", proc.stdout)

    def test_cli_json_report(self):
        f1 = str(RESPONSES_NORMAL / "chat_completion_rtsp.json")
        f2 = str(RESPONSES_CORRUPTED / "null_content.json")
        script = str(REPO_ROOT / "tools" / "offline_validation" / "validate_saved_responses.py")

        with tempfile.TemporaryDirectory() as tmp_dir:
            out_report = Path(tmp_dir) / "report.json"
            cmd = [sys.executable, script, f1, f2, "--json-output", str(out_report)]
            proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertEqual(proc.returncode, int(ResponseExitCode.NULL_VALUE_ERROR))
            self.assertTrue(out_report.is_file())
            data = json.loads(out_report.read_text(encoding="utf-8"))
            self.assertFalse(data["overall_success"])
            self.assertEqual(data["total_files"], 2)
            self.assertEqual(data["passed_files"], 1)
            self.assertEqual(data["failed_files"], 1)


if __name__ == "__main__":
    unittest.main()
