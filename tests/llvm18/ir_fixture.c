/* Compile-only coverage of every original StateAFL call recognition branch. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

struct custom_payload { unsigned long words[12]; };
struct custom_payload global_payload;
extern void escape(void *);
extern struct custom_payload *external_payload(void);
extern void custom_receive_local(struct custom_payload *);
extern void custom_send_local(struct custom_payload *);
extern void custom_receive_global(struct custom_payload *);
extern void custom_send_global(struct custom_payload *);
extern void custom_receive_formal(struct custom_payload *);
extern void custom_send_formal(struct custom_payload *);
extern void custom_receive_external(struct custom_payload *);
extern void custom_send_external(struct custom_payload *);
extern void custom_receive_nobuf(void);
extern void custom_send_nobuf(void);

/* Only the callee's debug signature describes this opaque external argument. */
__attribute__((noinline)) void custom_receive_debug(struct custom_payload *p) {
  escape(p);
}
__attribute__((noinline)) void custom_send_debug(struct custom_payload *p) {
  escape(p);
}

__attribute__((noinline)) void memory_fixture(size_t length) {
  void *a = malloc(length);
  escape(a);
  void *b = calloc(length, 3);
  escape(b);
  a = realloc(a, length + 3);
  escape(a);
  free(a);
  free(b);
  char stack_large[128];
  char stack_small[32];
  escape(stack_large);
  escape(stack_small);
}

__attribute__((noinline)) void io_fixture(int fd, FILE *stream, char *buffer,
                                         size_t length, struct msghdr *message,
                                         struct sockaddr *address,
                                         socklen_t *address_size) {
  recv(fd, buffer, length, 0);
  recvfrom(fd, buffer, length, 0, address, address_size);
  recvmsg(fd, message, 0);
  send(fd, buffer, length, 0);
  sendto(fd, buffer, length, 0, address, *address_size);
  sendmsg(fd, message, 0);
  read(fd, buffer, length);
  write(fd, buffer, length);
  fprintf(stream, "%s", buffer);
  fgets(buffer, 128, stream);
  fread(buffer, 2, length, stream);
  fwrite(buffer, 2, length, stream);
  close(fd);
  fclose(stream);
}

__attribute__((noinline)) void custom_fixture(struct custom_payload *parameter) {
  struct custom_payload local = {{0}};
  /* Typed GEP evidence is available even when debug information is disabled. */
  parameter->words[3] = global_payload.words[1];
  custom_receive_local(&local);
  custom_send_local(&local);
  custom_receive_global(&global_payload);
  custom_send_global(&global_payload);
  custom_receive_formal(parameter);
  custom_send_formal(parameter);
  custom_receive_external(external_payload());
  custom_send_external(external_payload());
  custom_receive_debug(external_payload());
  custom_send_debug(external_payload());
  custom_receive_nobuf();
  custom_send_nobuf();
}

__attribute__((noinline, noreturn)) void exit_fixture(int status) {
  _exit(status);
}
