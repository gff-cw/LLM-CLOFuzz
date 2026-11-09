#define _GNU_SOURCE
#include "replay_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;
static char g_test_dir[] = "./.test-replay-codec-XXXXXX";

static void test_path(char *path, size_t size, const char *name) {
  int n = snprintf(path, size, "%s/%s", g_test_dir, name);
  if (n < 0 || (size_t)n >= size) exit(EXIT_FAILURE);
}

static void cleanup_test_files(void) {
  const char *names[] = {"rewrite.bin", "io.bin", "limits.bin"};
  char path[256];
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    test_path(path, sizeof(path), names[i]);
    unlink(path);
  }
  rmdir(g_test_dir);
}

/* Only this test binary wraps fopen. The codec still executes libc fread,
   fwrite and fclose; faulting FILE streams make those operations report EIO. */
enum io_fault { IO_NORMAL, IO_READ_ERROR, IO_WRITE_ERROR, IO_FLUSH_ERROR };
static enum io_fault g_io_fault;
static unsigned g_fault_reads, g_fault_writes, g_fault_closes;
static off64_t g_fault_position;

static ssize_t fault_read(void *cookie, char *buf, size_t size) {
  (void)cookie; (void)buf; (void)size;
  g_fault_reads++;
  errno = EIO;
  return -1;
}

static ssize_t fault_write(void *cookie, const char *buf, size_t size) {
  (void)cookie; (void)buf; (void)size;
  g_fault_writes++;
  errno = EIO;
  return -1;
}

static int fault_seek(void *cookie, off64_t *offset, int whence) {
  (void)cookie;
  if (whence == SEEK_SET) g_fault_position = *offset;
  else if (whence == SEEK_CUR) g_fault_position += *offset;
  else if (whence == SEEK_END) g_fault_position = 8 + *offset;
  else { errno = EINVAL; return -1; }
  *offset = g_fault_position;
  return 0;
}

static int fault_close(void *cookie) {
  (void)cookie;
  g_fault_closes++;
  return 0;
}

FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode) {
  if (g_io_fault == IO_NORMAL) return __real_fopen(path, mode);
  cookie_io_functions_t ops = {
    .read = fault_read, .write = fault_write,
    .seek = fault_seek, .close = fault_close
  };
  g_fault_position = 0;
  FILE *fp = fopencookie(NULL, mode, ops);
  if (fp && g_io_fault == IO_WRITE_ERROR) setvbuf(fp, NULL, _IONBF, 0);
  return fp;
}

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
  TEST_ASSERT(st != REPLAY_OK, "Negative-control intentional failure");
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

/* T11: Single-record limits, pre-allocation file limits and size arithmetic */
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
  wire = NULL;

  char path[256];
  test_path(path, sizeof(path), "limits.bin");
  lim.max_records = 1;
  lim.max_total_len = 10;
  st = replay_encode_file(path, &in, &lim, &diag);
  TEST_ASSERT(st == REPLAY_OK, "File at the wire-size limit must encode");
  st = replay_decode_file(path, &lim, &out, &diag);
  TEST_ASSERT(st == REPLAY_OK, "File at the wire-size limit must decode");
  replay_container_free(&out);

  FILE *fp = fopen(path, "r+b");
  TEST_ASSERT(fp != NULL, "Open size-limit fixture");
  int resized = ftruncate(fileno(fp), 15); /* 10 payload bytes + 4 header bytes + 1 */
  int closed = fclose(fp);
  TEST_ASSERT(resized == 0 && closed == 0, "Extend file past its wire-size limit");
  st = replay_decode_file(path, &lim, &out, &diag);
  TEST_ASSERT(st == REPLAY_ERR_TOTAL_LIMIT_EXCEEDED, "Oversized file must fail before allocation");
  TEST_ASSERT(strstr(diag.detail, "before allocation") != NULL, "File limit diagnostic must identify the allocation guard");

  if (sizeof(off_t) > sizeof(uint32_t)) {
    fp = fopen(path, "r+b");
    TEST_ASSERT(fp != NULL, "Open sparse large-file fixture");
    resized = ftruncate(fileno(fp), (off_t)UINT32_MAX + 2);
    closed = fclose(fp);
    TEST_ASSERT(resized == 0 && closed == 0, "Create sparse file beyond u32 size");
    st = replay_decode_file(path, NULL, &out, &diag);
    TEST_ASSERT(st == REPLAY_ERR_TOTAL_LIMIT_EXCEEDED, "Default limits must reject large files before allocation");
  }
  unlink(path);

  /* Encoding must use actual record lengths, not trust a stale total field. */
  in.total_payload_bytes = 0;
  lim.max_total_len = 9;
  st = replay_encode_buffer(&in, &lim, &wire, &wire_size, &diag);
  TEST_ASSERT(st == REPLAY_ERR_TOTAL_LIMIT_EXCEEDED, "Stale totals must not bypass payload limits");
  in.total_payload_bytes = 10;
  lim.max_records = SIZE_MAX;
  lim.max_total_len = SIZE_MAX;
  st = replay_encode_buffer(&in, &lim, &wire, &wire_size, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Large configured limits must not overflow limit arithmetic");
  free(wire);
  st = replay_encode_file(path, &in, &lim, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Write file with large configured limits");
  st = replay_decode_file(path, &lim, &out, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Large file-limit sums must not overflow");
  unlink(path);

  if (SIZE_MAX > UINT32_MAX) {
    replay_message_t oversized = {.data = (uint8_t *)"X", .len = (size_t)UINT32_MAX + 1};
    replay_container_t synthetic = {.messages = &oversized, .count = 1};
    lim.max_single_len = SIZE_MAX;
    lim.enforce_int_limits = false;
    wire = NULL;
    st = replay_encode_buffer(&synthetic, &lim, &wire, &wire_size, &diag);
    TEST_ASSERT(st == REPLAY_ERR_TARGET_INT_OVERFLOW && wire == NULL, "u32 wire lengths must not truncate when signed-int checks are disabled");
  }
  replay_container_t saturated = {.total_payload_bytes = SIZE_MAX};
  TEST_ASSERT(replay_container_append(&saturated, (const uint8_t *)"X", 1) == REPLAY_ERR_TOTAL_LIMIT_EXCEEDED,
              "Appending must reject payload-total overflow before allocation");
  saturated.total_payload_bytes = 0;
  saturated.count = saturated.capacity = SIZE_MAX / 2 + 1;
  TEST_ASSERT(replay_container_append(&saturated, NULL, 0) == REPLAY_ERR_ALLOC,
              "Growing the message array must reject capacity overflow");
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
  char tmp_file[256];
  test_path(tmp_file, sizeof(tmp_file), "rewrite.bin");

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

/* T15: Missing file plus actual stdio read, write and flush errors */
static bool test_t15(void) {
  g_tests_run++;
  char path[256];
  test_path(path, sizeof(path), "missing.bin");
  replay_container_t out, in;
  replay_container_init(&out);
  replay_container_init(&in);
  replay_diagnostic_t diag;
  replay_status_t st = replay_decode_file(path, NULL, &out, &diag);
  TEST_ASSERT(st == REPLAY_ERR_IO && diag.code == REPLAY_ERR_IO, "Missing input must report an open error");
  TEST_ASSERT(strstr(diag.detail, "open input") != NULL, "Missing file must reach fopen");

  test_path(path, sizeof(path), "io.bin");
  TEST_ASSERT(replay_container_append(&in, (const uint8_t *)"HELLO", 5) == REPLAY_OK, "Build I/O fixture");
  TEST_ASSERT(replay_encode_file(path, &in, NULL, &diag) == REPLAY_OK, "Write real I/O fixture");

  g_io_fault = IO_READ_ERROR;
  st = replay_decode_file(path, NULL, &out, &diag);
  g_io_fault = IO_NORMAL;
  TEST_ASSERT(st == REPLAY_ERR_IO && g_fault_reads > 0 && g_fault_closes == 1, "Read EIO must reach fread and close the stream");
  TEST_ASSERT(out.count == 0, "Failed read must not publish a container");

  g_fault_writes = g_fault_closes = 0;
  g_io_fault = IO_WRITE_ERROR;
  st = replay_encode_file(path, &in, NULL, &diag);
  g_io_fault = IO_NORMAL;
  TEST_ASSERT(st == REPLAY_ERR_IO && g_fault_writes > 0 && g_fault_closes == 1, "Write EIO must reach fwrite and close the stream");
  TEST_ASSERT(strstr(diag.detail, "Short write") != NULL, "Write test must exercise the fwrite failure branch");

  g_fault_writes = g_fault_closes = 0;
  g_io_fault = IO_FLUSH_ERROR;
  st = replay_encode_file(path, &in, NULL, &diag);
  g_io_fault = IO_NORMAL;
  TEST_ASSERT(st == REPLAY_ERR_IO && g_fault_writes > 0 && g_fault_closes == 1, "Buffered write EIO must be reported at fclose");
  TEST_ASSERT(strstr(diag.detail, "close and flush") != NULL, "Flush test must exercise the fclose failure branch");
  printf("[+] T15 exercised missing-file, fread EIO, fwrite EIO and fclose/flush EIO paths\n");

  replay_container_free(&in);
  replay_container_free(&out);
  unlink(path);

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

/* T17: All-zero-length replay container (wire size > 0, total payload == 0) */
static bool test_t17(void) {
  g_tests_run++;
  replay_container_t in, out;
  replay_container_init(&in);
  replay_container_init(&out);

  replay_container_append(&in, NULL, 0);
  replay_container_append(&in, NULL, 0);
  replay_container_append(&in, NULL, 0);

  uint8_t *wire = NULL;
  size_t wire_size = 0;
  replay_diagnostic_t diag;
  replay_status_t st = replay_encode_buffer(&in, NULL, &wire, &wire_size, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Encode all-zero container failed");
  TEST_ASSERT(wire_size == 3 * sizeof(uint32_t), "Wire size must be 12 bytes");
  TEST_ASSERT(in.total_payload_bytes == 0, "Total payload bytes must be 0");

  st = replay_decode_buffer(wire, wire_size, NULL, &out, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Decode all-zero container failed");
  TEST_ASSERT(out.count == 3, "Decoded count must be 3");
  TEST_ASSERT(out.total_payload_bytes == 0, "Decoded total payload must be 0");
  TEST_ASSERT(out.messages[0].len == 0 && out.messages[1].len == 0 && out.messages[2].len == 0, "All lengths must be 0");

  free(wire);
  replay_container_free(&in);
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

/* T18: Leading, middle, and trailing zero-length records mixed with payloads */
static bool test_t18(void) {
  g_tests_run++;
  replay_container_t in, out;
  replay_container_init(&in);
  replay_container_init(&out);

  replay_container_append(&in, NULL, 0);
  replay_container_append(&in, (const uint8_t *)"HELLO", 5);
  replay_container_append(&in, NULL, 0);
  replay_container_append(&in, (const uint8_t *)"WORLD", 5);
  replay_container_append(&in, NULL, 0);

  uint8_t *wire = NULL;
  size_t wire_size = 0;
  replay_diagnostic_t diag;
  replay_status_t st = replay_encode_buffer(&in, NULL, &wire, &wire_size, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Encode mixed container failed");
  TEST_ASSERT(wire_size == 5 * sizeof(uint32_t) + 10, "Wire size must be 30 bytes");
  TEST_ASSERT(in.total_payload_bytes == 10, "Total payload bytes must be 10");

  st = replay_decode_buffer(wire, wire_size, NULL, &out, &diag);
  TEST_ASSERT(st == REPLAY_OK, "Decode mixed container failed");
  TEST_ASSERT(out.count == 5, "Decoded count must be 5");
  TEST_ASSERT(out.messages[0].len == 0, "Record 0 must be len 0");
  TEST_ASSERT(out.messages[1].len == 5 && memcmp(out.messages[1].data, "HELLO", 5) == 0, "Record 1 mismatch");
  TEST_ASSERT(out.messages[2].len == 0, "Record 2 must be len 0");
  TEST_ASSERT(out.messages[3].len == 5 && memcmp(out.messages[3].data, "WORLD", 5) == 0, "Record 3 mismatch");
  TEST_ASSERT(out.messages[4].len == 0, "Record 4 must be len 0");

  free(wire);
  replay_container_free(&in);
  replay_container_free(&out);
  g_tests_passed++;
  return true;
}

int main(void) {
  if (!mkdtemp(g_test_dir)) { perror("mkdtemp"); return EXIT_FAILURE; }
  if (atexit(cleanup_test_files) != 0) { cleanup_test_files(); return EXIT_FAILURE; }
  printf("[*] Running independent Replay Codec Tests (T01 - T18, 18 cases)...\n");
  if (!test_t01()) return EXIT_FAILURE;
  if (!test_t02()) return EXIT_FAILURE;
  if (!test_t03()) return EXIT_FAILURE;
  if (!test_t04()) return EXIT_FAILURE;
  if (!test_t05()) return EXIT_FAILURE;
  if (!test_t06()) return EXIT_FAILURE;
  if (!test_t07()) return EXIT_FAILURE;
  if (!test_t08()) return EXIT_FAILURE;
  if (!test_t09()) return EXIT_FAILURE;
  if (!test_t10()) return EXIT_FAILURE;
  if (!test_t11()) return EXIT_FAILURE;
  if (!test_t12()) return EXIT_FAILURE;
  if (!test_t13()) return EXIT_FAILURE;
  if (!test_t14()) return EXIT_FAILURE;
  if (!test_t15()) return EXIT_FAILURE;
  if (!test_t16()) return EXIT_FAILURE;
  if (!test_t17()) return EXIT_FAILURE;
  if (!test_t18()) return EXIT_FAILURE;
  if (g_tests_run != 18 || g_tests_passed != 18) return EXIT_FAILURE;
  printf("[+] ALL %d / %d REPLAY CODEC TESTS PASSED!\n", g_tests_passed, g_tests_run);
  return 0;
}
