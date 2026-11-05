#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../alloc-inl.h"
#include "../chat-llm.h"

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
    abort(); \
  } \
} while (0)

static int cases;
static int compile_calls, jit_calls, fail_compile, fail_jit;

pcre2_code *__real_pcre2_compile_8(PCRE2_SPTR, PCRE2_SIZE, uint32_t, int *,
                                  PCRE2_SIZE *, pcre2_compile_context *);
int __real_pcre2_jit_compile_8(pcre2_code *, uint32_t);

pcre2_code *__wrap_pcre2_compile_8(PCRE2_SPTR pattern, PCRE2_SIZE length,
                                  uint32_t options, int *error,
                                  PCRE2_SIZE *offset, pcre2_compile_context *context) {
  compile_calls++;
  if (fail_compile == compile_calls)
    return __real_pcre2_compile_8((PCRE2_SPTR)"(", 1, options, error, offset, context);
  return __real_pcre2_compile_8(pattern, length, options, error, offset, context);
}

int __wrap_pcre2_jit_compile_8(pcre2_code *code, uint32_t options) {
  CHECK(code != NULL); /* Production must check failed compilation first. */
  jit_calls++;
  if (fail_jit) return PCRE2_ERROR_JIT_BADOPTION;
  return __real_pcre2_jit_compile_8(code, options);
}

static void destroy_grammars(klist_t(gram) *list) {
  for (kliter_t(gram) *it = kl_begin(list); it != kl_end(list); it = kl_next(it))
    json_object_put(kl_val(it));
  kl_destroy_gram(list);
}

/* Use the same representation boundaries as startup: response JSON -> accepted
   string array -> JSON-encoded header plus decoded field-table strings. */
static char *compile_fixture(const char *header, const char **fields,
                             const int *frequencies, size_t count,
                             pcre2_code **patterns) {
  json_object *fixture = json_object_new_array();
  CHECK(fixture);
  CHECK(json_object_array_add(fixture, json_object_new_string(header)) == 0);
  for (size_t i = 0; i < count; i++)
    CHECK(json_object_array_add(fixture, json_object_new_string(fields[i])) == 0);
  const char *serialized = json_object_to_json_string(fixture);
  CHECK(serialized);
  char *response = strdup(serialized);
  CHECK(response);
  klist_t(gram) *list = kl_init(gram);
  CHECK(list);
  extract_message_grammars(response, list);
  CHECK(list->size == 1);
  json_object *grammar = kl_val(kl_begin(list));
  khash_t(field_table) *table = kh_init(field_table);
  CHECK(table);
  for (size_t i = 0; i < count; i++) {
    int absent;
    const char *field = json_object_get_string(json_object_array_get_idx(grammar, i + 1));
    khiter_t it = kh_put(field_table, table, field, &absent);
    CHECK(absent >= 0);
    kh_value(table, it) = frequencies ? frequencies[i] : TEMPLATE_CONSISTENCY_COUNT;
  }
  char *type = extract_message_pattern(json_object_to_json_string(
                                       json_object_array_get_idx(grammar, 0)),
                                       table, patterns, -1, NULL);
  kh_destroy(field_table, table);
  destroy_grammars(list);
  free(response);
  json_object_put(fixture);
  return type;
}

static void release_pattern(char *type, pcre2_code **patterns) {
  if (type) ck_free(type);
  if (patterns[0]) pcre2_code_free(patterns[0]);
  if (patterns[1]) pcre2_code_free(patterns[1]);
  patterns[0] = patterns[1] = NULL;
}

static int matches(pcre2_code *pattern, const char *text) {
  pcre2_match_data *match = pcre2_match_data_create_from_pattern(pattern, NULL);
  CHECK(match);
  int result = pcre2_match(pattern, (PCRE2_SPTR)text, strlen(text), 0, 0, match, NULL);
  pcre2_match_data_free(match);
  return result >= 0;
}

static void capture_equals(pcre2_code *pattern, const char *text,
                            unsigned int group, const char *expected) {
  pcre2_match_data *match = pcre2_match_data_create_from_pattern(pattern, NULL);
  CHECK(match);
  int result = pcre2_match(pattern, (PCRE2_SPTR)text, strlen(text), 0, 0, match, NULL);
  CHECK(result > (int)group);
  PCRE2_SIZE *offset = pcre2_get_ovector_pointer(match);
  CHECK(offset[group * 2 + 1] - offset[group * 2] == strlen(expected));
  CHECK(memcmp(text + offset[group * 2], expected, strlen(expected)) == 0);
  pcre2_match_data_free(match);
}

static void parser_tests(void) {
  char response[] = "```json\n[\"HDR <<VALUE>>\\r\\n\",\"Host: [::1]\\r\\n\","
                    "\"Quoted: \\\"x[y]\\\"\\\\folder\\r\\n\"]\n```";
  klist_t(gram) *list = kl_init(gram);
  extract_message_grammars(response, list);
  CHECK(list->size == 1);
  json_object *grammar = kl_val(kl_begin(list));
  CHECK(json_object_array_length(grammar) == 3);
  CHECK(strcmp(json_object_get_string(json_object_array_get_idx(grammar, 1)),
               "Host: [::1]\r\n") == 0);
  CHECK(strcmp(json_object_get_string(json_object_array_get_idx(grammar, 2)),
               "Quoted: \"x[y]\"\\folder\r\n") == 0);
  /* Destroying list nodes does not release borrowed JSON strings. The caller
     keeps this owned object until its table keys are no longer needed. */
  kl_destroy_gram(list);
  CHECK(strcmp(json_object_get_string(json_object_array_get_idx(grammar, 0)),
               "HDR <<VALUE>>\r\n") == 0);
  json_object_put(grammar);
  cases++;

  char multiple[] = "[not JSON] first: [\"ONE <<VALUE>>\\r\\n\"]\n"
                    "```json\n[\"TWO <<VALUE>>\\r\\n\",\"Host: [::1]\\r\\n\"]\n```";
  list = kl_init(gram);
  extract_message_grammars(multiple, list);
  CHECK(list->size == 2);
  CHECK(strcmp(json_object_get_string(json_object_array_get_idx(
                 kl_val(kl_begin(list)), 0)), "ONE <<VALUE>>\r\n") == 0);
  destroy_grammars(list);
  cases++;

  const char *invalid[] = {
    "{}", "123", "\"not an array\"", "[]", "[\"\"]",
    "[1,\"field\"]", "[null]", "[\"HDR\",17]", "[\"HDR\",[\"nested\"]]",
    "[\"HDR\",\"x\\u0000y\"]", "[\"HDR\",\"unfinished [::1]",
    "[\"HDR\",\"escaped \\\" quote [::1]",
    "[\"HDR\",\"bad\nnewline\"]", "[\"HDR\",]"
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    list = kl_init(gram);
    char *answer = strdup(invalid[i]);
    CHECK(answer);
    extract_message_grammars(answer, list);
    if (list->size != 0) fprintf(stderr, "Unexpectedly accepted invalid grammar case %zu\n", i);
    CHECK(list->size == 0);
    destroy_grammars(list);
    free(answer);
    cases++;
  }

  char *oversized = malloc(1024U * 1024U + 2);
  CHECK(oversized);
  memset(oversized, 'x', 1024U * 1024U + 1);
  oversized[1024U * 1024U + 1] = '\0';
  list = kl_init(gram);
  errno = 0;
  extract_message_grammars(oversized, list);
  CHECK(list->size == 0 && errno == E2BIG);
  destroy_grammars(list);
  free(oversized);
  cases++;
}

static void literal_and_capture_tests(void) {
  const char *fields[] = {"Host: [::1]\r\n", "Quoted: \"x[y]\"\\folder\r\n",
                          "Literal: \\r\\n\r\n"};
  pcre2_code *patterns[2] = {NULL, NULL};
  char *type = compile_fixture("DO[.*] <<VALUE>>\r\n", fields, NULL, 3, patterns);
  CHECK(type && strcmp(type, "DO[.*]") == 0);
  CHECK(matches(patterns[0], "DO[.*] abc\r\n"));
  CHECK(!matches(patterns[0], "DOaaaa abc\r\n"));
  capture_equals(patterns[0], "DO[.*] abc\r\n", 1, "abc");
  CHECK(matches(patterns[1], fields[0]));
  CHECK(matches(patterns[1], fields[1]));
  CHECK(matches(patterns[1], fields[2]));
  CHECK(!matches(patterns[1], "Literal: \r\n\r\n"));
  CHECK(!matches(patterns[1], "Host: ::1\r\n"));
  range_list ranges = starts_with("DO[.*] abc\r\n", 12, patterns[0]);
  CHECK(kv_size(ranges) == 2);
  CHECK(kv_A(ranges, 0).start == 7 && kv_A(ranges, 0).len == 3);
  kv_destroy(ranges);
  release_pattern(type, patterns);
  cases++;

  const char *pair_fields[] = {"Pair: <<LEFT>>/<<RIGHT>>\r\n", "Count: <<VALUE>>\r\n"};
  type = compile_fixture("CMD <<FIRST>> + <<SECOND>>\r\n", pair_fields, NULL, 2, patterns);
  CHECK(type);
  capture_equals(patterns[0], "CMD left + right\r\n", 1, "left");
  capture_equals(patterns[0], "CMD left + right\r\n", 2, "right");
  capture_equals(patterns[1], "Pair: A/B\r\n", 1, "A");
  capture_equals(patterns[1], "Pair: A/B\r\n", 2, "B");
  capture_equals(patterns[1], "Count: 5\r\n", 1, "5");
  uint32_t capture_count = 0;
  CHECK(pcre2_pattern_info(patterns[0], PCRE2_INFO_CAPTURECOUNT, &capture_count) == 0);
  CHECK(capture_count == 2);
  CHECK(pcre2_pattern_info(patterns[1], PCRE2_INFO_CAPTURECOUNT, &capture_count) == 0);
  CHECK(capture_count == 2); /* Branch-reset numbering, no literal captures. */
  release_pattern(type, patterns);
  cases++;

  type = compile_fixture("CMD <<VALUE>>\r\n", NULL, NULL, 0, patterns);
  CHECK(type && matches(patterns[1], "unclassified field"));
  release_pattern(type, patterns);
  cases++;

  const char *voted[] = {"Agreed: <<VALUE>>\r\n", "Rare: <<VALUE>>\r\n"};
  int votes[] = {3, 2};
  type = compile_fixture("CMD <<VALUE>>\r\n", voted, votes, 2, patterns);
  CHECK(type && matches(patterns[1], "Agreed: yes\r\n"));
  CHECK(!matches(patterns[1], "Rare: no\r\n"));
  release_pattern(type, patterns);
  cases++;
}

static void size_tests(void) {
  char long_header[512], long_payload[512];
  char fixed[301];
  memset(fixed, 'A', sizeof(fixed) - 1);
  fixed[sizeof(fixed) - 1] = '\0';
  CHECK(snprintf(long_header, sizeof(long_header), "LONG %s <<VALUE>>\r\n", fixed) > 123);
  CHECK(snprintf(long_payload, sizeof(long_payload), "LONG %s value\r\n", fixed) > 123);
  pcre2_code *patterns[2] = {NULL, NULL};
  char *type = compile_fixture(long_header, NULL, NULL, 0, patterns);
  CHECK(type && strcmp(type, "LONG") == 0);
  CHECK(matches(patterns[0], long_payload));
  capture_equals(patterns[0], long_payload, 1, "value");
  release_pattern(type, patterns);
  cases++;

  char storage[40][128];
  const char *fields[40];
  char filler[51];
  memset(filler, 'a', sizeof(filler) - 1);
  filler[sizeof(filler) - 1] = '\0';
  size_t total = 0;
  for (int i = 0; i < 40; i++) {
    int length = snprintf(storage[i], sizeof(storage[i]), "Field%02d.[x]: %s <<VALUE>>\r\n", i, filler);
    CHECK(length > 0 && length < (int)sizeof(storage[i]));
    fields[i] = storage[i];
    total += (size_t)length;
  }
  CHECK(total > 1024);
  type = compile_fixture("CMD <<VALUE>>\r\n", fields, NULL, 40, patterns);
  CHECK(type);
  char payload[128];
  CHECK(snprintf(payload, sizeof(payload), "Field39.[x]: %s value\r\n", filler) > 0);
  CHECK(matches(patterns[1], payload));
  capture_equals(patterns[1], payload, 1, "value");
  release_pattern(type, patterns);
  cases++;

  char *too_long = malloc(70001);
  CHECK(too_long);
  memset(too_long, 'A', 70000);
  too_long[70000] = '\0';
  compile_calls = 0;
  errno = 0;
  type = compile_fixture(too_long, NULL, NULL, 0, patterns);
  CHECK(!type && !patterns[0] && !patterns[1]);
  CHECK(errno == E2BIG && compile_calls == 0);
  free(too_long);
  cases++;
}

static void compilation_failure_tests(void) {
  pcre2_code *patterns[2] = {NULL, NULL};
  for (int which = 1; which <= 2; which++) {
    compile_calls = jit_calls = 0;
    fail_compile = which;
    char *type = compile_fixture("CMD <<VALUE>>\r\n", NULL, NULL, 0, patterns);
    CHECK(type == NULL && patterns[0] == NULL && patterns[1] == NULL);
    CHECK(compile_calls == which && jit_calls == which - 1);
    fail_compile = 0;
    cases++;
  }
  compile_calls = jit_calls = 0;
  fail_jit = 1;
  char *type = compile_fixture("CMD <<VALUE>>\r\n", NULL, NULL, 0, patterns);
  CHECK(type && compile_calls == 2 && jit_calls == 2);
  CHECK(matches(patterns[0], "CMD value\r\n"));
  release_pattern(type, patterns);
  fail_jit = 0;
  cases++;

  khash_t(field_table) *table = kh_init(field_table);
  const char *bad_headers[] = {"null", "[]", "\"\"", "\"CMD\" trailing"};
  for (size_t i = 0; i < sizeof(bad_headers) / sizeof(bad_headers[0]); i++) {
    type = extract_message_pattern(bad_headers[i], table, patterns, -1, NULL);
    CHECK(!type && !patterns[0] && !patterns[1]);
    cases++;
  }
  kh_destroy(field_table, table);
}

int main(void) {
  parser_tests();
  literal_and_capture_tests();
  size_tests();
  compilation_failure_tests();
  printf("PASS %d native grammar cases: real JSON boundaries, bounded patterns, literal bytes, captures and compile failures.\n",
         cases);
  return 0;
}
