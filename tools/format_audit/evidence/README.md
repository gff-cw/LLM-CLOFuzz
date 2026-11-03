# Independent test evidence

Recorded on 2026-11-03 on branch `fix/replay-recovery-integrity-20261103`,
against the uncommitted tool changes based on
`42bf625fa1d264b24601e21bedd065b8d20fe3f6`. The source hashes in `SHA256SUMS`
identify the tested tool files; this is not evidence for a later commit unless
those source hashes still match.

`independent-tests.log` is the actual output of `bash run_all_tests.sh` from
`tools/format_audit`. Both default and `-DNDEBUG` builds passed all 28 independent
cases (18 codec plus 10 checker), with no compiler warnings. The log identifies
the four T15 I/O failure paths. These suites do not compile native fuzzer paths.

`ndebug-negative-control.log` records an expected failure, not a regression.
A copy of `test_replay_codec.c` under `.build-tmp` had only the first
`TEST_ASSERT(st == REPLAY_OK, "Encode failed")` inverted to `st != REPLAY_OK`.
That copy was compiled with
`cc -Wall -Wextra -Werror -g -O2 -DNDEBUG -I. replay_codec.c .build-tmp/negative_ndebug.c -Wl,--wrap=fopen -o .build-tmp/negative_ndebug`.
It returned status 1 at T01, proving that an unsuccessful case remains visible
with NDEBUG. The checked-in test source was not altered for this control.

All fixtures, compiler temporary files and control binaries stayed below this
tool directory. Verify the manifest from `tools/format_audit` using
`sha256sum -c evidence/SHA256SUMS`.
