CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -O2
TEST_IO_LDFLAGS = -Wl,--wrap=fopen

all: replay_tool test_replay_codec test_region_checker

replay_tool: replay_codec.c replay_tool.c replay_codec.h
	$(CC) $(CPPFLAGS) $(CFLAGS) replay_codec.c replay_tool.c $(LDFLAGS) -o $@

test_replay_codec: replay_codec.c test_replay_codec.c replay_codec.h
	$(CC) $(CPPFLAGS) $(CFLAGS) replay_codec.c test_replay_codec.c $(LDFLAGS) $(TEST_IO_LDFLAGS) -o $@

test_region_checker: region_checker.c test_region_checker.c region_checker.h
	$(CC) $(CPPFLAGS) $(CFLAGS) region_checker.c test_region_checker.c $(LDFLAGS) -o $@

test_replay_codec_ndebug: replay_codec.c test_replay_codec.c replay_codec.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -DNDEBUG replay_codec.c test_replay_codec.c $(LDFLAGS) $(TEST_IO_LDFLAGS) -o $@

test_region_checker_ndebug: region_checker.c test_region_checker.c region_checker.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -DNDEBUG region_checker.c test_region_checker.c $(LDFLAGS) -o $@

# These 28 cases cover the independent modules, not the native fuzzer paths.
test: test_replay_codec test_region_checker
	./test_replay_codec
	./test_region_checker

test-ndebug: test_replay_codec_ndebug test_region_checker_ndebug
	./test_replay_codec_ndebug
	./test_region_checker_ndebug

check: test test-ndebug

clean:
	rm -f replay_tool test_replay_codec test_region_checker test_replay_codec_ndebug test_region_checker_ndebug *.o

.PHONY: all test test-ndebug check clean
