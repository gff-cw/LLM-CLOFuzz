/* Test actual production transactions without the fuzzer main, network, or LLM. */
#define main discarded_fuzzer_main
#include "../afl-fuzz.c"
#undef main
#include <limits.h>

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "FAIL %s:%d: %s (errno=%d: %s)\n", \
          __FILE__, __LINE__, #x, errno, strerror(errno)); exit(1); \
} } while (0)

static const u8 bytes[] = {'a', 'b', 'c', 'X', 'Y'};
static const u32 mixed[] = {0, 3, 0, 2, 0}, zeros[] = {0, 0, 0};
static const char *raw_path = "peer/queue/id:000007";
static const char *replay_path = "peer/replayable-queue/id:000007";
static unsigned read_fault, read_hits;
static struct stat read_identity;
static const char *read_path;

ssize_t __real_read(int fd, void *buffer, size_t len);
ssize_t __wrap_read(int fd, void *buffer, size_t len) {
  struct stat st;
  int matches = read_fault && !read_hits && !fstat(fd, &st) &&
      st.st_dev == read_identity.st_dev && st.st_ino == read_identity.st_ino;
  if (matches && read_fault == 1 && len) {
    int writer = open(read_path, O_WRONLY | O_TRUNC);
    CHECK(writer >= 0 && close(writer) == 0);
    ++read_hits;
  }
  ssize_t result = __real_read(fd, buffer, len);
  if (matches && read_fault == 2 && result == 0) {
    int writer = open(read_path, O_WRONLY | O_TRUNC);
    CHECK(writer >= 0 && write(writer, "stale", 5) == 5);
    CHECK(close(writer) == 0);
    ++read_hits;
  }
  return result;
}

static unsigned llm_calls, null_response;
static u32 expected_counter;
char *__wrap_chat_with_llm(char *prompt, char *model, int tries, float temperature) {
  CHECK(prompt && !strcmp(prompt, "fixture prompt"));
  CHECK(model && !strcmp(model, "turbo"));
  CHECK(tries == STALL_RETRIES && temperature == 1.5f);
  CHECK(chat_times == expected_counter && chat_times <= CHATTING_THRESHOLD);
  CHECK(uninteresting_times == 0);
  ++llm_calls;
  return null_response ? NULL : strdup("fixture response");
}

static void directory(const char *path) { CHECK(mkdir(path, 0700) == 0); }
static void put(const char *path, const void *data, size_t len) {
  FILE *file = fopen(path, "wb");
  CHECK(file && (!len || fwrite(data, 1, len, file) == len));
  CHECK(fclose(file) == 0);
}
static void same(const char *path, const void *data, size_t len) {
  FILE *file = fopen(path, "rb");
  CHECK(file);
  u8 *actual = malloc(len ? len : 1);
  CHECK(actual && (!len || fread(actual, 1, len, file) == len));
  CHECK(!len || !memcmp(actual, data, len));
  CHECK(fgetc(file) == EOF && !ferror(file) && fclose(file) == 0);
  free(actual);
}
/* Fixed fixture encoding only. Production performs all decoding/validation. */
static void replay(const char *path, const u8 *data, u32 len,
                   const u32 *lengths, u32 count) {
  FILE *file = fopen(path, "wb");
  CHECK(file);
  u32 offset = 0;
  for (u32 i = 0; i < count; ++i) {
    CHECK(lengths[i] <= len - offset);
    CHECK(fwrite(&lengths[i], sizeof(u32), 1, file) == 1);
    CHECK(!lengths[i] || fwrite(data + offset, 1, lengths[i], file) == lengths[i]);
    offset += lengths[i];
  }
  CHECK(offset == len && fclose(file) == 0);
}
static void mixed_replay(const char *path) { replay(path, bytes, 5, mixed, 5); }
static void setup(void) {
  directory("peer"); directory("peer/queue"); directory("peer/replayable-queue");
  put(raw_path, bytes, 5);
  corpus_read_or_sync = 2; messages_sent = 17;
}

struct context {
  const u8 *data;
  u32 len, count;
  const u32 *lengths;
  unsigned calls, fail, check_frozen_count;
};
static struct context mixed_context(void) {
  struct context context = {.data = bytes, .len = 5, .count = 5, .lengths = mixed};
  return context;
}
static u8 inspect(const u8 *data, u32 len, void *opaque) {
  struct context *context = opaque;
  ++context->calls;
  CHECK(len == context->len && (!len || !memcmp(data, context->data, len)));
  CHECK(kl_messages && M2_prev == NULL && M2_next == kl_end(kl_messages));
  CHECK(corpus_read_or_sync == 0 && messages_sent == 0);
  CHECK(sync_case_message_count == context->count);
  kliter_t(lms) *it = kl_begin(kl_messages);
  u32 offset = 0;
  for (u32 i = 0; i < context->count; ++i) {
    CHECK(it != kl_end(kl_messages) && kl_val(it));
    CHECK(kl_val(it)->msize == (int)context->lengths[i]);
    if (context->lengths[i]) {
      CHECK(kl_val(it)->mdata && !memcmp(kl_val(it)->mdata,
            context->data + offset, context->lengths[i]));
    } else CHECK(kl_val(it)->mdata == NULL);
    offset += context->lengths[i]; it = kl_next(it);
  }
  CHECK(it == kl_end(kl_messages));
  if (context->check_frozen_count) {
    const u32 attempted[] = {1, context->count, 0, 2};
    for (u32 i = 0; i < 4; ++i) {
      messages_sent = attempted[i];
      CHECK(queued_message_limit() == context->count);
      CHECK(save_kl_messages_to_file(kl_messages, (u8 *)"saved.raw", 0,
                                    queued_message_limit()) == context->len);
      same("saved.raw", context->data, context->len);
      save_kl_messages_to_file(kl_messages, (u8 *)"saved.replay", 1, queued_message_limit());
      u32 count = 0;
      region_t *parts = convert_kl_messages_to_regions(kl_messages, &count,
                                                       queued_message_limit());
      CHECK(parts && count == context->count);
      int cursor = 0;
      for (u32 j = 0; j < count; ++j) {
        CHECK(parts[j].start_byte == cursor);
        CHECK(parts[j].end_byte == cursor + (int)context->lengths[j] - 1);
        cursor += (int)context->lengths[j];
      }
      ck_free(parts);
      u8 *wire = NULL, *decoded = NULL;
      u32 wire_len = 0, decoded_len = 0;
      parts = NULL; count = 0;
      CHECK(read_pivot_file("saved.replay", MAX_REPLAY_BYTES, &wire, &wire_len));
      CHECK(decode_pivot_replay(wire, wire_len, &decoded, &decoded_len, &parts, &count));
      CHECK(decoded_len == context->len && count == context->count);
      CHECK(!decoded_len || !memcmp(decoded, context->data, decoded_len));
      ck_free(wire); ck_free(decoded); ck_free(parts);
    }
  }
  messages_sent = context->count;
  if (context->fail) { errno = EIO; return 0; }
  return 1;
}
static void import(struct context *context, u8 success) {
  u8 format = input_format;
  errno = 0;
  u8 result = import_sync_case(raw_path, inspect, context);
  int error = errno;
  CHECK(result == success && (success || error));
  CHECK(!kl_messages && !M2_prev && !M2_next && !sync_case_message_count);
  CHECK(corpus_read_or_sync == 2 && messages_sent == 17 && input_format == format);
}

static void sync_mixed(void) {
  setup(); mixed_replay(replay_path); struct context c = mixed_context();
  import(&c, 1); CHECK(c.calls == 1); same(raw_path, bytes, 5);
}
static void sync_zeros(void) {
  setup(); put(raw_path, NULL, 0); replay(replay_path, NULL, 0, zeros, 3);
  struct context c = {.count=3, .lengths=zeros}; import(&c, 1); CHECK(c.calls == 1);
}
static void sync_initial_replay(void) { input_format=INPUT_REPLAY; sync_mixed(); }
static void sync_initial_raw(void) { input_format=INPUT_RAW; sync_mixed(); }
static void sync_callback_failure(void) {
  setup(); mixed_replay(replay_path); struct context c=mixed_context();
  c.fail=1; import(&c, 0); CHECK(c.calls==1);
  c.fail=0; import(&c, 1); CHECK(c.calls==2);
}
static void sync_missing_then_retry(void) {
  setup(); struct context c=mixed_context(); import(&c,0); CHECK(c.calls==0);
  mixed_replay(replay_path); import(&c,1); CHECK(c.calls==1);
}
static void sync_stale(void) {
  setup(); const u32 length[]={5}; replay(replay_path,(const u8 *)"stale",5,length,1);
  struct context c=mixed_context(); import(&c,0); CHECK(!c.calls); same(raw_path,bytes,5);
}
static void sync_malformed(void) {
  setup(); put(replay_path,"broken",6); struct context c=mixed_context();
  import(&c,0); CHECK(!c.calls);
}
static void sync_wrong_basename(void) {
  setup(); mixed_replay("peer/replayable-queue/id:000008"); struct context c=mixed_context();
  import(&c,0); CHECK(!c.calls);
}
static void sync_priority_fallback(void) {
  setup(); directory("peer/_resume_replayable-queue");
  put("peer/_resume_replayable-queue/id:000007","broken",6);
  mixed_replay(replay_path); struct context c=mixed_context(); import(&c,1); CHECK(c.calls==1);
}
static void sync_oversized_companion(void) {
  setup(); int fd=open(replay_path,O_CREAT|O_TRUNC|O_WRONLY,0600);
  CHECK(fd>=0 && ftruncate(fd,(off_t)MAX_REPLAY_BYTES+1)==0 && close(fd)==0);
  struct context c=mixed_context(); import(&c,0); CHECK(!c.calls);
}
static void sync_companion_race_retry(void) {
  setup(); mixed_replay(replay_path); CHECK(stat(replay_path,&read_identity)==0);
  read_path=replay_path; read_fault=1; struct context c=mixed_context();
  import(&c,0); CHECK(read_hits==1 && !c.calls);
  read_fault=0; mixed_replay(replay_path); import(&c,1); CHECK(c.calls==1);
}
static void sync_snapshot_not_reread(void) {
  setup(); mixed_replay(replay_path); CHECK(stat(raw_path,&read_identity)==0);
  read_path=raw_path; read_fault=2; struct context c=mixed_context();
  import(&c,1); CHECK(read_hits==1 && c.calls==1); read_fault=0; same(raw_path,"stale",5);
}
static void sync_frozen_save_count(void) {
  setup(); mixed_replay(replay_path); struct context c=mixed_context();
  c.check_frozen_count=1; import(&c,1); CHECK(c.calls==1 && queued_message_limit()==17);
}
static void sync_frozen_zero_records(void) {
  setup(); put(raw_path,NULL,0); replay(replay_path,NULL,0,zeros,3);
  struct context c={.count=3,.lengths=zeros,.check_frozen_count=1};
  import(&c,1); CHECK(c.calls==1);
}
static void clear_idle(void) {
  clear_message_context(); clear_message_context(); CHECK(!kl_messages && !M2_prev && !M2_next);
}
static void clear_live(void) {
  setup(); region_t parts[5]; memset(parts,0,sizeof(parts)); int offset=0;
  for(u32 i=0;i<5;++i) { parts[i].start_byte=offset;
    parts[i].end_byte=offset+(int)mixed[i]-1; offset+=mixed[i]; }
  kl_messages=construct_kl_messages((u8 *)raw_path,parts,5);
  M2_prev=kl_begin(kl_messages); M2_next=kl_end(kl_messages); clear_idle();
}
static void sync_busy_list(void) {
  setup(); mixed_replay(replay_path); kl_messages=kl_init(lms); klist_t(lms) *old=kl_messages;
  M2_prev=kl_begin(old); M2_next=kl_end(old); struct context c=mixed_context(); errno=0;
  CHECK(!import_sync_case(raw_path,inspect,&c) && errno==EBUSY);
  CHECK(!c.calls && kl_messages==old && M2_prev==kl_begin(old) && M2_next==kl_end(old));
  CHECK(corpus_read_or_sync==2 && messages_sent==17); clear_message_context();
}
static void sync_busy_boundary(void) {
  setup(); mixed_replay(replay_path); kliter_t(lms) node; memset(&node,0,sizeof(node));
  M2_next=&node; struct context c=mixed_context(); errno=0;
  CHECK(!import_sync_case(raw_path,inspect,&c) && errno==EBUSY);
  CHECK(!c.calls && !kl_messages && !M2_prev && M2_next==&node); clear_message_context();
}
static void sync_busy_frozen_count(void) {
  setup(); mixed_replay(replay_path); sync_case_message_count=3;
  struct context c=mixed_context(); errno=0;
  CHECK(!import_sync_case(raw_path,inspect,&c) && errno==EBUSY);
  CHECK(!c.calls && sync_case_message_count==3); sync_case_message_count=0;
}
static void cursor_failed_before_higher_success(void) {
  u32 next=7,retry=UINT_MAX;
  record_sync_result(7,0,&next,&retry); CHECK(next==7 && retry==7);
  record_sync_result(8,1,&next,&retry); CHECK(next==9 && retry==7);
  record_sync_result(10,1,&next,&retry); CHECK(next==11 && retry==7);
}
static void cursor_lowest_retry(void) {
  u32 next=7,retry=UINT_MAX;
  record_sync_result(9,0,&next,&retry); CHECK(next==7 && retry==9);
  record_sync_result(8,0,&next,&retry); CHECK(next==7 && retry==8);
  record_sync_result(6,0,&next,&retry); CHECK(next==7 && retry==6);
}
static void cursor_completed(void) {
  u32 next=7,retry=UINT_MAX;
  record_sync_result(7,1,&next,&retry); CHECK(next==8 && retry==UINT_MAX);
  record_sync_result(9,1,&next,&retry); CHECK(next==10 && retry==UINT_MAX);
  record_sync_result(8,1,&next,&retry); CHECK(next==10 && retry==UINT_MAX);
}
static void cursor_real_retry(void) {
  setup(); struct context c=mixed_context(); u32 next=7,retry=UINT_MAX;
  record_sync_result(7,import_sync_case(raw_path,inspect,&c),&next,&retry);
  CHECK(next==7 && retry==7 && !c.calls);
  mixed_replay(replay_path); next=7; retry=UINT_MAX;
  record_sync_result(7,import_sync_case(raw_path,inspect,&c),&next,&retry);
  CHECK(next==8 && retry==UINT_MAX && c.calls==1);
  CHECK(!kl_messages && !M2_prev && !M2_next && !sync_case_message_count);
}
static void stall_skip(const char *history,char *prompt) {
  uninteresting_times=9; CHECK(!request_stall_chat(history,prompt));
  CHECK(chat_times==0 && llm_calls==0 && uninteresting_times==9);
}
static void stall_empty_history(void) { stall_skip("","fixture prompt"); }
static void stall_missing_history(void) { stall_skip(NULL,"fixture prompt"); }
static void stall_failed_prompt(void) { stall_skip("history",NULL); }
static void stall_empty_prompt(void) { stall_skip("history",""); }
static void stall_actual_attempt(void) {
  uninteresting_times=9; expected_counter=1;
  char *response=request_stall_chat("history","fixture prompt");
  CHECK(response && !strcmp(response,"fixture response")); free(response);
  CHECK(chat_times==1 && llm_calls==1 && uninteresting_times==0);
}
static void stall_response_failure_charged(void) {
  null_response=1; uninteresting_times=9; expected_counter=1;
  CHECK(!request_stall_chat("history","fixture prompt"));
  CHECK(chat_times==1 && llm_calls==1 && uninteresting_times==0);
}
static void stall_cap_64(void) {
  CHECK(CHATTING_THRESHOLD==64);
  for(u32 i=0;i<80;++i) {
    uninteresting_times=9; expected_counter=i+1;
    char *response=request_stall_chat("history","fixture prompt");
    if(i<64) { CHECK(response && chat_times==i+1 && llm_calls==i+1);
      CHECK(uninteresting_times==0); free(response); }
    else CHECK(!response && chat_times==64 && llm_calls==64 && uninteresting_times==9);
  }
}
static void stall_skips_preserve_quota(void) {
  uninteresting_times=9;
  for(u32 i=0;i<80;++i) { CHECK(!request_stall_chat("","fixture prompt"));
    CHECK(!request_stall_chat("history",NULL)); }
  CHECK(chat_times==0 && llm_calls==0 && uninteresting_times==9);
  expected_counter=1; char *response=request_stall_chat("history","fixture prompt");
  CHECK(response && chat_times==1 && llm_calls==1); free(response);
}
#define TEST(name) {#name,name}
static const struct { const char *name; void (*run)(void); } cases[]={
 TEST(sync_mixed), TEST(sync_zeros), TEST(sync_initial_replay), TEST(sync_initial_raw),
 TEST(sync_callback_failure), TEST(sync_missing_then_retry), TEST(sync_stale),
 TEST(sync_malformed), TEST(sync_wrong_basename), TEST(sync_priority_fallback),
 TEST(sync_oversized_companion), TEST(sync_companion_race_retry),
 TEST(sync_snapshot_not_reread), TEST(sync_frozen_save_count), TEST(sync_frozen_zero_records),
 TEST(clear_idle), TEST(clear_live), TEST(sync_busy_list), TEST(sync_busy_boundary),
 TEST(sync_busy_frozen_count), TEST(cursor_failed_before_higher_success),
 TEST(cursor_lowest_retry), TEST(cursor_completed), TEST(cursor_real_retry),
 TEST(stall_empty_history), TEST(stall_missing_history), TEST(stall_failed_prompt),
 TEST(stall_empty_prompt), TEST(stall_actual_attempt), TEST(stall_response_failure_charged),
 TEST(stall_cap_64), TEST(stall_skips_preserve_quota)
};
int main(int argc,char **argv) {
  CHECK(argc==2);
  if(!strcmp(argv[1],"--list")) {
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) puts(cases[i].name);
    return 0;
  }
  for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
    if(strcmp(argv[1],cases[i].name)) continue;
    cases[i].run(); printf("PASS %s\n",cases[i].name); return 0;
  }
  fprintf(stderr,"Unknown native sync/stall test: %s\n",argv[1]); return 2;
}
