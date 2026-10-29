#include "region_checker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t compute_hash(const uint8_t *data, size_t size) {
  if (!data || size == 0) return 0xcbf29ce484222325ULL;
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < size; i++) {
    h ^= data[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

void raw_buffer_init(raw_buffer_handle_t *handle, uint64_t object_id, const uint8_t *data, size_t size) {
  if (!handle) return;
  handle->object_id = object_id;
  handle->generation = 1;
  handle->data = data;
  handle->size = size;
  handle->content_hash = compute_hash(data, size);
}

void raw_buffer_update(raw_buffer_handle_t *handle, const uint8_t *data, size_t size) {
  if (!handle) return;
  handle->generation++;
  handle->data = data;
  handle->size = size;
  handle->content_hash = compute_hash(data, size);
}

void region_set_init(region_set_t *set) {
  if (!set) return;
  memset(set, 0, sizeof(*set));
  set->owns_state_sequences = true;
}

void region_set_free(region_set_t *set) {
  if (!set) return;
  if (set->regions) {
    if (set->owns_state_sequences) {
      for (size_t i = 0; i < set->count; i++) {
        free(set->regions[i].state_sequence);
      }
    }
    free(set->regions);
  }
  memset(set, 0, sizeof(*set));
}

region_status_t region_set_append(region_set_t *set, int start_byte, int end_byte) {
  if (!set) return REGION_ERR_NULL_PARAM;
  if (set->count >= set->capacity) {
    size_t new_cap = set->capacity ? set->capacity * 2 : 8;
    audit_region_t *new_arr = (audit_region_t *)realloc(set->regions, new_cap * sizeof(audit_region_t));
    if (!new_arr) return REGION_ERR_ALLOC;
    set->regions = new_arr;
    set->capacity = new_cap;
  }

  set->regions[set->count].start_byte = start_byte;
  set->regions[set->count].end_byte = end_byte;
  set->regions[set->count].state_sequence = NULL;
  set->regions[set->count].state_count = 0;
  set->count++;
  return REGION_OK;
}

void region_set_bind(region_set_t *set, const raw_buffer_handle_t *buf) {
  if (!set || !buf) return;
  set->bound_object_id = buf->object_id;
  set->bound_generation = buf->generation;
  set->bound_content_hash = buf->content_hash;
}

region_status_t region_set_clone(const region_set_t *src, region_set_t *dst) {
  if (!src || !dst) return REGION_ERR_NULL_PARAM;
  region_set_init(dst);
  dst->bound_object_id = src->bound_object_id;
  dst->bound_generation = src->bound_generation;
  dst->bound_content_hash = src->bound_content_hash;
  dst->owns_state_sequences = true;

  if (src->count > 0) {
    dst->regions = (audit_region_t *)malloc(src->count * sizeof(audit_region_t));
    if (!dst->regions) return REGION_ERR_ALLOC;
    dst->count = src->count;
    dst->capacity = src->count;

    for (size_t i = 0; i < src->count; i++) {
      dst->regions[i].start_byte = src->regions[i].start_byte;
      dst->regions[i].end_byte = src->regions[i].end_byte;
      dst->regions[i].state_count = src->regions[i].state_count;
      if (src->regions[i].state_count > 0 && src->regions[i].state_sequence) {
        dst->regions[i].state_sequence = (unsigned int *)malloc(src->regions[i].state_count * sizeof(unsigned int));
        if (!dst->regions[i].state_sequence) {
          region_set_free(dst);
          return REGION_ERR_ALLOC;
        }
        memcpy(dst->regions[i].state_sequence, src->regions[i].state_sequence, src->regions[i].state_count * sizeof(unsigned int));
      } else {
        dst->regions[i].state_sequence = NULL;
      }
    }
  }

  return REGION_OK;
}

const char *region_status_str(region_status_t code) {
  switch (code) {
    case REGION_OK: return "OK";
    case REGION_ERR_NULL_PARAM: return "Null parameter provided";
    case REGION_ERR_BOUNDS_NEGATIVE: return "Region boundary coordinate is negative";
    case REGION_ERR_BOUNDS_REVERSED: return "Region end coordinate is less than start (invalid reverse interval)";
    case REGION_ERR_BOUNDS_EXCEEDS_BUFFER: return "Region end coordinate exceeds raw buffer length";
    case REGION_ERR_UNORDERED: return "Regions are not strictly monotonically ordered";
    case REGION_ERR_OVERLAP: return "Overlapping regions detected under no-overlap policy";
    case REGION_ERR_GAP: return "Uncovered gap between adjacent regions under no-gap policy";
    case REGION_ERR_INCOMPLETE_COVERAGE: return "Regions do not fully span the buffer from 0 to size-1";
    case REGION_ERR_METADATA_STALE_HASH: return "Metadata content hash does not match current buffer hash";
    case REGION_ERR_METADATA_STALE_GENERATION: return "Metadata generation does not match current buffer generation";
    case REGION_ERR_METADATA_STALE_ID: return "Metadata bound object ID does not match buffer ID";
    case REGION_ERR_ALLOC: return "Memory allocation failed";
    default: return "Unknown region error";
  }
}

static void set_diag(region_diagnostic_t *diag, region_status_t code, size_t idx, int start, int end, const char *msg) {
  if (!diag) return;
  diag->code = code;
  diag->region_index = idx;
  diag->start_byte = start;
  diag->end_byte = end;
  if (msg) snprintf(diag->detail, sizeof(diag->detail), "%s", msg);
  else snprintf(diag->detail, sizeof(diag->detail), "%s", region_status_str(code));
}

region_status_t region_validate(const region_set_t *set,
                               const raw_buffer_handle_t *buf,
                               int policy_flags,
                               region_diagnostic_t *diag) {
  if (!set || !buf) {
    set_diag(diag, REGION_ERR_NULL_PARAM, 0, 0, 0, NULL);
    return REGION_ERR_NULL_PARAM;
  }

  /* 1. Invalidation & Association Checks */
  if (set->bound_object_id != 0) {
    if (set->bound_object_id != buf->object_id) {
      set_diag(diag, REGION_ERR_METADATA_STALE_ID, 0, 0, 0, "Bound object ID mismatch");
      return REGION_ERR_METADATA_STALE_ID;
    }
    if (set->bound_generation != buf->generation) {
      set_diag(diag, REGION_ERR_METADATA_STALE_GENERATION, 0, 0, 0, "Buffer generation modified since binding");
      return REGION_ERR_METADATA_STALE_GENERATION;
    }
    if (set->bound_content_hash != buf->content_hash) {
      set_diag(diag, REGION_ERR_METADATA_STALE_HASH, 0, 0, 0, "Buffer content hash changed (digest mismatch)");
      return REGION_ERR_METADATA_STALE_HASH;
    }
  }

  /* 2. Individual Region Validity */
  int prev_end = -1;
  for (size_t i = 0; i < set->count; i++) {
    int s = set->regions[i].start_byte;
    int e = set->regions[i].end_byte;

    if (s < 0) {
      set_diag(diag, REGION_ERR_BOUNDS_NEGATIVE, i, s, e, "Start coordinate cannot be negative");
      return REGION_ERR_BOUNDS_NEGATIVE;
    }

    /* Zero-length record convention: e == s - 1 */
    if (e < s - 1) {
      set_diag(diag, REGION_ERR_BOUNDS_REVERSED, i, s, e, "Reversed interval (end < start - 1)");
      return REGION_ERR_BOUNDS_REVERSED;
    }

    if (e >= (int)buf->size) {
      set_diag(diag, REGION_ERR_BOUNDS_EXCEEDS_BUFFER, i, s, e, "End coordinate exceeds buffer size");
      return REGION_ERR_BOUNDS_EXCEEDS_BUFFER;
    }

    /* Policy: Ordered */
    if (policy_flags & REGION_CHECK_ORDERED) {
      if (i > 0 && s < set->regions[i - 1].start_byte) {
        set_diag(diag, REGION_ERR_UNORDERED, i, s, e, "Region start is before previous region start");
        return REGION_ERR_UNORDERED;
      }
    }

    /* Policy: No Overlap */
    if (policy_flags & REGION_CHECK_NO_OVERLAP) {
      if (i > 0 && s <= prev_end) {
        set_diag(diag, REGION_ERR_OVERLAP, i, s, e, "Region overlaps with preceding region");
        return REGION_ERR_OVERLAP;
      }
    }

    /* Policy: No Gaps */
    if (policy_flags & REGION_CHECK_NO_GAPS) {
      if (i == 0) {
        if (s != 0) {
          set_diag(diag, REGION_ERR_GAP, i, s, e, "First region does not start at byte 0");
          return REGION_ERR_GAP;
        }
      } else {
        if (s != prev_end + 1) {
          set_diag(diag, REGION_ERR_GAP, i, s, e, "Gap between preceding region end and current start");
          return REGION_ERR_GAP;
        }
      }
    }

    if (e >= s) {
      prev_end = e;
    }
  }

  /* Policy: Full Coverage */
  if (policy_flags & REGION_CHECK_FULL_COVER) {
    if (buf->size == 0) {
      if (set->count > 0 && prev_end >= 0) {
        set_diag(diag, REGION_ERR_INCOMPLETE_COVERAGE, 0, 0, 0, "Buffer is empty but non-empty regions present");
        return REGION_ERR_INCOMPLETE_COVERAGE;
      }
    } else {
      if (prev_end != (int)buf->size - 1) {
        set_diag(diag, REGION_ERR_INCOMPLETE_COVERAGE, set->count ? set->count - 1 : 0, 0, prev_end, "Regions do not cover the complete buffer up to size - 1");
        return REGION_ERR_INCOMPLETE_COVERAGE;
      }
    }
  }

  set_diag(diag, REGION_OK, set->count, 0, prev_end, "Regions validated successfully");
  return REGION_OK;
}

region_status_t region_extract_slice(const raw_buffer_handle_t *buf,
                                     const audit_region_t *reg,
                                     const uint8_t **out_ptr,
                                     size_t *out_len) {
  if (!buf || !reg || !out_ptr || !out_len) return REGION_ERR_NULL_PARAM;
  int s = reg->start_byte;
  int e = reg->end_byte;
  if (s < 0 || e < s - 1 || e >= (int)buf->size) return REGION_ERR_BOUNDS_EXCEEDS_BUFFER;

  if (e == s - 1) {
    *out_ptr = NULL;
    *out_len = 0;
    return REGION_OK;
  }

  *out_ptr = buf->data + s;
  *out_len = (size_t)(e - s + 1);
  return REGION_OK;
}
