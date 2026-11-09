#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#include "region_checker.h"
#include "../../types.h"
#include "../../alloc-inl.h"
#include "../../aflnet.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { \
    printf("[-] FAILED: %s (line %d): %s\n", __func__, __LINE__, msg); \
    return false; \
  } \
} while(0)

/* Helper: verify strict geometric partition using region_checker */
static bool validate_regions_partition(const uint8_t *data, size_t size, const region_t *regions, unsigned int count) {
  if (size == 0) {
    return (count == 0 && regions == NULL);
  }
  if (!regions || count == 0) {
    return false;
  }

  raw_buffer_handle_t buf;
  raw_buffer_init(&buf, 1000 + count, data, size);

  region_set_t set;
  region_set_init(&set);
  for (unsigned int i = 0; i < count; i++) {
    region_status_t st = region_set_append(&set, regions[i].start_byte, regions[i].end_byte);
    if (st != REGION_OK) {
      region_set_free(&set);
      return false;
    }
  }
  region_set_bind(&set, &buf);

  region_diagnostic_t diag;
  region_status_t st = region_validate(&set, &buf, REGION_CHECK_STRICT_PARTITION, &diag);
  if (st != REGION_OK) {
    printf("[-] Geometric partition check failed: %s (code=%d, region %zu: [%d, %d])\n",
           diag.detail, (int)st, diag.region_index, diag.start_byte, diag.end_byte);
    region_set_free(&set);
    return false;
  }

  region_set_free(&set);
  return true;
}

/* ========================================================================= */
/* SUITE 1: IEC 60870-5-104                                                 */
/* ========================================================================= */

/* T01: Single U-format frame (STARTDT act: 68 04 07 00 00 00) */
static bool test_iec104_valid_single_u_frame(void) {
  g_tests_run++;
  uint8_t frame[] = { 0x68, 0x04, 0x07, 0x00, 0x00, 0x00 };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_iec104(frame, sizeof(frame), &rc);
  TEST_ASSERT(rc == 1, "IEC104 U-frame request count must be 1");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 5, "Region bounds mismatch");
  TEST_ASSERT(validate_regions_partition(frame, sizeof(frame), regions, rc), "Strict partition failed");

  unsigned int *states = extract_response_codes_iec104(frame, sizeof(frame), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2 (state 0 + U-frame)");
  TEST_ASSERT(states[0] == 0, "Initial state 0 must be prepended");
  TEST_ASSERT(states[1] == 0x010007, "U-format state code mismatch (0x010007)");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T02: Single S-format frame (Supervisory ACK: 68 04 01 00 02 00) */
static bool test_iec104_valid_single_s_frame(void) {
  g_tests_run++;
  uint8_t frame[] = { 0x68, 0x04, 0x01, 0x00, 0x02, 0x00 };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_iec104(frame, sizeof(frame), &rc);
  TEST_ASSERT(rc == 1, "IEC104 S-frame request count must be 1");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 5, "Region bounds mismatch");
  TEST_ASSERT(validate_regions_partition(frame, sizeof(frame), regions, rc), "Strict partition failed");

  unsigned int *states = extract_response_codes_iec104(frame, sizeof(frame), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0x020001, "S-format supervisory state code mismatch (0x020001)");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T03: Single I-format frame (Single Command C_SC_NA_1, Type 0x2D, COT 0x06) */
static bool test_iec104_valid_single_i_frame(void) {
  g_tests_run++;
  uint8_t frame[] = {
    0x68, 0x0E,                         /* Start 0x68, APDU len 14 (total 16 bytes) */
    0x00, 0x00, 0x00, 0x00,             /* Control fields (I-frame) */
    0x2D,                               /* Type ID 0x2D (45) */
    0x01,                               /* VSQ 1 */
    0x06, 0x00,                         /* COT 6 (activation) */
    0x01, 0x00,                         /* Common address */
    0x01, 0x00, 0x00, 0x01              /* IOA + Value */
  };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_iec104(frame, sizeof(frame), &rc);
  TEST_ASSERT(rc == 1, "IEC104 I-frame request count must be 1");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 15, "Region bounds mismatch");
  TEST_ASSERT(validate_regions_partition(frame, sizeof(frame), regions, rc), "Strict partition failed");

  unsigned int *states = extract_response_codes_iec104(frame, sizeof(frame), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0x032D06, "I-format state code mismatch (0x032D06)");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T04: Multi-frame concatenated stream */
static bool test_iec104_multi_frame_stream(void) {
  g_tests_run++;
  uint8_t stream[] = {
    /* Frame 1: STARTDT act (6 bytes, [0, 5]) */
    0x68, 0x04, 0x07, 0x00, 0x00, 0x00,
    /* Frame 2: STARTDT con (6 bytes, [6, 11]) */
    0x68, 0x04, 0x0B, 0x00, 0x00, 0x00,
    /* Frame 3: S-frame (6 bytes, [12, 17]) */
    0x68, 0x04, 0x01, 0x00, 0x00, 0x00,
    /* Frame 4: Interrogation I-frame (16 bytes, [18, 33]) */
    0x68, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x64, 0x01, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x14
  };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_iec104(stream, sizeof(stream), &rc);
  TEST_ASSERT(rc == 4, "Stream must yield 4 regions");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 5, "Region 0 mismatch");
  TEST_ASSERT(regions[1].start_byte == 6 && regions[1].end_byte == 11, "Region 1 mismatch");
  TEST_ASSERT(regions[2].start_byte == 12 && regions[2].end_byte == 17, "Region 2 mismatch");
  TEST_ASSERT(regions[3].start_byte == 18 && regions[3].end_byte == 33, "Region 3 mismatch");
  TEST_ASSERT(validate_regions_partition(stream, sizeof(stream), regions, rc), "Multi-frame partition failed");

  unsigned int *states = extract_response_codes_iec104(stream, sizeof(stream), &sc);
  TEST_ASSERT(sc == 5, "Must yield 5 states (state 0 + 4 frames)");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0x010007, "State 1 mismatch (STARTDT act)");
  TEST_ASSERT(states[2] == 0x01000B, "State 2 mismatch (STARTDT con)");
  TEST_ASSERT(states[3] == 0x020001, "State 3 mismatch (S-frame)");
  TEST_ASSERT(states[4] == 0x036406, "State 4 mismatch (Interrogation I-frame)");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T05: Incomplete / truncated buffer handling (strict partition with absorbed trailing bytes) */
static bool test_iec104_truncated_buffer(void) {
  g_tests_run++;
  uint8_t buf[] = {
    /* Frame 1: Valid U-frame (6 bytes) */
    0x68, 0x04, 0x07, 0x00, 0x00, 0x00,
    /* Frame 2: Truncated header claiming APDU len 10 (needs 12 bytes, only 4 present) */
    0x68, 0x0A, 0x00, 0x00
  };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_iec104(buf, sizeof(buf), &rc);
  TEST_ASSERT(rc == 2, "Must yield 2 regions (frame 1 + absorbed trailing)");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 5, "Frame 1 bounds mismatch");
  TEST_ASSERT(regions[1].start_byte == 6 && regions[1].end_byte == 9, "Absorbed trailing bounds mismatch");
  TEST_ASSERT(validate_regions_partition(buf, sizeof(buf), regions, rc), "Truncated partition must have no gaps/overlaps");

  unsigned int *states = extract_response_codes_iec104(buf, sizeof(buf), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2 (state 0 + valid frame 1)");
  TEST_ASSERT(states[0] == 0 && states[1] == 0x010007, "States mismatch on truncated buffer");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T06: Corrupt magic byte and malformed length handling */
static bool test_iec104_corrupt_magic_and_length(void) {
  g_tests_run++;
  unsigned int rc = 0, sc = 0;

  /* Corrupt start byte 0x69 instead of 0x68 */
  uint8_t bad_start[] = { 0x69, 0x04, 0x07, 0x00, 0x00, 0x00 };
  region_t *reg1 = extract_requests_iec104(bad_start, sizeof(bad_start), &rc);
  TEST_ASSERT(rc == 1 && reg1[0].start_byte == 0 && reg1[0].end_byte == 5, "Corrupt start byte must be fully absorbed");
  TEST_ASSERT(validate_regions_partition(bad_start, sizeof(bad_start), reg1, rc), "Corrupt magic partition failed");
  unsigned int *st1 = extract_response_codes_iec104(bad_start, sizeof(bad_start), &sc);
  TEST_ASSERT(sc == 1 && st1[0] == 0, "Corrupt start byte must produce only state 0");
  if (reg1) ck_free(reg1);
  if (st1) ck_free(st1);

  /* Malformed APDU length < 4 */
  uint8_t small_len[] = { 0x68, 0x02, 0x00, 0x00 };
  region_t *reg2 = extract_requests_iec104(small_len, sizeof(small_len), &rc);
  TEST_ASSERT(rc == 1 && reg2[0].start_byte == 0 && reg2[0].end_byte == 3, "APDU len < 4 must be absorbed");
  TEST_ASSERT(validate_regions_partition(small_len, sizeof(small_len), reg2, rc), "Partition failed");
  unsigned int *st2 = extract_response_codes_iec104(small_len, sizeof(small_len), &sc);
  TEST_ASSERT(sc == 1 && st2[0] == 0, "Malformed length must produce only state 0");
  if (reg2) ck_free(reg2);
  if (st2) ck_free(st2);

  /* Malformed APDU length > 253 */
  uint8_t huge_len[] = { 0x68, 0xFE, 0x00, 0x00, 0x00, 0x00 };
  region_t *reg3 = extract_requests_iec104(huge_len, sizeof(huge_len), &rc);
  TEST_ASSERT(rc == 1 && reg3[0].start_byte == 0 && reg3[0].end_byte == 5, "APDU len > 253 must be absorbed");
  TEST_ASSERT(validate_regions_partition(huge_len, sizeof(huge_len), reg3, rc), "Partition failed");
  if (reg3) ck_free(reg3);

  /* Valid frame followed by corrupt magic */
  uint8_t mixed[] = { 0x68, 0x04, 0x07, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0xBE, 0xEF };
  region_t *reg4 = extract_requests_iec104(mixed, sizeof(mixed), &rc);
  TEST_ASSERT(rc == 2, "Valid + corrupt must yield 2 regions");
  TEST_ASSERT(reg4[0].start_byte == 0 && reg4[0].end_byte == 5, "Valid frame bounds mismatch");
  TEST_ASSERT(reg4[1].start_byte == 6 && reg4[1].end_byte == 9, "Corrupt trailer bounds mismatch");
  TEST_ASSERT(validate_regions_partition(mixed, sizeof(mixed), reg4, rc), "Partition failed");
  unsigned int *st4 = extract_response_codes_iec104(mixed, sizeof(mixed), &sc);
  TEST_ASSERT(sc == 2 && st4[1] == 0x010007, "Response code on valid frame must be preserved");
  if (reg4) ck_free(reg4);
  if (st4) ck_free(st4);

  g_tests_passed++;
  return true;
}

/* T07: Empty buffer handling */
static bool test_iec104_empty_buffer(void) {
  g_tests_run++;
  unsigned int rc = 99, sc = 99;
  region_t *regions = extract_requests_iec104(NULL, 0, &rc);
  TEST_ASSERT(rc == 0 && regions == NULL, "Empty buffer must yield 0 regions and NULL");

  unsigned int *states = extract_response_codes_iec104(NULL, 0, &sc);
  TEST_ASSERT(sc == 1 && states != NULL && states[0] == 0, "Empty buffer must yield 1 state (state 0)");

  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* ========================================================================= */
/* SUITE 2: IEC 61850 (MMS over TPKT/COTP)                                  */
/* ========================================================================= */

/* T08: Single COTP Connection Request (CR) */
static bool test_iec61850_valid_single_cotp_cr(void) {
  g_tests_run++;
  /* TPKT len=7: 03 00 00 07, COTP LI=2, type=0xE0 (CR), dst ref=0x00 */
  uint8_t frame[] = { 0x03, 0x00, 0x00, 0x07, 0x02, 0xE0, 0x00 };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_iec61850(frame, sizeof(frame), &rc);
  TEST_ASSERT(rc == 1, "Request count must be 1");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 6, "Region bounds mismatch");
  TEST_ASSERT(validate_regions_partition(frame, sizeof(frame), regions, rc), "Partition failed");

  unsigned int *states = extract_response_codes_iec61850(frame, sizeof(frame), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0xE0, "State code must be 0xE0 (COTP CR)");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T09: Single COTP Connection Confirm (CC) */
static bool test_iec61850_valid_single_cotp_cc(void) {
  g_tests_run++;
  /* TPKT len=7: 03 00 00 07, COTP LI=2, type=0xD0 (CC), dst ref=0x01 */
  uint8_t frame[] = { 0x03, 0x00, 0x00, 0x07, 0x02, 0xD0, 0x01 };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_iec61850(frame, sizeof(frame), &rc);
  TEST_ASSERT(rc == 1, "Request count must be 1");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 6, "Region bounds mismatch");
  TEST_ASSERT(validate_regions_partition(frame, sizeof(frame), regions, rc), "Partition failed");

  unsigned int *states = extract_response_codes_iec61850(frame, sizeof(frame), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0xD0, "State code must be 0xD0 (COTP CC)");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T10: Single MMS Confirmed-Request PDU */
static bool test_iec61850_valid_single_mms_confirmed_request(void) {
  g_tests_run++;
  /* TPKT len=18: 03 00 00 12, COTP: 02 F0 80 (DT), MMS: A1 ... A4 (getNameList) */
  uint8_t frame[] = {
    0x03, 0x00, 0x00, 0x12,
    0x02, 0xF0, 0x80,
    0xA1, 0x09, 0x02, 0x01, 0x01, 0xA4, 0x04, 0x80, 0x02, 0x01, 0x00
  };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_iec61850(frame, sizeof(frame), &rc);
  TEST_ASSERT(rc == 1, "Request count must be 1");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 17, "Region bounds mismatch");
  TEST_ASSERT(validate_regions_partition(frame, sizeof(frame), regions, rc), "Partition failed");

  unsigned int *states = extract_response_codes_iec61850(frame, sizeof(frame), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  /* In aflnet.c: tag=0xA1 at offset 7, and byte offset 7+4=11 is frame[11]=0x01 */
  TEST_ASSERT(states[1] == 0xF0A101, "State code must be (0xF0<<16)|(0xA1<<8)|0x01 (0xF0A101)");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T11: Multi-frame concatenated stream */
static bool test_iec61850_multi_frame_stream(void) {
  g_tests_run++;
  uint8_t stream[] = {
    /* Frame 1: COTP CR (7 bytes, [0, 6]) */
    0x03, 0x00, 0x00, 0x07, 0x02, 0xE0, 0x00,
    /* Frame 2: COTP CC (7 bytes, [7, 13]) */
    0x03, 0x00, 0x00, 0x07, 0x02, 0xD0, 0x01,
    /* Frame 3: MMS Confirmed-Request (18 bytes, [14, 31]) */
    0x03, 0x00, 0x00, 0x12, 0x02, 0xF0, 0x80,
    0xA1, 0x09, 0x02, 0x01, 0x01, 0xA4, 0x04, 0x80, 0x02, 0x01, 0x00
  };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_iec61850(stream, sizeof(stream), &rc);
  TEST_ASSERT(rc == 3, "Stream must yield 3 regions");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 6, "Region 0 mismatch");
  TEST_ASSERT(regions[1].start_byte == 7 && regions[1].end_byte == 13, "Region 1 mismatch");
  TEST_ASSERT(regions[2].start_byte == 14 && regions[2].end_byte == 31, "Region 2 mismatch");
  TEST_ASSERT(validate_regions_partition(stream, sizeof(stream), regions, rc), "Partition failed");

  unsigned int *states = extract_response_codes_iec61850(stream, sizeof(stream), &sc);
  TEST_ASSERT(sc == 4, "Must yield 4 states");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0xE0, "State 1 mismatch (CR)");
  TEST_ASSERT(states[2] == 0xD0, "State 2 mismatch (CC)");
  TEST_ASSERT(states[3] == 0xF0A101, "State 3 mismatch (MMS Confirmed-Request)");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T12: Incomplete / truncated buffer handling */
static bool test_iec61850_truncated_buffer(void) {
  g_tests_run++;
  uint8_t buf[] = {
    /* Frame 1: Complete COTP CR (7 bytes, [0, 6]) */
    0x03, 0x00, 0x00, 0x07, 0x02, 0xE0, 0x00,
    /* Frame 2: Truncated frame claiming length 20, but only 6 bytes present */
    0x03, 0x00, 0x00, 0x14, 0x02, 0xF0
  };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_iec61850(buf, sizeof(buf), &rc);
  TEST_ASSERT(rc == 2, "Must yield 2 regions (valid frame + absorbed trailer)");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 6, "Region 0 mismatch");
  TEST_ASSERT(regions[1].start_byte == 7 && regions[1].end_byte == 12, "Region 1 mismatch");
  TEST_ASSERT(validate_regions_partition(buf, sizeof(buf), regions, rc), "Truncated partition must have no gaps/overlaps");

  unsigned int *states = extract_response_codes_iec61850(buf, sizeof(buf), &sc);
  TEST_ASSERT(sc == 2 && states[0] == 0 && states[1] == 0xE0, "States mismatch on truncated buffer");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T13: Corrupt magic byte and malformed length handling */
static bool test_iec61850_corrupt_magic_and_length(void) {
  g_tests_run++;
  unsigned int rc = 0, sc = 0;

  /* Corrupt TPKT version 0x04 instead of 0x03 */
  uint8_t bad_version[] = { 0x04, 0x00, 0x00, 0x07, 0x02, 0xE0, 0x00 };
  region_t *reg1 = extract_requests_iec61850(bad_version, sizeof(bad_version), &rc);
  TEST_ASSERT(rc == 1 && reg1[0].start_byte == 0 && reg1[0].end_byte == 6, "Bad version must be absorbed");
  TEST_ASSERT(validate_regions_partition(bad_version, sizeof(bad_version), reg1, rc), "Partition failed");
  unsigned int *st1 = extract_response_codes_iec61850(bad_version, sizeof(bad_version), &sc);
  TEST_ASSERT(sc == 1 && st1[0] == 0, "Bad version must produce only state 0");
  if (reg1) ck_free(reg1);
  if (st1) ck_free(st1);

  /* Corrupt reserved byte != 0x00 */
  uint8_t bad_reserved[] = { 0x03, 0x01, 0x00, 0x07, 0x02, 0xE0, 0x00 };
  region_t *reg2 = extract_requests_iec61850(bad_reserved, sizeof(bad_reserved), &rc);
  TEST_ASSERT(rc == 1 && reg2[0].start_byte == 0 && reg2[0].end_byte == 6, "Bad reserved byte must be absorbed");
  TEST_ASSERT(validate_regions_partition(bad_reserved, sizeof(bad_reserved), reg2, rc), "Partition failed");
  if (reg2) ck_free(reg2);

  /* Malformed TPKT length < 4 */
  uint8_t small_len[] = { 0x03, 0x00, 0x00, 0x03 };
  region_t *reg3 = extract_requests_iec61850(small_len, sizeof(small_len), &rc);
  TEST_ASSERT(rc == 1 && reg3[0].start_byte == 0 && reg3[0].end_byte == 3, "Length < 4 must be absorbed");
  TEST_ASSERT(validate_regions_partition(small_len, sizeof(small_len), reg3, rc), "Partition failed");
  if (reg3) ck_free(reg3);

  /* Valid frame followed by corrupt magic */
  uint8_t mixed[] = { 0x03, 0x00, 0x00, 0x07, 0x02, 0xE0, 0x00, 0xAA, 0xBB };
  region_t *reg4 = extract_requests_iec61850(mixed, sizeof(mixed), &rc);
  TEST_ASSERT(rc == 2 && reg4[0].start_byte == 0 && reg4[0].end_byte == 6, "Region 0 mismatch");
  TEST_ASSERT(reg4[1].start_byte == 7 && reg4[1].end_byte == 8, "Trailer mismatch");
  TEST_ASSERT(validate_regions_partition(mixed, sizeof(mixed), reg4, rc), "Partition failed");
  if (reg4) ck_free(reg4);

  g_tests_passed++;
  return true;
}

/* T14: Empty buffer handling */
static bool test_iec61850_empty_buffer(void) {
  g_tests_run++;
  unsigned int rc = 99, sc = 99;
  region_t *regions = extract_requests_iec61850(NULL, 0, &rc);
  TEST_ASSERT(rc == 0 && regions == NULL, "Empty buffer must yield 0 regions");

  unsigned int *states = extract_response_codes_iec61850(NULL, 0, &sc);
  TEST_ASSERT(sc == 1 && states != NULL && states[0] == 0, "Empty buffer must yield state 0");

  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* ========================================================================= */
/* SUITE 3: OPC UA Binary                                                   */
/* ========================================================================= */

/* T15: Single HEL message (32 bytes) */
static bool test_opcua_valid_single_hel(void) {
  g_tests_run++;
  uint8_t frame[32] = {
    'H', 'E', 'L', 'F',
    0x20, 0x00, 0x00, 0x00,             /* Size: 32 bytes */
    0                                   /* 24 bytes zeroed payload */
  };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_opcua(frame, sizeof(frame), &rc);
  TEST_ASSERT(rc == 1, "Request count must be 1");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 31, "Region bounds mismatch");
  TEST_ASSERT(validate_regions_partition(frame, sizeof(frame), regions, rc), "Partition failed");

  unsigned int *states = extract_response_codes_opcua(frame, sizeof(frame), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0x48454C46, "State code must be 'HELF' (0x48454C46)");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T16: Single ACK message (28 bytes) */
static bool test_opcua_valid_single_ack(void) {
  g_tests_run++;
  uint8_t frame[28] = {
    'A', 'C', 'K', 'F',
    0x1C, 0x00, 0x00, 0x00,             /* Size: 28 bytes */
    0
  };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_opcua(frame, sizeof(frame), &rc);
  TEST_ASSERT(rc == 1, "Request count must be 1");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 27, "Region bounds mismatch");
  TEST_ASSERT(validate_regions_partition(frame, sizeof(frame), regions, rc), "Partition failed");

  unsigned int *states = extract_response_codes_opcua(frame, sizeof(frame), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0x41434B46, "State code must be 'ACKF' (0x41434B46)");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T17: Single ERR message with StatusCode */
static bool test_opcua_valid_single_err(void) {
  g_tests_run++;
  uint8_t frame[16] = {
    'E', 'R', 'R', 'F',
    0x10, 0x00, 0x00, 0x00,             /* Size: 16 bytes */
    0x00, 0x00, 0x05, 0x80,             /* StatusCode: 0x80050000 (BadTcpEndpointUrlInvalid) */
    0x00, 0x00, 0x00, 0x00              /* Reason length: 0 */
  };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_opcua(frame, sizeof(frame), &rc);
  TEST_ASSERT(rc == 1, "Request count must be 1");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 15, "Region bounds mismatch");
  TEST_ASSERT(validate_regions_partition(frame, sizeof(frame), regions, rc), "Partition failed");

  unsigned int *states = extract_response_codes_opcua(frame, sizeof(frame), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0x80050000, "ERR state code must extract status code 0x80050000");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T18: Multi-frame concatenated stream */
static bool test_opcua_multi_frame_stream(void) {
  g_tests_run++;
  uint8_t stream[76];
  memset(stream, 0, sizeof(stream));

  /* Frame 1: HELF (32 bytes, [0, 31]) */
  memcpy(&stream[0], "HELF", 4);
  uint32_t len1 = 32;
  memcpy(&stream[4], &len1, 4);

  /* Frame 2: ACKF (28 bytes, [32, 59]) */
  memcpy(&stream[32], "ACKF", 4);
  uint32_t len2 = 28;
  memcpy(&stream[36], &len2, 4);

  /* Frame 3: ERRF with status 0x80050000 (16 bytes, [60, 75]) */
  memcpy(&stream[60], "ERRF", 4);
  uint32_t len3 = 16;
  memcpy(&stream[64], &len3, 4);
  uint32_t err_code = 0x80050000;
  memcpy(&stream[68], &err_code, 4);

  unsigned int rc = 0, sc = 0;
  region_t *regions = extract_requests_opcua(stream, sizeof(stream), &rc);
  TEST_ASSERT(rc == 3, "Stream must yield 3 regions");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 31, "Region 0 mismatch");
  TEST_ASSERT(regions[1].start_byte == 32 && regions[1].end_byte == 59, "Region 1 mismatch");
  TEST_ASSERT(regions[2].start_byte == 60 && regions[2].end_byte == 75, "Region 2 mismatch");
  TEST_ASSERT(validate_regions_partition(stream, sizeof(stream), regions, rc), "Partition failed");

  unsigned int *states = extract_response_codes_opcua(stream, sizeof(stream), &sc);
  TEST_ASSERT(sc == 4, "Must yield 4 states");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0x48454C46, "State 1 mismatch (HELF)");
  TEST_ASSERT(states[2] == 0x41434B46, "State 2 mismatch (ACKF)");
  TEST_ASSERT(states[3] == 0x80050000, "State 3 mismatch (ERRF StatusCode)");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T19: Incomplete / truncated buffer handling */
static bool test_opcua_truncated_buffer(void) {
  g_tests_run++;
  uint8_t buf[40];
  memset(buf, 0, sizeof(buf));

  /* Frame 1: Valid HELF (32 bytes, [0, 31]) */
  memcpy(&buf[0], "HELF", 4);
  uint32_t len1 = 32;
  memcpy(&buf[4], &len1, 4);

  /* Frame 2: Truncated ACKF claiming length 28, but only 8 bytes present */
  memcpy(&buf[32], "ACKF", 4);
  uint32_t len2 = 28;
  memcpy(&buf[36], &len2, 4);

  unsigned int rc = 0, sc = 0;
  region_t *regions = extract_requests_opcua(buf, sizeof(buf), &rc);
  TEST_ASSERT(rc == 2, "Must yield 2 regions");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 31, "Region 0 mismatch");
  TEST_ASSERT(regions[1].start_byte == 32 && regions[1].end_byte == 39, "Region 1 mismatch");
  TEST_ASSERT(validate_regions_partition(buf, sizeof(buf), regions, rc), "Truncated partition must have no gaps/overlaps");

  unsigned int *states = extract_response_codes_opcua(buf, sizeof(buf), &sc);
  TEST_ASSERT(sc == 2 && states[0] == 0 && states[1] == 0x48454C46, "States mismatch on truncated buffer");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T20: Corrupt magic byte and malformed length handling */
static bool test_opcua_corrupt_magic_and_length(void) {
  g_tests_run++;
  unsigned int rc = 0, sc = 0;

  /* Invalid message type "XYZF" */
  uint8_t bad_type[16];
  memset(bad_type, 0, sizeof(bad_type));
  memcpy(bad_type, "XYZF", 4);
  uint32_t len = 16;
  memcpy(&bad_type[4], &len, 4);
  region_t *reg1 = extract_requests_opcua(bad_type, sizeof(bad_type), &rc);
  TEST_ASSERT(rc == 1 && reg1[0].start_byte == 0 && reg1[0].end_byte == 15, "Bad type must be absorbed");
  TEST_ASSERT(validate_regions_partition(bad_type, sizeof(bad_type), reg1, rc), "Partition failed");
  unsigned int *st1 = extract_response_codes_opcua(bad_type, sizeof(bad_type), &sc);
  TEST_ASSERT(sc == 1 && st1[0] == 0, "Bad type must produce only state 0");
  if (reg1) ck_free(reg1);
  if (st1) ck_free(st1);

  /* Invalid chunk type 'X' (not F, C, A) */
  uint8_t bad_chunk[16];
  memset(bad_chunk, 0, sizeof(bad_chunk));
  memcpy(bad_chunk, "HELX", 4);
  memcpy(&bad_chunk[4], &len, 4);
  region_t *reg2 = extract_requests_opcua(bad_chunk, sizeof(bad_chunk), &rc);
  TEST_ASSERT(rc == 1 && reg2[0].start_byte == 0 && reg2[0].end_byte == 15, "Bad chunk must be absorbed");
  TEST_ASSERT(validate_regions_partition(bad_chunk, sizeof(bad_chunk), reg2, rc), "Partition failed");
  if (reg2) ck_free(reg2);

  /* Packet length < 8 */
  uint8_t small_len[8];
  memcpy(small_len, "HELF", 4);
  uint32_t small = 6;
  memcpy(&small_len[4], &small, 4);
  region_t *reg3 = extract_requests_opcua(small_len, sizeof(small_len), &rc);
  TEST_ASSERT(rc == 1 && reg3[0].start_byte == 0 && reg3[0].end_byte == 7, "Length < 8 must be absorbed");
  TEST_ASSERT(validate_regions_partition(small_len, sizeof(small_len), reg3, rc), "Partition failed");
  if (reg3) ck_free(reg3);

  g_tests_passed++;
  return true;
}

/* T21: Empty buffer handling */
static bool test_opcua_empty_buffer(void) {
  g_tests_run++;
  unsigned int rc = 99, sc = 99;
  region_t *regions = extract_requests_opcua(NULL, 0, &rc);
  TEST_ASSERT(rc == 0 && regions == NULL, "Empty buffer must yield 0 regions");

  unsigned int *states = extract_response_codes_opcua(NULL, 0, &sc);
  TEST_ASSERT(sc == 1 && states != NULL && states[0] == 0, "Empty buffer must yield state 0");

  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* ========================================================================= */
/* SUITE 4: EtherNet/IP (ENIP / CIP)                                        */
/* ========================================================================= */

/* T22: Single ListIdentity message (24 bytes encapsulation header, cmd 0x0063) */
static bool test_enip_valid_single_list_identity(void) {
  g_tests_run++;
  uint8_t frame[24] = {
    0x63, 0x00,                         /* Command: ListIdentity (0x0063) */
    0x00, 0x00,                         /* Payload length: 0 */
    0x00, 0x00, 0x00, 0x00,             /* Session handle */
    0x00, 0x00, 0x00, 0x00,             /* Status: 0 */
    0, 0, 0, 0, 0, 0, 0, 0,             /* Sender context */
    0, 0, 0, 0                          /* Options */
  };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_enip(frame, sizeof(frame), &rc);
  TEST_ASSERT(rc == 1, "Request count must be 1");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 23, "Region bounds mismatch");
  TEST_ASSERT(validate_regions_partition(frame, sizeof(frame), regions, rc), "Partition failed");

  unsigned int *states = extract_response_codes_enip(frame, sizeof(frame), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0x0063, "State code must be 0x0063");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T23: Single frame with Error Status */
static bool test_enip_valid_single_error_status(void) {
  g_tests_run++;
  uint8_t frame[24] = {
    0x65, 0x00,                         /* Command: RegisterSession (0x0065) */
    0x00, 0x00,                         /* Payload length: 0 */
    0x00, 0x00, 0x00, 0x00,             /* Session handle */
    0x01, 0x00, 0x00, 0x00,             /* Status: 0x00000001 (Invalid command) */
    0, 0, 0, 0, 0, 0, 0, 0,             /* Sender context */
    0, 0, 0, 0                          /* Options */
  };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_enip(frame, sizeof(frame), &rc);
  TEST_ASSERT(rc == 1, "Request count must be 1");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 23, "Region bounds mismatch");
  TEST_ASSERT(validate_regions_partition(frame, sizeof(frame), regions, rc), "Partition failed");

  unsigned int *states = extract_response_codes_enip(frame, sizeof(frame), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0x00650001, "Error state must be (cmd<<16)|status = 0x00650001");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T24: Single SendRRData frame with CIP payload */
static bool test_enip_valid_single_send_rr_data_cip(void) {
  g_tests_run++;
  uint8_t frame[46] = {
    /* 0..23 Encapsulation Header */
    0x6F, 0x00,                         /* cmd: SendRRData (0x006F) */
    0x16, 0x00,                         /* payload len: 22 bytes */
    0x00, 0x00, 0x00, 0x00,             /* session */
    0x00, 0x00, 0x00, 0x00,             /* status: 0 */
    0, 0, 0, 0, 0, 0, 0, 0,             /* sender context */
    0, 0, 0, 0,                         /* options */
    /* 24..29 CPF Interface & Timeout */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* 30..31 Item Count */
    0x02, 0x00,
    /* 32..35 Item 1 (Null Address) */
    0x00, 0x00, 0x00, 0x00,
    /* 36..39 Item 2 Header */
    0xB2, 0x00, 0x06, 0x00,
    /* 40..45 CIP Payload: Service 0x4E, Reserved 0x00, Status 0x00 */
    0x4E, 0x00, 0x00, 0x01, 0x02, 0x03
  };
  unsigned int rc = 0, sc = 0;

  region_t *regions = extract_requests_enip(frame, sizeof(frame), &rc);
  TEST_ASSERT(rc == 1, "Request count must be 1");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 45, "Region bounds mismatch");
  TEST_ASSERT(validate_regions_partition(frame, sizeof(frame), regions, rc), "Partition failed");

  unsigned int *states = extract_response_codes_enip(frame, sizeof(frame), &sc);
  TEST_ASSERT(sc == 2, "State count must be 2");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0x006F4E00, "CIP response state must be (0x006F<<16)|(0x4E<<8)|0x00");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T25: Multi-frame concatenated stream */
static bool test_enip_multi_frame_stream(void) {
  g_tests_run++;
  uint8_t stream[94];
  memset(stream, 0, sizeof(stream));

  /* Frame 1: ListIdentity (24 bytes, [0, 23]) */
  stream[0] = 0x63;

  /* Frame 2: Error status frame (24 bytes, [24, 47]) */
  stream[24] = 0x65;
  stream[32] = 0x01;

  /* Frame 3: SendRRData with CIP (46 bytes, [48, 93]) */
  stream[48] = 0x6F;
  stream[50] = 0x16;
  stream[78] = 0x02; /* Item count */
  stream[84] = 0xB2; /* Item 2 type */
  stream[86] = 0x06; /* Item 2 length */
  stream[88] = 0x4E; /* CIP Service */
  stream[90] = 0x00; /* CIP Status */

  unsigned int rc = 0, sc = 0;
  region_t *regions = extract_requests_enip(stream, sizeof(stream), &rc);
  TEST_ASSERT(rc == 3, "Stream must yield 3 regions");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 23, "Region 0 mismatch");
  TEST_ASSERT(regions[1].start_byte == 24 && regions[1].end_byte == 47, "Region 1 mismatch");
  TEST_ASSERT(regions[2].start_byte == 48 && regions[2].end_byte == 93, "Region 2 mismatch");
  TEST_ASSERT(validate_regions_partition(stream, sizeof(stream), regions, rc), "Partition failed");

  unsigned int *states = extract_response_codes_enip(stream, sizeof(stream), &sc);
  TEST_ASSERT(sc == 4, "Must yield 4 states");
  TEST_ASSERT(states[0] == 0, "Initial state 0 required");
  TEST_ASSERT(states[1] == 0x0063, "State 1 mismatch (ListIdentity)");
  TEST_ASSERT(states[2] == 0x00650001, "State 2 mismatch (RegisterSession Error)");
  TEST_ASSERT(states[3] == 0x006F4E00, "State 3 mismatch (SendRRData CIP)");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T26: Incomplete / truncated buffer handling */
static bool test_enip_truncated_buffer(void) {
  g_tests_run++;
  uint8_t buf[58];
  memset(buf, 0, sizeof(buf));

  /* Frame 1: Complete ListIdentity (24 bytes, [0, 23]) */
  buf[0] = 0x63;

  /* Frame 2: Header claiming payload length 40, but only 10 bytes present after header */
  buf[24] = 0x65;
  buf[26] = 40;

  unsigned int rc = 0, sc = 0;
  region_t *regions = extract_requests_enip(buf, sizeof(buf), &rc);
  TEST_ASSERT(rc == 2, "Must yield 2 regions");
  TEST_ASSERT(regions[0].start_byte == 0 && regions[0].end_byte == 23, "Region 0 mismatch");
  TEST_ASSERT(regions[1].start_byte == 24 && regions[1].end_byte == 57, "Region 1 mismatch");
  TEST_ASSERT(validate_regions_partition(buf, sizeof(buf), regions, rc), "Truncated partition must have no gaps/overlaps");

  unsigned int *states = extract_response_codes_enip(buf, sizeof(buf), &sc);
  TEST_ASSERT(sc == 2 && states[0] == 0 && states[1] == 0x0063, "States mismatch on truncated buffer");

  if (regions) ck_free(regions);
  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* T27: Corrupt magic byte and malformed length handling */
static bool test_enip_corrupt_magic_and_length(void) {
  g_tests_run++;
  unsigned int rc = 0, sc = 0;

  /* Buffer smaller than 24 bytes */
  uint8_t short_buf[12] = { 0 };
  region_t *reg1 = extract_requests_enip(short_buf, sizeof(short_buf), &rc);
  TEST_ASSERT(rc == 1 && reg1[0].start_byte == 0 && reg1[0].end_byte == 11, "Short buffer must be absorbed");
  TEST_ASSERT(validate_regions_partition(short_buf, sizeof(short_buf), reg1, rc), "Partition failed");
  unsigned int *st1 = extract_response_codes_enip(short_buf, sizeof(short_buf), &sc);
  TEST_ASSERT(sc == 1 && st1[0] == 0, "Short buffer must yield only state 0");
  if (reg1) ck_free(reg1);
  if (st1) ck_free(st1);

  /* Payload length exceeds buffer */
  uint8_t huge_payload[28] = { 0 };
  huge_payload[0] = 0x63;
  huge_payload[2] = 0xFF;
  huge_payload[3] = 0xFF;
  region_t *reg2 = extract_requests_enip(huge_payload, sizeof(huge_payload), &rc);
  TEST_ASSERT(rc == 1 && reg2[0].start_byte == 0 && reg2[0].end_byte == 27, "Huge payload must be absorbed");
  TEST_ASSERT(validate_regions_partition(huge_payload, sizeof(huge_payload), reg2, rc), "Partition failed");
  if (reg2) ck_free(reg2);

  g_tests_passed++;
  return true;
}

/* T28: Empty buffer handling */
static bool test_enip_empty_buffer(void) {
  g_tests_run++;
  unsigned int rc = 99, sc = 99;
  region_t *regions = extract_requests_enip(NULL, 0, &rc);
  TEST_ASSERT(rc == 0 && regions == NULL, "Empty buffer must yield 0 regions");

  unsigned int *states = extract_response_codes_enip(NULL, 0, &sc);
  TEST_ASSERT(sc == 1 && states != NULL && states[0] == 0, "Empty buffer must yield state 0");

  if (states) ck_free(states);
  g_tests_passed++;
  return true;
}

/* ========================================================================= */
/* MAIN TEST RUNNER                                                          */
/* ========================================================================= */

int main(void) {
  printf("[*] Running Native Industrial Protocol Extractor Unit Tests (28 tests)...\n");

  printf("[*] Suite 1: IEC 60870-5-104 (7 tests)\n");
  assert(test_iec104_valid_single_u_frame());
  assert(test_iec104_valid_single_s_frame());
  assert(test_iec104_valid_single_i_frame());
  assert(test_iec104_multi_frame_stream());
  assert(test_iec104_truncated_buffer());
  assert(test_iec104_corrupt_magic_and_length());
  assert(test_iec104_empty_buffer());

  printf("[*] Suite 2: IEC 61850 (7 tests)\n");
  assert(test_iec61850_valid_single_cotp_cr());
  assert(test_iec61850_valid_single_cotp_cc());
  assert(test_iec61850_valid_single_mms_confirmed_request());
  assert(test_iec61850_multi_frame_stream());
  assert(test_iec61850_truncated_buffer());
  assert(test_iec61850_corrupt_magic_and_length());
  assert(test_iec61850_empty_buffer());

  printf("[*] Suite 3: OPC UA Binary (7 tests)\n");
  assert(test_opcua_valid_single_hel());
  assert(test_opcua_valid_single_ack());
  assert(test_opcua_valid_single_err());
  assert(test_opcua_multi_frame_stream());
  assert(test_opcua_truncated_buffer());
  assert(test_opcua_corrupt_magic_and_length());
  assert(test_opcua_empty_buffer());

  printf("[*] Suite 4: EtherNet/IP (7 tests)\n");
  assert(test_enip_valid_single_list_identity());
  assert(test_enip_valid_single_error_status());
  assert(test_enip_valid_single_send_rr_data_cip());
  assert(test_enip_multi_frame_stream());
  assert(test_enip_truncated_buffer());
  assert(test_enip_corrupt_magic_and_length());
  assert(test_enip_empty_buffer());

  printf("\n========================================================\n");
  printf("[+] Test Summary: %d / %d tests passed (100.0%%)\n", g_tests_passed, g_tests_run);
  printf("========================================================\n");

  return (g_tests_passed == g_tests_run) ? 0 : 1;
}
