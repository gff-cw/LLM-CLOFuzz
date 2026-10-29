#define _GNU_SOURCE
#include "replay_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

void replay_limits_default(replay_codec_limits_t *limits) {
  if (!limits) return;
  limits->max_records = 100000;
  limits->max_single_len = 16 * 1024 * 1024; /* 16 MB */
  limits->max_total_len = 64 * 1024 * 1024;  /* 64 MB */
  limits->enforce_int_limits = true;
  limits->little_endian = true;
}

void replay_container_init(replay_container_t *container) {
  if (!container) return;
  memset(container, 0, sizeof(*container));
}

void replay_container_free(replay_container_t *container) {
  if (!container) return;
  if (container->messages) {
    for (size_t i = 0; i < container->count; i++) {
      free(container->messages[i].data);
    }
    free(container->messages);
  }
  memset(container, 0, sizeof(*container));
}

replay_status_t replay_container_append(replay_container_t *container, const uint8_t *data, size_t len) {
  if (!container) return REPLAY_ERR_NULL_PARAM;
  if (container->count >= container->capacity) {
    size_t new_cap = container->capacity ? container->capacity * 2 : 16;
    replay_message_t *new_msgs = (replay_message_t *)realloc(container->messages, new_cap * sizeof(replay_message_t));
    if (!new_msgs) return REPLAY_ERR_ALLOC;
    container->messages = new_msgs;
    container->capacity = new_cap;
  }

  uint8_t *copied = NULL;
  if (len > 0) {
    copied = (uint8_t *)malloc(len);
    if (!copied) return REPLAY_ERR_ALLOC;
    if (data) memcpy(copied, data, len);
    else memset(copied, 0, len);
  }

  container->messages[container->count].data = copied;
  container->messages[container->count].len = len;
  container->count++;
  container->total_payload_bytes += len;
  return REPLAY_OK;
}

const char *replay_status_str(replay_status_t code) {
  switch (code) {
    case REPLAY_OK: return "OK";
    case REPLAY_ERR_NULL_PARAM: return "Null parameter provided";
    case REPLAY_ERR_EMPTY_INPUT: return "Empty input";
    case REPLAY_ERR_TRUNCATED_HEADER: return "Truncated length header (fewer than 4 bytes remaining)";
    case REPLAY_ERR_TRUNCATED_PAYLOAD: return "Payload truncated (buffer ended before declared message length)";
    case REPLAY_ERR_TRAILING_GARBAGE: return "Trailing garbage bytes after complete record parsing";
    case REPLAY_ERR_RECORD_LIMIT_EXCEEDED: return "Record count exceeded configured threshold";
    case REPLAY_ERR_LENGTH_LIMIT_EXCEEDED: return "Single message length exceeded configured limit";
    case REPLAY_ERR_TOTAL_LIMIT_EXCEEDED: return "Total payload bytes exceeded configured limit";
    case REPLAY_ERR_TARGET_INT_OVERFLOW: return "Length exceeds target signed int (INT_MAX)";
    case REPLAY_ERR_IO: return "I/O operation failed";
    case REPLAY_ERR_ALLOC: return "Memory allocation failed";
    default: return "Unknown error";
  }
}

static uint32_t read_u32(const uint8_t *p, bool little_endian) {
  if (little_endian) {
    return ((uint32_t)p[0]) |
           (((uint32_t)p[1]) << 8) |
           (((uint32_t)p[2]) << 16) |
           (((uint32_t)p[3]) << 24);
  } else {
    return (((uint32_t)p[0]) << 24) |
           (((uint32_t)p[1]) << 16) |
           (((uint32_t)p[2]) << 8) |
           ((uint32_t)p[3]);
  }
}

static void write_u32(uint8_t *p, uint32_t v, bool little_endian) {
  if (little_endian) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
  } else {
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
  }
}

static void set_diag(replay_diagnostic_t *diag, replay_status_t code, size_t rec_idx, size_t offset, const char *msg) {
  if (!diag) return;
  diag->code = code;
  diag->record_index = rec_idx;
  diag->byte_offset = offset;
  if (msg) snprintf(diag->detail, sizeof(diag->detail), "%s", msg);
  else snprintf(diag->detail, sizeof(diag->detail), "%s", replay_status_str(code));
}

replay_status_t replay_decode_buffer(const uint8_t *buf, size_t buf_size,
                                    const replay_codec_limits_t *limits,
                                    replay_container_t *out_container,
                                    replay_diagnostic_t *diag) {
  if (!buf || !out_container) {
    set_diag(diag, REPLAY_ERR_NULL_PARAM, 0, 0, NULL);
    return REPLAY_ERR_NULL_PARAM;
  }

  replay_codec_limits_t lim;
  if (limits) lim = *limits;
  else replay_limits_default(&lim);

  if (buf_size == 0) {
    set_diag(diag, REPLAY_ERR_EMPTY_INPUT, 0, 0, "Input buffer is 0 bytes");
    return REPLAY_ERR_EMPTY_INPUT;
  }

  replay_container_t temp;
  replay_container_init(&temp);

  size_t offset = 0;
  size_t rec_idx = 0;

  while (offset < buf_size) {
    if (buf_size - offset < 4) {
      replay_container_free(&temp);
      set_diag(diag, REPLAY_ERR_TRUNCATED_HEADER, rec_idx, offset, "Fewer than 4 bytes left for length header");
      return REPLAY_ERR_TRUNCATED_HEADER;
    }

    uint32_t msg_len = read_u32(buf + offset, lim.little_endian);
    offset += 4;

    if (lim.enforce_int_limits && msg_len > (uint32_t)INT_MAX) {
      replay_container_free(&temp);
      set_diag(diag, REPLAY_ERR_TARGET_INT_OVERFLOW, rec_idx, offset - 4, "Length exceeds target signed int limit (INT_MAX)");
      return REPLAY_ERR_TARGET_INT_OVERFLOW;
    }

    if (msg_len > lim.max_single_len) {
      replay_container_free(&temp);
      set_diag(diag, REPLAY_ERR_LENGTH_LIMIT_EXCEEDED, rec_idx, offset - 4, "Single message length exceeds configured max limit");
      return REPLAY_ERR_LENGTH_LIMIT_EXCEEDED;
    }

    if (rec_idx + 1 > lim.max_records) {
      replay_container_free(&temp);
      set_diag(diag, REPLAY_ERR_RECORD_LIMIT_EXCEEDED, rec_idx, offset - 4, "Record count exceeds max limit");
      return REPLAY_ERR_RECORD_LIMIT_EXCEEDED;
    }

    if (temp.total_payload_bytes + msg_len > lim.max_total_len) {
      replay_container_free(&temp);
      set_diag(diag, REPLAY_ERR_TOTAL_LIMIT_EXCEEDED, rec_idx, offset - 4, "Total payload bytes exceed max limit");
      return REPLAY_ERR_TOTAL_LIMIT_EXCEEDED;
    }

    if (msg_len > buf_size - offset) {
      replay_container_free(&temp);
      set_diag(diag, REPLAY_ERR_TRUNCATED_PAYLOAD, rec_idx, offset, "Payload truncated before declared message length");
      return REPLAY_ERR_TRUNCATED_PAYLOAD;
    }

    replay_status_t rc = replay_container_append(&temp, buf + offset, msg_len);
    if (rc != REPLAY_OK) {
      replay_container_free(&temp);
      set_diag(diag, rc, rec_idx, offset, "Failed to allocate memory for container message");
      return rc;
    }

    offset += msg_len;
    rec_idx++;
  }

  if (offset != buf_size) {
    replay_container_free(&temp);
    set_diag(diag, REPLAY_ERR_TRAILING_GARBAGE, rec_idx, offset, "Unconsumed trailing bytes in buffer");
    return REPLAY_ERR_TRAILING_GARBAGE;
  }

  *out_container = temp;
  set_diag(diag, REPLAY_OK, rec_idx, offset, "Decoded successfully");
  return REPLAY_OK;
}

replay_status_t replay_encode_buffer(const replay_container_t *container,
                                    const replay_codec_limits_t *limits,
                                    uint8_t **out_buf, size_t *out_size,
                                    replay_diagnostic_t *diag) {
  if (!container || !out_buf || !out_size) {
    set_diag(diag, REPLAY_ERR_NULL_PARAM, 0, 0, NULL);
    return REPLAY_ERR_NULL_PARAM;
  }

  replay_codec_limits_t lim;
  if (limits) lim = *limits;
  else replay_limits_default(&lim);

  if (container->count > lim.max_records) {
    set_diag(diag, REPLAY_ERR_RECORD_LIMIT_EXCEEDED, container->count, 0, "Record count exceeds limit");
    return REPLAY_ERR_RECORD_LIMIT_EXCEEDED;
  }

  size_t total_wire_bytes = 0;
  for (size_t i = 0; i < container->count; i++) {
    size_t mlen = container->messages[i].len;
    if (mlen > lim.max_single_len) {
      set_diag(diag, REPLAY_ERR_LENGTH_LIMIT_EXCEEDED, i, 0, "Message length exceeds single limit");
      return REPLAY_ERR_LENGTH_LIMIT_EXCEEDED;
    }
    if (lim.enforce_int_limits && mlen > (size_t)INT_MAX) {
      set_diag(diag, REPLAY_ERR_TARGET_INT_OVERFLOW, i, 0, "Message length exceeds target signed int");
      return REPLAY_ERR_TARGET_INT_OVERFLOW;
    }
    total_wire_bytes += 4 + mlen;
  }

  if (container->total_payload_bytes > lim.max_total_len || total_wire_bytes > lim.max_total_len + 4 * container->count) {
    set_diag(diag, REPLAY_ERR_TOTAL_LIMIT_EXCEEDED, 0, 0, "Total payload bytes exceed limit");
    return REPLAY_ERR_TOTAL_LIMIT_EXCEEDED;
  }

  uint8_t *wire = (uint8_t *)malloc(total_wire_bytes ? total_wire_bytes : 1);
  if (!wire) {
    set_diag(diag, REPLAY_ERR_ALLOC, 0, 0, "Failed to allocate wire buffer");
    return REPLAY_ERR_ALLOC;
  }

  size_t offset = 0;
  for (size_t i = 0; i < container->count; i++) {
    size_t mlen = container->messages[i].len;
    write_u32(wire + offset, (uint32_t)mlen, lim.little_endian);
    offset += 4;
    if (mlen > 0) {
      memcpy(wire + offset, container->messages[i].data, mlen);
      offset += mlen;
    }
  }

  *out_buf = wire;
  *out_size = total_wire_bytes;
  set_diag(diag, REPLAY_OK, container->count, total_wire_bytes, "Encoded successfully");
  return REPLAY_OK;
}

replay_status_t replay_decode_file(const char *path,
                                  const replay_codec_limits_t *limits,
                                  replay_container_t *out_container,
                                  replay_diagnostic_t *diag) {
  if (!path || !out_container) {
    set_diag(diag, REPLAY_ERR_NULL_PARAM, 0, 0, NULL);
    return REPLAY_ERR_NULL_PARAM;
  }

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    set_diag(diag, REPLAY_ERR_IO, 0, 0, "Failed to open input file");
    return REPLAY_ERR_IO;
  }

  fseek(fp, 0, SEEK_END);
  long fsize_signed = ftell(fp);
  if (fsize_signed < 0) {
    fclose(fp);
    set_diag(diag, REPLAY_ERR_IO, 0, 0, "Failed to determine file size");
    return REPLAY_ERR_IO;
  }
  size_t fsize = (size_t)fsize_signed;
  fseek(fp, 0, SEEK_SET);

  if (fsize == 0) {
    fclose(fp);
    set_diag(diag, REPLAY_ERR_EMPTY_INPUT, 0, 0, "File is empty");
    return REPLAY_ERR_EMPTY_INPUT;
  }

  uint8_t *buf = (uint8_t *)malloc(fsize);
  if (!buf) {
    fclose(fp);
    set_diag(diag, REPLAY_ERR_ALLOC, 0, 0, "Out of memory reading file");
    return REPLAY_ERR_ALLOC;
  }

  size_t r = fread(buf, 1, fsize, fp);
  fclose(fp);
  if (r != fsize) {
    free(buf);
    set_diag(diag, REPLAY_ERR_IO, 0, r, "Short read encountered reading file");
    return REPLAY_ERR_IO;
  }

  replay_status_t status = replay_decode_buffer(buf, fsize, limits, out_container, diag);
  free(buf);
  return status;
}

replay_status_t replay_encode_file(const char *path,
                                  const replay_container_t *container,
                                  const replay_codec_limits_t *limits,
                                  replay_diagnostic_t *diag) {
  if (!path || !container) {
    set_diag(diag, REPLAY_ERR_NULL_PARAM, 0, 0, NULL);
    return REPLAY_ERR_NULL_PARAM;
  }

  uint8_t *wire = NULL;
  size_t wire_size = 0;
  replay_status_t st = replay_encode_buffer(container, limits, &wire, &wire_size, diag);
  if (st != REPLAY_OK) return st;

  /* Use "wb" mode to truncate any existing file and eliminate stale tail bytes */
  FILE *fp = fopen(path, "wb");
  if (!fp) {
    free(wire);
    set_diag(diag, REPLAY_ERR_IO, 0, 0, "Failed to open destination file for writing");
    return REPLAY_ERR_IO;
  }

  if (wire_size > 0) {
    size_t w = fwrite(wire, 1, wire_size, fp);
    if (w != wire_size) {
      fclose(fp);
      free(wire);
      set_diag(diag, REPLAY_ERR_IO, 0, w, "Short write occurred while saving file");
      return REPLAY_ERR_IO;
    }
  }

  if (fclose(fp) != 0) {
    free(wire);
    set_diag(diag, REPLAY_ERR_IO, 0, 0, "Failed to close and flush file handle");
    return REPLAY_ERR_IO;
  }

  free(wire);
  set_diag(diag, REPLAY_OK, container->count, wire_size, "Saved file successfully");
  return REPLAY_OK;
}
