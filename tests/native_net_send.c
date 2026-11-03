#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "../types.h"
#include "../aflnet.h"

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
    abort(); \
  } \
} while (0)

static const struct timeval bounded_timeout = {.tv_sec = 0, .tv_usec = 20000};
static int short_send;
static int send_calls;

ssize_t __real_send(int fd, const void *buf, size_t len, int flags);

ssize_t __wrap_send(int fd, const void *buf, size_t len, int flags) {
  send_calls++;
  if (short_send) return len ? (ssize_t)len - 1 : 0;
  return __real_send(fd, buf, len, flags);
}

static int readable(int fd, int millis) {
  struct pollfd p = {.fd = fd, .events = POLLIN};
  int n = poll(&p, 1, millis);
  CHECK(n >= 0);
  return n > 0 && (p.revents & POLLIN);
}

static int loopback_bound(int type, struct sockaddr_in *address) {
  int fd = socket(AF_INET, type, 0);
  CHECK(fd >= 0);
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  CHECK(bind(fd, (struct sockaddr *)address, sizeof(*address)) == 0);
  socklen_t size = sizeof(*address);
  CHECK(getsockname(fd, (struct sockaddr *)address, &size) == 0);
  return fd;
}

static void udp_tests(void) {
  struct sockaddr_in address;
  int receiver = loopback_bound(SOCK_DGRAM, &address);
  int sender = socket(AF_INET, SOCK_DGRAM, 0);
  CHECK(sender >= 0);
  CHECK(connect(sender, (struct sockaddr *)&address, sizeof(address)) == 0);
  char received[65536];

  send_calls = 0;
  CHECK(net_send(sender, bounded_timeout, NULL, 0) == 0);
  CHECK(send_calls == 1);
  CHECK(readable(receiver, 100));
  CHECK(recv(receiver, received, sizeof(received), 0) == 0);
  CHECK(!readable(receiver, 20));
  puts("PASS zero UDP: exactly one real loopback datagram");

  char payload[5000];
  for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (char)i;
  send_calls = 0;
  CHECK(net_send(sender, bounded_timeout, payload, sizeof(payload)) == sizeof(payload));
  CHECK(send_calls == 1);
  CHECK(readable(receiver, 100));
  CHECK(recv(receiver, received, sizeof(received), 0) == sizeof(payload));
  CHECK(memcmp(received, payload, sizeof(payload)) == 0);
  CHECK(!readable(receiver, 20));
  puts("PASS ordinary UDP: payload preserved in one datagram");

  static char oversized[65536];
  errno = 0;
  CHECK(net_send(sender, bounded_timeout, oversized, sizeof(oversized)) == -1);
  CHECK(errno == EMSGSIZE);
  CHECK(!readable(receiver, 20));
  puts("PASS oversized UDP: kernel failure is propagated");

  short_send = 1;
  send_calls = 0;
  errno = 0;
  CHECK(net_send(sender, bounded_timeout, payload, 4) == -1);
  CHECK(errno == EIO && send_calls == 1);
  short_send = 0;
  CHECK(!readable(receiver, 20));
  puts("PASS injected partial datagram: failure without sending a second fragment");

  struct timeval invalid_timeout = {.tv_sec = 0, .tv_usec = -1};
  errno = 0;
  CHECK(net_send(sender, invalid_timeout, NULL, 0) == -1);
  CHECK(errno == EINVAL);
  CHECK(!readable(receiver, 20));
  puts("PASS invalid datagram timeout: no false zero-byte success");

  close(sender);
  close(receiver);

  sender = socket(AF_INET, SOCK_DGRAM, 0);
  CHECK(sender >= 0);
  errno = 0;
  CHECK(net_send(sender, bounded_timeout, NULL, 0) == -1);
  CHECK(errno == EDESTADDRREQ || errno == ENOTCONN);
  close(sender);
  puts("PASS unconnected zero UDP: destination failure is propagated");
}

static void queue_timeout_test(void) {
  int sockets[2];
  CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);
  int capacity = 4096;
  CHECK(setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &capacity, sizeof(capacity)) == 0);
  int filled = 0;
  while (filled < 10000 && __real_send(sockets[0], "x", 1, MSG_DONTWAIT) == 1) filled++;
  CHECK(filled > 0 && filled < 10000);
  CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
  struct timespec before, after;
  CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
  errno = 0;
  CHECK(net_send(sockets[0], bounded_timeout, NULL, 0) == -1);
  CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
  CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
  long long elapsed_ns = (after.tv_sec - before.tv_sec) * 1000000000LL +
                         after.tv_nsec - before.tv_nsec;
  CHECK(elapsed_ns >= 1000000 && elapsed_ns < 2000000000LL);
  char byte;
  for (int i = 0; i < filled; i++) CHECK(recv(sockets[1], &byte, 1, 0) == 1);
  CHECK(!readable(sockets[1], 20));
  CHECK(net_send(sockets[0], bounded_timeout, NULL, 0) == 0);
  CHECK(readable(sockets[1], 100));
  CHECK(recv(sockets[1], &byte, 1, 0) == 0);
  CHECK(!readable(sockets[1], 20));
  close(sockets[0]);
  close(sockets[1]);
  puts("PASS full local datagram queue: timeout fails, drained queue sends empty record");
}

static void tcp_tests(void) {
  struct sockaddr_in address;
  int listener = loopback_bound(SOCK_STREAM, &address);
  CHECK(listen(listener, 1) == 0);
  int sender = socket(AF_INET, SOCK_STREAM, 0);
  CHECK(sender >= 0);
  CHECK(connect(sender, (struct sockaddr *)&address, sizeof(address)) == 0);
  int receiver = accept(listener, NULL, NULL);
  CHECK(receiver >= 0);

  send_calls = 0;
  CHECK(net_send(sender, bounded_timeout, NULL, 0) == 0);
  CHECK(send_calls == 0);
  CHECK(!readable(receiver, 20));
  puts("PASS zero TCP: no send, no bytes, no connection close");

  char payload[] = "stream check";
  char received[sizeof(payload)];
  CHECK(net_send(sender, bounded_timeout, payload, sizeof(payload)) == sizeof(payload));
  CHECK(readable(receiver, 100));
  CHECK(recv(receiver, received, sizeof(received), MSG_WAITALL) == sizeof(payload));
  CHECK(memcmp(received, payload, sizeof(payload)) == 0);
  close(sender);
  close(receiver);
  close(listener);
  puts("PASS ordinary TCP: stream payload unchanged after empty no-op");
}

int main(void) {
  alarm(10);
  udp_tests();
  queue_timeout_test();
  tcp_tests();
  errno = 0;
  CHECK(net_send(-1, bounded_timeout, NULL, 0) == -1);
  CHECK(errno == EBADF);
  puts("PASS invalid descriptor: no false empty success");
  puts("All native net_send tests passed.");
  return 0;
}
