#ifndef __REGION_CHECKER_H
#define __REGION_CHECKER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int start_byte;
  int end_byte;
  unsigned int *state_sequence;
  unsigned int state_count;
} audit_region_t;

typedef struct {
  uint64_t object_id;
  uint64_t generation;
  uint64_t content_hash;
  const uint8_t *data;
  size_t size;
} raw_buffer_handle_t;

typedef struct {
  audit_region_t *regions;
  size_t count;
  size_t capacity;
  uint64_t bound_object_id;
  uint64_t bound_generation;
  uint64_t bound_content_hash;
  bool owns_state_sequences;
} region_set_t;

typedef enum {
  REGION_CHECK_PERMISSIVE = 0,
  REGION_CHECK_ORDERED    = (1 << 0),
  REGION_CHECK_NO_OVERLAP = (1 << 1),
  REGION_CHECK_NO_GAPS    = (1 << 2),
  REGION_CHECK_FULL_COVER = (1 << 3),
  REGION_CHECK_STRICT_PARTITION = (REGION_CHECK_ORDERED | REGION_CHECK_NO_OVERLAP | REGION_CHECK_NO_GAPS | REGION_CHECK_FULL_COVER)
} region_policy_flags_t;

typedef enum {
  REGION_OK = 0,
  REGION_ERR_NULL_PARAM,
  REGION_ERR_BOUNDS_NEGATIVE,
  REGION_ERR_BOUNDS_REVERSED,
  REGION_ERR_BOUNDS_EXCEEDS_BUFFER,
  REGION_ERR_UNORDERED,
  REGION_ERR_OVERLAP,
  REGION_ERR_GAP,
  REGION_ERR_INCOMPLETE_COVERAGE,
  REGION_ERR_METADATA_STALE_HASH,
  REGION_ERR_METADATA_STALE_GENERATION,
  REGION_ERR_METADATA_STALE_ID,
  REGION_ERR_ALLOC
} region_status_t;

typedef struct {
  region_status_t code;
  size_t region_index;
  int start_byte;
  int end_byte;
  char detail[256];
} region_diagnostic_t;

void raw_buffer_init(raw_buffer_handle_t *handle, uint64_t object_id, const uint8_t *data, size_t size);
void raw_buffer_update(raw_buffer_handle_t *handle, const uint8_t *data, size_t size);

void region_set_init(region_set_t *set);
void region_set_free(region_set_t *set);
region_status_t region_set_append(region_set_t *set, int start_byte, int end_byte);
void region_set_bind(region_set_t *set, const raw_buffer_handle_t *buf);
region_status_t region_set_clone(const region_set_t *src, region_set_t *dst);

const char *region_status_str(region_status_t code);

region_status_t region_validate(const region_set_t *set,
                               const raw_buffer_handle_t *buf,
                               int policy_flags,
                               region_diagnostic_t *diag);

region_status_t region_extract_slice(const raw_buffer_handle_t *buf,
                                     const audit_region_t *reg,
                                     const uint8_t **out_ptr,
                                     size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* __REGION_CHECKER_H */
