"""Common definitions, error codes, and result reporting for offline validation."""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass, field, asdict
from enum import IntEnum
from typing import Any, Dict, List, Optional


class ReprExitCode(IntEnum):
    """Exit codes for representation validation."""
    SUCCESS = 0
    CLI_ERROR = 1
    TRUNCATED_HEADER = 2
    OVERFLOW_LENGTH = 3
    ZERO_LENGTH_MESSAGE = 4
    TRAILING_GARBAGE = 5
    EMPTY_FILE = 6
    RAW_MISMATCH = 7
    RESOURCE_EXCEEDED = 8


class ResponseExitCode(IntEnum):
    """Exit codes for saved response validation."""
    SUCCESS = 0
    CLI_ERROR = 1
    INVALID_JSON_SYNTAX = 2
    SCHEMA_VIOLATION = 3
    NULL_VALUE_ERROR = 4
    CONSTRAINT_VIOLATION = 5
    RESOURCE_EXCEEDED = 6


# Alias ExitCode for general use
ExitCode = ReprExitCode


@dataclass
class ValidationResult:
    """Standardized validation outcome report."""
    valid: bool
    exit_code: int
    error_type: str = "NONE"
    message: str = "Validation successful"
    target: str = ""
    details: Dict[str, Any] = field(default_factory=dict)
    warnings: List[str] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    def to_json(self, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent, ensure_ascii=False)

    def print_summary(self, verbose: bool = False, quiet: bool = False, stream=None) -> None:
        out = stream or sys.stdout
        if quiet and self.valid:
            return

        status = "[PASS]" if self.valid else "[FAIL]"
        print(f"{status} {self.target or 'Input'}: {self.message}", file=out)

        if not self.valid:
            print(f"       Code: {self.exit_code} ({self.error_type})", file=out)
            if self.details and verbose:
                for k, v in self.details.items():
                    print(f"       - {k}: {v}", file=out)
        else:
            if self.details and verbose:
                for k, v in self.details.items():
                    print(f"       - {k}: {v}", file=out)

        if self.warnings and (verbose or not self.valid):
            for w in self.warnings:
                print(f"       [WARN] {w}", file=out)
