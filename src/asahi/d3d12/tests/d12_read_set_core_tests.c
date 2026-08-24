/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_read_set_core.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

static void
require_result(int condition, const char *message)
{
   ++checks;
   if (!condition) {
      fprintf(stderr, "FAIL: %s\n", message);
      exit(1);
   }
}

static void
require_range(const struct d12_read_range *ranges, uint32_t index,
              uint64_t gpu_va, uint64_t length, const char *message)
{
   require_result(ranges[index].gpu_va == gpu_va &&
                     ranges[index].length == length,
                  message);
}

int
main(void)
{
   struct d12_read_range ranges[4] = {0};
   uint32_t count = 0;

   require_result(d12_read_set_add(ranges, &count, 4, 0x3000, 0x1000) ==
                     D12_READ_SET_OK && count == 1,
                  "first range inserts");
   require_result(d12_read_set_add(ranges, &count, 4, 0x1000, 0x1000) ==
                     D12_READ_SET_OK && count == 2,
                  "earlier disjoint range sorts");
   require_result(d12_read_set_add(ranges, &count, 4, 0x2000, 0x1000) ==
                     D12_READ_SET_OK && count == 1,
                  "bridge merges adjacent ranges transitively");
   require_range(ranges, 0, 0x1000, 0x3000, "transitive union bytes");

   require_result(d12_read_set_add(ranges, &count, 4, 0x1800, 0x200) ==
                     D12_READ_SET_OK && count == 1,
                  "contained range deduplicates");
   require_result(d12_read_set_add(ranges, &count, 4, 0x800, 0x1000) ==
                     D12_READ_SET_OK && count == 1,
                  "partial overlap expands lower bound");
   require_result(d12_read_set_add(ranges, &count, 4, 0x4000, 0x800) ==
                     D12_READ_SET_OK && count == 1,
                  "upper adjacency merges");
   require_range(ranges, 0, 0x800, 0x4000, "merged range bytes");

   struct d12_read_range full[2] = {
      {.gpu_va = 0x1000, .length = 0x100},
      {.gpu_va = 0x2000, .length = 0x100},
   };
   uint32_t full_count = 2;
   struct d12_read_range snapshot[2];
   memcpy(snapshot, full, sizeof(snapshot));
   require_result(d12_read_set_add(full, &full_count, 2, 0x3000, 0x100) ==
                     D12_READ_SET_NOSPACE && full_count == 2 &&
                     memcmp(snapshot, full, sizeof(snapshot)) == 0,
                  "capacity failure is transactional");
   require_result(d12_read_set_add(full, &full_count, 2, 0x1100, 0xf00) ==
                     D12_READ_SET_OK && full_count == 1,
                  "merge succeeds at capacity");
   require_range(full, 0, 0x1000, 0x1100, "capacity merge union bytes");

   struct d12_read_range allocation_fragments[3] = {
      {.gpu_va = 0x10000, .length = 0x1000},
      {.gpu_va = 0x14000, .length = 0x1000},
      {.gpu_va = 0x30000, .length = 0x1000},
   };
   uint32_t allocation_fragment_count = 3;
   require_result(d12_read_set_add_allocation(
                     allocation_fragments, &allocation_fragment_count, 3,
                     0x10000, 0x10000, 0x8000, 0x1000) == D12_READ_SET_OK &&
                     allocation_fragment_count == 2,
                  "full vector widens one fragmented allocation");
   require_range(allocation_fragments, 0, 0x10000, 0x10000,
                 "widened allocation replaces its tile fragments");
   require_range(allocation_fragments, 1, 0x30000, 0x1000,
                 "widening preserves unrelated allocation");

   struct d12_read_range no_room[2] = {
      {.gpu_va = 0x30000, .length = 0x1000},
      {.gpu_va = 0x50000, .length = 0x1000},
   };
   uint32_t no_room_count = 2;
   require_result(d12_read_set_add_allocation(
                     no_room, &no_room_count, 2, 0x10000, 0x10000,
                     0x8000, 0x1000) == D12_READ_SET_NOSPACE &&
                     no_room_count == 2,
                  "new allocation still fails when widening cannot fit");
   require_result(d12_read_set_add_allocation(
                     no_room, &no_room_count, 2, 0x10000, 0x10000,
                     0x10000, 1) == D12_READ_SET_INVALID,
                  "allocation-relative offset at end is rejected");
   require_result(d12_read_set_add_allocation(
                     no_room, &no_room_count, 2, 0x10000, 0x10000,
                     0xf000, 0x1001) == D12_READ_SET_INVALID,
                  "allocation-relative range beyond end is rejected");

   uint32_t invalid_count = 0;
   require_result(d12_read_set_add(NULL, &invalid_count, 1, 1, 1) ==
                     D12_READ_SET_INVALID,
                  "null vector rejected");
   require_result(d12_read_set_add(ranges, NULL, 4, 1, 1) ==
                     D12_READ_SET_INVALID,
                  "null count rejected");
   require_result(d12_read_set_add(ranges, &invalid_count, 4, 0, 1) ==
                     D12_READ_SET_INVALID,
                  "null GPU address rejected");
   require_result(d12_read_set_add(ranges, &invalid_count, 4, 1, 0) ==
                     D12_READ_SET_INVALID,
                  "empty range rejected");
   require_result(d12_read_set_add(ranges, &invalid_count, 4,
                                   UINT64_MAX, 1) == D12_READ_SET_INVALID,
                  "overflowing range rejected");

   struct d12_read_range malformed[2] = {
      {.gpu_va = 0x2000, .length = 0x100},
      {.gpu_va = 0x1000, .length = 0x100},
   };
   uint32_t malformed_count = 2;
   require_result(d12_read_set_add(malformed, &malformed_count, 2,
                                   0x4000, 0x100) ==
                     D12_READ_SET_INVALID && malformed_count == 2,
                  "unordered input vector fails closed");

   puts("d12_read_set_core_tests: PASS");
   printf("d12_read_set_core_tests: %u checks passed\n", checks);
   return 0;
}
