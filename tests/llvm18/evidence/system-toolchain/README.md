# System toolchain verification

This follow-up run uses only the existing system commands `clang`, `clang++`,
`llvm-config`, `opt`, `llvm-dis` and `cmake`, with no toolchain environment
script or compiler overrides. All LLVM/Clang tools report 18.1.3.

`system-selection-and-cleanup.json` records system entry points, their package
layout and removal of the previous temporary toolchain, downloads, environment
script and staged installation. `/usr/lib/llvm-18` is the pre-existing target
of system package symlinks; it was retained so the system commands keep working.

`clean.log`, `root-build.log` and `llvm-build.log` record the plain clean build.
`suite.log` records a successful complete run. The four PATH probe calls are in
`path-compiler-calls.jsonl`, proving default compiler selection uses PATH names.
`artifact-paths.json` confirms rebuilt artifacts do not contain the removed
prefix and CMake selects the system compiler. `version-mismatch.log` records
expected rejection of a simulated Clang 17 with LLVM 18.

IR 21/21, pass edges 11/11 and compiler features 40/40 passed. Dynamic object/I/O
checks, seven state/calibration SHM runs and the 45-second campaign passed.
The campaign executed 443 cases, produced 13 queue entries and states 0-8,
with 100% stability and no recorded crashes/hangs. Raw generated artifacts are
retained locally in `tests/llvm18/out/system-toolchain`.

Archive text has ANSI escapes and trailing whitespace removed. Check this
folder using `sha256sum -c SHA256SUMS`. Reproduce from the repository root:

```sh
make clean
make -j2
make -C llvm_mode -j2
bash tests/llvm18/run_all.sh tests/llvm18/out/system-reproduction
```
