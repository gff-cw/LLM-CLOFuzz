#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static volatile unsigned observed;

int main(int argc, char **argv) {
  (void)argv;
  size_t size = 255 + argc;
  unsigned char *first = malloc(size);
  assert(first);
  for (size_t i = 0; i < size; ++i) first[i] = i;
  observed = first[size / 2];
  unsigned char *second = calloc(3, size);
  assert(second);
  for (size_t i = 0; i < size * 3; ++i) assert(second[i] == 0);
  second[0] = 23;
  second = realloc(second, size * 4);
  assert(second && second[0] == 23);
  observed = second[0];
  free(first);
  free(second);
  puts("malloc/calloc/realloc/free passed");
}
