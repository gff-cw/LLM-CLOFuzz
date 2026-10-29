#!/usr/bin/env python3
"""Unified test runner for all offline validation tests in LLM-CLOFuzz.

Executes tests for:
1. Replay, raw, and region format validation
2. Saved LLM responses and grammar JSON validation

Can be executed standalone or integrated into CI without online models or fuzzer runtime.
"""

from __future__ import annotations

import argparse
import sys
import time
import unittest
from pathlib import Path

# Setup paths
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tests.offline_validation.generate_fixtures import generate_all_fixtures


def run_tests(verbose: bool = True) -> int:
    start_time = time.time()
    print("=" * 72)
    print(" LLM-CLOFuzz Offline Validation Test Suite")
    print("=" * 72)

    # Step 1: Ensure test fixtures are generated
    print("[1/2] Ensuring test fixtures are generated...")
    generate_all_fixtures()
    print("      Done.")

    # Step 2: Load and run test suite
    print("[2/2] Running offline validation test cases...")
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    from tests.offline_validation.test_validate_representation import TestValidateRepresentation
    from tests.offline_validation.test_validate_saved_responses import TestValidateSavedResponses

    suite.addTests(loader.loadTestsFromTestCase(TestValidateRepresentation))
    suite.addTests(loader.loadTestsFromTestCase(TestValidateSavedResponses))

    verbosity = 2 if verbose else 1
    runner = unittest.TextTestRunner(verbosity=verbosity)
    result = runner.run(suite)

    elapsed = time.time() - start_time
    total = result.testsRun
    failures = len(result.failures)
    errors = len(result.errors)
    passed = total - failures - errors

    print("-" * 72)
    print(f"Summary: {passed}/{total} passed, {failures} failed, {errors} errors in {elapsed:.3f}s")
    if result.wasSuccessful():
        print("RESULT: ALL OFFLINE VALIDATION TESTS PASSED [OK]")
        return 0
    else:
        print("RESULT: TEST SUITE FAILED [FAIL]")
        return 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Run offline validation test suite.")
    parser.add_argument("--quiet", "-q", action="store_true", help="Quiet output")
    parser.add_argument("--generate-only", action="store_true", help="Only generate fixtures and exit")
    args = parser.parse_args()

    if args.generate_only:
        generate_all_fixtures()
        print("Test fixtures generated successfully.")
        return 0

    return run_tests(verbose=not args.quiet)


if __name__ == "__main__":
    sys.exit(main())
