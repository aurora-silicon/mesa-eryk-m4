/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Portable canonicalisation for the bounded AGX firmware write set.  Keep
 * this free of Mesa and Windows headers so the exact queue-side range logic
 * is covered by host tests.
 */

#ifndef D12_WRITE_SET_CORE_H
#define D12_WRITE_SET_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct d12_write_range {
   uint64_t pointer;
   uint64_t size;
   uint32_t pad;
   uint32_t flags;
};

_Static_assert(sizeof(struct d12_write_range) == 24,
               "AGX attachment wire width");

enum d12_write_set_result {
   D12_WRITE_SET_OK = 0,
   D12_WRITE_SET_INVALID = -1,
   D12_WRITE_SET_NOSPACE = -2,
};

/* Insert one non-empty range into a sorted, overlap-free vector.  Overlap and
 * direct adjacency are merged transitively.  The input vector is unchanged
 * when capacity is exhausted or validation fails. */
enum d12_write_set_result
d12_write_set_add(struct d12_write_range *ranges, unsigned *count,
                  unsigned capacity, uint64_t pointer, uint64_t size);

enum d12_write_set_result
d12_write_set_add_allocation(struct d12_write_range *ranges, unsigned *count,
                             unsigned capacity, uint64_t allocation_va,
                             uint64_t allocation_size, uint64_t offset,
                             uint64_t size);

#ifdef __cplusplus
}
#endif

#endif
