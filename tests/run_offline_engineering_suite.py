#!/usr/bin/env python3
"""Unified Offline Engineering Test Suite for LLM-CLOFuzz.

This test runner executes all offline verification suites:
1. C Reliability fixes (S01-S05) unit tests (Python harness + compiled C unit tests via MinGW GCC)
2. Offline representation and saved response validation tests (46 test cases)
3. Offline benchmark analysis and evaluation governance tests (E01-E08, 12 test cases)

Purely offline execution:
- NO online LLM calls or API keys
- NO fuzzer or network target execution
- NO benchmark campaign execution
"""

from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def run_command(cmd: list[str], desc: str) -> tuple[int, str]:
    print(f"\n{'='*72}")
    print(f"[*] Running: {desc}")
    print(f"    Command: {' '.join(cmd)}")
    print(f"{'='*72}")
    start = time.time()
    proc = subprocess.run(cmd, cwd=str(REPO_ROOT), capture_output=True, text=True)
    elapsed = time.time() - start
    print(proc.stdout)
    if proc.stderr:
        print("[stderr]", proc.stderr)
    print(f"[-] {desc}: Exit Code {proc.returncode} in {elapsed:.2f}s")
    return proc.returncode, proc.stdout + ("\n" + proc.stderr if proc.stderr else "")


def main() -> int:
    overall_start = time.time()
    print("=" * 72)
    print("  LLM-CLOFUZZ OFFLINE ENGINEERING SUITE VERIFICATION")
    print(f"  Root: {REPO_ROOT}")
    print("=" * 72)

    results = []

    # 1. Run S01-S05 C Reliability Tests
    rc1, out1 = run_command(
        [sys.executable, "tests/test_s01_s05.py"],
        "S01-S05 C Reliability & Boundary Protection (Python + C harness)"
    )
    results.append(("S01-S05 C Reliability", rc1 == 0, rc1))

    # 2. Run Offline Representation & Response Validation Tests
    rc2, out2 = run_command(
        [sys.executable, "tests/offline_validation/run_offline_validation_tests.py"],
        "Representation & Saved Model Response Validation Suite (46 tests)"
    )
    results.append(("Representation & Response Validation", rc2 == 0, rc2))

    # 3. Run Offline Analytics & Evaluation Governance Tests
    rc3, out3 = run_command(
        [sys.executable, "tools/offline_analysis/test_offline_analysis.py"],
        "Evaluation Governance & Offline Analysis Suite (E01-E08, 12 tests)"
    )
    results.append(("Offline Analysis Governance", rc3 == 0, rc3))

    total_elapsed = time.time() - overall_start

    print("\n" + "=" * 72)
    print("  OVERALL TEST EXECUTION SUMMARY")
    print("=" * 72)
    all_passed = True
    for name, ok, rc in results:
        status_str = "PASS [OK]" if ok else f"FAIL [RC={rc}]"
        print(f"  - {name:<45} : {status_str}")
        if not ok:
            all_passed = False

    print("-" * 72)
    if all_passed:
        print(f"ALL SUITES PASSED SUCCESSFULLY in {total_elapsed:.2f}s")
        return 0
    else:
        print(f"SOME SUITES FAILED in {total_elapsed:.2f}s")
        return 1


if __name__ == "__main__":
    sys.exit(main())
