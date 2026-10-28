#!/usr/bin/env python3
"""Real runtime TLSH/MVP/state/calibration SHM regression (Linux x86_64)."""
import ctypes as c
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


class State(c.Structure):
    _fields_ = [("seq_len", c.c_uint), ("seq", c.c_uint * 100),
                ("iterations", c.c_uint)]


class Calibration(c.Structure):
    _fields_ = [("enabled", c.c_int), ("initialized", c.c_int),
                ("ref_len", c.c_int), ("ref_state_seq", (c.c_char * 73) * 100),
                ("dist_len", c.c_int), ("dist", c.c_int * 100000),
                ("mvp_radius", c.c_int), ("rate_limit", c.c_int)]


def main():
    fixture = str(Path(sys.argv[1]).resolve())
    evidence = Path(sys.argv[2]).resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    # Repeating the test must begin with an empty tree while preserving earlier
    # logs as evidence, including the logs from failed runs.
    out = Path(tempfile.mkdtemp(prefix="run-", dir=evidence))
    lib = c.CDLL(None, use_errno=True)
    lib.shmget.argtypes = [c.c_int, c.c_size_t, c.c_int]
    lib.shmget.restype = c.c_int
    lib.shmat.argtypes = [c.c_int, c.c_void_p, c.c_int]
    lib.shmat.restype = c.c_void_p
    lib.shmdt.argtypes = [c.c_void_p]
    lib.shmctl.argtypes = [c.c_int, c.c_int, c.c_void_p]
    allocated = []

    def shared(kind):
        ident = lib.shmget(0, c.sizeof(kind), 0o1000 | 0o600)
        assert ident >= 0, os.strerror(c.get_errno())
        address = lib.shmat(ident, None, 0)
        assert address != c.c_void_p(-1).value
        allocated.append((ident, address))
        return ident, kind.from_address(address)

    try:
        state_id, state = shared(State)
        calib_id, calib = shared(Calibration)
        assert c.sizeof(State) == 408 and c.sizeof(Calibration) == 407324
        environment = dict(os.environ, STATE_SHM_ID=str(state_id),
                           MVP_CALIBRATION_SHM_ID=str(calib_id),
                           AFL_OUTDIR=str(out))
        calib.mvp_radius = 1
        results = []

        def run(name, variant):
            c.memset(c.addressof(state), 0, c.sizeof(state))
            # This is exactly afl-fuzz's initial-state reset contract.
            state.seq_len = 1
            state.seq[0] = 0
            completed = subprocess.run([fixture, str(variant)], env=environment,
                                       capture_output=True, text=True, timeout=15)
            (out / (name + ".stdout")).write_text(completed.stdout)
            (out / (name + ".stderr")).write_text(completed.stderr)
            assert completed.returncode == 0, (name, completed.returncode,
                                               completed.stderr)
            sequence = list(state.seq[:state.seq_len])
            assert state.iterations == 3, (name, state.iterations)
            assert state.seq_len == 4 and sequence[0] == 0, (name, sequence)
            result = {"name": name, "iterations": state.iterations,
                      "sequence": sequence, "reference_length": calib.ref_len,
                      "distance_length": calib.dist_len}
            results.append(result)
            return sequence

        initial = run("initial", 0)
        assert all(value > 0 for value in initial[1:]), initial
        count = int((out / ".tree.count.mvp").read_text())
        repeat = run("repeat", 0)
        assert repeat == initial, (initial, repeat)
        assert int((out / ".tree.count.mvp").read_text()) == count
        branch = run("branch", 1)
        assert branch != initial, (initial, branch)
        assert int((out / ".tree.count.mvp").read_text()) > count
        assert run("branch_repeat", 1) == branch
        tree_before_calibration = (out / ".tree.mvp").read_bytes()
        calib.enabled = 1
        calib.initialized = calib.ref_len = calib.dist_len = 0
        run("calibration_reference", 0)
        assert calib.initialized == 1 and calib.ref_len == 3
        hashes = [bytes(calib.ref_state_seq[i]).split(b"\0")[0].decode()
                  for i in range(calib.ref_len)]
        assert all(len(value) == 72 and value.startswith("T1") for value in hashes), hashes
        run("calibration_repeat", 0)
        distances = list(calib.dist[:calib.dist_len])
        assert len(distances) == 3 and distances == [0, 0, 0], distances
        assert (out / ".tree.mvp").read_bytes() == tree_before_calibration
        calib.enabled = 0
        assert run("after_calibration", 0) == initial
        report = {"shared_sizes": [c.sizeof(State), c.sizeof(Calibration)],
                  "evidence_directory": str(out),
                  "runs": results, "reference_hashes": hashes,
                  "calibration_distances": distances,
                  "unique_states": int((out / ".tree.count.mvp").read_text())}
        (out / "state-feedback.json").write_text(json.dumps(report, indent=2) + "\n")
        (evidence / "state-feedback.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
    finally:
        for ident, address in allocated:
            lib.shmdt(address)
            lib.shmctl(ident, 0, None)


if __name__ == "__main__":
    main()
