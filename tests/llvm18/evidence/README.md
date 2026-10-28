# LLVM18 evidence

Latest system-command rebuild: see [system-toolchain](system-toolchain/README.md),
with its own SHA256 manifest. The files below preserve the first migration run.

These are outputs from the actual Ubuntu 24.04 / LLVM 18.1.3 runs, not expected
output templates. `verified-suite.log` records the final complete suite;
`e2e-result.json`, `state-feedback.json`, `ipsm.dot` and `fuzzer_stats` provide
independent feedback evidence. `*-commands.json` records executed argv.

Failure logs are retained alongside the corrected results. The first full
campaign failed because the test server used TCP receive chunks as messages;
`stream-framing-before.log` and `stream-framing.json` show the diagnosis and fix.
The final campaign used the corrected two-byte protocol and no forced radius.

Archived text has terminal ANSI color escapes and trailing whitespace removed
for readable review. Original outputs and binaries remain locally under
`tests/llvm18/out/verified-suite`, earlier `tests/llvm18/out` directories, and
`/tmp/stateafl-evidence`. Embedded absolute paths identify that original run.
No binary snapshots or private toolchain packages are committed here.

Run `sha256sum -c SHA256SUMS` from this directory to verify the archived files.
To reproduce: build as described in the root README, then run
`bash tests/llvm18/run_all.sh tests/llvm18/out/reproduction` from the repository.
