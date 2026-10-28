/* Counts executed lifetime hooks, including O0's unreachable small-object hook. */
#include <assert.h>
#include <stdint.h>
static unsigned char coverage[65536];
unsigned char *__afl_area_ptr = coverage;
__thread unsigned __afl_prev_loc;
static unsigned starts, ends;
static void *recorded;
void new_stack_alloc_record(void *address, uint64_t size) {
  assert(size == 64);
  recorded = address;
  ++starts;
}
void free_stack_alloc_record(void *address, uint64_t size) {
  assert(size == 64 && address == recorded);
  ++ends;
}
extern void lifetimes(void);
int main(void) {
  lifetimes();
  assert(starts == 1 && ends == 1);
  return 0;
}
