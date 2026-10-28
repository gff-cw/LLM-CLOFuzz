/* Deterministic local StateAFL regression server; one connection per process. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct protocol_state {
  uint32_t phase;
  unsigned char bytes[1024];
};

static void fill_state(struct protocol_state *state, uint32_t phase) {
  uint32_t value = 0x91e10da5u ^ (phase * 0x9e3779b9u);
  state->phase = phase;
  for (size_t i = 0; i < sizeof(state->bytes); ++i) {
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    state->bytes[i] = (unsigned char)value;
  }
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  int port = atoi(argv[1]);
  if (port < 1024 || port > 65535) return 2;
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) return 3;
  int reuse = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons((uint16_t)port);
  if (bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
      listen(listener, 1)) return 4;
  int client = accept(listener, NULL, NULL);
  if (client < 0) return 5;

  struct protocol_state *state = malloc(sizeof(*state));
  unsigned char *input = malloc(2);
  if (!state || !input) return 6;
  fill_state(state, 0);
  /* The protocol has fixed two-byte frames, independent of TCP segmentation.
     Bound the fixture below StateAFL's original 100-state array limit even
     when mutations generate a long stream of tiny frames. */
  for (unsigned frames = 0; frames < 8;) {
    ssize_t count = recv(client, input, 2, MSG_WAITALL);
    if (count < 0 && errno == EINTR) continue;
    if (count != 2) break;
    ++frames;
    switch (input[0]) {
      case 'A': fill_state(state, 1); break;
      case 'B': fill_state(state, state->phase == 1 ? 2 : 3); break;
      case 'C': fill_state(state, state->phase == 2 ? 4 : 5); break;
      default: fill_state(state, 6 + input[0] % 3); break;
    }
    /* Read the tracked object so allocation and updates survive optimization. */
    unsigned char reply[4] = {'O', 'K', (unsigned char)('0' + state->phase),
                             state->bytes[17]};
    if (send(client, reply, sizeof(reply), MSG_NOSIGNAL) < 0) break;
  }
  close(client);
  close(listener);
  free(input);
  free(state);
  return 0;
}
