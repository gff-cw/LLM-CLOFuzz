#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct Packet { unsigned char data[256]; };
static volatile unsigned observed;

__attribute__((noinline)) void custom_receive(int channel, struct Packet *packet) {
  observed = channel + packet->data[42];
}

__attribute__((noinline)) void custom_send(int channel, struct Packet *packet) {
  observed = channel + packet->data[71];
}

int main(void) {
  struct Packet incoming, outgoing;
  unsigned char *state = malloc(4096);
  assert(state);
  uint32_t random = 0x12345678U;
  for (size_t i = 0; i < 4096; ++i) {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    state[i] = random;
  }
  for (unsigned i = 0; i < 256; ++i) incoming.data[i] = outgoing.data[i] = i;
  custom_receive(3, &incoming);
  custom_send(3, &outgoing);
  free(state);
  puts("legacy custom struct receive/send passed");
}
