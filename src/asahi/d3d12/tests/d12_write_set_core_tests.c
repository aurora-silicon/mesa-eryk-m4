/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_write_set_core.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

static void
require_result(int condition, const char *message)
{
   checks++;
   if (!condition) {
      fprintf(stderr, "FAIL: %s\n", message);
      exit(1);
   }
}

static void
require_range(const struct d12_write_range *ranges, unsigned index,
              uint64_t pointer, uint64_t size, const char *message)
{
   require_result(ranges[index].pointer == pointer &&
                     ranges[index].size == size,
                  message);
}

int
main(void)
{
   struct d12_write_range ranges[4] = {0};
   unsigned count = 0;

   require_result(d12_write_set_add(ranges, &count, 4, 0x3000, 0x1000) ==
                     D12_WRITE_SET_OK && count == 1,
                  "first range inserts");
   require_range(ranges, 0, 0x3000, 0x1000, "first range bytes");

   require_result(d12_write_set_add(ranges, &count, 4, 0x1000, 0x1000) ==
                     D12_WRITE_SET_OK && count == 2,
                  "disjoint earlier range inserts in sorted order");
   require_range(ranges, 0, 0x1000, 0x1000, "sorted earlier range");
   require_range(ranges, 1, 0x3000, 0x1000, "sorted later range");

   require_result(d12_write_set_add(ranges, &count, 4, 0x2000, 0x1000) ==
                     D12_WRITE_SET_OK && count == 1,
                  "bridge merges both adjacent ranges transitively");
   require_range(ranges, 0, 0x1000, 0x3000, "transitive union bytes");

   require_result(d12_write_set_add(ranges, &count, 4, 0x1800, 0x200) ==
                     D12_WRITE_SET_OK && count == 1,
                  "contained range deduplicates");
   require_range(ranges, 0, 0x1000, 0x3000, "containment preserves union");

   require_result(d12_write_set_add(ranges, &count, 4, 0x800, 0x1000) ==
                     D12_WRITE_SET_OK && count == 1,
                  "partial overlap expands lower bound");
   require_range(ranges, 0, 0x800, 0x3800, "partial overlap union bytes");

   require_result(d12_write_set_add(ranges, &count, 4, 0x4000, 0x800) ==
                     D12_WRITE_SET_OK && count == 1,
                  "upper adjacency merges");
   require_range(ranges, 0, 0x800, 0x4000, "upper adjacency union bytes");

   struct d12_write_range full[2] = {
      {.pointer = 0x1000, .size = 0x100},
      {.pointer = 0x2000, .size = 0x100},
   };
   unsigned full_count = 2;
   struct d12_write_range snapshot[2];
   memcpy(snapshot, full, sizeof(snapshot));
   require_result(d12_write_set_add(full, &full_count, 2, 0x3000, 0x100) ==
                     D12_WRITE_SET_NOSPACE && full_count == 2 &&
                     memcmp(snapshot, full, sizeof(snapshot)) == 0,
                  "capacity failure is transactional");
   require_result(d12_write_set_add(full, &full_count, 2, 0x1100, 0xf00) ==
                     D12_WRITE_SET_OK && full_count == 1,
                  "merge succeeds at capacity");
   require_range(full, 0, 0x1000, 0x1100, "capacity merge union bytes");

   unsigned invalid_count = 0;
   require_result(d12_write_set_add(NULL, &invalid_count, 1, 1, 1) ==
                     D12_WRITE_SET_INVALID,
                  "null vector rejected");
   require_result(d12_write_set_add(ranges, NULL, 4, 1, 1) ==
                     D12_WRITE_SET_INVALID,
                  "null count rejected");
   require_result(d12_write_set_add(ranges, &invalid_count, 4, 0, 1) ==
                     D12_WRITE_SET_INVALID,
                  "null GPU address rejected");
   require_result(d12_write_set_add(ranges, &invalid_count, 4, 1, 0) ==
                     D12_WRITE_SET_INVALID,
                  "empty range rejected");
   require_result(d12_write_set_add(ranges, &invalid_count, 4,
                                    UINT64_MAX, 1) == D12_WRITE_SET_INVALID,
                  "overflowing range rejected");

   struct d12_write_range malformed[2] = {
      {.pointer = 0x2000, .size = 0x100},
      {.pointer = 0x1000, .size = 0x100},
   };
   unsigned malformed_count = 2;
   require_result(d12_write_set_add(malformed, &malformed_count, 2,
                                    0x4000, 0x100) ==
                     D12_WRITE_SET_INVALID && malformed_count == 2,
                  "unordered input vector fails closed");

   malformed[0] = (struct d12_write_range){
      .pointer = 0x1000,
      .size = 0x100,
      .flags = 1,
   };
   malformed_count = 1;
   require_result(d12_write_set_add(malformed, &malformed_count, 2,
                                    0x4000, 0x100) ==
                     D12_WRITE_SET_INVALID,
                  "non-MBZ UAPI tail fails closed");

   puts("d12_write_set_core_tests: PASS");
   printf("d12_write_set_core_tests: %u checks passed\n", checks);
   return 0;
}
