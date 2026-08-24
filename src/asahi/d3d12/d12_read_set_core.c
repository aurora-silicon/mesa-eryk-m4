/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_read_set_core.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

enum d12_read_set_result
d12_read_set_add(struct d12_read_range *ranges, uint32_t *count,
                 uint32_t capacity, uint64_t gpu_va, uint64_t length)
{
   if (!ranges || !count || *count > capacity || !gpu_va || !length ||
       gpu_va > UINT64_MAX - length)
      return D12_READ_SET_INVALID;

   uint64_t previous_end = 0;
   for (uint32_t i = 0; i < *count; ++i) {
      const struct d12_read_range old = ranges[i];
      if (!old.gpu_va || !old.length ||
          old.gpu_va > UINT64_MAX - old.length)
         return D12_READ_SET_INVALID;

      const uint64_t old_end = old.gpu_va + old.length;
      if (i && old.gpu_va <= previous_end)
         return D12_READ_SET_INVALID;
      previous_end = old_end;
   }

   uint64_t begin = gpu_va;
   uint64_t end = gpu_va + length;
   uint32_t first = 0;
   while (first < *count) {
      const uint64_t old_end =
         ranges[first].gpu_va + ranges[first].length;
      if (old_end >= begin)
         break;
      ++first;
   }

   uint32_t last = first;
   while (last < *count && ranges[last].gpu_va <= end) {
      if (ranges[last].gpu_va < begin)
         begin = ranges[last].gpu_va;
      const uint64_t old_end = ranges[last].gpu_va + ranges[last].length;
      if (old_end > end)
         end = old_end;
      ++last;
   }

   if (first == last && *count == capacity)
      return D12_READ_SET_NOSPACE;

   const uint32_t removed = last - first;
   const uint32_t tail = *count - last;
   if (removed == 0) {
      memmove(&ranges[first + 1], &ranges[first],
              (size_t)tail * sizeof(ranges[0]));
      ++*count;
   } else {
      memmove(&ranges[first + 1], &ranges[last],
              (size_t)tail * sizeof(ranges[0]));
      *count = *count - removed + 1;
   }

   ranges[first] = (struct d12_read_range){
      .gpu_va = begin,
      .length = end - begin,
   };
   return D12_READ_SET_OK;
}

enum d12_read_set_result
d12_read_set_add_allocation(struct d12_read_range *ranges, uint32_t *count,
                            uint32_t capacity, uint64_t allocation_va,
                            uint64_t allocation_size, uint64_t offset,
                            uint64_t length)
{
   if (!allocation_va || !allocation_size || !length ||
       offset >= allocation_size || length > allocation_size - offset ||
       allocation_va > UINT64_MAX - offset)
      return D12_READ_SET_INVALID;

   enum d12_read_set_result result =
      d12_read_set_add(ranges, count, capacity, allocation_va + offset,
                       length);
   if (result != D12_READ_SET_NOSPACE)
      return result;

   return d12_read_set_add(ranges, count, capacity, allocation_va,
                           allocation_size);
}
