#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

static void peer_write(int fd, const void *data, size_t size) {
  assert(syscall(SYS_write, fd, data, size) == (ssize_t)size);
}

static void peer_read(int fd, size_t size) {
  char buffer[256];
  assert(size <= sizeof(buffer));
  assert(syscall(SYS_read, fd, buffer, size) == (ssize_t)size);
}

int main(int argc, char **argv) {
  (void)argc;
  char io[1024];
  struct {
    struct msghdr message;
    char keep_large_enough_for_lifetime[128];
  } incoming, outgoing;
  memset(io, 'A', sizeof(io));
  memset(&incoming, 0, sizeof(incoming));
  memset(&outgoing, 0, sizeof(outgoing));
  int datagram[2], stream[2];
  assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, datagram) == 0);
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, stream) == 0);

  peer_write(datagram[1], "hello", 5);
  assert(recv(datagram[0], io + 16, 32, 0) == 5);
  puts("recv");
  assert(send(datagram[0], io + 256, 7, 0) == 7);
  peer_read(datagram[1], 7);
  puts("send");
  peer_write(datagram[1], "world", 5);
  assert(recvfrom(datagram[0], io + 64, 48, 0, NULL, NULL) == 5);
  puts("recvfrom");
  assert(sendto(datagram[0], io + 320, 9, 0, NULL, 0) == 9);
  peer_read(datagram[1], 9);
  puts("sendto");

  struct iovec receive_vector = {io + 128, 24};
  incoming.message.msg_iov = &receive_vector;
  incoming.message.msg_iovlen = 1;
  peer_write(datagram[1], "message", 7);
  assert(recvmsg(datagram[0], &incoming.message, 0) == 7);
  puts("recvmsg");
  struct iovec send_vector = {io + 384, 11};
  outgoing.message.msg_iov = &send_vector;
  outgoing.message.msg_iovlen = 1;
  assert(sendmsg(datagram[0], &outgoing.message, 0) == 11);
  peer_read(datagram[1], 11);
  puts("sendmsg");

  peer_write(stream[1], "read", 4);
  assert(read(stream[0], io + 448, 16) == 4);
  puts("read");
  assert(write(stream[0], io + 480, 13) == 13);
  peer_read(stream[1], 13);
  puts("write");

  FILE *file = fdopen(dup(stream[0]), "r+");
  assert(file);
  setvbuf(file, NULL, _IONBF, 0);
  peer_write(stream[1], "line\n", 5);
  assert(fgets(io + 512, 32, file));
  puts("fgets");
  /* argv keeps the format opaque to builtin simplification. */
  const char *format = argv[1] ? argv[1] : "%s";
  assert(fprintf(file, format, "formatted") == 9);
  peer_read(stream[1], 9);
  puts("fprintf");
  peer_write(stream[1], "abcdef", 6);
  assert(fread(io + 576, 2, 3, file) == 3);
  puts("fread");
  assert(fwrite(io + 640, 3, 5, file) == 5);
  peer_read(stream[1], 15);
  puts("fwrite");
  assert(fclose(file) == 0);
  puts("fclose");

  FILE *plain = tmpfile();
  assert(plain);
  assert(fwrite(io + 704, 1, 8, plain) == 8);
  rewind(plain);
  assert(fread(io + 768, 1, 8, plain) == 8);
  assert(fclose(plain) == 0);
  puts("plain-file");
  assert(close(datagram[0]) == 0);
  assert(close(datagram[1]) == 0);
  assert(close(stream[0]) == 0);
  assert(close(stream[1]) == 0);
  puts("close");
  fflush(stdout);
  _exit(0); /* The pass must run end_state_tracer before bypassing destructors. */
}
