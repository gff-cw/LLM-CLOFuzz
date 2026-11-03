/* Exercise the actual static recovery helpers; no copied production decoder.
 * The discarded fuzzer main is never called, and the linker removes its graph.
 */
#define main discarded_fuzzer_main
#include "../afl-fuzz.c"
#undef main

#include <assert.h>
#include <limits.h>

enum fault_kind {
  NO_FAULT, SHORT_WRITE, INTERRUPTED_WRITE, ZERO_WRITE, FAILED_WRITE,
  PARTIAL_FAILED_WRITE, FAILED_FSYNC, FAILED_RENAME,
  FAILED_METADATA_WRITE, FAILED_METADATA_FSYNC, FAILED_METADATA_RENAME
};

static enum fault_kind injected_fault;
static unsigned fault_calls;
static unsigned fault_hits;
static unsigned successful_renames;

ssize_t __real_write(int fd, const void *buf, size_t len);
int __real_fsync(int fd);
int __real_rename(const char *from, const char *to);

ssize_t __wrap_write(int fd, const void *buf, size_t len) {
  if (injected_fault == SHORT_WRITE && len > 3) {
    ++fault_hits;
    return __real_write(fd, buf, 3);
  }
  if (injected_fault == INTERRUPTED_WRITE && !fault_calls++) {
    ++fault_hits;
    errno = EINTR;
    return -1;
  }
  if (injected_fault == ZERO_WRITE) {
    ++fault_hits;
    return 0;
  }
  if (injected_fault == FAILED_WRITE ||
      (injected_fault == FAILED_METADATA_WRITE && successful_renames) ||
      (injected_fault == PARTIAL_FAILED_WRITE && fault_calls++)) {
    ++fault_hits;
    errno = EIO;
    return -1;
  }
  if (injected_fault == PARTIAL_FAILED_WRITE && len > 3) {
    ++fault_hits;
    return __real_write(fd, buf, 3);
  }
  return __real_write(fd, buf, len);
}

int __wrap_fsync(int fd) {
  if (injected_fault == FAILED_FSYNC ||
      (injected_fault == FAILED_METADATA_FSYNC && successful_renames)) {
    ++fault_hits;
    errno = EIO;
    return -1;
  }
  return __real_fsync(fd);
}

int __wrap_rename(const char *from, const char *to) {
  if (injected_fault == FAILED_RENAME ||
      (injected_fault == FAILED_METADATA_RENAME && successful_renames)) {
    ++fault_hits;
    errno = EIO;
    return -1;
  }
  int result = __real_rename(from, to);
  if (!result) ++successful_renames;
  return result;
}

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s (errno=%d: %s)\n", \
            __FILE__, __LINE__, #condition, errno, strerror(errno)); \
    exit(1); \
  } \
} while (0)

static const u8 raw_fixture[] = {'a', 'b', 'c', 'X', 'Y'};
static const u32 mixed_lengths[] = {0, 3, 0, 2, 0};
static const u32 zero_lengths[] = {0, 0, 0};
static const u8 sentinel[] = "existing output must survive";

static void make_dir(const char *path) {
  CHECK(mkdir(path, 0700) == 0);
}

static void put_bytes(const char *path, const void *bytes, size_t len) {
  FILE *file = fopen(path, "wb");
  CHECK(file != NULL);
  CHECK(!len || fwrite(bytes, 1, len, file) == len);
  CHECK(fclose(file) == 0);
}

static u8 *get_bytes(const char *path, size_t *len) {
  FILE *file = fopen(path, "rb");
  CHECK(file != NULL);
  CHECK(fseek(file, 0, SEEK_END) == 0);
  long size = ftell(file);
  CHECK(size >= 0);
  CHECK(fseek(file, 0, SEEK_SET) == 0);
  u8 *bytes = malloc(size ? (size_t)size : 1);
  CHECK(bytes != NULL);
  CHECK(!size || fread(bytes, 1, (size_t)size, file) == (size_t)size);
  CHECK(fclose(file) == 0);
  *len = (size_t)size;
  return bytes;
}

static void expect_bytes(const char *path, const void *expected, size_t len) {
  size_t actual_len;
  u8 *actual = get_bytes(path, &actual_len);
  CHECK(actual_len == len);
  CHECK(!len || memcmp(actual, expected, len) == 0);
  free(actual);
}

/* Build fixed wire fixtures only. Decoding and boundary recovery use production
 * functions exclusively. The existing replay format stores native-endian u32s.
 */
static void put_replay(const char *path, const u8 *raw, u32 raw_len,
                       const u32 *lengths, u32 count) {
  FILE *file = fopen(path, "wb");
  CHECK(file != NULL);
  u32 offset = 0;
  for (u32 i = 0; i < count; ++i) {
    CHECK(lengths[i] <= raw_len - offset);
    CHECK(fwrite(&lengths[i], sizeof(u32), 1, file) == 1);
    CHECK(!lengths[i] || fwrite(raw + offset, 1, lengths[i], file) == lengths[i]);
    offset += lengths[i];
  }
  CHECK(offset == raw_len);
  CHECK(fclose(file) == 0);
}

static void put_mixed_replay(const char *path) {
  put_replay(path, raw_fixture, sizeof(raw_fixture), mixed_lengths,
             sizeof(mixed_lengths) / sizeof(mixed_lengths[0]));
}

static void put_oversized(const char *path) {
  int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  CHECK(fd >= 0);
  CHECK(ftruncate(fd, (off_t)MAX_REPLAY_BYTES + 1) == 0);
  CHECK(close(fd) == 0);
}

static unsigned entry_count(const char *path) {
  DIR *dir = opendir(path);
  CHECK(dir != NULL);
  unsigned count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) ++count;
  }
  CHECK(closedir(dir) == 0);
  return count;
}

static void check_regions(const region_t *regions, u32 count,
                          const u32 *lengths, u32 expected_count) {
  CHECK(regions != NULL);
  CHECK(count == expected_count);
  int offset = 0;
  for (u32 i = 0; i < count; ++i) {
    CHECK(regions[i].start_byte == offset);
    CHECK(regions[i].end_byte == offset + (int)lengths[i] - 1);
    CHECK(regions[i].state_sequence == NULL);
    CHECK(regions[i].state_count == 0);
    offset += (int)lengths[i];
  }
}

static void begin_fault(enum fault_kind fault) {
  injected_fault = fault;
  fault_calls = 0;
  fault_hits = 0;
  successful_renames = 0;
}

static void finish_fault(enum fault_kind fault) {
  injected_fault = NO_FAULT;
  if (fault != NO_FAULT) CHECK(fault_hits > 0);
}

static void expect_prepare(const char *input, const char *output, int format,
                           const u8 *expected, u32 expected_len,
                           const u32 *lengths, u32 count, int success,
                           enum fault_kind fault) {
  size_t input_len;
  u8 *original = get_bytes(input, &input_len);
  size_t old_output_len = 0;
  int output_existed = access(output, F_OK) == 0;
  u8 *old_output = output_existed ? get_bytes(output, &old_output_len) : NULL;
  u32 len = UINT_MAX, actual_count = UINT_MAX;
  region_t *regions = (region_t *)(uintptr_t)1;
  input_format = format;
  begin_fault(fault);
  errno = 0;
  u8 ok = prepare_pivot_input(input, output, &len, &regions, &actual_count);
  int saved_errno = errno;
  finish_fault(fault);
  if (success) {
    CHECK(ok);
    CHECK(len == expected_len);
    expect_bytes(output, expected, expected_len);
    if (lengths) check_regions(regions, actual_count, lengths, count);
    else CHECK(regions == NULL && actual_count == 0);
  } else {
    CHECK(!ok);
    CHECK(saved_errno != 0);
    CHECK(len == 0 && actual_count == 0 && regions == NULL);
    if (output_existed) expect_bytes(output, old_output, old_output_len);
    else CHECK(access(output, F_OK) < 0 && errno == ENOENT);
  }
  if (!success || strcmp(input, output)) expect_bytes(input, original, input_len);
  if (regions) ck_free(regions);
  free(original);
  free(old_output);
}

static void expect_mixed(const char *input, int format) {
  put_bytes("output", sentinel, sizeof(sentinel));
  expect_prepare(input, "output", format, raw_fixture, sizeof(raw_fixture),
                 mixed_lengths, 5, 1, NO_FAULT);
}

static void expect_rejected(const char *input, int format) {
  put_bytes("output", sentinel, sizeof(sentinel));
  expect_prepare(input, "output", format, NULL, 0, NULL, 0, 0, NO_FAULT);
}

static void setup_raw_queue(const char *directory) {
  make_dir(directory);
  char path[128];
  CHECK(snprintf(path, sizeof(path), "%s/id:000000", directory) > 0);
  put_bytes(path, raw_fixture, sizeof(raw_fixture));
}

static void fresh_raw(void) {
  put_bytes("seed", raw_fixture, sizeof(raw_fixture));
  expect_prepare("seed", "output", INPUT_AUTO, raw_fixture, sizeof(raw_fixture),
                 NULL, 0, 1, NO_FAULT);
}

static void fresh_replay(void) {
  put_mixed_replay("seed");
  expect_mixed("seed", INPUT_AUTO);
}

static void direct_replay_queue(void) {
  make_dir("replayable-queue");
  put_mixed_replay("replayable-queue/id:000000");
  expect_mixed("replayable-queue/id:000000", INPUT_AUTO);
}

static void direct_resume_replay_queue(void) {
  make_dir("_resume_replayable-queue");
  put_mixed_replay("_resume_replayable-queue/id:000000");
  expect_mixed("_resume_replayable-queue/id:000000", INPUT_AUTO);
}

static void direct_replay_malformed(void) {
  make_dir("replayable-queue");
  put_bytes("replayable-queue/id:000000", "broken", 6);
  expect_rejected("replayable-queue/id:000000", INPUT_AUTO);
}

static void explicit_replay_in_queue(void) {
  make_dir("queue");
  put_mixed_replay("queue/id:000000");
  expect_mixed("queue/id:000000", INPUT_REPLAY);
}

static void explicit_raw_ambiguous(void) {
  put_mixed_replay("seed");
  size_t len;
  u8 *wire = get_bytes("seed", &len);
  expect_prepare("seed", "output", INPUT_RAW, wire, (u32)len, NULL, 0, 1, NO_FAULT);
  free(wire);
}

static void explicit_raw_in_replay_queue(void) {
  make_dir("replayable-queue");
  put_mixed_replay("replayable-queue/id:000000");
  size_t len;
  u8 *wire = get_bytes("replayable-queue/id:000000", &len);
  expect_prepare("replayable-queue/id:000000", "output", INPUT_RAW, wire,
                 (u32)len, NULL, 0, 1, NO_FAULT);
  free(wire);
}

static void explicit_replay_malformed(void) {
  put_bytes("seed", "broken", 6);
  expect_rejected("seed", INPUT_REPLAY);
}

static void explicit_replay_empty(void) {
  put_bytes("seed", NULL, 0);
  expect_rejected("seed", INPUT_REPLAY);
}

static void auto_partial_framing(void) {
  put_mixed_replay("seed");
  FILE *file = fopen("seed", "ab");
  CHECK(file != NULL);
  CHECK(fwrite("\x01\x00", 1, 2, file) == 2);
  CHECK(fclose(file) == 0);
  size_t len;
  u8 *wire = get_bytes("seed", &len);
  expect_prepare("seed", "output", INPUT_AUTO, wire, (u32)len, NULL, 0, 1, NO_FAULT);
  free(wire);
}

static void auto_empty_raw(void) {
  put_bytes("seed", NULL, 0);
  expect_prepare("seed", "output", INPUT_AUTO, NULL, 0, NULL, 0, 1, NO_FAULT);
}

static void replay_zero_records(void) {
  put_replay("seed", NULL, 0, zero_lengths, 3);
  expect_prepare("seed", "output", INPUT_AUTO, NULL, 0, zero_lengths, 3, 1, NO_FAULT);
}

static void resume_normal(void) {
  setup_raw_queue("queue");
  make_dir("replayable-queue");
  put_mixed_replay("replayable-queue/id:000000");
  expect_mixed("queue/id:000000", INPUT_AUTO);
}

static void resume_path_variant(const char *input) {
  setup_raw_queue("queue");
  make_dir("replayable-queue");
  put_mixed_replay("replayable-queue/id:000000");
  expect_mixed(input, INPUT_AUTO);
}

static void resume_duplicate_slash(void) { resume_path_variant("queue//id:000000"); }
static void resume_dot_component(void) { resume_path_variant("queue/./id:000000"); }

static void malformed_replay_path_variant(const char *input) {
  make_dir("replayable-queue");
  put_bytes("replayable-queue/id:000000", "broken", 6);
  expect_rejected(input, INPUT_AUTO);
}

static void replay_duplicate_slash_is_strict(void) {
  malformed_replay_path_variant("replayable-queue//id:000000");
}

static void replay_dot_component_is_strict(void) {
  malformed_replay_path_variant("replayable-queue/./id:000000");
}

static void resume_in_place_layout(void) {
  setup_raw_queue("_resume");
  make_dir("_resume_replayable-queue");
  put_mixed_replay("_resume_replayable-queue/id:000000");
  expect_mixed("_resume/id:000000", INPUT_AUTO);
}

static void resume_same_output_path(void) {
  setup_raw_queue("_resume");
  make_dir("_resume_replayable-queue");
  put_mixed_replay("_resume_replayable-queue/id:000000");
  expect_prepare("_resume/id:000000", "_resume/id:000000", INPUT_AUTO,
                 raw_fixture, sizeof(raw_fixture), mixed_lengths, 5, 1, NO_FAULT);
}

static void resume_zero_records(void) {
  make_dir("queue");
  make_dir("replayable-queue");
  put_bytes("queue/id:000000", NULL, 0);
  put_replay("replayable-queue/id:000000", NULL, 0, zero_lengths, 3);
  expect_prepare("queue/id:000000", "output", INPUT_AUTO, NULL, 0,
                 zero_lengths, 3, 1, NO_FAULT);
}

static void resume_missing_companion(void) {
  setup_raw_queue("queue");
  expect_rejected("queue/id:000000", INPUT_AUTO);
}

static void resume_empty_missing_companion(void) {
  make_dir("queue");
  put_bytes("queue/id:000000", NULL, 0);
  expect_rejected("queue/id:000000", INPUT_AUTO);
}

static void resume_regions_only(void) {
  setup_raw_queue("queue");
  make_dir("regions");
  const char *metadata = "Region 0 - Start: 0, End: 4\n";
  put_bytes("regions/id:000000", metadata, strlen(metadata));
  expect_rejected("queue/id:000000", INPUT_AUTO);
}

static void resume_stale_same_length(void) {
  setup_raw_queue("queue");
  make_dir("replayable-queue");
  const u32 lengths[] = {5};
  put_replay("replayable-queue/id:000000", (const u8 *)"stale", 5, lengths, 1);
  expect_rejected("queue/id:000000", INPUT_AUTO);
}

static void resume_wrong_length(void) {
  setup_raw_queue("queue");
  make_dir("replayable-queue");
  const u32 lengths[] = {4};
  put_replay("replayable-queue/id:000000", (const u8 *)"abcd", 4, lengths, 1);
  expect_rejected("queue/id:000000", INPUT_AUTO);
}

static void resume_malformed_companion(void) {
  setup_raw_queue("queue");
  make_dir("replayable-queue");
  put_bytes("replayable-queue/id:000000", "broken", 6);
  expect_rejected("queue/id:000000", INPUT_AUTO);
}

static void fallback_companion(int bad_kind) {
  setup_raw_queue("_resume");
  make_dir("_resume_replayable-queue");
  make_dir("replayable-queue");
  if (bad_kind == 0) {
    const u32 lengths[] = {5};
    put_replay("_resume_replayable-queue/id:000000", (const u8 *)"stale", 5,
                lengths, 1);
  } else if (bad_kind == 1) {
    put_bytes("_resume_replayable-queue/id:000000", "broken", 6);
  } else {
    put_oversized("_resume_replayable-queue/id:000000");
  }
  put_mixed_replay("replayable-queue/id:000000");
  expect_mixed("_resume/id:000000", INPUT_AUTO);
}

static void resume_stale_priority_fallback(void) { fallback_companion(0); }
static void resume_malformed_priority_fallback(void) { fallback_companion(1); }
static void resume_oversized_priority_fallback(void) { fallback_companion(2); }

static void resume_oversized_companion(void) {
  setup_raw_queue("queue");
  make_dir("replayable-queue");
  put_oversized("replayable-queue/id:000000");
  expect_rejected("queue/id:000000", INPUT_AUTO);
}

static void resume_basename_mismatch(void) {
  setup_raw_queue("queue");
  make_dir("replayable-queue");
  put_mixed_replay("replayable-queue/id:000001");
  expect_rejected("queue/id:000000", INPUT_AUTO);
}

static void resume_corrupt_replay_and_regions(void) {
  setup_raw_queue("queue");
  make_dir("replayable-queue");
  make_dir("regions");
  put_bytes("replayable-queue/id:000000", "broken", 6);
  const char *metadata = "Region 0 - Start: 0, End: 4\n";
  put_bytes("regions/id:000000", metadata, strlen(metadata));
  expect_rejected("queue/id:000000", INPUT_AUTO);
}

static void make_fixture_regions(region_t *regions, const u32 *lengths, u32 count) {
  memset(regions, 0, count * sizeof(*regions));
  int offset = 0;
  for (u32 i = 0; i < count; ++i) {
    regions[i].start_byte = offset;
    regions[i].end_byte = offset + (int)lengths[i] - 1;
    offset += (int)lengths[i];
  }
}

static void save_round_trip(void) {
  region_t regions[5];
  make_fixture_regions(regions, mixed_lengths, 5);
  put_bytes("raw", raw_fixture, sizeof(raw_fixture));
  put_mixed_replay("expected");
  CHECK(save_raw_as_replay("raw", regions, 5, "saved"));
  size_t len;
  u8 *wire = get_bytes("expected", &len);
  expect_bytes("saved", wire, len);
  free(wire);
  expect_mixed("saved", INPUT_REPLAY);
}

static void save_zero_records(void) {
  region_t regions[3];
  make_fixture_regions(regions, zero_lengths, 3);
  put_bytes("raw", NULL, 0);
  put_replay("expected", NULL, 0, zero_lengths, 3);
  CHECK(save_raw_as_replay("raw", regions, 3, "saved"));
  size_t len;
  u8 *wire = get_bytes("expected", &len);
  expect_bytes("saved", wire, len);
  free(wire);
}

static void save_invalid_partition(void) {
  region_t regions[5];
  make_fixture_regions(regions, mixed_lengths, 5);
  regions[1].start_byte = 1;
  put_bytes("raw", raw_fixture, sizeof(raw_fixture));
  put_bytes("saved", sentinel, sizeof(sentinel));
  errno = 0;
  CHECK(!save_raw_as_replay("raw", regions, 5, "saved"));
  CHECK(errno != 0);
  expect_bytes("saved", sentinel, sizeof(sentinel));
  expect_bytes("raw", raw_fixture, sizeof(raw_fixture));
}

static void save_invalid_bounds(void) {
  region_t region = {.start_byte = 0, .end_byte = 999};
  put_bytes("raw", raw_fixture, sizeof(raw_fixture));
  put_bytes("saved", sentinel, sizeof(sentinel));
  errno = 0;
  CHECK(!save_raw_as_replay("raw", &region, 1, "saved"));
  CHECK(errno != 0);
  expect_bytes("saved", sentinel, sizeof(sentinel));
}

static void atomic_case(enum fault_kind fault, int success, int existing) {
  const u8 replacement[] = "complete replacement file contents";
  if (existing) put_bytes("target", sentinel, sizeof(sentinel));
  unsigned entries = entry_count(".");
  begin_fault(fault);
  errno = 0;
  u8 ok = atomic_write_file("target", replacement, sizeof(replacement));
  int saved_errno = errno;
  finish_fault(fault);
  CHECK((ok != 0) == success);
  if (success) expect_bytes("target", replacement, sizeof(replacement));
  else {
    CHECK(saved_errno != 0);
    if (existing) expect_bytes("target", sentinel, sizeof(sentinel));
    else CHECK(access("target", F_OK) < 0 && errno == ENOENT);
  }
  CHECK(entry_count(".") == entries + (success && !existing));
}

static void atomic_replace(void) { atomic_case(NO_FAULT, 1, 1); }
static void atomic_short_write(void) { atomic_case(SHORT_WRITE, 1, 1); }
static void atomic_interrupted_write(void) { atomic_case(INTERRUPTED_WRITE, 1, 1); }
static void atomic_failed_write(void) { atomic_case(FAILED_WRITE, 0, 1); }
static void atomic_partial_failed_write(void) { atomic_case(PARTIAL_FAILED_WRITE, 0, 1); }
static void atomic_zero_write(void) { atomic_case(ZERO_WRITE, 0, 1); }
static void atomic_failed_fsync(void) { atomic_case(FAILED_FSYNC, 0, 1); }
static void atomic_failed_rename(void) { atomic_case(FAILED_RENAME, 0, 1); }
static void atomic_failure_new_path(void) { atomic_case(FAILED_WRITE, 0, 0); }

static void prepare_fault_case(enum fault_kind fault, int success, int same_path) {
  if (same_path) {
    setup_raw_queue("_resume");
    make_dir("_resume_replayable-queue");
    put_mixed_replay("_resume_replayable-queue/id:000000");
  } else {
    put_mixed_replay("seed");
    put_bytes("output", sentinel, sizeof(sentinel));
  }
  unsigned entries = entry_count(same_path ? "_resume" : ".");
  expect_prepare(same_path ? "_resume/id:000000" : "seed",
                 same_path ? "_resume/id:000000" : "output", INPUT_AUTO,
                 raw_fixture, sizeof(raw_fixture), mixed_lengths, 5, success, fault);
  CHECK(entry_count(same_path ? "_resume" : ".") == entries);
}

static void prepare_failed_write(void) { prepare_fault_case(FAILED_WRITE, 0, 0); }
static void prepare_failed_fsync(void) { prepare_fault_case(FAILED_FSYNC, 0, 0); }
static void prepare_failed_rename(void) { prepare_fault_case(FAILED_RENAME, 0, 0); }
static void prepare_short_write(void) { prepare_fault_case(SHORT_WRITE, 1, 0); }
static void prepare_same_path_failed_write(void) { prepare_fault_case(FAILED_WRITE, 0, 1); }
static void prepare_same_path_failed_fsync(void) { prepare_fault_case(FAILED_FSYNC, 0, 1); }
static void prepare_same_path_failed_rename(void) { prepare_fault_case(FAILED_RENAME, 0, 1); }

static void save_fault_case(enum fault_kind fault) {
  region_t regions[5];
  make_fixture_regions(regions, mixed_lengths, 5);
  put_bytes("raw", raw_fixture, sizeof(raw_fixture));
  put_bytes("saved", sentinel, sizeof(sentinel));
  unsigned entries = entry_count(".");
  begin_fault(fault);
  errno = 0;
  u8 ok = save_raw_as_replay("raw", regions, 5, "saved");
  int saved_errno = errno;
  finish_fault(fault);
  CHECK(!ok && saved_errno != 0);
  expect_bytes("raw", raw_fixture, sizeof(raw_fixture));
  expect_bytes("saved", sentinel, sizeof(sentinel));
  CHECK(entry_count(".") == entries);
}

static void save_failed_write(void) { save_fault_case(FAILED_WRITE); }
static void save_failed_fsync(void) { save_fault_case(FAILED_FSYNC); }
static void save_failed_rename(void) { save_fault_case(FAILED_RENAME); }

static unsigned protocol_parser_calls;

static region_t *fixture_protocol_parser(unsigned char *bytes, unsigned int len,
                                          unsigned int *count) {
  CHECK(len == sizeof(raw_fixture));
  CHECK(memcmp(bytes, raw_fixture, len) == 0);
  ++protocol_parser_calls;
  region_t *region = ck_alloc(sizeof(*region));
  region->start_byte = 0;
  region->end_byte = (int)len - 1;
  *count = 1;
  return region;
}

static void pivot_case(int mode) {
  const char *source;
  make_dir("target");
  make_dir("target/queue");
  make_dir("target/regions");
  make_dir("target/replayable-queue");
  if (mode == 0) {
    make_dir("source");
    make_dir("source/replayable-queue");
    source = "source/replayable-queue/id:000000";
    put_mixed_replay(source);
    put_bytes("target/replayable-queue/id:000000", sentinel, sizeof(sentinel));
  } else if (mode == 1) {
    make_dir("target/_resume");
    make_dir("target/_resume_replayable-queue");
    source = "target/_resume/id:000000";
    put_bytes(source, raw_fixture, sizeof(raw_fixture));
    put_mixed_replay("target/_resume_replayable-queue/id:000000");
    in_place_resume = 1;
  } else {
    make_dir("source");
    source = "source/seed";
    put_bytes(source, raw_fixture, sizeof(raw_fixture));
  }
  struct queue_entry entry;
  memset(&entry, 0, sizeof(entry));
  entry.fname = ck_strdup((u8 *)source);
  entry.len = sizeof(raw_fixture);
  /* Existing annotations must be released when the recovered metadata wins. */
  entry.regions = ck_alloc(sizeof(*entry.regions));
  entry.regions[0].state_sequence = ck_alloc(sizeof(unsigned int));
  entry.regions[0].state_count = 1;
  entry.region_count = 1;
  queue = &entry;
  out_dir = ck_strdup((u8 *)"target");
  orig_extract_requests = fixture_protocol_parser;
  protocol_parser_calls = 0;
  input_format = INPUT_AUTO;
  pivot_inputs();
  CHECK(entry.len == sizeof(raw_fixture));
  expect_bytes((char *)entry.fname, raw_fixture, sizeof(raw_fixture));
  if (mode < 2) {
    CHECK(protocol_parser_calls == 0);
    check_regions(entry.regions, entry.region_count, mixed_lengths, 5);
    CHECK(max_seed_region_count == 5);
    put_mixed_replay("expected");
  } else {
    const u32 length[] = {sizeof(raw_fixture)};
    CHECK(protocol_parser_calls == 1);
    check_regions(entry.regions, entry.region_count, length, 1);
    CHECK(max_seed_region_count == 1);
    put_replay("expected", raw_fixture, sizeof(raw_fixture), length, 1);
  }
  const char *base = strrchr((char *)entry.fname, '/') + 1;
  char replay_path[256];
  CHECK(snprintf(replay_path, sizeof(replay_path), "target/replayable-queue/%s", base) > 0);
  size_t expected_len;
  u8 *expected = get_bytes("expected", &expected_len);
  expect_bytes(replay_path, expected, expected_len);
  free(expected);
  if (mode == 1) {
    CHECK(access("target/_resume", F_OK) < 0 && errno == ENOENT);
    CHECK(access("target/_resume_replayable-queue", F_OK) < 0 && errno == ENOENT);
  } else if (mode == 0) {
    expected = get_bytes("expected", &expected_len);
    expect_bytes(source, expected, expected_len);
    free(expected);
  } else {
    expect_bytes(source, raw_fixture, sizeof(raw_fixture));
  }
  ck_free(entry.fname);
  ck_free(entry.regions);
  ck_free(out_dir);
  out_dir = NULL;
  queue = NULL;
  orig_extract_requests = NULL;
}

static void pivot_replay_preserves_empty_regions(void) { pivot_case(0); }
static void pivot_in_place_preserves_empty_regions(void) { pivot_case(1); }
static void pivot_raw_uses_protocol_parser(void) { pivot_case(2); }

static void pivot_failure_case(enum fault_kind fault) {
  make_dir("target");
  make_dir("target/queue");
  make_dir("target/regions");
  make_dir("target/replayable-queue");
  make_dir("target/_resume");
  make_dir("target/_resume_regions");
  make_dir("target/_resume_replayable-queue");
  put_bytes("target/_resume/id:000000", raw_fixture, sizeof(raw_fixture));
  put_bytes("target/_resume_regions/id:000000", sentinel, sizeof(sentinel));
  put_mixed_replay("target/_resume_replayable-queue/id:000000");
  put_mixed_replay("expected");
  struct queue_entry entry;
  memset(&entry, 0, sizeof(entry));
  entry.fname = ck_strdup((u8 *)"target/_resume/id:000000");
  entry.len = sizeof(raw_fixture);
  queue = &entry;
  out_dir = ck_strdup((u8 *)"target");
  orig_extract_requests = fixture_protocol_parser;
  in_place_resume = 1;
  input_format = INPUT_AUTO;
  CHECK(fflush(NULL) == 0);
  pid_t child = fork();
  CHECK(child >= 0);
  if (!child) {
    CHECK(freopen("child.stdout.log", "w", stdout) != NULL);
    CHECK(freopen("child.stderr.log", "w", stderr) != NULL);
    begin_fault(fault);
    /* The first raw publication succeeds. Metadata publication then fails,
     * so the real pivot_inputs error path must retain every recovery original.
     */
    pivot_inputs();
    _exit(99);
  }
  int status;
  CHECK(waitpid(child, &status, 0) == child);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 1);
  int saw_expected_error = 0;
  const char *logs[] = {"child.stdout.log", "child.stderr.log"};
  for (u32 i = 0; i < 2; ++i) {
    size_t log_len;
    u8 *log = get_bytes(logs[i], &log_len);
    if (memmem(log, log_len, "Cannot save", strlen("Cannot save"))) saw_expected_error = 1;
    CHECK(!memmem(log, log_len, "AddressSanitizer", strlen("AddressSanitizer")));
    CHECK(!memmem(log, log_len, "LeakSanitizer", strlen("LeakSanitizer")));
    CHECK(!memmem(log, log_len, "runtime error:", strlen("runtime error:")));
    free(log);
  }
  CHECK(saw_expected_error);
  expect_bytes("target/_resume/id:000000", raw_fixture, sizeof(raw_fixture));
  expect_bytes("target/_resume_regions/id:000000", sentinel, sizeof(sentinel));
  expect_bytes("target/queue/id:000000", raw_fixture, sizeof(raw_fixture));
  CHECK(entry_count("target/queue") == 1);
  CHECK(entry_count("target/regions") == 0);
  CHECK(entry_count("target/replayable-queue") == 0);
  size_t expected_len;
  u8 *expected = get_bytes("expected", &expected_len);
  expect_bytes("target/_resume_replayable-queue/id:000000", expected, expected_len);
  free(expected);
  ck_free(entry.fname);
  ck_free(out_dir);
  out_dir = NULL;
  queue = NULL;
  orig_extract_requests = NULL;
}

static void pivot_failed_metadata_write(void) { pivot_failure_case(FAILED_METADATA_WRITE); }
static void pivot_failed_metadata_fsync(void) { pivot_failure_case(FAILED_METADATA_FSYNC); }
static void pivot_failed_metadata_rename(void) { pivot_failure_case(FAILED_METADATA_RENAME); }

static void setup_preservation(void) {
  make_dir("target");
  out_dir = ck_strdup((u8 *)"target");
}

static void finish_preservation(void) {
  ck_free(out_dir);
  out_dir = NULL;
}

static void preserve_required_directory(void) {
  setup_preservation();
  make_dir("target/queue");
  put_bytes("target/queue/id:000000", raw_fixture, sizeof(raw_fixture));
  CHECK(preserve_resume_dir("queue", "_resume", 1));
  CHECK(access("target/queue", F_OK) < 0 && errno == ENOENT);
  expect_bytes("target/_resume/id:000000", raw_fixture, sizeof(raw_fixture));
  finish_preservation();
}

static void preserve_optional_missing(void) {
  setup_preservation();
  CHECK(preserve_resume_dir("regions", "_resume_regions", 0));
  CHECK(access("target/_resume_regions", F_OK) < 0 && errno == ENOENT);
  finish_preservation();
}

static void preserve_required_missing(void) {
  setup_preservation();
  errno = 0;
  CHECK(!preserve_resume_dir("queue", "_resume", 1));
  CHECK(errno == ENOENT);
  finish_preservation();
}

static void preserve_existing_backup(int nonempty) {
  setup_preservation();
  make_dir("target/queue");
  make_dir("target/_resume");
  put_bytes("target/queue/id:000000", raw_fixture, sizeof(raw_fixture));
  if (nonempty) put_bytes("target/_resume/id:000000", sentinel, sizeof(sentinel));
  CHECK(preserve_resume_dir("queue", "_resume", 1));
  expect_bytes("target/queue/id:000000", raw_fixture, sizeof(raw_fixture));
  CHECK(entry_count("target/_resume") == (unsigned)nonempty);
  if (nonempty) expect_bytes("target/_resume/id:000000", sentinel, sizeof(sentinel));
  finish_preservation();
}

static void preserve_existing_empty_backup(void) { preserve_existing_backup(0); }
static void preserve_existing_nonempty_backup(void) { preserve_existing_backup(1); }

static void preserve_backup_is_file(void) {
  setup_preservation();
  make_dir("target/queue");
  put_bytes("target/queue/id:000000", raw_fixture, sizeof(raw_fixture));
  put_bytes("target/_resume", sentinel, sizeof(sentinel));
  errno = 0;
  CHECK(!preserve_resume_dir("queue", "_resume", 1));
  CHECK(errno == ENOTDIR);
  expect_bytes("target/queue/id:000000", raw_fixture, sizeof(raw_fixture));
  expect_bytes("target/_resume", sentinel, sizeof(sentinel));
  finish_preservation();
}

static void preserve_failed_rename(void) {
  setup_preservation();
  make_dir("target/queue");
  put_bytes("target/queue/id:000000", raw_fixture, sizeof(raw_fixture));
  begin_fault(FAILED_RENAME);
  errno = 0;
  u8 ok = preserve_resume_dir("queue", "_resume", 1);
  int saved_errno = errno;
  finish_fault(FAILED_RENAME);
  CHECK(!ok && saved_errno == EIO);
  expect_bytes("target/queue/id:000000", raw_fixture, sizeof(raw_fixture));
  CHECK(access("target/_resume", F_OK) < 0 && errno == ENOENT);
  finish_preservation();
}

static void preserve_source_file_rejected(void) {
  setup_preservation();
  put_bytes("target/queue", sentinel, sizeof(sentinel));
  errno = 0;
  CHECK(!preserve_resume_dir("queue", "_resume", 1));
  CHECK(errno == ENOTDIR);
  expect_bytes("target/queue", sentinel, sizeof(sentinel));
  CHECK(access("target/_resume", F_OK) < 0 && errno == ENOENT);
  finish_preservation();
}

static void setup_stage(void) {
  setup_preservation();
  make_dir("target/queue");
  make_dir("target/replayable-queue");
  make_dir("target/regions");
  put_bytes("target/queue/id:000000", raw_fixture, sizeof(raw_fixture));
  put_mixed_replay("target/replayable-queue/id:000000");
  put_bytes("target/regions/id:000000", sentinel, sizeof(sentinel));
  put_mixed_replay("expected");
}

static void expect_stage_replay(const char *path) {
  size_t expected_len;
  u8 *expected = get_bytes("expected", &expected_len);
  expect_bytes(path, expected, expected_len);
  free(expected);
}

static void stage_orphan_backup(const char *directory) {
  setup_stage();
  make_dir(directory);
  char orphan[256];
  CHECK(snprintf(orphan, sizeof(orphan), "%s/id:000000", directory) > 0);
  put_bytes(orphan, sentinel, sizeof(sentinel));
  errno = 0;
  CHECK(!stage_resume_inputs());
  CHECK(errno == EEXIST);
  expect_bytes("target/queue/id:000000", raw_fixture, sizeof(raw_fixture));
  expect_stage_replay("target/replayable-queue/id:000000");
  expect_bytes("target/regions/id:000000", sentinel, sizeof(sentinel));
  expect_bytes(orphan, sentinel, sizeof(sentinel));
  CHECK(access("target/_resume", F_OK) < 0 && errno == ENOENT);
  finish_preservation();
}

static void stage_orphan_replay_backup(void) {
  stage_orphan_backup("target/_resume_replayable-queue");
}

static void stage_orphan_regions_backup(void) {
  stage_orphan_backup("target/_resume_regions");
}

static void stage_fresh_snapshot(void) {
  setup_stage();
  CHECK(stage_resume_inputs());
  expect_bytes("target/_resume/id:000000", raw_fixture, sizeof(raw_fixture));
  expect_stage_replay("target/_resume_replayable-queue/id:000000");
  expect_bytes("target/_resume_regions/id:000000", sentinel, sizeof(sentinel));
  CHECK(access("target/queue", F_OK) < 0 && errno == ENOENT);
  CHECK(access("target/replayable-queue", F_OK) < 0 && errno == ENOENT);
  CHECK(access("target/regions", F_OK) < 0 && errno == ENOENT);
  finish_preservation();
}

static void stage_interrupted_existing_raw_backup(void) {
  setup_stage();
  make_dir("target/_resume");
  put_bytes("target/_resume/id:000000", raw_fixture, sizeof(raw_fixture));
  put_bytes("target/queue/id:000000", sentinel, sizeof(sentinel));
  CHECK(stage_resume_inputs());
  expect_bytes("target/_resume/id:000000", raw_fixture, sizeof(raw_fixture));
  expect_bytes("target/queue/id:000000", sentinel, sizeof(sentinel));
  expect_stage_replay("target/_resume_replayable-queue/id:000000");
  expect_bytes("target/_resume_regions/id:000000", sentinel, sizeof(sentinel));
  CHECK(access("target/replayable-queue", F_OK) < 0 && errno == ENOENT);
  CHECK(access("target/regions", F_OK) < 0 && errno == ENOENT);
  finish_preservation();
}

static region_t *fixture_mixed_protocol_parser(unsigned char *bytes, unsigned int len,
                                                unsigned int *count) {
  CHECK(len == sizeof(raw_fixture));
  CHECK(memcmp(bytes, raw_fixture, len) == 0);
  ++protocol_parser_calls;
  region_t *regions = ck_alloc(5 * sizeof(*regions));
  make_fixture_regions(regions, mixed_lengths, 5);
  regions[2].state_sequence = ck_alloc(sizeof(unsigned int));
  regions[2].state_count = 1;
  *count = 5;
  return regions;
}

static void enriched_case(int format, int replay_directory, int with_parser,
                           int framed) {
  make_dir("replayable-queue");
  const char *path = replay_directory ? "replayable-queue/generated" : "generated";
  input_format = format;
  orig_extract_requests = with_parser ? fixture_mixed_protocol_parser : NULL;
  protocol_parser_calls = 0;
  CHECK(write_enriched_seed(path, "abcXY"));
  if (framed) {
    CHECK(protocol_parser_calls == (unsigned)with_parser);
    const u32 single_length[] = {sizeof(raw_fixture)};
    const u32 *lengths = with_parser ? mixed_lengths : single_length;
    const u32 count = with_parser ? 5 : 1;
    put_replay("expected", raw_fixture, sizeof(raw_fixture), lengths, count);
    size_t wire_len;
    u8 *wire = get_bytes("expected", &wire_len);
    expect_bytes(path, wire, wire_len);
    free(wire);
    expect_prepare(path, "decoded", INPUT_REPLAY, raw_fixture, sizeof(raw_fixture),
                   lengths, count, 1, NO_FAULT);
  } else {
    CHECK(protocol_parser_calls == 0);
    expect_bytes(path, raw_fixture, sizeof(raw_fixture));
  }
  orig_extract_requests = NULL;
}

static void enriched_explicit_replay(void) { enriched_case(INPUT_REPLAY, 0, 0, 1); }
static void enriched_auto_replay_directory(void) { enriched_case(INPUT_AUTO, 1, 0, 1); }
static void enriched_explicit_raw_replay_directory(void) { enriched_case(INPUT_RAW, 1, 1, 0); }
static void enriched_auto_raw(void) { enriched_case(INPUT_AUTO, 0, 1, 0); }
static void enriched_protocol_boundaries(void) { enriched_case(INPUT_REPLAY, 0, 1, 1); }

static void enriched_failed_write(void) {
  put_bytes("generated", sentinel, sizeof(sentinel));
  input_format = INPUT_REPLAY;
  orig_extract_requests = fixture_mixed_protocol_parser;
  unsigned entries = entry_count(".");
  begin_fault(PARTIAL_FAILED_WRITE);
  errno = 0;
  u8 ok = write_enriched_seed("generated", "abcXY");
  int saved_errno = errno;
  finish_fault(PARTIAL_FAILED_WRITE);
  CHECK(!ok && saved_errno == EIO);
  expect_bytes("generated", sentinel, sizeof(sentinel));
  CHECK(entry_count(".") == entries);
  orig_extract_requests = NULL;
}

#define TEST(name) {#name, name}
static const struct {
  const char *name;
  void (*run)(void);
} test_cases[] = {
  TEST(fresh_raw), TEST(fresh_replay), TEST(direct_replay_queue),
  TEST(direct_resume_replay_queue), TEST(direct_replay_malformed),
  TEST(explicit_replay_in_queue), TEST(explicit_raw_ambiguous),
  TEST(explicit_raw_in_replay_queue), TEST(explicit_replay_malformed),
  TEST(explicit_replay_empty), TEST(auto_partial_framing), TEST(auto_empty_raw),
  TEST(replay_zero_records), TEST(resume_normal), TEST(resume_in_place_layout),
  TEST(resume_duplicate_slash), TEST(resume_dot_component),
  TEST(replay_duplicate_slash_is_strict), TEST(replay_dot_component_is_strict),
  TEST(resume_same_output_path), TEST(resume_zero_records),
  TEST(resume_missing_companion), TEST(resume_empty_missing_companion),
  TEST(resume_regions_only), TEST(resume_stale_same_length), TEST(resume_wrong_length),
  TEST(resume_malformed_companion), TEST(resume_stale_priority_fallback),
  TEST(resume_malformed_priority_fallback), TEST(resume_oversized_priority_fallback),
  TEST(resume_oversized_companion), TEST(resume_basename_mismatch),
  TEST(resume_corrupt_replay_and_regions), TEST(save_round_trip),
  TEST(save_zero_records), TEST(save_invalid_partition), TEST(save_invalid_bounds),
  TEST(atomic_replace), TEST(atomic_short_write), TEST(atomic_interrupted_write),
  TEST(atomic_failed_write), TEST(atomic_partial_failed_write), TEST(atomic_zero_write),
  TEST(atomic_failed_fsync), TEST(atomic_failed_rename), TEST(atomic_failure_new_path),
  TEST(prepare_failed_write), TEST(prepare_failed_fsync), TEST(prepare_failed_rename),
  TEST(prepare_short_write), TEST(prepare_same_path_failed_write),
  TEST(prepare_same_path_failed_fsync), TEST(prepare_same_path_failed_rename),
  TEST(save_failed_write), TEST(save_failed_fsync), TEST(save_failed_rename),
  TEST(pivot_replay_preserves_empty_regions),
  TEST(pivot_in_place_preserves_empty_regions), TEST(pivot_raw_uses_protocol_parser),
  TEST(pivot_failed_metadata_write), TEST(pivot_failed_metadata_fsync),
  TEST(pivot_failed_metadata_rename),
  TEST(preserve_required_directory), TEST(preserve_optional_missing),
  TEST(preserve_required_missing), TEST(preserve_existing_empty_backup),
  TEST(preserve_existing_nonempty_backup), TEST(preserve_backup_is_file),
  TEST(preserve_failed_rename), TEST(preserve_source_file_rejected),
  TEST(stage_orphan_replay_backup), TEST(stage_orphan_regions_backup),
  TEST(stage_fresh_snapshot), TEST(stage_interrupted_existing_raw_backup),
  TEST(enriched_explicit_replay),
  TEST(enriched_auto_replay_directory), TEST(enriched_explicit_raw_replay_directory),
  TEST(enriched_auto_raw), TEST(enriched_protocol_boundaries), TEST(enriched_failed_write)
};

int main(int argc, char **argv) {
  CHECK(argc == 2);
  if (!strcmp(argv[1], "--list")) {
    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); ++i)
      puts(test_cases[i].name);
    return 0;
  }
  for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); ++i) {
    if (strcmp(argv[1], test_cases[i].name)) continue;
    test_cases[i].run();
    printf("PASS %s\n", test_cases[i].name);
    return 0;
  }
  fprintf(stderr, "Unknown native recovery test: %s\n", argv[1]);
  return 2;
}
