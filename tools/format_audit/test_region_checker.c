#define _GNU_SOURCE
#include "region_checker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { \
    printf("[-] FAILED: %s (line %d): %s\n", __func__, __LINE__, msg); \
    return false; \
  } \
} while(0)

/* T17: Region bounds exactly at end vs 1 unit beyond end */
static bool test_t17(void) {
  g_tests_run++;
  uint8_t buf_data[10] = "0123456789";
  raw_buffer_handle_t buf;
  raw_buffer_init(&buf, 1001, buf_data, 10);

  region_set_t set_valid, set_over;
  region_set_init(&set_valid);
  region_set_init(&set_over);

  /* Exactly at end: [0, 9] */
  region_set_append(&set_valid, 0, 9);
  region_diagnostic_t diag;
  region_status_t st = region_validate(&set_valid, &buf, REGION_CHECK_PERMISSIVE, &diag);
  TEST_ASSERT(st == REGION_OK, "Valid region [0, 9] must pass");

  /* 1 unit beyond end: [0, 10] */
  region_set_append(&set_over, 0, 10);
  st = region_validate(&set_over, &buf, REGION_CHECK_PERMISSIVE, &diag);
  TEST_ASSERT(st == REGION_ERR_BOUNDS_EXCEEDS_BUFFER, "Region [0, 10] must be rejected as exceeds buffer");

  region_set_free(&set_valid);
  region_set_free(&set_over);
  g_tests_passed++;
  return true;
}

/* T18: Negative, reversed, overlapping, gap, out-of-order regions */
static bool test_t18(void) {
  g_tests_run++;
  uint8_t buf_data[10] = "ABCDEFGHIJ";
  raw_buffer_handle_t buf;
  raw_buffer_init(&buf, 1002, buf_data, 10);
  region_diagnostic_t diag;

  /* Negative */
  region_set_t set_neg;
  region_set_init(&set_neg);
  region_set_append(&set_neg, -1, 4);
  TEST_ASSERT(region_validate(&set_neg, &buf, REGION_CHECK_PERMISSIVE, &diag) == REGION_ERR_BOUNDS_NEGATIVE, "Must reject negative start");
  region_set_free(&set_neg);

  /* Reversed */
  region_set_t set_rev;
  region_set_init(&set_rev);
  region_set_append(&set_rev, 5, 2);
  TEST_ASSERT(region_validate(&set_rev, &buf, REGION_CHECK_PERMISSIVE, &diag) == REGION_ERR_BOUNDS_REVERSED, "Must reject reversed bounds");
  region_set_free(&set_rev);

  /* Overlap */
  region_set_t set_overlap;
  region_set_init(&set_overlap);
  region_set_append(&set_overlap, 0, 4);
  region_set_append(&set_overlap, 4, 8); /* Overlaps at index 4 */
  TEST_ASSERT(region_validate(&set_overlap, &buf, REGION_CHECK_NO_OVERLAP, &diag) == REGION_ERR_OVERLAP, "Must reject overlap");
  region_set_free(&set_overlap);

  /* Gap */
  region_set_t set_gap;
  region_set_init(&set_gap);
  region_set_append(&set_gap, 0, 3);
  region_set_append(&set_gap, 5, 9); /* Gap at index 4 */
  TEST_ASSERT(region_validate(&set_gap, &buf, REGION_CHECK_NO_GAPS, &diag) == REGION_ERR_GAP, "Must reject gap");
  region_set_free(&set_gap);

  /* Unordered */
  region_set_t set_unord;
  region_set_init(&set_unord);
  region_set_append(&set_unord, 5, 9);
  region_set_append(&set_unord, 0, 4);
  TEST_ASSERT(region_validate(&set_unord, &buf, REGION_CHECK_ORDERED, &diag) == REGION_ERR_UNORDERED, "Must reject unordered");
  region_set_free(&set_unord);

  g_tests_passed++;
  return true;
}

/* T19: New content with same length as old content (digest mismatch) */
static bool test_t19(void) {
  g_tests_run++;
  uint8_t old_data[4] = "AAAA";
  uint8_t new_data[4] = "BBBB";
  raw_buffer_handle_t buf;
  raw_buffer_init(&buf, 1003, old_data, 4);

  region_set_t set;
  region_set_init(&set);
  region_set_append(&set, 0, 3);
  region_set_bind(&set, &buf);

  /* Replace data with different content of exact same length */
  raw_buffer_update(&buf, new_data, 4);

  region_diagnostic_t diag;
  region_status_t st = region_validate(&set, &buf, REGION_CHECK_PERMISSIVE, &diag);
  TEST_ASSERT(st == REGION_ERR_METADATA_STALE_GENERATION || st == REGION_ERR_METADATA_STALE_HASH,
              "Must reject stale metadata on same-length content alteration");

  region_set_free(&set);
  g_tests_passed++;
  return true;
}

/* T20: Object address reused but generation changed */
static bool test_t20(void) {
  g_tests_run++;
  uint8_t data[6] = "TEST01";
  raw_buffer_handle_t buf;
  raw_buffer_init(&buf, 1004, data, 6);

  region_set_t set;
  region_set_init(&set);
  region_set_append(&set, 0, 5);
  region_set_bind(&set, &buf);

  /* Simulate buffer reuse / modification at same address */
  data[5] = '2';
  raw_buffer_update(&buf, data, 6);

  region_diagnostic_t diag;
  region_status_t st = region_validate(&set, &buf, REGION_CHECK_PERMISSIVE, &diag);
  TEST_ASSERT(st == REGION_ERR_METADATA_STALE_GENERATION, "Must detect updated generation upon reuse");

  region_set_free(&set);
  g_tests_passed++;
  return true;
}

/* T21: Trimmed, appended, or reordered objects */
static bool test_t21(void) {
  g_tests_run++;
  uint8_t initial[10] = "0123456789";
  raw_buffer_handle_t buf;
  raw_buffer_init(&buf, 1005, initial, 10);

  region_set_t set;
  region_set_init(&set);
  region_set_append(&set, 0, 4);
  region_set_append(&set, 5, 9);
  region_set_bind(&set, &buf);

  /* Trim buffer to 6 bytes */
  raw_buffer_update(&buf, initial, 6);

  region_diagnostic_t diag;
  region_status_t st = region_validate(&set, &buf, REGION_CHECK_PERMISSIVE, &diag);
  TEST_ASSERT(st == REGION_ERR_METADATA_STALE_GENERATION || st == REGION_ERR_BOUNDS_EXCEEDS_BUFFER,
              "Must invalidate metadata when buffer is trimmed");

  region_set_free(&set);
  g_tests_passed++;
  return true;
}

/* T22: Deep clone ownership lifecycle test */
static bool test_t22(void) {
  g_tests_run++;
  region_set_t orig, clone;
  region_set_init(&orig);
  region_set_append(&orig, 0, 3);

  /* Allocate state sequence in orig */
  orig.regions[0].state_count = 2;
  orig.regions[0].state_sequence = (unsigned int *)malloc(2 * sizeof(unsigned int));
  orig.regions[0].state_sequence[0] = 0xAA;
  orig.regions[0].state_sequence[1] = 0xBB;

  region_status_t st = region_set_clone(&orig, &clone);
  TEST_ASSERT(st == REGION_OK, "Clone failed");

  /* Free orig completely */
  region_set_free(&orig);

  /* Clone must retain its own independent deep copy */
  TEST_ASSERT(clone.count == 1, "Clone count mismatch");
  TEST_ASSERT(clone.regions[0].state_count == 2, "State count mismatch");
  TEST_ASSERT(clone.regions[0].state_sequence[0] == 0xAA && clone.regions[0].state_sequence[1] == 0xBB, "Deep copy corrupted");

  region_set_free(&clone);
  g_tests_passed++;
  return true;
}

/* T23: Verifying normal object after a failed verification */
static bool test_t23(void) {
  g_tests_run++;
  uint8_t data[5] = "HELLO";
  raw_buffer_handle_t buf;
  raw_buffer_init(&buf, 1007, data, 5);

  region_set_t bad_set, good_set;
  region_set_init(&bad_set);
  region_set_append(&bad_set, 0, 999); /* Intentionally invalid */

  region_diagnostic_t diag;
  region_status_t st1 = region_validate(&bad_set, &buf, REGION_CHECK_PERMISSIVE, &diag);
  TEST_ASSERT(st1 != REGION_OK, "Bad set must fail");

  region_set_init(&good_set);
  region_set_append(&good_set, 0, 4); /* Exactly valid */
  region_set_bind(&good_set, &buf);

  region_status_t st2 = region_validate(&good_set, &buf, REGION_CHECK_STRICT_PARTITION, &diag);
  TEST_ASSERT(st2 == REGION_OK, "Good set must pass without residual pollution from previous failure");

  region_set_free(&bad_set);
  region_set_free(&good_set);
  g_tests_passed++;
  return true;
}

/* T24: Deterministic repeated verification */
static bool test_t24(void) {
  g_tests_run++;
  uint8_t data[8] = "12345678";
  raw_buffer_handle_t buf;
  raw_buffer_init(&buf, 1008, data, 8);

  region_set_t set;
  region_set_init(&set);
  region_set_append(&set, 0, 3);
  region_set_append(&set, 4, 7);
  region_set_bind(&set, &buf);

  region_diagnostic_t diag1, diag2;
  region_status_t st1 = region_validate(&set, &buf, REGION_CHECK_STRICT_PARTITION, &diag1);
  region_status_t st2 = region_validate(&set, &buf, REGION_CHECK_STRICT_PARTITION, &diag2);

  TEST_ASSERT(st1 == REGION_OK && st2 == REGION_OK, "Both runs must succeed");
  TEST_ASSERT(diag1.code == diag2.code, "Status code must match");
  TEST_ASSERT(diag1.region_index == diag2.region_index, "Region index must match");
  TEST_ASSERT(strcmp(diag1.detail, diag2.detail) == 0, "Diagnostic message must match");

  const uint8_t *slice = NULL;
  size_t slen = 0;
  region_extract_slice(&buf, &set.regions[0], &slice, &slen);
  TEST_ASSERT(slen == 4 && memcmp(slice, "1234", 4) == 0, "Slice 0 mismatch");

  region_extract_slice(&buf, &set.regions[1], &slice, &slen);
  TEST_ASSERT(slen == 4 && memcmp(slice, "5678", 4) == 0, "Slice 1 mismatch");

  region_set_free(&set);
  g_tests_passed++;
  return true;
}

/* T25: Zero-length regions in empty buffer (size == 0, [0, -1]) */
static bool test_t25(void) {
  g_tests_run++;
  raw_buffer_handle_t buf;
  raw_buffer_init(&buf, 1009, NULL, 0);

  region_set_t set;
  region_set_init(&set);
  region_set_append(&set, 0, -1);
  region_set_bind(&set, &buf);

  region_diagnostic_t diag;
  region_status_t st = region_validate(&set, &buf, REGION_CHECK_PERMISSIVE | REGION_CHECK_ORDERED, &diag);
  TEST_ASSERT(st == REGION_OK, "Zero-length region in empty buffer must pass permissive/ordered");

  st = region_validate(&set, &buf, REGION_CHECK_FULL_COVER, &diag);
  TEST_ASSERT(st == REGION_OK, "Zero-length region in empty buffer must satisfy full coverage");

  const uint8_t *slice = (const uint8_t *)0x1;
  size_t slen = 999;
  st = region_extract_slice(&buf, &set.regions[0], &slice, &slen);
  TEST_ASSERT(st == REGION_OK, "Extract zero-length slice must succeed");
  TEST_ASSERT(slice == NULL && slen == 0, "Slice must be NULL with length 0");

  region_set_free(&set);
  g_tests_passed++;
  return true;
}

/* T26: Mixed zero-length and non-empty regions in 10-byte buffer */
static bool test_t26(void) {
  g_tests_run++;
  uint8_t data[10] = "ABCDEFGHIJ";
  raw_buffer_handle_t buf;
  raw_buffer_init(&buf, 1010, data, 10);

  region_set_t set;
  region_set_init(&set);
  /* [0, -1] (zero-len), [0, 4] (len 5), [5, 4] (zero-len), [5, 9] (len 5), [10, 9] (zero-len) */
  region_set_append(&set, 0, -1);
  region_set_append(&set, 0, 4);
  region_set_append(&set, 5, 4);
  region_set_append(&set, 5, 9);
  region_set_append(&set, 10, 9);
  region_set_bind(&set, &buf);

  region_diagnostic_t diag;
  region_status_t st = region_validate(&set, &buf, REGION_CHECK_ORDERED | REGION_CHECK_NO_GAPS | REGION_CHECK_FULL_COVER, &diag);
  TEST_ASSERT(st == REGION_OK, "Interleaved zero-length regions must pass strict validation");

  const uint8_t *slice = NULL;
  size_t slen = 0;

  region_extract_slice(&buf, &set.regions[0], &slice, &slen);
  TEST_ASSERT(slen == 0 && slice == NULL, "Region 0 slice mismatch");

  region_extract_slice(&buf, &set.regions[1], &slice, &slen);
  TEST_ASSERT(slen == 5 && memcmp(slice, "ABCDE", 5) == 0, "Region 1 slice mismatch");

  region_extract_slice(&buf, &set.regions[2], &slice, &slen);
  TEST_ASSERT(slen == 0 && slice == NULL, "Region 2 slice mismatch");

  region_extract_slice(&buf, &set.regions[3], &slice, &slen);
  TEST_ASSERT(slen == 5 && memcmp(slice, "FGHIJ", 5) == 0, "Region 3 slice mismatch");

  region_extract_slice(&buf, &set.regions[4], &slice, &slen);
  TEST_ASSERT(slen == 0 && slice == NULL, "Region 4 slice mismatch");

  region_set_free(&set);
  g_tests_passed++;
  return true;
}

int main(void) {
  printf("[*] Running independent Region Checker Tests (T17 - T26, 10 cases)...\n");
  if (!test_t17()) return EXIT_FAILURE;
  if (!test_t18()) return EXIT_FAILURE;
  if (!test_t19()) return EXIT_FAILURE;
  if (!test_t20()) return EXIT_FAILURE;
  if (!test_t21()) return EXIT_FAILURE;
  if (!test_t22()) return EXIT_FAILURE;
  if (!test_t23()) return EXIT_FAILURE;
  if (!test_t24()) return EXIT_FAILURE;
  if (!test_t25()) return EXIT_FAILURE;
  if (!test_t26()) return EXIT_FAILURE;
  if (g_tests_run != 10 || g_tests_passed != 10) return EXIT_FAILURE;
  printf("[+] ALL %d / %d REGION CHECKER TESTS PASSED!\n", g_tests_passed, g_tests_run);
  return 0;
}
