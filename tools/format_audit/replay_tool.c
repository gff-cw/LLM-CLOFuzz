#include "replay_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
  printf("Usage: %s <command> [options] <file>\n\n", prog);
  printf("Commands:\n");
  printf("  validate <replay_file>     Validate replay container format and completeness\n");
  printf("  dump <replay_file>         Dump record details (count, payload sizes, offsets)\n");
  printf("  roundtrip <replay_file>    Test encode/decode roundtrip integrity\n");
}

int main(int argc, char **argv) {
  if (argc < 3) {
    print_usage(argv[0]);
    return 1;
  }

  const char *cmd = argv[1];
  const char *file = argv[2];

  replay_codec_limits_t limits;
  replay_limits_default(&limits);
  replay_container_t container;
  replay_container_init(&container);
  replay_diagnostic_t diag;
  memset(&diag, 0, sizeof(diag));

  if (strcmp(cmd, "validate") == 0) {
    replay_status_t st = replay_decode_file(file, &limits, &container, &diag);
    if (st == REPLAY_OK) {
      printf("[+] VALID: '%s' has %zu records, %zu total payload bytes\n",
             file, container.count, container.total_payload_bytes);
      replay_container_free(&container);
      return 0;
    } else {
      printf("[-] INVALID: '%s' failed with [%s] at record %zu, offset %zu: %s\n",
             file, replay_status_str(st), diag.record_index, diag.byte_offset, diag.detail);
      replay_container_free(&container);
      return 2;
    }
  } else if (strcmp(cmd, "dump") == 0) {
    replay_status_t st = replay_decode_file(file, &limits, &container, &diag);
    if (st != REPLAY_OK) {
      printf("[-] ERROR: %s (%s)\n", replay_status_str(st), diag.detail);
      return 2;
    }
    printf("Replay Container: %s\n", file);
    printf("Total records: %zu\n", container.count);
    printf("Total payload bytes: %zu\n", container.total_payload_bytes);
    size_t cur_offset = 0;
    for (size_t i = 0; i < container.count; i++) {
      printf("  [%03zu] Offset: %6zu, Payload Len: %6zu\n",
             i, cur_offset, container.messages[i].len);
      cur_offset += 4 + container.messages[i].len;
    }
    replay_container_free(&container);
    return 0;
  } else if (strcmp(cmd, "roundtrip") == 0) {
    replay_status_t st = replay_decode_file(file, &limits, &container, &diag);
    if (st != REPLAY_OK) {
      printf("[-] Decode error: %s (%s)\n", replay_status_str(st), diag.detail);
      return 2;
    }
    uint8_t *encoded = NULL;
    size_t enc_size = 0;
    st = replay_encode_buffer(&container, &limits, &encoded, &enc_size, &diag);
    if (st != REPLAY_OK) {
      printf("[-] Encode error: %s (%s)\n", replay_status_str(st), diag.detail);
      replay_container_free(&container);
      return 3;
    }
    replay_container_t roundtrip;
    replay_container_init(&roundtrip);
    st = replay_decode_buffer(encoded, enc_size, &limits, &roundtrip, &diag);
    free(encoded);
    if (st != REPLAY_OK) {
      printf("[-] Roundtrip re-decode error: %s (%s)\n", replay_status_str(st), diag.detail);
      replay_container_free(&container);
      return 4;
    }
    bool match = (container.count == roundtrip.count &&
                  container.total_payload_bytes == roundtrip.total_payload_bytes);
    if (match) {
      for (size_t i = 0; i < container.count; i++) {
        if (container.messages[i].len != roundtrip.messages[i].len ||
            memcmp(container.messages[i].data, roundtrip.messages[i].data, container.messages[i].len) != 0) {
          match = false;
          break;
        }
      }
    }
    replay_container_free(&container);
    replay_container_free(&roundtrip);
    if (match) {
      printf("[+] Roundtrip byte-for-byte exact match verified\n");
      return 0;
    } else {
      printf("[-] Roundtrip mismatch detected!\n");
      return 5;
    }
  }

  print_usage(argv[0]);
  return 1;
}
