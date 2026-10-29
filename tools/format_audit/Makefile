CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -O2

all: replay_tool test_replay_codec test_region_checker

replay_tool: replay_codec.c replay_tool.c replay_codec.h
	$(CC) $(CFLAGS) replay_codec.c replay_tool.c -o $@

test_replay_codec: replay_codec.c test_replay_codec.c replay_codec.h
	$(CC) $(CFLAGS) replay_codec.c test_replay_codec.c -o $@

test_region_checker: region_checker.c test_region_checker.c region_checker.h
	$(CC) $(CFLAGS) region_checker.c test_region_checker.c -o $@

test: test_replay_codec test_region_checker
	./test_replay_codec
	./test_region_checker

clean:
	rm -f replay_tool test_replay_codec test_region_checker *.o

.PHONY: all test clean
