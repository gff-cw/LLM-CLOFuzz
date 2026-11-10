CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -O2

all: replay_tool test_replay_codec test_region_checker test_industrial_protocols test_prompt_profiles

replay_tool: replay_codec.c replay_tool.c replay_codec.h
	$(CC) $(CFLAGS) replay_codec.c replay_tool.c -o $@

test_replay_codec: replay_codec.c test_replay_codec.c replay_codec.h
	$(CC) $(CFLAGS) replay_codec.c test_replay_codec.c -o $@

test_region_checker: region_checker.c test_region_checker.c region_checker.h
	$(CC) $(CFLAGS) region_checker.c test_region_checker.c -o $@

test_industrial_protocols: test_industrial_protocols.c region_checker.c region_checker.h ../../aflnet.o
	$(CC) $(CFLAGS) -I../../ test_industrial_protocols.c region_checker.c ../../aflnet.o -o $@

test_prompt_profiles: test_prompt_profiles.c ../../chat-llm.o
	$(CC) $(CFLAGS) -I../../ test_prompt_profiles.c ../../chat-llm.o -o $@ -lcurl -ljson-c -lpcre2-8

test: test_replay_codec test_region_checker test_industrial_protocols test_prompt_profiles
	./test_replay_codec
	./test_region_checker
	./test_industrial_protocols
	./test_prompt_profiles

clean:
	rm -f replay_tool test_replay_codec test_region_checker test_industrial_protocols test_prompt_profiles *.o

.PHONY: all test clean
