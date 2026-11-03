# Native repair verification evidence

Source commit: `799351e7da7aa4515e819d5a1590ed4c82b78c26`, with code changes in `731beb1`, `013d59c`, and `799351e`, based on `42bf625`. The later documentation commit only records these results. Source hashes bind the actual files used for compilation; binaries and temporary fixtures remain under ignored test output directories.

- `summary.json`: scope, source commits and aggregate results.
- `build/`: final full native build log and exit status, final LLVM 18 smoke log, earlier full LLVM 18 rebuild log, CLI help check, compiler versions and source/binary hashes.
- `native-recovery/`: all 80 cases passed in each of two builds; commands, real harness compiler output, retained symbols, aggregate per-case output, and tested source hashes. The original run directory named in results is local generated output, not a required path for reproducing the test.
- `native-net-send/`: actual local socket tests, ten groups per build.
- `native-prompt/`: real json-c plus individual fault injection, eighteen cases per build.
- `SHA256SUMS`: checksums of recorded files and tested source files, relative to the repository root.

The harness includes the real `afl-fuzz.c` helpers and links the actual native sources. Symbol checks ensure the fuzzer main and external model execution paths were removed from the recovery test binary. It covers file imports, empty record boundaries, companion mismatches, publication errors, staging interrupted by orphan backups, and format-aware enrichment output. The three actual pivot failure cases preserve all original recovery files after a raw output has already been published.

Network/prompt scripts also passed with NDEBUG, ASan/UBSan, and their combination; all test checks stay active. The independent codec/checker suite is documented separately under `tools/format_audit/evidence`; it is not counted as native coverage.

Verify from this repository root:

```bash
sha256sum -c tests/evidence/recovery-integrity-20261103/SHA256SUMS
(cd tools/format_audit && sha256sum -c evidence/SHA256SUMS)
```

Reproduce using the commands in [the repair report](../../../docs/REPAIR_VERIFICATION_20261103.md). Compilation and fixed regressions do not establish fuzzing effectiveness, arbitrary binary LLM support, directory-wide crash consistency, or 32-bit runtime compatibility. There were no target fuzzing campaigns or online LLM requests in this verification.
