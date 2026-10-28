/* Build without the StateAFL pass: the helpers are called explicitly so their
 * real object maps, snapshots and ignored regions can be asserted directly. */
#define DEBUG_LOG_LEVEL
#include "../../llvm_mode/afl-llvm-rt-state-tracer.o.c"

static struct alloc_record *record_at(void *address) {
  struct alloc_record *record = NULL;
  assert(map_get(&record, alloc_records_map, &address));
  return record;
}

static void expect_ignored(void *address, size_t size) {
  struct ignore_area *area = NULL;
  assert(map_get(&area, ignore_map, &address));
  assert(area->addr == address && area->size == size);
}

int main(void) {
  assert(sizeof(struct state_shared) == 408);
  assert(sizeof(struct calibration) == 407324);
  assert(offsetof(struct calibration, dist) == 7316);

  char *heap = malloc(4096);
  assert(heap);
  new_heap_alloc_record(heap, 4096);
  assert(record_at(heap)->size == 4096 && !record_at(heap)->freed);
  for (size_t i = 0; i < 4096; ++i) assert(heap[i] == 0);
  uint32_t random = 0x12345678U;
  for (size_t i = 0; i < 4096; ++i) {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    heap[i] = random;
  }

  char stack[256];
  new_stack_alloc_record(stack, sizeof(stack));
  assert(record_at(stack)->size == sizeof(stack));
  char transient[128], mixed[256];
  new_stack_alloc_record(transient, sizeof(transient));
  new_stack_alloc_record(mixed, sizeof(mixed));
  trace_receive(mixed + 64, 16);
  expect_ignored(mixed + 64, 16);

  char *zero = calloc(3, 128);
  assert(zero);
  trace_calloc(zero, 128, 3);
  assert(record_at(zero)->size == 384);
  char *old = zero;
  zero = realloc(zero, 768);
  assert(zero);
  trace_realloc(zero, 768, old);
  assert(record_at(zero)->size == 768 && !record_at(zero)->freed);
  trace_realloc(NULL, 4096, zero); /* Failure retains the existing record. */
  assert(!record_at(zero)->freed);
  new_heap_alloc_record(NULL, 1024);
  trace_calloc(NULL, 1024, 1);

  /* Exercise byte counts above 32 bits without allocating or dereferencing
   * them: ASAN mode skips initialization, and sizes are restored before dump. */
  char wide_storage[128];
  size_t wide = ((size_t)1 << 33) + 17;
  addr_san_detected = 1;
  trace_calloc(wide_storage, wide, 1);
  assert(record_at(wide_storage)->size == wide);
  trace_realloc(wide_storage, wide - 1, wide_storage);
  assert(record_at(wide_storage)->size == wide - 1);
  record_at(wide_storage)->size = sizeof(wide_storage);
  addr_san_detected = 0;
  trace_receive(wide_storage, wide);
  expect_ignored(wide_storage, wide);

  int fd[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
  char io[512];
  new_stack_alloc_record(io, sizeof(io));
  trace_read(fd[0], io, 13);
  expect_ignored(io, 13);
  assert(curr_tracer_state == RECEIVING);
  size_t previous_dumps = queue_size(alloc_dumps_queue);
  trace_write(fd[0], io + 32, 7);
  expect_ignored(io + 32, 7);
  assert(curr_iter_no == 1);
  assert(queue_size(alloc_dumps_queue) > previous_dumps);
  free_stack_alloc_record(transient, sizeof(transient));
  assert(record_at(transient)->iter_no_end == 1);

  FILE *stream = fdopen(dup(fd[0]), "r+");
  assert(stream);
  trace_fgets(stream, io + 64, 16);
  expect_ignored(io + 64, 16);
  trace_fprintf(stream, io + 96, 5);
  expect_ignored(io + 96, 5);
  trace_fread(stream, io + 128, 3, 7);
  expect_ignored(io + 128, 21);
  trace_fwrite(stream, io + 160, 5, 7);
  expect_ignored(io + 160, 35);

  struct msghdr message;
  memset(&message, 0, sizeof(message));
  /* Later allocations are intentionally ignored by the original algorithm;
   * directly register this test-only record before checking msg helper size. */
  int saved_iter = curr_iter_no;
  curr_iter_no = 0;
  new_stack_alloc_record(&message, sizeof(message));
  curr_iter_no = saved_iter;
  trace_recvmsg(&message);
  expect_ignored(&message, sizeof(struct msghdr));
  trace_sendmsg(&message);

  int stream_fd = fileno(stream);
  assert(set_contains(sockets_set, &stream_fd));
  trace_fclose(stream); /* Must run while FILE is still alive. */
  assert(!set_contains(sockets_set, &stream_fd));
  assert(fclose(stream) == 0);
  assert(is_socket(fd[0]));
  trace_close(fd[0]);
  assert(!set_contains(sockets_set, &fd[0]));
  assert(close(fd[0]) == 0);
  assert(close(fd[1]) == 0);

  FILE *plain = tmpfile();
  assert(plain);
  int before = curr_iter_no;
  trace_fgets(plain, io + 224, 16);
  trace_fprintf(plain, io + 256, 4);
  trace_fread(plain, io + 288, 2, 3);
  trace_fwrite(plain, io + 320, 2, 3);
  trace_read(fileno(plain), io + 352, 8);
  trace_write(fileno(plain), io + 384, 8);
  assert(curr_iter_no == before);
  trace_fclose(plain);
  fclose(plain);

  /* Exercise ignored subregions, transient objects, lifecycle updates and
   * the postexecution TLSH/MVP path when the normal destructor runs. */
  char late[128];
  new_stack_alloc_record(late, sizeof(late));
  struct alloc_record *absent = NULL;
  void *late_address = late;
  assert(!map_get(&absent, alloc_records_map, &late_address));
  free_stack_alloc_record(&message, sizeof(message));
  assert(record_at(&message)->freed);
  free_stack_alloc_record(stack, sizeof(stack));
  assert(record_at(stack)->freed);
  free_stack_alloc_record(io, sizeof(io));
  free_stack_alloc_record(wide_storage, sizeof(wide_storage));
  free_stack_alloc_record(mixed, sizeof(mixed));
  free_heap_alloc_record(heap, 0);
  assert(record_at(heap)->freed);
  free(heap);
  free_heap_alloc_record(zero, 0);
  assert(record_at(zero)->freed);
  free(zero);
  printf("runtime records: heap/calloc/realloc/free, stack, all IO helpers, "
         "socket filtering, wide sizes and SHM ABI passed; iterations=%d\n",
         curr_iter_no);
  return 0;
}
