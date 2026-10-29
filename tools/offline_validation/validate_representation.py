#!/usr/bin/env python3
"""Offline validation and conversion tool for replay, raw, and region representations.

Explicitly validates format integrity, boundary consistency, and ownership:
- replay: 4-byte unsigned little-endian length prefix + payload, consecutive.
- raw: pure concatenated message payloads without length headers.
- region: [start_byte, end_byte] closed intervals referencing raw offsets.

Provides attributable exit codes, detailed corruption diagnosis, and zero
network or online dependencies.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

# Enable execution both as package module and standalone script
if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from tools.offline_validation.common import ReprExitCode, ValidationResult
else:
    from .common import ReprExitCode, ValidationResult


@dataclass
class Region:
    """Represents a message region in raw data as defined in aflnet.h."""
    index: int
    start_byte: int
    end_byte: int
    length: int

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)


class RepresentationValidator:
    """Validator and converter between replay, raw, and region structures."""

    def __init__(
        self,
        max_file_size: int = 64 * 1024 * 1024,  # 64 MB
        max_messages: int = 100_000,
        allow_empty: bool = False,
    ):
        self.max_file_size = max_file_size
        self.max_messages = max_messages
        self.allow_empty = allow_empty

    def parse_replay_bytes(self, data: bytes) -> Tuple[List[bytes], List[Region], Optional[ValidationResult]]:
        """Parse replay binary data into individual message payloads and region mappings.

        Returns:
            (messages, regions, failure_result)
            If failure_result is not None, parsing failed and error details are populated.
        """
        total_len = len(data)

        if total_len > self.max_file_size:
            return [], [], ValidationResult(
                valid=False,
                exit_code=ReprExitCode.RESOURCE_EXCEEDED,
                error_type="RESOURCE_EXCEEDED",
                message=f"Replay data size ({total_len} bytes) exceeds maximum limit ({self.max_file_size} bytes)",
                details={"file_size": total_len, "max_allowed": self.max_file_size},
            )

        if total_len == 0:
            if self.allow_empty:
                return [], [], None
            return [], [], ValidationResult(
                valid=False,
                exit_code=ReprExitCode.EMPTY_FILE,
                error_type="EMPTY_FILE",
                message="Replay file is 0 bytes (empty file not allowed without --allow-empty)",
                details={"file_size": 0},
            )

        offset = 0
        raw_offset = 0
        messages: List[bytes] = []
        regions: List[Region] = []
        msg_index = 0

        while offset < total_len:
            remaining = total_len - offset

            # Check 1: 4-byte length header completeness
            if remaining < 4:
                return messages, regions, ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.TRUNCATED_HEADER,
                    error_type="TRUNCATED_HEADER",
                    message=f"Incomplete 4-byte message length header at offset {offset}: only {remaining} bytes remaining",
                    details={
                        "offset": offset,
                        "remaining_bytes": remaining,
                        "parsed_messages": msg_index,
                        "total_file_size": total_len,
                    },
                )

            # Check message limit
            if msg_index >= self.max_messages:
                return messages, regions, ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.RESOURCE_EXCEEDED,
                    error_type="RESOURCE_EXCEEDED",
                    message=f"Message count reached safety limit of {self.max_messages}",
                    details={"parsed_messages": msg_index, "offset": offset},
                )

            # Read 4-byte unsigned little-endian length
            (msg_len,) = struct.unpack_from("<I", data, offset)
            offset += 4

            # Check 2: Zero-length message check (AFLNet region requires non-empty messages)
            if msg_len == 0:
                return messages, regions, ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.ZERO_LENGTH_MESSAGE,
                    error_type="ZERO_LENGTH_MESSAGE",
                    message=f"Zero-length message declared at message index {msg_index} (offset {offset - 4})",
                    details={
                        "message_index": msg_index,
                        "header_offset": offset - 4,
                        "declared_length": 0,
                    },
                )

            # Check 3: Declared length bounds and overflow check
            remaining_payload = total_len - offset
            if msg_len > remaining_payload:
                return messages, regions, ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.OVERFLOW_LENGTH,
                    error_type="OVERFLOW_LENGTH",
                    message=(
                        f"Message {msg_index} declared length {msg_len} bytes, "
                        f"but only {remaining_payload} bytes remaining in file (offset {offset})"
                    ),
                    details={
                        "message_index": msg_index,
                        "header_offset": offset - 4,
                        "payload_offset": offset,
                        "declared_length": msg_len,
                        "remaining_bytes": remaining_payload,
                        "total_file_size": total_len,
                    },
                )

            # Extract message payload
            payload = data[offset : offset + msg_len]
            offset += msg_len

            # Build region for raw data: [start_byte, end_byte] inclusive closed interval
            start_b = raw_offset
            end_b = raw_offset + msg_len - 1
            regions.append(Region(index=msg_index, start_byte=start_b, end_byte=end_b, length=msg_len))

            raw_offset += msg_len
            messages.append(payload)
            msg_index += 1

        # Check 4: Trailing garbage check
        if offset != total_len:
            return messages, regions, ValidationResult(
                valid=False,
                exit_code=ReprExitCode.TRAILING_GARBAGE,
                error_type="TRAILING_GARBAGE",
                message=f"Unexpected trailing garbage after declared messages: offset={offset}, total={total_len}",
                details={"parsed_offset": offset, "total_size": total_len},
            )

        return messages, regions, None

    def verify_consistency(
        self,
        messages: List[bytes],
        raw_bytes: bytes,
        regions: List[Region],
    ) -> Optional[ValidationResult]:
        """Verify that regions, raw bytes, and messages strictly align."""
        total_raw = len(raw_bytes)

        if not messages:
            if total_raw == 0 and len(regions) == 0:
                return None
            return ValidationResult(
                valid=False,
                exit_code=ReprExitCode.RAW_MISMATCH,
                error_type="REGION_MISMATCH",
                message="Empty message list but non-empty raw data or regions",
            )

        expected_total = sum(len(m) for m in messages)
        if total_raw != expected_total:
            return ValidationResult(
                valid=False,
                exit_code=ReprExitCode.RAW_MISMATCH,
                error_type="RAW_LENGTH_MISMATCH",
                message=f"Raw length ({total_raw}) does not match sum of message lengths ({expected_total})",
                details={"raw_length": total_raw, "sum_message_lengths": expected_total},
            )

        # Region boundary and coverage verification
        for i, reg in enumerate(regions):
            if reg.index != i:
                return ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.RAW_MISMATCH,
                    error_type="REGION_INDEX_INVALID",
                    message=f"Region {i} has mismatched index field {reg.index}",
                )

            if reg.start_byte < 0 or reg.end_byte < reg.start_byte:
                return ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.RAW_MISMATCH,
                    error_type="REGION_RANGE_INVALID",
                    message=f"Region {i} has invalid range [{reg.start_byte}, {reg.end_byte}]",
                )

            expected_len = reg.end_byte - reg.start_byte + 1
            if reg.length != expected_len or reg.length != len(messages[i]):
                return ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.RAW_MISMATCH,
                    error_type="REGION_LENGTH_MISMATCH",
                    message=f"Region {i} length {reg.length} != message length {len(messages[i])}",
                )

            # Verify contiguous boundary
            if i == 0:
                if reg.start_byte != 0:
                    return ValidationResult(
                        valid=False,
                        exit_code=ReprExitCode.RAW_MISMATCH,
                        error_type="REGION_GAP",
                        message=f"First region does not start at 0: start_byte={reg.start_byte}",
                    )
            else:
                prev_end = regions[i - 1].end_byte
                if reg.start_byte != prev_end + 1:
                    return ValidationResult(
                        valid=False,
                        exit_code=ReprExitCode.RAW_MISMATCH,
                        error_type="REGION_GAP_OR_OVERLAP",
                        message=(
                            f"Region {i} start {reg.start_byte} does not immediately follow "
                            f"previous region end {prev_end}"
                        ),
                    )

            # Byte-for-byte slice verification against raw
            slice_data = raw_bytes[reg.start_byte : reg.end_byte + 1]
            if slice_data != messages[i]:
                return ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.RAW_MISMATCH,
                    error_type="REGION_SLICE_MISMATCH",
                    message=f"Region {i} slice in raw data does not match original message payload",
                )

        # Check last region reaches the end of raw
        if regions[-1].end_byte != total_raw - 1:
            return ValidationResult(
                valid=False,
                exit_code=ReprExitCode.RAW_MISMATCH,
                error_type="REGION_INCOMPLETE_COVERAGE",
                message=f"Last region end {regions[-1].end_byte} != raw last byte index {total_raw - 1}",
            )

        return None

    def compare_with_reference_raw(
        self,
        generated_raw: bytes,
        reference_raw: bytes,
    ) -> Optional[ValidationResult]:
        """Compare generated raw with a reference raw file byte-for-byte."""
        gen_len = len(generated_raw)
        ref_len = len(reference_raw)

        if gen_len != ref_len:
            mismatch_offset = min(gen_len, ref_len)
            for idx in range(mismatch_offset):
                if generated_raw[idx] != reference_raw[idx]:
                    mismatch_offset = idx
                    break
            return ValidationResult(
                valid=False,
                exit_code=ReprExitCode.RAW_MISMATCH,
                error_type="RAW_LENGTH_MISMATCH",
                message=f"Generated raw length ({gen_len}) != reference raw length ({ref_len})",
                details={
                    "generated_len": gen_len,
                    "reference_len": ref_len,
                    "first_diff_offset": mismatch_offset,
                },
            )

        for idx in range(gen_len):
            if generated_raw[idx] != reference_raw[idx]:
                ctx_start = max(0, idx - 8)
                ctx_end = min(gen_len, idx + 8)
                return ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.RAW_MISMATCH,
                    error_type="RAW_BYTE_MISMATCH",
                    message=(
                        f"Raw byte mismatch at offset {idx}: "
                        f"generated 0x{generated_raw[idx]:02x} vs reference 0x{reference_raw[idx]:02x}"
                    ),
                    details={
                        "mismatch_offset": idx,
                        "generated_byte": f"0x{generated_raw[idx]:02x}",
                        "reference_byte": f"0x{reference_raw[idx]:02x}",
                        "context_offset": ctx_start,
                        "generated_context": generated_raw[ctx_start:ctx_end].hex(),
                        "reference_context": reference_raw[ctx_start:ctx_end].hex(),
                    },
                )

        return None


def replay_to_raw(replay_bytes: bytes, allow_empty: bool = False) -> Tuple[bytes, List[Region]]:
    """Convenience function to convert replay bytes to raw bytes and regions.

    Raises ValueError on corruption with the attributable error details.
    """
    validator = RepresentationValidator(allow_empty=allow_empty)
    messages, regions, err = validator.parse_replay_bytes(replay_bytes)
    if err is not None:
        raise ValueError(f"[{err.error_type}] {err.message} (code={err.exit_code})")
    raw_bytes = b"".join(messages)
    cons_err = validator.verify_consistency(messages, raw_bytes, regions)
    if cons_err is not None:
        raise ValueError(f"[{cons_err.error_type}] {cons_err.message} (code={cons_err.exit_code})")
    return raw_bytes, regions


def validate_replay_file(
    replay_path: str | Path,
    raw_path: Optional[str | Path] = None,
    output_raw_path: Optional[str | Path] = None,
    output_regions_path: Optional[str | Path] = None,
    max_file_size: int = 64 * 1024 * 1024,
    max_messages: int = 100_000,
    allow_empty: bool = False,
) -> Tuple[ValidationResult, Optional[bytes], List[Region]]:
    """Validate a replay file from disk and optionally check/export raw and regions."""
    replay_p = Path(replay_path)

    if not replay_p.is_file():
        return (
            ValidationResult(
                valid=False,
                exit_code=ReprExitCode.CLI_ERROR,
                error_type="FILE_NOT_FOUND",
                message=f"Replay file does not exist: {replay_path}",
                target=str(replay_path),
            ),
            None,
            [],
        )

    try:
        data = replay_p.read_bytes()
    except Exception as exc:
        return (
            ValidationResult(
                valid=False,
                exit_code=ReprExitCode.CLI_ERROR,
                error_type="READ_ERROR",
                message=f"Failed to read replay file: {exc}",
                target=str(replay_path),
            ),
            None,
            [],
        )

    validator = RepresentationValidator(
        max_file_size=max_file_size,
        max_messages=max_messages,
        allow_empty=allow_empty,
    )

    messages, regions, err = validator.parse_replay_bytes(data)
    if err is not None:
        err.target = str(replay_path)
        return err, None, []

    raw_bytes = b"".join(messages)

    # Internal consistency check
    cons_err = validator.verify_consistency(messages, raw_bytes, regions)
    if cons_err is not None:
        cons_err.target = str(replay_path)
        return cons_err, None, []

    # Check against reference raw file if supplied
    if raw_path is not None:
        raw_p = Path(raw_path)
        if not raw_p.is_file():
            return (
                ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.CLI_ERROR,
                    error_type="RAW_FILE_NOT_FOUND",
                    message=f"Reference raw file does not exist: {raw_path}",
                    target=str(raw_path),
                ),
                raw_bytes,
                regions,
            )
        try:
            ref_raw = raw_p.read_bytes()
        except Exception as exc:
            return (
                ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.CLI_ERROR,
                    error_type="RAW_READ_ERROR",
                    message=f"Failed to read reference raw file: {exc}",
                    target=str(raw_path),
                ),
                raw_bytes,
                regions,
            )

        cmp_err = validator.compare_with_reference_raw(raw_bytes, ref_raw)
        if cmp_err is not None:
            cmp_err.target = str(raw_path)
            return cmp_err, raw_bytes, regions

    # Export raw if requested
    if output_raw_path is not None:
        try:
            out_p = Path(output_raw_path)
            out_p.parent.mkdir(parents=True, exist_ok=True)
            out_p.write_bytes(raw_bytes)
        except Exception as exc:
            return (
                ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.CLI_ERROR,
                    error_type="WRITE_ERROR",
                    message=f"Failed to write output raw file: {exc}",
                    target=str(output_raw_path),
                ),
                raw_bytes,
                regions,
            )

    # Export regions if requested
    if output_regions_path is not None:
        try:
            out_reg_p = Path(output_regions_path)
            out_reg_p.parent.mkdir(parents=True, exist_ok=True)
            reg_dicts = [r.to_dict() for r in regions]
            out_reg_p.write_text(json.dumps(reg_dicts, indent=2), encoding="utf-8")
        except Exception as exc:
            return (
                ValidationResult(
                    valid=False,
                    exit_code=ReprExitCode.CLI_ERROR,
                    error_type="WRITE_ERROR",
                    message=f"Failed to write output regions file: {exc}",
                    target=str(output_regions_path),
                ),
                raw_bytes,
                regions,
            )

    return (
        ValidationResult(
            valid=True,
            exit_code=ReprExitCode.SUCCESS,
            error_type="NONE",
            message=f"Replay verified successfully ({len(messages)} messages, {len(raw_bytes)} raw bytes)",
            target=str(replay_path),
            details={
                "message_count": len(messages),
                "raw_size": len(raw_bytes),
                "replay_size": len(data),
                "region_count": len(regions),
            },
        ),
        raw_bytes,
        regions,
    )


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Offline validation tool for AFLNet replay, raw, and region representations.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--replay", "-i", required=True, help="Input replay file path")
    parser.add_argument("--raw", "-r", default=None, help="Optional reference raw file to verify against")
    parser.add_argument("--output-raw", "-o", default=None, help="Path to write converted raw file")
    parser.add_argument("--output-regions", default=None, help="Path to write extracted regions JSON")
    parser.add_argument("--json-output", default=None, help="Path to write machine-readable validation result")
    parser.add_argument("--max-file-size", type=int, default=64 * 1024 * 1024, help="Maximum allowed replay size in bytes")
    parser.add_argument("--max-messages", type=int, default=100_000, help="Maximum allowed message count")
    parser.add_argument("--allow-empty", action="store_true", help="Allow 0-byte replay files")
    parser.add_argument("--quiet", "-q", action="store_true", help="Suppress output on success")
    parser.add_argument("--verbose", "-v", action="store_true", help="Print detailed diagnostic info")

    args = parser.parse_args(argv)

    result, _, _ = validate_replay_file(
        replay_path=args.replay,
        raw_path=args.raw,
        output_raw_path=args.output_raw,
        output_regions_path=args.output_regions,
        max_file_size=args.max_file_size,
        max_messages=args.max_messages,
        allow_empty=args.allow_empty,
    )

    result.print_summary(verbose=args.verbose, quiet=args.quiet)

    if args.json_output:
        try:
            out_json = Path(args.json_output)
            out_json.parent.mkdir(parents=True, exist_ok=True)
            out_json.write_text(result.to_json(), encoding="utf-8")
        except Exception as exc:
            print(f"[FAIL] Unable to write JSON report to {args.json_output}: {exc}", file=sys.stderr)
            return ReprExitCode.CLI_ERROR

    return result.exit_code


if __name__ == "__main__":
    sys.exit(main())
