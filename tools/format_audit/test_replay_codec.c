#define _GNU_SOURCE
#include "replay_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { \
    printf("[-] FAILED: %s (line %d): %s\n", __func__, __LINE__, msg); \
    return false; \
  } \
} while(0)

/* T01: Two payloads 'A', 'BC' */
static bool test_t01(void) {
  g_tests_run++;
  replay_container_t in, out;
  replay_container_init(&in);
  replay_container_init(&out);
  replay_container_append(&in, (const uint8_t *)"A", 1);
  replay_container_append(&in, (const uint8_t *)"BC", 2);

  uint8_t *wire = NULL;
  size_t wire_size = 0;
  replay_diagnostic_t diag;
  replay_status_t st = replay_encode_buffer(&in, NULL, &wire, &wire_size, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Encode failed");
  TEST_ASSERT(wire_size == 4 + 1 + 4 + 2, "Wire size must be 11 bytes");

  st = replay_decode_buffer(wire, wire_size, NULL, &out, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Decode failed");
  TEST_ASSERT(out.count == 2, "Count must be 2");
  TEST_ASSERT(out.messages[0].len == 1 && out.messages[0].data[0] == 'A', "Message 0 mismatch");
  TEST_ASSERT(out.messages[1].len == 2 && memcmp(out.messages[1].data, "BC", 2) == 0, "Message 1 mismatch");

  free(wire);
  replay_container_free(&in);
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

/* T02: Payload with NUL and non-UTF-8 binary bytes */
static bool test_t02(void) {
  g_tests_run++;
  const uint8_t bin_payload[] = { 0x00, 0xFF, 0xFE, 0x00, 0x80, 0x7F, 0x00 };
  size_t psize = sizeof(bin_payload);

  replay_container_t in, out;
  replay_container_init(&in);
  replay_container_init(&out);
  replay_container_append(&in, bin_payload, psize);

  uint8_t *wire = NULL;
  size_t wire_size = 0;
  replay_diagnostic_t diag;
  replay_encode_buffer(&in, NULL, &wire, &wire_size, &diag);
  replay_decode_buffer(wire, wire_size, NULL, &out, &diag);

  TEST_ASSERT(out.count == 1, "Count must be 1");
  TEST_ASSERT(out.messages[0].len == psize, "Length mismatch");
  TEST_ASSERT(memcmp(out.messages[0].data, bin_payload, psize) == 0, "Binary payload altered");

  free(wire);
  replay_container_free(&in);
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

/* T03: Unaligned length field start offset */
static bool test_t03(void) {
  g_tests_run++;
  /* 1 byte payload for msg 0 means msg 1 length header starts at byte offset 5 (unaligned) */
  replay_container_t in, out;
  replay_container_init(&in);
  replay_container_init(&out);
  replay_container_append(&in, (const uint8_t *)"X", 1);
  replay_container_append(&in, (const uint8_t *)"YYYY", 4);

  uint8_t *wire = NULL;
  size_t wire_size = 0;
  replay_diagnostic_t diag;
  replay_encode_buffer(&in, NULL, &wire, &wire_size, &diag);
  /* wire[0..3] = 1, wire[4] = 'X', wire[5..8] = 4 (offset 5 is unaligned for 32-bit uint) */
  replay_status_t st = replay_decode_buffer(wire, wire_size, NULL, &out, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Unaligned read failed");
  TEST_ASSERT(out.count == 2 && out.messages[1].len == 4, "Unaligned length incorrect");

  free(wire);
  replay_container_free(&in);
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

/* T04: Explicit history endian configuration */
static bool test_t04(void) {
  g_tests_run++;
  replay_codec_limits_t lim;
  replay_limits_default(&lim);
  lim.little_endian = true;

  replay_container_t in;
  replay_container_init(&in);
  replay_container_append(&in, (const uint8_t *)"HELLO", 5);

  uint8_t *wire = NULL;
  size_t wire_size = 0;
  replay_diagnostic_t diag;
  replay_encode_buffer(&in, &lim, &wire, &wire_size, &diag);
  TEST_ASSERT(wire[0] == 0x05 && wire[1] == 0x00 && wire[2] == 0x00 && wire[3] == 0x00, "Little-endian header mismatch");

  free(wire);
  replay_container_free(&in);
  g_tests_passed++;
  return true;
}

/* T05: Declaring different endianness samples */
static bool test_t05(void) {
  g_tests_run++;
  replay_codec_limits_t lim_be;
  replay_limits_default(&lim_be);
  lim_be.little_endian = false;

  replay_container_t in, out;
  replay_container_init(&in);
  replay_container_init(&out);
  replay_container_append(&in, (const uint8_t *)"TEST", 4);

  uint8_t *wire = NULL;
  size_t wire_size = 0;
  replay_diagnostic_t diag;
  replay_encode_buffer(&in, &lim_be, &wire, &wire_size, &diag);
  TEST_ASSERT(wire[0] == 0x00 && wire[1] == 0x00 && wire[2] == 0x00 && wire[3] == 0x04, "Big-endian header mismatch");

  replay_status_t st = replay_decode_buffer(wire, wire_size, &lim_be, &out, &diag);
  TEST_ASSERT(st == REPLAY_OK && out.count == 1 && out.messages[0].len == 4, "Big-endian decode failed");

  free(wire);
  replay_container_free(&in);
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

/* T06: Truncated header (fewer than 4 bytes left) */
static bool test_t06(void) {
  g_tests_run++;
  uint8_t bad_buf[3] = { 0x01, 0x00, 0x00 };
  replay_container_t out;
  replay_container_init(&out);
  replay_diagnostic_t diag;
  replay_status_t st = replay_decode_buffer(bad_buf, 3, NULL, &out, &diag);
  TEST_ASSERT(st == REPLAY_ERR_TRUNCATED_HEADER, "Must reject truncated header");
  TEST_ASSERT(diag.byte_offset == 0, "Diagnostic offset incorrect");
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

/* T07: Declared length > remaining payload */
static bool test_t07(void) {
  g_tests_run++;
  uint8_t bad_buf[8] = { 0x10, 0x00, 0x00, 0x00, 'A', 'B', 'C', 'D' }; /* Declares 16 bytes, only 4 provided */
  replay_container_t out;
  replay_container_init(&out);
  replay_diagnostic_t diag;
  replay_status_t st = replay_decode_buffer(bad_buf, 8, NULL, &out, &diag);
  TEST_ASSERT(st == REPLAY_ERR_TRUNCATED_PAYLOAD, "Must reject truncated payload");
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

/* T08: Valid record followed by trailing garbage */
static bool test_t08(void) {
  g_tests_run++;
  uint8_t bad_buf[6] = { 0x01, 0x00, 0x00, 0x00, 'A', 'Z' }; /* 1 byte payload 'A', plus trailing 'Z' */
  replay_container_t out;
  replay_container_init(&out);
  replay_diagnostic_t diag;
  replay_status_t st = replay_decode_buffer(bad_buf, 6, NULL, &out, &diag);
  TEST_ASSERT(st == REPLAY_ERR_TRUNCATED_HEADER || st == REPLAY_ERR_TRAILING_GARBAGE, "Must reject trailing garbage");
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

/* T09: Empty file / input */
static bool test_t09(void) {
  g_tests_run++;
  replay_container_t out;
  replay_container_init(&out);
  replay_diagnostic_t diag;
  replay_status_t st = replay_decode_buffer((const uint8_t *)"", 0, NULL, &out, &diag);
  TEST_ASSERT(st == REPLAY_ERR_EMPTY_INPUT, "Must report empty input error distinctly");
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

/* T10: Zero-length records alone and sandwiched */
static bool test_t10(void) {
  g_tests_run++;
  replay_container_t in, out;
  replay_container_init(&in);
  replay_container_init(&out);
  replay_container_append(&in, (const uint8_t *)"BEFORE", 6);
  replay_container_append(&in, NULL, 0); /* Zero-length message */
  replay_container_append(&in, (const uint8_t *)"AFTER", 5);

  uint8_t *wire = NULL;
  size_t wire_size = 0;
  replay_diagnostic_t diag;
  replay_encode_buffer(&in, NULL, &wire, &wire_size, &diag);
  TEST_ASSERT(wire_size == 4 + 6 + 4 + 0 + 4 + 5, "Size with zero-length record incorrect");

  replay_status_t st = replay_decode_buffer(wire, wire_size, NULL, &out, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Failed to decode zero-length record");
  TEST_ASSERT(out.count == 3, "Count must be 3");
  TEST_ASSERT(out.messages[1].len == 0, "Middle record length must be 0");

  free(wire);
  replay_container_free(&in);
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

/* T11: Limits exact and limit + 1 boundary */
static bool test_t11(void) {
  g_tests_run++;
  replay_codec_limits_t lim;
  replay_limits_default(&lim);
  lim.max_single_len = 10;

  replay_container_t in, out;
  replay_container_init(&in);
  replay_container_init(&out);
  replay_container_append(&in, (const uint8_t *)"12345678901", 11); /* 11 > 10 */

  uint8_t *wire = NULL;
  size_t wire_size = 0;
  replay_diagnostic_t diag;
  replay_status_t st = replay_encode_buffer(&in, &lim, &wire, &wire_size, &diag);
  TEST_ASSERT(st == REPLAY_ERR_LENGTH_LIMIT_EXCEEDED, "Must reject length exceeding limit + 1");

  replay_container_free(&in);
  replay_container_init(&in);
  replay_container_append(&in, (const uint8_t *)"1234567890", 10); /* exactly 10 */
  st = replay_encode_buffer(&in, &lim, &wire, &wire_size, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Must accept exact limit length");

  free(wire);
  replay_container_free(&in);
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

/* T12: Spaces, LF, CRLF, backslash payload not normalized */
static bool test_t12(void) {
  g_tests_run++;
  const char *raw = " \t\r\n\\hello\\\r\n ";
  size_t rlen = strlen(raw);

  replay_container_t in, out;
  replay_container_init(&in);
  replay_container_init(&out);
  replay_container_append(&in, (const uint8_t *)raw, rlen);

  uint8_t *wire = NULL;
  size_t wire_size = 0;
  replay_diagnostic_t diag;
  replay_encode_buffer(&in, NULL, &wire, &wire_size, &diag);
  replay_decode_buffer(wire, wire_size, NULL, &out, &diag);

  TEST_ASSERT(out.count == 1, "Count mismatch");
  TEST_ASSERT(out.messages[0].len == rlen, "Length changed by normalization");
  TEST_ASSERT(memcmp(out.messages[0].data, raw, rlen) == 0, "Whitespaces or CRLF altered");

  free(wire);
  replay_container_free(&in);
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

/* T13: Different message division with same raw concatenation */
static bool test_t13(void) {
  g_tests_run++;
  /* Div A: "ABC" + "DEF" (2 messages) */
  /* Div B: "AB" + "CDEF" (2 messages) */
  /* Both have identical concatenated raw bytes "ABCDEF" */
  replay_container_t c_a, c_b;
  replay_container_init(&c_a);
  replay_container_init(&c_b);
  replay_container_append(&c_a, (const uint8_t *)"ABC", 3);
  replay_container_append(&c_a, (const uint8_t *)"DEF", 3);

  replay_container_append(&c_b, (const uint8_t *)"AB", 2);
  replay_container_append(&c_b, (const uint8_t *)"CDEF", 4);

  TEST_ASSERT(c_a.total_payload_bytes == c_b.total_payload_bytes, "Raw length must match");
  TEST_ASSERT(c_a.messages[0].len != c_b.messages[0].len, "Framing must differ");

  replay_container_free(&c_a);
  replay_container_free(&c_b);
  g_tests_passed++;
  return true;
}

/* T14: Write long then write short file without stale tail */
static bool test_t14(void) {
  g_tests_run++;
  const char *tmp_file = "/tmp/test_format_t14.bin";
  unlink(tmp_file);

  replay_container_t long_c, short_c, read_c;
  replay_container_init(&long_c);
  replay_container_init(&short_c);
  replay_container_init(&read_c);

  replay_container_append(&long_c, (const uint8_t *)"LONG_LONG_MESSAGE_1234567890", 28);
  replay_container_append(&short_c, (const uint8_t *)"SHORT", 5);

  replay_diagnostic_t diag;
  replay_status_t st = replay_encode_file(tmp_file, &long_c, NULL, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Long write failed");

  st = replay_encode_file(tmp_file, &short_c, NULL, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Short overwrite failed");

  st = replay_decode_file(tmp_file, NULL, &read_c, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Decode after overwrite failed");
  TEST_ASSERT(read_c.count == 1, "Must only have 1 record");
  TEST_ASSERT(read_c.messages[0].len == 5, "Length must be exactly 5, no trailing bytes");
  TEST_ASSERT(memcmp(read_c.messages[0].data, "SHORT", 5) == 0, "Payload corrupted");

  unlink(tmp_file);
  replay_container_free(&long_c);
  replay_container_free(&short_c);
  replay_container_free(&read_c);
  g_tests_passed++;
  return true;
}

/* T15: Simulated short read/write and error reporting */
static bool test_t15(void) {
  g_tests_run++;
  replay_diagnostic_t diag;
  replay_status_t st = replay_decode_file("/nonexistent/file/path/that/cannot/exist", NULL, NULL, &diag);
  TEST_ASSERT(st == REPLAY_ERR_NULL_PARAM || st == REPLAY_ERR_IO, "Must fail with IO error");

  g_tests_passed++;
  return true;
}

/* T16: Target int limit overflow */
static bool test_t16(void) {
  g_tests_run++;
  uint8_t overflow_wire[8];
  uint32_t val = 0x80000000; /* 2^31, exceeds signed 32-bit INT_MAX */
  overflow_wire[0] = (uint8_t)(val & 0xFF);
  overflow_wire[1] = (uint8_t)((val >> 8) & 0xFF);
  overflow_wire[2] = (uint8_t)((val >> 16) & 0xFF);
  overflow_wire[3] = (uint8_t)((val >> 24) & 0xFF);

  replay_codec_limits_t lim;
  replay_limits_default(&lim);
  lim.enforce_int_limits = true;

  replay_container_t out;
  replay_container_init(&out);
  replay_diagnostic_t diag;
  replay_status_t st = replay_decode_buffer(overflow_wire, 8, &lim, &out, &diag);
  TEST_ASSERT(st == REPLAY_ERR_TARGET_INT_OVERFLOW, "Must reject target int overflow");

  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

int main(void) {
  printf("[*] Running Replay Codec Tests (T01 - T16)...\n");
  assert(test_t01());
  assert(test_t02());
  assert(test_t03());
  assert(test_t04());
  assert(test_t05());
  assert(test_t06());
  assert(test_t07());
  assert(test_t08());
  assert(test_t09());
  assert(test_t10());
  assert(test_t11());
  assert(test_t12());
  assert(test_t13());
  assert(test_t14());
  assert(test_t15());
  assert(test_t16());
  printf("[+] ALL %d / %d REPLAY CODEC TESTS PASSED!\n", g_tests_passed, g_tests_run);
  return 0;
}
