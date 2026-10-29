"""Fixture generator for offline validation test cases.

Generates deterministic normal and corrupted test files for:
1. Representation tests (replay, raw, region)
2. Saved response tests (API responses, grammar JSON, message types, stagnation)
"""

from __future__ import annotations

import json
import os
import struct
from pathlib import Path

FIXTURES_DIR = Path(__file__).resolve().parent / "fixtures"
REPLAY_NORMAL = FIXTURES_DIR / "replay_normal"
REPLAY_CORRUPTED = FIXTURES_DIR / "replay_corrupted"
RESPONSES_NORMAL = FIXTURES_DIR / "saved_responses_normal"
RESPONSES_CORRUPTED = FIXTURES_DIR / "saved_responses_corrupted"


def make_replay_bytes(messages: list[bytes]) -> bytes:
    """Pack a list of payload bytes into AFLNet replay format."""
    out = bytearray()
    for m in messages:
        out.extend(struct.pack("<I", len(m)))
        out.extend(m)
    return bytes(out)


def generate_representation_fixtures() -> None:
    """Generate all replay and raw normal and corrupted fixtures."""
    REPLAY_NORMAL.mkdir(parents=True, exist_ok=True)
    REPLAY_CORRUPTED.mkdir(parents=True, exist_ok=True)

    # 1. Normal: Single message
    m1 = [b"HELLO WORLD IN REPLAY FORMAT\r\n"]
    (REPLAY_NORMAL / "single_msg.replay").write_bytes(make_replay_bytes(m1))
    (REPLAY_NORMAL / "single_msg.raw").write_bytes(b"".join(m1))

    # 2. Normal: Multi message RTSP sequence
    m2 = [
        b"DESCRIBE rtsp://127.0.0.1:8554/live.sdp RTSP/1.0\r\nCSeq: 1\r\n\r\n",
        b"SETUP rtsp://127.0.0.1:8554/live.sdp/track1 RTSP/1.0\r\nCSeq: 2\r\nTransport: RTP/AVP;unicast;client_port=8000-8001\r\n\r\n",
        b"PLAY rtsp://127.0.0.1:8554/live.sdp RTSP/1.0\r\nCSeq: 3\r\nSession: 12345678\r\n\r\n",
        b"TEARDOWN rtsp://127.0.0.1:8554/live.sdp RTSP/1.0\r\nCSeq: 4\r\nSession: 12345678\r\n\r\n",
    ]
    (REPLAY_NORMAL / "multi_msg_rtsp.replay").write_bytes(make_replay_bytes(m2))
    (REPLAY_NORMAL / "multi_msg_rtsp.raw").write_bytes(b"".join(m2))

    # 3. Normal: Binary payloads with null bytes and control chars
    m3 = [
        bytes(range(0, 32)),
        bytes(range(32, 128)),
        bytes([0xFF, 0xFE, 0xFD, 0x00, 0x01, 0x02]),
    ]
    (REPLAY_NORMAL / "binary_payloads.replay").write_bytes(make_replay_bytes(m3))
    (REPLAY_NORMAL / "binary_payloads.raw").write_bytes(b"".join(m3))

    # 4. Normal: 1-byte message boundary case
    m4 = [b"A", b"B", b"C"]
    (REPLAY_NORMAL / "minimal_boundary.replay").write_bytes(make_replay_bytes(m4))
    (REPLAY_NORMAL / "minimal_boundary.raw").write_bytes(b"".join(m4))

    # ------------------ Corrupted Replay Fixtures ------------------

    # 1. Truncated header: file has only 2 bytes total (< 4 bytes)
    (REPLAY_CORRUPTED / "truncated_header.replay").write_bytes(b"\x05\x00")

    # 2. Truncated header mid-stream: message 1 valid, message 2 has only 3 bytes of header
    valid_m = make_replay_bytes([b"FIRST MESSAGE"])
    (REPLAY_CORRUPTED / "truncated_mid_header.replay").write_bytes(valid_m + b"\x0a\x00\x00")

    # 3. Overflow length: declared length is 500000, but only 10 bytes payload present
    overflow_data = bytearray(struct.pack("<I", 500000))
    overflow_data.extend(b"0123456789")
    (REPLAY_CORRUPTED / "overflow_length.replay").write_bytes(bytes(overflow_data))

    # 4. Zero-length message: declared length is 0 (violates AFLNet non-empty constraint)
    (REPLAY_CORRUPTED / "zero_length_message.replay").write_bytes(struct.pack("<I", 0))

    # 5. Empty file: 0 bytes
    (REPLAY_CORRUPTED / "empty_file.replay").write_bytes(b"")

    # 6. Raw mismatch: content byte flipped
    m_flip = [b"ABCDEFGH12345678", b"IJKLMNOP87654321"]
    (REPLAY_CORRUPTED / "raw_mismatch_content.replay").write_bytes(make_replay_bytes(m_flip))
    # Flip byte at index 5 from 'F' to 'X'
    flipped_raw = bytearray(b"".join(m_flip))
    flipped_raw[5] = ord("X")
    (REPLAY_CORRUPTED / "raw_mismatch_content.raw").write_bytes(bytes(flipped_raw))

    # 7. Raw mismatch: length differs (truncated raw)
    m_len = [b"AAAAABBBBB", b"CCCCCDDDDD"]
    (REPLAY_CORRUPTED / "raw_mismatch_length.replay").write_bytes(make_replay_bytes(m_len))
    (REPLAY_CORRUPTED / "raw_mismatch_length.raw").write_bytes(b"".join(m_len)[:-4])


def generate_saved_responses_fixtures() -> None:
    """Generate normal and corrupted JSON saved response fixtures."""
    RESPONSES_NORMAL.mkdir(parents=True, exist_ok=True)
    RESPONSES_CORRUPTED.mkdir(parents=True, exist_ok=True)

    # 1. Normal: OpenAI Chat Completion response (RTSP)
    chat_rtsp = {
        "id": "chatcmpl-test-001",
        "object": "chat.completion",
        "created": 1700000000,
        "model": "gpt-3.5-turbo",
        "choices": [
            {
                "index": 0,
                "message": {
                    "role": "assistant",
                    "content": "DESCRIBE: [\"DESCRIBE <<VALUE>> RTSP/1.0\\r\\n\", \"CSeq: <<VALUE>>\\r\\n\", \"\\r\\n\"]",
                },
                "finish_reason": "stop",
            }
        ],
        "usage": {"prompt_tokens": 50, "completion_tokens": 30, "total_tokens": 80},
    }
    (RESPONSES_NORMAL / "chat_completion_rtsp.json").write_text(json.dumps(chat_rtsp, indent=2), encoding="utf-8")

    # 2. Normal: Text completion response (instruct model)
    compl_data = {
        "id": "cmpl-test-002",
        "object": "text_completion",
        "choices": [
            {
                "text": "\nGET: [\"GET <<VALUE>> HTTP/1.1\\r\\n\", \"Host: <<VALUE>>\\r\\n\", \"\\r\\n\"]",
                "index": 0,
            }
        ],
    }
    (RESPONSES_NORMAL / "completion_instruct.json").write_text(json.dumps(compl_data, indent=2), encoding="utf-8")

    # 3. Normal: Grammar array (standard ChatAFL template format)
    grammar_array = [
        [
            "DESCRIBE",
            "DESCRIBE <<VALUE>> RTSP/1.0\r\n",
            "CSeq: <<VALUE>>\r\n",
            "Accept: application/sdp\r\n",
            "\r\n",
        ],
        [
            "SETUP",
            "SETUP <<VALUE>> RTSP/1.0\r\n",
            "CSeq: <<VALUE>>\r\n",
            "Transport: RTP/AVP;unicast;client_port=<<VALUE>>\r\n",
            "\r\n",
        ],
        [
            "PLAY",
            "PLAY <<VALUE>> RTSP/1.0\r\n",
            "CSeq: <<VALUE>>\r\n",
            "Session: <<VALUE>>\r\n",
            "\r\n",
        ],
    ]
    (RESPONSES_NORMAL / "grammar_rtsp_array.json").write_text(json.dumps(grammar_array, indent=2), encoding="utf-8")

    # 4. Normal: Grammar dictionary format
    grammar_dict = {
        "GET": [
            "GET <<VALUE>> HTTP/1.1\r\n",
            "Host: <<VALUE>>\r\n",
            "User-Agent: <<VALUE>>\r\n",
            "\r\n",
        ],
        "POST": [
            "POST <<VALUE>> HTTP/1.1\r\n",
            "Host: <<VALUE>>\r\n",
            "Content-Length: <<VALUE>>\r\n",
            "\r\n",
        ],
    }
    (RESPONSES_NORMAL / "grammar_http_dict.json").write_text(json.dumps(grammar_dict, indent=2), encoding="utf-8")

    # 5. Normal: Message types list
    msg_types = ["DESCRIBE", "SETUP", "PLAY", "PAUSE", "TEARDOWN", "OPTIONS"]
    (RESPONSES_NORMAL / "message_types_list.json").write_text(json.dumps(msg_types, indent=2), encoding="utf-8")

    # 6. Normal: Stagnation intervention response
    stagnation_data = {
        "message": "PLAY rtsp://127.0.0.1:8554/live.sdp RTSP/1.0\r\nCSeq: 4\r\nSession: 000022B8\r\n\r\n"
    }
    (RESPONSES_NORMAL / "stagnation_intervention.json").write_text(json.dumps(stagnation_data, indent=2), encoding="utf-8")

    # ------------------ Corrupted Saved Responses ------------------

    # 1. Invalid JSON syntax (unclosed array)
    (RESPONSES_CORRUPTED / "syntax_unclosed_array.json").write_bytes(b'{"choices": [ {"message": "unclosed')

    # 2. Invalid escape: trailing standalone backslash
    (RESPONSES_CORRUPTED / "syntax_trailing_backslash.json").write_bytes(b'{"choices": []}\\')

    # 3. Embedded raw null byte
    (RESPONSES_CORRUPTED / "syntax_embedded_null.json").write_bytes(b'{"message": "hello\x00world"}')

    # 4. Prohibited escaped null byte (\u0000)
    (RESPONSES_CORRUPTED / "syntax_escaped_null.json").write_bytes(b'{"message": "hello\\u0000world"}')

    # 5. Invalid UTF-8 bytes
    (RESPONSES_CORRUPTED / "syntax_invalid_utf8.json").write_bytes(b'{"choices": "\xff\xfe\x80\x81"}')

    # 6. Missing mandatory choices field in chat API response
    (RESPONSES_CORRUPTED / "schema_missing_choices.json").write_text('{"id": "test", "usage": {}}', encoding="utf-8")

    # 7. Choices field is empty array (requires >= 1)
    (RESPONSES_CORRUPTED / "schema_choices_empty.json").write_text('{"choices": []}', encoding="utf-8")

    # 8. Choices field is not a list (string)
    (RESPONSES_CORRUPTED / "schema_choices_not_list.json").write_text('{"choices": "not a list"}', encoding="utf-8")

    # 9. Choice entry is not a dict
    (RESPONSES_CORRUPTED / "schema_choice_not_dict.json").write_text('{"choices": ["string_item"]}', encoding="utf-8")

    # 10. Message field in choice is missing
    (RESPONSES_CORRUPTED / "schema_missing_message.json").write_text('{"choices": [{"index": 0}]}', encoding="utf-8")

    # 11. Message field in choice is null (Audit C03)
    (RESPONSES_CORRUPTED / "null_message.json").write_text('{"choices": [{"message": null}]}', encoding="utf-8")

    # 12. Content field in message is null (Audit C03)
    (RESPONSES_CORRUPTED / "null_content.json").write_text('{"choices": [{"message": {"role": "assistant", "content": null}}]}', encoding="utf-8")

    # 13. Content field in message is integer instead of string
    (RESPONSES_CORRUPTED / "wrong_type_content.json").write_text('{"choices": [{"message": {"role": "assistant", "content": 12345}}]}', encoding="utf-8")

    # 14. Grammar: empty template array
    (RESPONSES_CORRUPTED / "grammar_empty_template.json").write_text('[ [] ]', encoding="utf-8")

    # 15. Grammar: header at index 0 is integer instead of string (Audit C06)
    (RESPONSES_CORRUPTED / "grammar_header_not_string.json").write_text('[ [9999, "field1"] ]', encoding="utf-8")

    # 16. Grammar: header is empty string
    (RESPONSES_CORRUPTED / "grammar_header_empty.json").write_text('[ ["", "field1"] ]', encoding="utf-8")

    # 17. Grammar: field is null
    (RESPONSES_CORRUPTED / "grammar_field_null.json").write_text('[ ["DESCRIBE", null] ]', encoding="utf-8")

    # 18. Grammar: field is not a string
    (RESPONSES_CORRUPTED / "grammar_field_not_string.json").write_text('[ ["DESCRIBE", {"bad": "field"}] ]', encoding="utf-8")

    # 19. Grammar: mismatched placeholder brackets
    (RESPONSES_CORRUPTED / "grammar_mismatched_placeholder.json").write_text('[ ["DESCRIBE", "DESCRIBE <<VALUE RTSP/1.0\\r\\n"] ]', encoding="utf-8")

    # 20. Resource: single string exceeding 64KB limit
    long_str = "A" * 70_000
    (RESPONSES_CORRUPTED / "resource_string_too_long.json").write_text(json.dumps({"message": long_str}), encoding="utf-8")

    # 21. Resource: deeply nested arrays (exceeds max depth 32)
    nested = "hello"
    for _ in range(40):
        nested = [nested]
    (RESPONSES_CORRUPTED / "resource_deep_nesting.json").write_text(json.dumps(nested), encoding="utf-8")


def generate_all_fixtures() -> None:
    """Generate all fixtures in deterministic order."""
    generate_representation_fixtures()
    generate_saved_responses_fixtures()


if __name__ == "__main__":
    generate_all_fixtures()
    print(f"Successfully generated all fixtures in: {FIXTURES_DIR}")
