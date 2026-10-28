/* Header-free so 32-bit IR tests do not require a 32-bit libc installation. */
#ifndef COUNT_TYPE
#define COUNT_TYPE __SIZE_TYPE__
#endif
typedef COUNT_TYPE count_t;
extern void *malloc(count_t), *calloc(count_t, count_t), *realloc(void *, count_t);
extern void free(void *);
extern long recv(int, void *, count_t, int), send(int, void *, count_t, int);
void byte_counts(int fd, void *buffer, count_t length) {
  void *p = malloc(length);
  void *q = calloc(length, 2);
  p = realloc(p, length);
  recv(fd, buffer, length, 0);
  send(fd, buffer, length, 0);
  free(p);
  free(q);
}
