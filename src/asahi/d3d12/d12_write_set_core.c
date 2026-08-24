/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_write_set_core.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

enum d12_write_set_result
d12_write_set_add(struct d12_write_range *ranges, unsigned *count,
                  unsigned capacity, uint64_t pointer, uint64_t size)
{
   if (!ranges || !count || *count > capacity || !pointer || !size ||
       pointer > UINT64_MAX - size)
      return D12_WRITE_SET_INVALID;

   /* This helper owns the canonical representation.  Validate callers rather
    * than silently operating on a corrupt vector: malformed attachment data
    * must never be declared complete to the KMD. */
   uint64_t previous_end = 0;
   for (unsigned i = 0; i < *count; i++) {
      const struct d12_write_range old = ranges[i];
      if (!old.pointer || !old.size || old.pad || old.flags ||
          old.pointer > UINT64_MAX - old.size)
         return D12_WRITE_SET_INVALID;

      const uint64_t old_end = old.pointer + old.size;
      if (i && old.pointer <= previous_end)
         return D12_WRITE_SET_INVALID;
      previous_end = old_end;
   }

   uint64_t begin = pointer;
   uint64_t end = pointer + size;
   unsigned first = 0;
   while (first < *count) {
      const uint64_t old_end =
         ranges[first].pointer + ranges[first].size;
      if (old_end >= begin)
         break;
      first++;
   }

   unsigned last = first;
   while (last < *count && ranges[last].pointer <= end) {
      if (ranges[last].pointer < begin)
         begin = ranges[last].pointer;
      const uint64_t old_end = ranges[last].pointer + ranges[last].size;
      if (old_end > end)
         end = old_end;
      last++;
   }

   if (first == last && *count == capacity)
      return D12_WRITE_SET_NOSPACE;

   const unsigned removed = last - first;
   const unsigned tail = *count - last;
   if (removed == 0) {
      memmove(&ranges[first + 1], &ranges[first],
              tail * sizeof(ranges[0]));
      (*count)++;
   } else {
      memmove(&ranges[first + 1], &ranges[last],
              tail * sizeof(ranges[0]));
      *count = *count - removed + 1;
   }

   ranges[first] = (struct d12_write_range){
      .pointer = begin,
      .size = end - begin,
      .pad = 0,
      .flags = 0,
   };
   return D12_WRITE_SET_OK;
}

enum d12_write_set_result
d12_write_set_add_allocation(struct d12_write_range *ranges, unsigned *count,
                             unsigned capacity, uint64_t allocation_va,
                             uint64_t allocation_size, uint64_t offset,
                             uint64_t size)
{
   if (!allocation_va || !allocation_size || !size ||
       offset >= allocation_size || size > allocation_size - offset ||
       allocation_va > UINT64_MAX - offset)
      return D12_WRITE_SET_INVALID;

   enum d12_write_set_result result =
      d12_write_set_add(ranges, count, capacity, allocation_va + offset,
                        size);
   if (result != D12_WRITE_SET_NOSPACE)
      return result;

   return d12_write_set_add(ranges, count, capacity, allocation_va,
                            allocation_size);
}
