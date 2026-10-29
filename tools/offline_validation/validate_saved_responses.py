#!/usr/bin/env python3
"""Offline validation tool for saved LLM responses and grammar definitions.

Performs strict, bounded schema and sanitization checks on saved JSON artifacts:
- Chat / completion API responses (OpenAI choices/message/text structure)
- Message grammar JSON / protocol template definitions
- Protocol message type lists and stagnation intervention responses

Completely offline: no network access, no API keys, and no runtime fuzzing dependencies.
Addresses vulnerabilities C01, C02, C03, C06, C07, and C10 identified in ChatAFL audit.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

# Enable execution both as package module and standalone script
if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from tools.offline_validation.common import ResponseExitCode, ValidationResult
else:
    from .common import ResponseExitCode, ValidationResult


class SavedResponseValidator:
    """Strict offline validator for LLM response and grammar JSON files."""

    def __init__(
        self,
        max_file_size: int = 10 * 1024 * 1024,  # 10 MB
        max_string_len: int = 65_536,          # 64 KB per string
        max_field_len: int = 4_096,            # 4 KB per grammar field
        max_elements: int = 10_000,            # Max array/dict items
        max_depth: int = 32,                   # Max nesting depth
    ):
        self.max_file_size = max_file_size
        self.max_string_len = max_string_len
        self.max_field_len = max_field_len
        self.max_elements = max_elements
        self.max_depth = max_depth

    def validate_raw_text_syntax(self, raw_bytes: bytes) -> Optional[ValidationResult]:
        """Perform pre-parse checks for encoding, illegal controls, and invalid escapes."""
        if len(raw_bytes) > self.max_file_size:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.RESOURCE_EXCEEDED,
                error_type="RESOURCE_EXCEEDED",
                message=f"File size ({len(raw_bytes)} bytes) exceeds maximum limit ({self.max_file_size} bytes)",
                details={"file_size": len(raw_bytes), "max_limit": self.max_file_size},
            )

        # 1. UTF-8 decoding
        try:
            text = raw_bytes.decode("utf-8")
        except UnicodeDecodeError as exc:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.INVALID_JSON_SYNTAX,
                error_type="ENCODING_ERROR",
                message=f"Invalid UTF-8 sequence at offset {exc.start}: {exc.reason}",
                details={"offset": exc.start, "encoding": "utf-8"},
            )

        # 2. Check for embedded raw null byte
        if "\x00" in text:
            idx = text.index("\x00")
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.INVALID_JSON_SYNTAX,
                error_type="EMBEDDED_NULL_BYTE",
                message=f"Embedded raw NUL byte found at character index {idx}",
                details={"char_index": idx},
            )

        # 3. Check for escaped null byte in JSON representation: \u0000
        if "\\u0000" in text:
            idx = text.index("\\u0000")
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.INVALID_JSON_SYNTAX,
                error_type="ESCAPED_NULL_BYTE",
                message=f"Prohibited escaped NUL byte (\\u0000) at character index {idx}",
                details={"char_index": idx},
            )

        # 4. Check for trailing standalone backslash
        stripped = text.rstrip()
        if stripped.endswith("\\"):
            # Check if it is an unescaped trailing backslash
            count = 0
            for char in reversed(stripped):
                if char == "\\":
                    count += 1
                else:
                    break
            if count % 2 != 0:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.INVALID_JSON_SYNTAX,
                    error_type="INVALID_ESCAPE",
                    message="Trailing unescaped backslash at end of file",
                    details={"trailing_backslash_count": count},
                )

        return None

    def parse_json(self, raw_bytes: bytes) -> Tuple[Optional[Any], Optional[ValidationResult]]:
        """Parse JSON safely with syntax and resource verification."""
        syntax_err = self.validate_raw_text_syntax(raw_bytes)
        if syntax_err is not None:
            return None, syntax_err

        text = raw_bytes.decode("utf-8")
        try:
            obj = json.loads(text)
        except json.JSONDecodeError as exc:
            return None, ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.INVALID_JSON_SYNTAX,
                error_type="JSON_SYNTAX_ERROR",
                message=f"JSON parse error at line {exc.lineno}, column {exc.colno}: {exc.msg}",
                details={"line": exc.lineno, "column": exc.colno, "char_pos": exc.pos},
            )

        depth_err = self._check_depth_and_sizes(obj, current_depth=1)
        if depth_err is not None:
            return None, depth_err

        return obj, None

    def _check_depth_and_sizes(self, obj: Any, current_depth: int) -> Optional[ValidationResult]:
        """Check recursion depth, container element counts, and string lengths."""
        if current_depth > self.max_depth:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.RESOURCE_EXCEEDED,
                error_type="MAX_DEPTH_EXCEEDED",
                message=f"JSON nesting depth ({current_depth}) exceeds maximum limit ({self.max_depth})",
                details={"current_depth": current_depth, "max_depth": self.max_depth},
            )

        if isinstance(obj, str):
            if len(obj) > self.max_string_len:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.RESOURCE_EXCEEDED,
                    error_type="STRING_TOO_LONG",
                    message=f"String length ({len(obj)}) exceeds safety limit ({self.max_string_len})",
                    details={"string_len": len(obj), "max_allowed": self.max_string_len},
                )
        elif isinstance(obj, list):
            if len(obj) > self.max_elements:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.RESOURCE_EXCEEDED,
                    error_type="TOO_MANY_ELEMENTS",
                    message=f"List size ({len(obj)}) exceeds limit ({self.max_elements})",
                    details={"list_len": len(obj)},
                )
            for item in obj:
                res = self._check_depth_and_sizes(item, current_depth + 1)
                if res is not None:
                    return res
        elif isinstance(obj, dict):
            if len(obj) > self.max_elements:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.RESOURCE_EXCEEDED,
                    error_type="TOO_MANY_ELEMENTS",
                    message=f"Dict key count ({len(obj)}) exceeds limit ({self.max_elements})",
                    details={"dict_len": len(obj)},
                )
            for k, v in obj.items():
                if not isinstance(k, str):
                    return ValidationResult(
                        valid=False,
                        exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                        error_type="NON_STRING_KEY",
                        message=f"Dictionary contains non-string key: {type(k).__name__}",
                    )
                if len(k) > 1024:
                    return ValidationResult(
                        valid=False,
                        exit_code=ResponseExitCode.RESOURCE_EXCEEDED,
                        error_type="KEY_TOO_LONG",
                        message=f"Dictionary key length ({len(k)}) exceeds 1024 characters",
                    )
                res = self._check_depth_and_sizes(v, current_depth + 1)
                if res is not None:
                    return res

        return None

    def validate_api_chat(self, obj: Any) -> ValidationResult:
        """Validate standard OpenAI chat-completion response format."""
        if not isinstance(obj, dict):
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                error_type="TYPE_MISMATCH",
                message=f"Root of chat response must be an object/dict, got {type(obj).__name__}",
            )

        if "choices" not in obj:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                error_type="MISSING_FIELD",
                message="Missing mandatory field 'choices' in API response",
                details={"missing_field": "choices"},
            )

        choices = obj["choices"]
        if choices is None:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.NULL_VALUE_ERROR,
                error_type="NULL_FIELD",
                message="Field 'choices' cannot be null",
                details={"field_path": "choices"},
            )

        if not isinstance(choices, list):
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                error_type="TYPE_MISMATCH",
                message=f"Field 'choices' must be a list/array, got {type(choices).__name__}",
                details={"field_path": "choices"},
            )

        if len(choices) == 0:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.CONSTRAINT_VIOLATION,
                error_type="EMPTY_ARRAY",
                message="Field 'choices' array must not be empty (requires >= 1 choice)",
                details={"field_path": "choices"},
            )

        # Validate first choice and subsequent choices
        for idx, choice in enumerate(choices):
            path = f"choices[{idx}]"
            if not isinstance(choice, dict):
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                    error_type="TYPE_MISMATCH",
                    message=f"{path} must be a dictionary, got {type(choice).__name__}",
                    details={"field_path": path},
                )

            if "message" not in choice:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                    error_type="MISSING_FIELD",
                    message=f"Missing field 'message' in {path}",
                    details={"field_path": path, "missing_field": "message"},
                )

            msg = choice["message"]
            if msg is None:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.NULL_VALUE_ERROR,
                    error_type="NULL_FIELD",
                    message=f"Field 'message' in {path} cannot be null",
                    details={"field_path": f"{path}.message"},
                )

            if not isinstance(msg, dict):
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                    error_type="TYPE_MISMATCH",
                    message=f"Field 'message' in {path} must be a dict, got {type(msg).__name__}",
                    details={"field_path": f"{path}.message"},
                )

            if "content" not in msg:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                    error_type="MISSING_FIELD",
                    message=f"Missing 'content' in {path}.message",
                    details={"field_path": f"{path}.message", "missing_field": "content"},
                )

            content = msg["content"]
            if content is None:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.NULL_VALUE_ERROR,
                    error_type="NULL_FIELD",
                    message=f"Field 'content' in {path}.message cannot be null",
                    details={"field_path": f"{path}.message.content"},
                )

            if not isinstance(content, str):
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                    error_type="TYPE_MISMATCH",
                    message=f"Field 'content' in {path}.message must be string, got {type(content).__name__}",
                    details={"field_path": f"{path}.message.content"},
                )

        return ValidationResult(
            valid=True,
            exit_code=ResponseExitCode.SUCCESS,
            message="Valid chat completion response",
            details={"choice_count": len(choices)},
        )

    def validate_api_completion(self, obj: Any) -> ValidationResult:
        """Validate legacy text completion response format (instruct models)."""
        if not isinstance(obj, dict):
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                error_type="TYPE_MISMATCH",
                message=f"Root must be a dict, got {type(obj).__name__}",
            )

        if "choices" not in obj:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                error_type="MISSING_FIELD",
                message="Missing mandatory field 'choices'",
            )

        choices = obj["choices"]
        if not isinstance(choices, list) or len(choices) == 0:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.CONSTRAINT_VIOLATION,
                error_type="INVALID_CHOICES",
                message="Field 'choices' must be a non-empty list",
            )

        for idx, choice in enumerate(choices):
            path = f"choices[{idx}]"
            if not isinstance(choice, dict):
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                    error_type="TYPE_MISMATCH",
                    message=f"{path} must be a dict",
                )
            if "text" not in choice:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                    error_type="MISSING_FIELD",
                    message=f"Missing 'text' in {path}",
                )
            text_val = choice["text"]
            if text_val is None:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.NULL_VALUE_ERROR,
                    error_type="NULL_FIELD",
                    message=f"'text' in {path} cannot be null",
                )
            if not isinstance(text_val, str):
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                    error_type="TYPE_MISMATCH",
                    message=f"'text' in {path} must be string, got {type(text_val).__name__}",
                )

        return ValidationResult(
            valid=True,
            exit_code=ResponseExitCode.SUCCESS,
            message="Valid text completion response",
        )

    def validate_grammar(self, obj: Any) -> ValidationResult:
        """Validate message grammar JSON structure.

        Accepted representations:
        1. List of template lists: [ ["HEADER", "field1", "field2"], ... ]
        2. Single template list: ["HEADER", "field1", "field2"]
        3. Dict of templates: {"HEADER": ["field1", "field2"], ...}
        """
        templates: List[Tuple[str, List[Any]]] = []

        if isinstance(obj, list):
            if len(obj) == 0:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.CONSTRAINT_VIOLATION,
                    error_type="EMPTY_GRAMMAR",
                    message="Grammar template list cannot be empty",
                )
            # Check if it's a single template: ["DESCRIBE", "field1", ...]
            if len(obj) > 0 and isinstance(obj[0], str) and not any(isinstance(x, list) for x in obj):
                header = obj[0]
                fields = obj[1:]
                templates.append((header, fields))
            else:
                for idx, item in enumerate(obj):
                    if not isinstance(item, list):
                        return ValidationResult(
                            valid=False,
                            exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                            error_type="TYPE_MISMATCH",
                            message=f"Grammar template entry [{idx}] must be a list, got {type(item).__name__}",
                            details={"index": idx},
                        )
                    if len(item) == 0:
                        return ValidationResult(
                            valid=False,
                            exit_code=ResponseExitCode.CONSTRAINT_VIOLATION,
                            error_type="EMPTY_TEMPLATE",
                            message=f"Grammar template entry [{idx}] is empty (must have header as first element)",
                            details={"index": idx},
                        )
                    header = item[0]
                    if not isinstance(header, str):
                        return ValidationResult(
                            valid=False,
                            exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                            error_type="TYPE_MISMATCH",
                            message=(
                                f"Template [{idx}][0] (header) must be string, got {type(header).__name__}"
                            ),
                            details={"index": idx, "field_path": f"[{idx}][0]"},
                        )
                    fields = item[1:]
                    templates.append((header, fields))
        elif isinstance(obj, dict):
            if len(obj) == 0:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.CONSTRAINT_VIOLATION,
                    error_type="EMPTY_GRAMMAR",
                    message="Grammar template dictionary cannot be empty",
                )
            for k, v in obj.items():
                if not isinstance(v, list):
                    return ValidationResult(
                        valid=False,
                        exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                        error_type="TYPE_MISMATCH",
                        message=f"Grammar value for key '{k}' must be list, got {type(v).__name__}",
                    )
                templates.append((k, v))
        else:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                error_type="TYPE_MISMATCH",
                message=f"Grammar root must be list or dict, got {type(obj).__name__}",
            )

        # Validate each template header and fields
        header_pattern = re.compile(r"^[A-Za-z0-9_\-]+$")
        for idx, (header, fields) in enumerate(templates):
            # Check header string constraints
            clean_header = header.strip()
            if not clean_header:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.CONSTRAINT_VIOLATION,
                    error_type="EMPTY_HEADER",
                    message=f"Template header at index {idx} is empty or whitespace-only",
                    details={"index": idx},
                )

            if len(clean_header) > 128:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.CONSTRAINT_VIOLATION,
                    error_type="HEADER_TOO_LONG",
                    message=f"Template header '{clean_header[:32]}...' exceeds maximum 128 characters",
                    details={"index": idx, "header_len": len(clean_header)},
                )

            if not header_pattern.match(clean_header):
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.CONSTRAINT_VIOLATION,
                    error_type="INVALID_HEADER_CHARACTERS",
                    message=f"Template header '{clean_header}' contains invalid characters (expected alphanumeric/dash/underscore)",
                    details={"index": idx, "header": clean_header},
                )

            # Check field items
            for f_idx, field_val in enumerate(fields):
                field_path = f"template[{idx}].fields[{f_idx}]"
                if field_val is None:
                    return ValidationResult(
                        valid=False,
                        exit_code=ResponseExitCode.NULL_VALUE_ERROR,
                        error_type="NULL_FIELD",
                        message=f"Grammar field at {field_path} is null",
                        details={"field_path": field_path},
                    )

                if not isinstance(field_val, str):
                    return ValidationResult(
                        valid=False,
                        exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                        error_type="TYPE_MISMATCH",
                        message=f"Grammar field at {field_path} must be string, got {type(field_val).__name__}",
                        details={"field_path": field_path},
                    )

                if len(field_val) > self.max_field_len:
                    return ValidationResult(
                        valid=False,
                        exit_code=ResponseExitCode.RESOURCE_EXCEEDED,
                        error_type="FIELD_TOO_LONG",
                        message=(
                            f"Grammar field at {field_path} length ({len(field_val)}) "
                            f"exceeds limit ({self.max_field_len} bytes) - risk of regex buffer overflow (C01)"
                        ),
                        details={"field_path": field_path, "field_len": len(field_val)},
                    )

                # Check for malformed placeholders
                if "<<" in field_val or ">>" in field_val:
                    open_count = field_val.count("<<")
                    close_count = field_val.count(">>")
                    if open_count != close_count:
                        return ValidationResult(
                            valid=False,
                            exit_code=ResponseExitCode.CONSTRAINT_VIOLATION,
                            error_type="MALFORMED_PLACEHOLDER",
                            message=f"Mismatched '<<' and '>>' in field at {field_path}: open={open_count}, close={close_count}",
                            details={"field_path": field_path, "open_count": open_count, "close_count": close_count},
                        )

        return ValidationResult(
            valid=True,
            exit_code=ResponseExitCode.SUCCESS,
            message=f"Valid message grammar structure ({len(templates)} templates)",
            details={"template_count": len(templates)},
        )

    def validate_message_types(self, obj: Any) -> ValidationResult:
        """Validate protocol message type identifiers."""
        items: List[str] = []
        if isinstance(obj, list):
            for i, x in enumerate(obj):
                if not isinstance(x, str):
                    return ValidationResult(
                        valid=False,
                        exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                        error_type="TYPE_MISMATCH",
                        message=f"Message type at index {i} must be string, got {type(x).__name__}",
                    )
                items.append(x)
        elif isinstance(obj, str):
            items = [s.strip() for s in obj.split(",") if s.strip()]
        else:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                error_type="TYPE_MISMATCH",
                message=f"Message types must be list or comma-separated string, got {type(obj).__name__}",
            )

        if len(items) == 0:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.CONSTRAINT_VIOLATION,
                error_type="EMPTY_TYPES",
                message="Message types list cannot be empty",
            )

        ident_pattern = re.compile(r"^[A-Za-z0-9_\-]+$")
        for idx, item in enumerate(items):
            if not ident_pattern.match(item):
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.CONSTRAINT_VIOLATION,
                    error_type="INVALID_TYPE_IDENTIFIER",
                    message=f"Invalid message type identifier '{item}' at index {idx}",
                    details={"index": idx, "identifier": item},
                )

        return ValidationResult(
            valid=True,
            exit_code=ResponseExitCode.SUCCESS,
            message=f"Valid message types list ({len(items)} types)",
            details={"type_count": len(items)},
        )

    def validate_stagnation(self, obj: Any) -> ValidationResult:
        """Validate stagnation intervention request content."""
        req_text = ""
        if isinstance(obj, str):
            req_text = obj
        elif isinstance(obj, dict):
            for k in ("message", "request", "content", "suggestion"):
                if k in obj and isinstance(obj[k], str):
                    req_text = obj[k]
                    break
            if not req_text:
                return ValidationResult(
                    valid=False,
                    exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                    error_type="MISSING_FIELD",
                    message="Stagnation intervention dict must contain 'message' or 'request' string field",
                )
        else:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.SCHEMA_VIOLATION,
                error_type="TYPE_MISMATCH",
                message=f"Stagnation response must be string or dict, got {type(obj).__name__}",
            )

        if not req_text.strip():
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.CONSTRAINT_VIOLATION,
                error_type="EMPTY_CONTENT",
                message="Stagnation request message is empty",
            )

        if len(req_text) > self.max_string_len:
            return ValidationResult(
                valid=False,
                exit_code=ResponseExitCode.RESOURCE_EXCEEDED,
                error_type="STRING_TOO_LONG",
                message=f"Stagnation request length ({len(req_text)}) exceeds limit ({self.max_string_len})",
            )

        return ValidationResult(
            valid=True,
            exit_code=ResponseExitCode.SUCCESS,
            message="Valid stagnation intervention response",
            details={"length": len(req_text)},
        )

    def validate_auto(self, obj: Any) -> ValidationResult:
        """Auto-detect schema and dispatch to matching validator."""
        if isinstance(obj, dict):
            if "choices" in obj:
                choices = obj["choices"]
                if isinstance(choices, list) and len(choices) > 0 and isinstance(choices[0], dict):
                    if "message" in choices[0]:
                        return self.validate_api_chat(obj)
                    if "text" in choices[0]:
                        return self.validate_api_completion(obj)
                return self.validate_api_chat(obj)

            # Check if grammar dict: values are lists of strings
            if all(isinstance(v, list) for v in obj.values()) and len(obj) > 0:
                return self.validate_grammar(obj)

            if any(k in obj for k in ("message", "request", "content", "suggestion")):
                return self.validate_stagnation(obj)

        elif isinstance(obj, list):
            if len(obj) > 0:
                if isinstance(obj[0], list):
                    return self.validate_grammar(obj)
                if isinstance(obj[0], str):
                    # Could be message_types or single template
                    if all(isinstance(x, str) for x in obj):
                        if any("<<" in x or "\r\n" in x or "\n" in x for x in obj):
                            return self.validate_grammar(obj)
                        return self.validate_message_types(obj)

        elif isinstance(obj, str):
            return self.validate_stagnation(obj)

        return ValidationResult(
            valid=False,
            exit_code=ResponseExitCode.SCHEMA_VIOLATION,
            error_type="UNKNOWN_SCHEMA",
            message="Unable to auto-detect schema: structure does not match API response, grammar, message types, or stagnation",
        )


def validate_saved_response_content(
    raw_bytes: bytes,
    schema: str = "auto",
    max_file_size: int = 10 * 1024 * 1024,
    max_string_len: int = 65_536,
    max_field_len: int = 4_096,
    max_elements: int = 10_000,
    max_depth: int = 32,
) -> ValidationResult:
    """Validate JSON bytes directly against the requested schema."""
    validator = SavedResponseValidator(
        max_file_size=max_file_size,
        max_string_len=max_string_len,
        max_field_len=max_field_len,
        max_elements=max_elements,
        max_depth=max_depth,
    )

    obj, err = validator.parse_json(raw_bytes)
    if err is not None:
        return err

    schema_norm = schema.lower().strip()
    if schema_norm == "auto":
        return validator.validate_auto(obj)
    elif schema_norm in ("api_chat", "chat"):
        return validator.validate_api_chat(obj)
    elif schema_norm in ("api_completion", "completion"):
        return validator.validate_api_completion(obj)
    elif schema_norm == "grammar":
        return validator.validate_grammar(obj)
    elif schema_norm in ("message_types", "types"):
        return validator.validate_message_types(obj)
    elif schema_norm == "stagnation":
        return validator.validate_stagnation(obj)
    else:
        return ValidationResult(
            valid=False,
            exit_code=ResponseExitCode.CLI_ERROR,
            error_type="INVALID_SCHEMA_NAME",
            message=f"Unknown schema name '{schema}'. Must be auto, api_chat, api_completion, grammar, message_types, or stagnation",
        )


def validate_saved_response_file(
    file_path: str | Path,
    schema: str = "auto",
    max_file_size: int = 10 * 1024 * 1024,
    max_string_len: int = 65_536,
    max_field_len: int = 4_096,
    max_elements: int = 10_000,
    max_depth: int = 32,
) -> ValidationResult:
    """Validate a saved response JSON file from disk."""
    fpath = Path(file_path)
    if not fpath.is_file():
        return ValidationResult(
            valid=False,
            exit_code=ResponseExitCode.CLI_ERROR,
            error_type="FILE_NOT_FOUND",
            message=f"Saved response file does not exist: {file_path}",
            target=str(file_path),
        )

    try:
        raw_bytes = fpath.read_bytes()
    except Exception as exc:
        return ValidationResult(
            valid=False,
            exit_code=ResponseExitCode.CLI_ERROR,
            error_type="READ_ERROR",
            message=f"Failed to read file: {exc}",
            target=str(file_path),
        )

    res = validate_saved_response_content(
        raw_bytes=raw_bytes,
        schema=schema,
        max_file_size=max_file_size,
        max_string_len=max_string_len,
        max_field_len=max_field_len,
        max_elements=max_elements,
        max_depth=max_depth,
    )
    res.target = str(file_path)
    return res


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Offline validation tool for saved LLM responses and protocol grammar files.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("files", nargs="+", help="One or more JSON files to validate")
    parser.add_argument(
        "--schema",
        "-s",
        default="auto",
        choices=["auto", "api_chat", "api_completion", "grammar", "message_types", "stagnation"],
        help="Target schema for validation (default: auto)",
    )
    parser.add_argument("--json-output", default=None, help="Path to write machine-readable JSON validation report")
    parser.add_argument("--max-file-size", type=int, default=10 * 1024 * 1024, help="Max file size in bytes")
    parser.add_argument("--max-string-len", type=int, default=65_536, help="Max string length in characters")
    parser.add_argument("--max-field-len", type=int, default=4_096, help="Max grammar field length")
    parser.add_argument("--quiet", "-q", action="store_true", help="Suppress output on success")
    parser.add_argument("--verbose", "-v", action="store_true", help="Print detailed diagnostic info")

    args = parser.parse_args(argv)

    results: List[ValidationResult] = []
    worst_exit_code = ResponseExitCode.SUCCESS

    for f in args.files:
        res = validate_saved_response_file(
            file_path=f,
            schema=args.schema,
            max_file_size=args.max_file_size,
            max_string_len=args.max_string_len,
            max_field_len=args.max_field_len,
        )
        res.print_summary(verbose=args.verbose, quiet=args.quiet)
        results.append(res)
        if not res.valid and worst_exit_code == ResponseExitCode.SUCCESS:
            worst_exit_code = res.exit_code

    if args.json_output:
        try:
            out_p = Path(args.json_output)
            out_p.parent.mkdir(parents=True, exist_ok=True)
            report_data = {
                "overall_success": (worst_exit_code == ResponseExitCode.SUCCESS),
                "exit_code": int(worst_exit_code),
                "total_files": len(results),
                "passed_files": sum(1 for r in results if r.valid),
                "failed_files": sum(1 for r in results if not r.valid),
                "results": [r.to_dict() for r in results],
            }
            out_p.write_text(json.dumps(report_data, indent=2, ensure_ascii=False), encoding="utf-8")
        except Exception as exc:
            print(f"[FAIL] Unable to write JSON report to {args.json_output}: {exc}", file=sys.stderr)
            return ResponseExitCode.CLI_ERROR

    return worst_exit_code


if __name__ == "__main__":
    sys.exit(main())
