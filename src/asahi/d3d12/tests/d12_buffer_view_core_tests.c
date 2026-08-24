/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_buffer_view_core.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int
main(void)
{
   uint64_t offset = 0, size = 0;

   assert(d12_buffer_view_element_count_valid(0));
   assert(d12_buffer_view_element_count_valid(
      D12_BUFFER_VIEW_MAX_ELEMENTS));
   assert(!d12_buffer_view_element_count_valid(
      D12_BUFFER_VIEW_MAX_ELEMENTS + 1));
   assert(!d12_buffer_view_element_count_valid(UINT32_MAX));

   const uint64_t max_structured_size =
      d12_buffer_view_size_B(D12_BUFFER_VIEW_MAX_ELEMENTS, 64);
   assert(max_structured_size == UINT64_C(0x200000000));
   assert(max_structured_size > UINT32_MAX);
   assert(d12_buffer_view_range(max_structured_size, 0,
                                D12_BUFFER_VIEW_MAX_ELEMENTS, 64,
                                &offset, &size));
   assert(offset == 0);
   assert(size == max_structured_size);

   assert(d12_buffer_view_range(UINT64_C(0x100000080), 0,
                                UINT32_C(0x02000001), 128, &offset, &size));
   assert(offset == 0);
   assert(size == UINT64_C(0x100000080));
   assert(size > UINT32_MAX);
   assert(d12_buffer_view_size_B(UINT32_C(0x02000001), 128) == size);

   assert(d12_buffer_view_range(UINT64_C(0x200000100), UINT64_C(0x01000000),
                                UINT32_C(0x01000001), 128, &offset, &size));
   assert(offset == UINT64_C(0x80000000));
   assert(size == UINT64_C(0x80000080));

   assert(!d12_buffer_view_range(UINT64_MAX, UINT64_MAX, 1, 2,
                                 &offset, &size));
   assert(!d12_buffer_view_range(UINT64_C(0x100000000), 0,
                                 UINT32_C(0x02000001), 128, &offset, &size));
   assert(!d12_buffer_view_range(4096, 0, 1, 0, &offset, &size));

   assert(d12_buffer_access_in_bounds(8, 0, 8));
   assert(!d12_buffer_access_in_bounds(8, 4, 8));
   assert(!d12_buffer_access_in_bounds(8, 8, 1));
   assert(!d12_buffer_access_in_bounds(8, 0, 0));
   assert(d12_buffer_access_in_bounds(UINT64_C(0x100000008),
                                      UINT64_C(0x100000000), 8));
   assert(!d12_buffer_access_in_bounds(UINT64_C(0x100000008),
                                       UINT64_C(0x100000004), 8));
   assert(!d12_buffer_access_in_bounds(UINT64_MAX, UINT64_MAX - 3, 8));

   puts("d12_buffer_view_core_tests: PASS");
   return 0;
}
