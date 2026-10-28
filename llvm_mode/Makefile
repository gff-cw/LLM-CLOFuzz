.DEFAULT_GOAL := all

#
# american fuzzy lop - LLVM instrumentation
# -----------------------------------------
#
# Written by Laszlo Szekeres <lszekeres@google.com> and
#            Michal Zalewski <lcamtuf@google.com>
#
# LLVM integration design comes from Laszlo Szekeres.
#
# Copyright 2015, 2016 Google LLC All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at:
#
#   http://www.apache.org/licenses/LICENSE-2.0
#

PREFIX      ?= /usr/local
HELPER_PATH  = $(PREFIX)/lib/afl
BIN_PATH     = $(PREFIX)/bin

VERSION     = $(shell grep '^\#define VERSION ' ../config.h | cut -d '"' -f2)

# Resolve the system toolchain through PATH; llvm-config supplies its headers/libraries.
LLVM_CONFIG ?= llvm-config
CLANG ?= clang
CLANGXX ?= clang++
CMAKE ?= cmake

CFLAGS      ?= -O3 -funroll-loops
CFLAGS      += -Wall -D_FORTIFY_SOURCE=2 -g -Wno-pointer-sign \
               -DAFL_PATH=\"$(HELPER_PATH)\" -DBIN_PATH=\"$(BIN_PATH)\" \
               -DVERSION=\"$(VERSION)\" 
ifdef AFL_TRACE_PC
  CFLAGS    += -DUSE_TRACE_PC=1
endif

CXXFLAGS    ?= -O3 -funroll-loops
CXXFLAGS    += -Wall -D_FORTIFY_SOURCE=2 -g -Wno-pointer-sign \
               -DVERSION=\"$(VERSION)\" -Wno-variadic-macros

# Mark nodelete to work around unload bug in upstream LLVM 5.0+
CLANG_CFL    = `$(LLVM_CONFIG) --cxxflags` -Wl,-znodelete -fno-rtti -fpic $(CXXFLAGS)
CLANG_LFL    = `$(LLVM_CONFIG) --ldflags` $(LDFLAGS)

# User teor2345 reports that this is required to make things work on MacOS X.

ifeq "$(shell uname)" "Darwin"
  CLANG_LFL += -Wl,-flat_namespace -Wl,-undefined,suppress
endif

# Use the system Clang commands by default; preserve explicit build compiler overrides.
ifeq "$(origin CC)" "default"
  CC = $(CLANG)
endif
ifeq "$(origin CXX)" "default"
  CXX = $(CLANGXX)
endif
CFLAGS += -DAFL_DEFAULT_CC=\"$(CLANG)\" \
          -DAFL_DEFAULT_CXX=\"$(CLANGXX)\"

ifndef AFL_TRACE_PC
  PROGS      = ../afl-clang-fast ../afl-llvm-pass.so ../afl-llvm-rt.o ../afl-llvm-rt-32.o ../afl-llvm-rt-64.o ../afl-llvm-rt-state-tracer.o ../afl-llvm-rt-state-tracer.o.cpp.o
else
  PROGS      = ../afl-clang-fast ../afl-llvm-rt.o ../afl-llvm-rt-32.o ../afl-llvm-rt-64.o ../afl-llvm-rt-state-tracer.o ../afl-llvm-rt-state-tracer.o.cpp.o
endif

# Build from pinned source; never reuse the historical tracked binary archives.
BUILD_DIR = .build
CONTAINERS = $(BUILD_DIR)/containers.a
TLSH = $(BUILD_DIR)/libtlsh.a
MVPTREE = $(BUILD_DIR)/libmvptree.a
CONTAINER_SOURCES = $(wildcard Containers/src/*.c)
CONTAINER_OBJECTS = $(patsubst Containers/src/%.c,$(BUILD_DIR)/containers/%.o,$(CONTAINER_SOURCES))
STDCXX = $(shell $(CXX) -print-file-name=libstdc++.a)

# Changing compiler, LLVM version, flags or the optional mode rebuilds all artifacts.
.PHONY: FORCE test_deps obj_section_rename test_build all_done
FORCE:
$(BUILD_DIR)/config: FORCE
	@mkdir -p $(BUILD_DIR)
	@{ echo '$(LLVM_CONFIG) $(shell command -v $(LLVM_CONFIG)) $(shell $(LLVM_CONFIG) --version)'; echo '$(CC) $(shell $(CC) --version | head -n 1)'; echo '$(CXX) $(shell $(CXX) --version | head -n 1)'; echo '$(CLANG) $(shell command -v $(CLANG)) $(shell $(CLANG) -dumpversion)'; echo '$(CLANGXX) $(shell command -v $(CLANGXX)) $(shell $(CLANGXX) -dumpversion)'; echo '$(CFLAGS) $(CXXFLAGS) $(LDFLAGS) $(AFL_TRACE_PC)'; } > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

all: test_deps $(PROGS) test_build all_done

test_deps:
ifndef AFL_TRACE_PC
	@echo "[*] Checking for working 'llvm-config'..."
	@which $(LLVM_CONFIG) >/dev/null 2>&1 || ( echo "[-] Oops, can't find 'llvm-config'. Install clang or set \$$LLVM_CONFIG or \$$PATH beforehand."; echo "    Use the llvm-config command from your system LLVM 18 development package."; exit 1 )
	@test "`$(LLVM_CONFIG) --version | cut -d. -f1`" = 18 || ( echo "[-] This pass requires LLVM 18; set LLVM_CONFIG."; exit 1 )
else
	@echo "[!] Note: using -fsanitize=trace-pc mode (this will fail with older LLVM)."
endif
	@echo "[*] Using system LLVM configuration: $$(command -v $(LLVM_CONFIG))"
	@for compiler in "$(CLANG)" "$(CLANGXX)"; do \
		command -v "$$compiler" >/dev/null 2>&1 || { echo "[-] Cannot find $$compiler in PATH."; exit 1; }; \
		"$$compiler" --version | head -n 1 | grep -q 'clang version' || { echo "[-] $$compiler is not Clang."; exit 1; }; \
		test "$$($$compiler -dumpversion | cut -d. -f1)" = "$$($(LLVM_CONFIG) --version | cut -d. -f1)" || { echo "[-] $$compiler and $(LLVM_CONFIG) must use the same LLVM major version."; exit 1; }; \
		echo "[*] Using target compiler: $$(command -v "$$compiler") ($$($$compiler -dumpversion))"; \
	done
	@echo "[*] Checking for working '$(CC)'..."
	@which $(CC) >/dev/null 2>&1 || ( echo "[-] Oops, can't find '$(CC)'. Make sure that it's in your \$$PATH (or set \$$CC and \$$CXX)."; exit 1 )
	@echo "[*] Checking for '../afl-showmap'..."
	@test -f ../afl-showmap || ( echo "[-] Oops, can't find '../afl-showmap'. Be sure to compile AFL first."; exit 1 )
	@echo "[+] All set and ready to build."

../afl-clang-fast: afl-clang-fast.c $(BUILD_DIR)/config | test_deps
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)
	ln -sf afl-clang-fast ../afl-clang-fast++

../afl-llvm-pass.so: afl-llvm-pass.so.cc state-tracer.h $(BUILD_DIR)/config | test_deps
	$(CXX) $(CLANG_CFL) -shared $< -o $@ $(CLANG_LFL)

$(CONTAINER_OBJECTS): $(BUILD_DIR)/containers/%.o: Containers/src/%.c $(wildcard Containers/src/include/*.h) $(BUILD_DIR)/config
	@mkdir -p $(@D)
	$(CC) -O3 -fPIC -c $< -o $@

$(CONTAINERS): $(CONTAINER_OBJECTS) $(BUILD_DIR)/config
	@test -n "$(CONTAINER_SOURCES)" || ( echo "Initialize submodules with git submodule update --init --recursive"; exit 1 )
	$(AR) rcs $@ $(CONTAINER_OBJECTS)

$(MVPTREE): mvptree/mvptree.c mvptree/mvptree.h $(BUILD_DIR)/config
	$(CC) -O3 -fPIC -c mvptree/mvptree.c -o $(BUILD_DIR)/mvptree.o
	$(AR) rcs $@ $(BUILD_DIR)/mvptree.o

$(TLSH): patches/tlsh-return-value.patch $(wildcard tlsh/src/*.cpp tlsh/include/*.h) tlsh/CMakeLists.txt $(BUILD_DIR)/config
	rm -rf $(BUILD_DIR)/tlsh $(BUILD_DIR)/tlsh-src
	mkdir -p $(BUILD_DIR)/tlsh-src
	cp -a tlsh/. $(BUILD_DIR)/tlsh-src/
	patch -d $(BUILD_DIR)/tlsh-src -p1 < patches/tlsh-return-value.patch
	$(CMAKE) -S $(BUILD_DIR)/tlsh-src -B $(BUILD_DIR)/tlsh -DCMAKE_C_COMPILER=$(CC) -DCMAKE_CXX_COMPILER=$(CXX) -DCMAKE_POSITION_INDEPENDENT_CODE=ON
	$(CMAKE) --build $(BUILD_DIR)/tlsh --target tlsh
	cp $(BUILD_DIR)/tlsh-src/lib/libtlsh.a $@

$(BUILD_DIR)/state-tracer-core.o: afl-llvm-rt-state-tracer.o.c state-tracer.h $(BUILD_DIR)/config $(CONTAINERS) $(TLSH) $(MVPTREE)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@.tmp.o
	ld -r -o $@ $@.tmp.o $(CONTAINERS) $(TLSH) $(MVPTREE)
	rm $@.tmp.o

../afl-llvm-rt-state-tracer.o: $(BUILD_DIR)/state-tracer-core.o
	ld -r -o $@ $< $(STDCXX)

../afl-llvm-rt-state-tracer.o.cpp.o: $(BUILD_DIR)/state-tracer-core.o
	cp $< $@

../afl-llvm-rt.o: afl-llvm-rt.o.c $(BUILD_DIR)/config | test_deps
	$(CC) $(CFLAGS) -fPIC -c $< -o $@


../afl-llvm-rt-32.o: afl-llvm-rt.o.c $(BUILD_DIR)/config | test_deps
	@printf "[*] Building 32-bit variant of the runtime (-m32)... "
	@rm -f $@; $(CC) $(CFLAGS) -m32 -fPIC -c $< -o $@ 2>/dev/null; if [ "$$?" = "0" ]; then echo "success!"; else echo "failed (that's fine)"; fi

../afl-llvm-rt-64.o: afl-llvm-rt.o.c $(BUILD_DIR)/config | test_deps
	@printf "[*] Building 64-bit variant of the runtime (-m64)... "
	@rm -f $@; $(CC) $(CFLAGS) -m64 -fPIC -c $< -o $@ 2>/dev/null; if [ "$$?" = "0" ]; then echo "success!"; else echo "failed (that's fine)"; fi

obj_section_rename: $(PROGS)
	for OBJ_FILE in ../afl-llvm-rt*.o ; do \
		objcopy --rename-section .data=.tracer_data $${OBJ_FILE}; \
		objcopy --rename-section .bss=.tracer_bss $${OBJ_FILE}; \
	done

test_build: $(PROGS) obj_section_rename
	@echo "[*] Testing the CC wrapper and instrumentation output..."
	unset AFL_USE_ASAN AFL_USE_MSAN AFL_INST_RATIO; AFL_QUIET=1 AFL_PATH=. AFL_CC=$(CC) ../afl-clang-fast $(CFLAGS) ../test-instr.c -o test-instr $(LDFLAGS)
# Use /dev/null to avoid problems with optimization messing up expected
# branches. See https://github.com/google/AFL/issues/30.
	../afl-showmap -m none -q -o .test-instr0 ./test-instr < /dev/null
	echo 1 | ../afl-showmap -m none -q -o .test-instr1 ./test-instr
	@rm -f test-instr
	@cmp -s .test-instr0 .test-instr1; DR="$$?"; rm -f .test-instr0 .test-instr1; if [ "$$DR" = "0" ]; then echo; echo "Oops, the instrumentation does not seem to be behaving correctly!"; echo; echo "Please ping <lcamtuf@google.com> to troubleshoot the issue."; echo; exit 1; fi
	@echo "[+] All right, the instrumentation seems to be working!"

all_done: test_build
	@echo "[+] All done! You can now use '../afl-clang-fast' to compile programs."

.NOTPARALLEL: clean

clean:
	rm -f *.o *.so *~ a.out core core.[1-9][0-9]* test-instr .test-instr0 .test-instr1 
	rm -f $(PROGS) ../afl-clang-fast++
	rm -rf $(BUILD_DIR)
