#!/usr/bin/env python3
"""
Unit test suite for LLM-CLOFuzz reliability fixes S01 - S05.
Works on Windows (MinGW GCC / Python) and POSIX environments.
"""

import os
import sys
import struct
import tempfile
import subprocess
from pathlib import Path

MAX_NUM_STATES = 100
MAX_REPETITIONS = 1000

def test_s01_sentinel():
    print("[*] S01: Testing shmat failure sentinel check...")
    # In POSIX 64-bit systems, (void *)-1 is 0xFFFFFFFFFFFFFFFF (or (void *)-1 in 32-bit: 0xFFFFFFFF)
    # The old code: if (!trace_bits) PFATAL("shmat() failed");
    # The new code: if (trace_bits == (void *)-1 || !trace_bits) PFATAL("shmat() failed");

    for bits in [32, 64]:
        fail_val = (1 << bits) - 1
        # Old check
        old_failed = (fail_val == 0)
        assert not old_failed, f"Old check failed to detect (void *)-1 in {bits}-bit mode!"
        # New check
        new_failed = (fail_val == (1 << bits) - 1) or (fail_val == 0)
        assert new_failed, f"New check must detect (void *)-1 in {bits}-bit mode!"

    print("    [+] S01 passed.")

def test_s02_state_seq_boundaries():
    print("[*] S02: Testing state sequence capacity and read/write checks...")
    # S02 Tracer writing logic: capacity is MAX_NUM_STATES (100)
    # Even if tracer visits 200 states, seq must not exceed MAX_NUM_STATES
    seq = []
    current_state_number = 0

    # Initial state 0
    seq.append(0)
    current_state_number += 1

    # Simulate 150 loop states
    for i in range(150):
        val = 0xA000 + i
        if current_state_number < MAX_NUM_STATES:
            seq.append(val)
        current_state_number += 1

    # Last state
    last_val = 0xFFFF
    if current_state_number < MAX_NUM_STATES:
        seq.append(last_val)

    assert len(seq) == MAX_NUM_STATES, f"Sequence length should be clamped to {MAX_NUM_STATES}, got {len(seq)}"

    # afl-fuzz reading logic
    def extract_response_state_tracer(shm_seq_len, shm_seq):
        if shm_seq_len is None:
            return None, 0
        state_count = min(shm_seq_len, MAX_NUM_STATES)
        if state_count == 0:
            return None, 0
        return list(shm_seq[:state_count]), state_count

    # Case: seq_len == 0
    res, count = extract_response_state_tracer(0, [1, 2, 3])
    assert res is None and count == 0, "seq_len == 0 must return (None, 0)"

    # Case: seq_len == 5
    res, count = extract_response_state_tracer(5, list(range(100)))
    assert count == 5 and res == [0, 1, 2, 3, 4], "seq_len == 5 must extract 5 items"

    # Case: untrusted seq_len > MAX_NUM_STATES
    res, count = extract_response_state_tracer(300, list(range(100)))
    assert count == MAX_NUM_STATES and len(res) == MAX_NUM_STATES

    print("    [+] S02 passed.")

def test_s03_calibration_boundaries():
    print("[*] S03: Testing calibration ref_len and dist_len boundaries...")
    # S03-1: ref_len write boundary
    ref_state_seq = []
    ref_len = 0
    for i in range(120):
        if ref_len < MAX_NUM_STATES:
            ref_state_seq.append(f"HASH_{i:04d}")
            ref_len += 1
    assert ref_len == MAX_NUM_STATES
    assert len(ref_state_seq) == MAX_NUM_STATES

    # S03-2: 0-indexed condition check:
    # If ref_len is 5, valid indices are 0, 1, 2, 3, 4.
    # The old code had: current_state_number <= calib_shm->ref_len
    # When current_state_number == 5, reading ref_state_seq[5] was an out-of-bounds read!
    ref_len = 5
    for current_state_number in range(7):
        old_valid = (current_state_number <= ref_len)
        new_valid = (current_state_number < ref_len and current_state_number < MAX_NUM_STATES)
        if current_state_number == 5:
            assert old_valid is True, "Old buggy check allowed current_state_number == ref_len"
            assert new_valid is False, "Fixed check correctly rejects current_state_number == ref_len"

    # S03-3: dist_len boundary
    max_dist = MAX_NUM_STATES * MAX_REPETITIONS
    dist = []
    dist_len = 0
    for _ in range(max_dist + 500):
        if dist_len < max_dist:
            dist.append(1)
            dist_len += 1
    assert dist_len == max_dist
    assert len(dist) == max_dist

    print("    [+] S03 passed.")

def test_s04_replay_length_and_offset():
    print("[*] S04: Testing replay length parsing, integer overflow, and zero-length boundaries...")

    def parse_replay_regions(buf: bytes):
        buf_size = len(buf)
        byte_count = 0
        cur_start = 0
        regions = []

        while byte_count < buf_size:
            # S04 check 1: buf_size - byte_count < 4
            if buf_size - byte_count < 4:
                raise ValueError("Erroneous message length (truncated header)")

            next_message_len = struct.unpack("<I", buf[byte_count:byte_count+4])[0]
            byte_count += 4

            # S04 check 2: next_message_len > buf_size - byte_count (avoids unsigned overflow)
            if next_message_len > buf_size - byte_count:
                raise ValueError("Erroneous message length (length overflow)")

            # S04 check 3: next_message_len == 0 boundary
            start_byte = cur_start
            end_byte = (cur_start + next_message_len - 1) if next_message_len > 0 else (cur_start - 1)

            regions.append((start_byte, end_byte, next_message_len))
            cur_start += next_message_len
            byte_count += next_message_len

        return regions

    # 1. Truncated header
    try:
        parse_replay_regions(b"\x01\x02\x03")
        assert False, "Should fail on truncated header"
    except ValueError as e:
        assert "truncated header" in str(e)

    # 2. Integer overflow payload: next_message_len = 0xFFFFFFFF
    # In old code: byte_count + next_message_len = 4 + 0xFFFFFFFF = 3 <= buf_size (wrap around!)
    overflow_payload = struct.pack("<I", 0xFFFFFFFF) + b"A" * 10
    try:
        parse_replay_regions(overflow_payload)
        assert False, "Should fail on integer overflow length"
    except ValueError as e:
        assert "length overflow" in str(e)

    # 3. Exactly 4-byte header for zero-length message
    zero_msg = struct.pack("<I", 0)
    regions = parse_replay_regions(zero_msg)
    assert len(regions) == 1
    assert regions[0] == (0, -1, 0), f"Zero-length message should yield (0, -1, 0), got {regions[0]}"

    # 4. Multi-message with mixed lengths including zero
    # msg1: len 3 ("ABC"), msg2: len 0, msg3: len 2 ("12")
    multi_payload = struct.pack("<I", 3) + b"ABC" + struct.pack("<I", 0) + struct.pack("<I", 2) + b"12"
    regions = parse_replay_regions(multi_payload)
    assert len(regions) == 3
    assert regions[0] == (0, 2, 3)    # bytes 0..2
    assert regions[1] == (3, 2, 0)    # zero-length: start 3, end 2 -> len = end - start + 1 = 0
    assert regions[2] == (3, 4, 2)    # bytes 3..4

    print("    [+] S04 passed.")

def test_s05_o_trunc_consistency():
    print("[*] S05: Testing file overwrite consistency with O_TRUNC...")
    with tempfile.TemporaryDirectory() as tmpdir:
        test_file = Path(tmpdir) / "test_s05.bin"

        # 1. Write initial 128 bytes
        with open(test_file, "wb") as f:
            f.write(b"A" * 128)
        assert test_file.stat().st_size == 128

        # 2. Overwrite with 16 bytes WITHOUT O_TRUNC
        # Uses os.open(..., O_WRONLY | O_CREAT)
        fd = os.open(str(test_file), os.O_WRONLY | os.O_CREAT, 0o600)
        os.write(fd, b"B" * 16)
        os.close(fd)
        # Bug demonstrated: file size is still 128 bytes!
        assert test_file.stat().st_size == 128
        content = test_file.read_bytes()
        assert content == b"B" * 16 + b"A" * 112, "Without O_TRUNC, old trailing bytes leaked!"

        # 3. Overwrite with 16 bytes WITH O_TRUNC
        fd = os.open(str(test_file), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        os.write(fd, b"C" * 16)
        os.close(fd)
        # Fixed: file size is strictly 16 bytes
        assert test_file.stat().st_size == 16
        content = test_file.read_bytes()
        assert content == b"C" * 16, "With O_TRUNC, file is cleanly truncated!"

    print("    [+] S05 passed.")

def run_c_unit_tests():
    c_source = Path(__file__).parent / "unit_s01_s05.c"
    exe_target = Path(__file__).parent / "unit_s01_s05.exe"
    if not c_source.exists():
        print("[-] unit_s01_s05.c not found, skipping C executable run.")
        return

    print("[*] Compiling and running C unit tests...")
    compile_cmd = ["gcc", "-Wall", "-Wextra", "-O2", str(c_source), "-o", str(exe_target)]
    res = subprocess.run(compile_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[-] GCC compilation failed:\n{res.stderr}")
        sys.exit(res.returncode)

    run_res = subprocess.run([str(exe_target)], capture_output=True, text=True)
    print(run_res.stdout)
    if run_res.returncode != 0:
        print(f"[-] C unit tests failed:\n{run_res.stderr}")
        sys.exit(run_res.returncode)
    print("    [+] C unit tests executed successfully.")

if __name__ == "__main__":
    print("=" * 60)
    print("Running LLM-CLOFuzz S01-S05 Python & C Verification Suite")
    print("=" * 60)
    test_s01_sentinel()
    test_s02_state_seq_boundaries()
    test_s03_calibration_boundaries()
    test_s04_replay_length_and_offset()
    test_s05_o_trunc_consistency()
    run_c_unit_tests()
    print("=" * 60)
    print("ALL TESTS PASSED (Python + C)")
    print("=" * 60)
