# Independent format audit tools

Run `bash tools/format_audit/run_all_tests.sh` from the repository, or `make check`
from this directory. There are 28 cases per build: 18 replay codec cases
(T01–T18) and 10 region checker cases (T17–T26). The two suites retain their
existing, overlapping case numbers. The runner executes all 28 cases with the
default build flags and again with `-DNDEBUG`, for 56 case executions. Tests use
explicit failure returns, so disabling C assertions does not remove test calls.

These tests compile the independent `replay_codec.c` and `region_checker.c`.
They do not compile or establish coverage of the native fuzzer, its queue
recovery, protocol extraction, or LLM interactions.

T11 checks exact and exceeded record/file limits, a sparse file larger than
u32 on systems with wide file offsets, stale payload totals, wide configured
limits, and representability of encoded u32 lengths. File decoding checks its
configured wire-size envelope before allocating file contents.

T14 uses a unique directory below the working directory for its overwrite
fixture. T15 passes a valid output container when opening a missing file. Its
other cases link a test-only `fopen` wrapper that returns GNU `fopencookie`
streams. Those streams inject `EIO` through libc `fread`, unbuffered `fwrite`,
and buffered `fclose`/flush, with callback counts and error diagnostics checked.
They do not require special devices, filesystem permissions, or external
services. The CLI does not link this wrapper.

The runner runs in this directory and puts compiler temporary files in
`.build-tmp`; codec fixtures use unique `.test-replay-codec-*` directories here
and are removed on normal or test-failure exits. For manual compilation, set
`TMPDIR` to an existing directory within the workspace when that location is
required. Generated binaries are excluded from version control.
