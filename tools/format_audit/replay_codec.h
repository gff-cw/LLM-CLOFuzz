#ifndef __REPLAY_CODEC_H
#define __REPLAY_CODEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  REPLAY_OK = 0,
  REPLAY_ERR_NULL_PARAM,
  REPLAY_ERR_EMPTY_INPUT,
  REPLAY_ERR_TRUNCATED_HEADER,
  REPLAY_ERR_TRUNCATED_PAYLOAD,
  REPLAY_ERR_TRAILING_GARBAGE,
  REPLAY_ERR_RECORD_LIMIT_EXCEEDED,
  REPLAY_ERR_LENGTH_LIMIT_EXCEEDED,
  REPLAY_ERR_TOTAL_LIMIT_EXCEEDED,
  REPLAY_ERR_TARGET_INT_OVERFLOW,
  REPLAY_ERR_IO,
  REPLAY_ERR_ALLOC
} replay_status_t;

typedef struct {
  replay_status_t code;
  size_t record_index;
  size_t byte_offset;
  char detail[256];
} replay_diagnostic_t;

typedef struct {
  uint8_t *data;
  size_t len;
} replay_message_t;

typedef struct {
  replay_message_t *messages;
  size_t count;
  size_t capacity;
  size_t total_payload_bytes;
} replay_container_t;

typedef struct {
  size_t max_records;        /* Default: 100,000 */
  size_t max_single_len;     /* Default: 16 MB */
  size_t max_total_len;      /* Default: 64 MB */
  bool enforce_int_limits;   /* Verify lengths fit in target signed int */
  bool little_endian;        /* Standard AFLNet is little-endian u32 */
} replay_codec_limits_t;

void replay_limits_default(replay_codec_limits_t *limits);

void replay_container_init(replay_container_t *container);
void replay_container_free(replay_container_t *container);
replay_status_t replay_container_append(replay_container_t *container, const uint8_t *data, size_t len);

const char *replay_status_str(replay_status_t code);

replay_status_t replay_decode_buffer(const uint8_t *buf, size_t buf_size,
                                    const replay_codec_limits_t *limits,
                                    replay_container_t *out_container,
                                    replay_diagnostic_t *diag);

replay_status_t replay_encode_buffer(const replay_container_t *container,
                                    const replay_codec_limits_t *limits,
                                    uint8_t **out_buf, size_t *out_size,
                                    replay_diagnostic_t *diag);

replay_status_t replay_decode_file(const char *path,
                                  const replay_codec_limits_t *limits,
                                  replay_container_t *out_container,
                                  replay_diagnostic_t *diag);

replay_status_t replay_encode_file(const char *path,
                                  const replay_container_t *container,
                                  const replay_codec_limits_t *limits,
                                  replay_diagnostic_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* __REPLAY_CODEC_H */
