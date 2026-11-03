#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>

#include "../chat-llm.h"

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
    abort(); \
  } \
} while (0)

/* Linker wrappers inject failures at the actual C/JSON boundaries used by
   construct_prompt_stall, while the success path uses the real json-c. */
enum fault_kind { NONE, PROMPT_ALLOC, JSON_ALLOC, OBJECT_ADD, ARRAY_ADD,
                  SERIALIZE, FINAL_DUP };
static enum fault_kind fault;
static int fault_index;
static int alloc_calls, object_add_calls, array_add_calls;
static int test_cases;

struct tracked_object {
  json_object *object;
  int parent;
  int live;
};
static struct tracked_object tracked[32];
static int tracked_count;

static int find_object(json_object *object) {
  for (int i = tracked_count - 1; i >= 0; i--)
    if (tracked[i].object == object) return i;
  return -1;
}

static json_object *track(json_object *object) {
  CHECK(object);
  CHECK(tracked_count < (int)(sizeof(tracked) / sizeof(tracked[0])));
  tracked[tracked_count++] = (struct tracked_object){object, -1, 1};
  return object;
}

static void transferred(json_object *child, json_object *parent) {
  int child_index = find_object(child), parent_index = find_object(parent);
  CHECK(child_index >= 0 && parent_index >= 0);
  CHECK(tracked[child_index].live && tracked[parent_index].live);
  CHECK(tracked[child_index].parent == -1);
  tracked[child_index].parent = parent_index;
}

static void retire(int index) {
  CHECK(tracked[index].live); /* Detect a second release of an owned object. */
  tracked[index].live = 0;
  for (int i = 0; i < tracked_count; i++)
    if (tracked[i].parent == index && tracked[i].live) retire(i);
}

json_object *__real_json_object_new_array(void);
json_object *__real_json_object_new_object(void);
json_object *__real_json_object_new_string(const char *);
int __real_json_object_object_add(json_object *, const char *, json_object *);
int __real_json_object_array_add(json_object *, json_object *);
int __real_json_object_put(json_object *);
const char *__real_json_object_to_json_string(json_object *);
char *__real_strdup(const char *);

json_object *__wrap_json_object_new_array(void) {
  if (++alloc_calls == fault_index && fault == JSON_ALLOC) return NULL;
  return track(__real_json_object_new_array());
}

json_object *__wrap_json_object_new_object(void) {
  if (++alloc_calls == fault_index && fault == JSON_ALLOC) return NULL;
  return track(__real_json_object_new_object());
}

json_object *__wrap_json_object_new_string(const char *text) {
  if (++alloc_calls == fault_index && fault == JSON_ALLOC) return NULL;
  return track(__real_json_object_new_string(text));
}

int __wrap_json_object_object_add(json_object *object, const char *key,
                                  json_object *value) {
  CHECK(object && value);
  if (++object_add_calls == fault_index && fault == OBJECT_ADD) return -1;
  int result = __real_json_object_object_add(object, key, value);
  if (result == 0) transferred(value, object);
  return result;
}

int __wrap_json_object_array_add(json_object *array, json_object *value) {
  CHECK(array && value);
  if (++array_add_calls == fault_index && fault == ARRAY_ADD) return -1;
  int result = __real_json_object_array_add(array, value);
  if (result == 0) transferred(value, array);
  return result;
}

int __wrap_json_object_put(json_object *object) {
  int index = find_object(object);
  if (index >= 0) retire(index);
  return __real_json_object_put(object);
}

const char *__wrap_json_object_to_json_string(json_object *object) {
  if (fault == SERIALIZE) return NULL;
  return __real_json_object_to_json_string(object);
}

char *__wrap_strdup(const char *text) {
  if (fault == FINAL_DUP) return NULL;
  return __real_strdup(text);
}

int __wrap_asprintf(char **text, const char *format, ...) {
  if (fault == PROMPT_ALLOC) {
    *text = NULL;
    return -1;
  }
  va_list arguments;
  va_start(arguments, format);
  int result = vasprintf(text, format, arguments);
  va_end(arguments);
  return result;
}

static void reset(enum fault_kind kind, int index) {
  fault = kind;
  fault_index = index;
  alloc_calls = object_add_calls = array_add_calls = 0;
  tracked_count = 0;
  memset(tracked, 0, sizeof(tracked));
}

static void no_live_objects(void) {
  for (int i = 0; i < tracked_count; i++) CHECK(!tracked[i].live);
}

static void validate_payload(const char *payload, const char *history) {
  json_object *array = json_tokener_parse(payload);
  CHECK(array && json_object_is_type(array, json_type_array));
  CHECK(json_object_array_length(array) == 2);
  json_object *system = json_object_array_get_idx(array, 0);
  json_object *user = json_object_array_get_idx(array, 1);
  json_object *role, *content;
  CHECK(json_object_object_get_ex(system, "role", &role));
  CHECK(strcmp(json_object_get_string(role), "system") == 0);
  CHECK(json_object_object_get_ex(user, "role", &role));
  CHECK(strcmp(json_object_get_string(role), "user") == 0);
  CHECK(json_object_object_get_ex(user, "content", &content));
  CHECK(json_object_is_type(content, json_type_string));
  CHECK(strstr(json_object_get_string(content), history));
  __real_json_object_put(array);
}

static void failure_case(enum fault_kind kind, int index) {
  reset(kind, index);
  char *answer = construct_prompt_stall("fixture", "example", "history");
  CHECK(answer == NULL);
  no_live_objects();
  test_cases++;
}

int main(void) {
  char history[] = "fixed \"quotes\", backslash \\, newline\n and tab\t";
  reset(NONE, 0);
  char *payload = construct_prompt_stall("fixture", "sample\n", history);
  CHECK(payload);
  CHECK(alloc_calls == 7 && object_add_calls == 4 && array_add_calls == 2);
  no_live_objects();
  validate_payload(payload, history);
  free(payload);
  test_cases++;

  reset(NONE, 0);
  payload = construct_prompt_stall(NULL, NULL, NULL);
  CHECK(payload);
  no_live_objects();
  validate_payload(payload, "Communication History:");
  free(payload);
  test_cases++;

  failure_case(PROMPT_ALLOC, 0);
  for (int i = 1; i <= 7; i++) failure_case(JSON_ALLOC, i);
  for (int i = 1; i <= 4; i++) failure_case(OBJECT_ADD, i);
  for (int i = 1; i <= 2; i++) failure_case(ARRAY_ADD, i);
  failure_case(SERIALIZE, 0);
  failure_case(FINAL_DUP, 0);
  printf("PASS %d native prompt cases: real JSON, escaping and every allocation/add cleanup path.\n",
         test_cases);
  return 0;
}
