/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Portable canonicalisation for AppleAgxWddm's bounded submit read set.
 * Keep this free of Mesa, Windows and KMD headers so the exact queue-side
 * range logic is covered by native host tests.
 */

#ifndef D12_READ_SET_CORE_H
#define D12_READ_SET_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct d12_read_range {
   uint64_t gpu_va;
   uint64_t length;
};

_Static_assert(sizeof(struct d12_read_range) == 16,
               "AGX read-range wire width");

enum d12_read_set_result {
   D12_READ_SET_OK = 0,
   D12_READ_SET_INVALID = -1,
   D12_READ_SET_NOSPACE = -2,
};

/* Insert one non-empty range into a sorted, overlap-free vector. Overlap and
 * direct adjacency are merged transitively. The input vector is unchanged
 * when capacity is exhausted or validation fails. */
enum d12_read_set_result
d12_read_set_add(struct d12_read_range *ranges, uint32_t *count,
                 uint32_t capacity, uint64_t gpu_va, uint64_t length);

/* Add a byte range within one allocation. If the bounded vector is full,
 * retry with the complete allocation so existing fragments of that same BO
 * collapse before declaring the exact set incomplete. */
enum d12_read_set_result
d12_read_set_add_allocation(struct d12_read_range *ranges, uint32_t *count,
                            uint32_t capacity, uint64_t allocation_va,
                            uint64_t allocation_size, uint64_t offset,
                            uint64_t length);

#ifdef __cplusplus
}
#endif

#endif
