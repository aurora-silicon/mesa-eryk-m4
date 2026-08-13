/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_umd_residency_core.h"

#include <assert.h>
#include <stdio.h>

static void
test_aliases_and_collisions(void)
{
   struct d12_umd_residency_set set;
   uint32_t allocations[17];
   uint32_t object_indices[17];
   uint32_t hash_slots[64];
   const uint32_t first = 1;
   const uint32_t collision = first + 64;

   assert(d12_umd_residency_set_init(
      &set, allocations, object_indices, 17, hash_slots, 64));
   assert(d12_umd_residency_set_add(&set, 0, 0));
   for (uint32_t index = 1; index < 16; ++index)
      assert(d12_umd_residency_set_add(&set, index, first));
   assert(d12_umd_residency_set_add(&set, 16, collision));

   assert(set.allocation_count == 2);
   assert(set.allocations[0] == first);
   assert(set.allocations[1] == collision);
   assert(object_indices[0] == D12_UMD_RESIDENCY_NO_ALLOCATION);
   for (uint32_t index = 1; index < 16; ++index)
      assert(object_indices[index] == 0);
   assert(object_indices[16] == 1);
}

static void
test_reclaim_fanout(void)
{
   struct d12_umd_residency_set set;
   uint32_t allocations[4];
   uint32_t object_indices[4];
   uint32_t hash_slots[8];
   const bool discarded[2] = {true, false};

   assert(d12_umd_residency_set_init(
      &set, allocations, object_indices, 4, hash_slots, 8));
   assert(d12_umd_residency_set_add(&set, 0, 0x100));
   assert(d12_umd_residency_set_add(&set, 1, 0x200));
   assert(d12_umd_residency_set_add(&set, 2, 0x100));
   assert(d12_umd_residency_set_add(&set, 3, 0));

   assert(discarded[object_indices[0]]);
   assert(!discarded[object_indices[1]]);
   assert(discarded[object_indices[2]]);
   assert(object_indices[3] == D12_UMD_RESIDENCY_NO_ALLOCATION);
}

static void
test_validation(void)
{
   struct d12_umd_residency_set set;
   uint32_t allocations[2];
   uint32_t object_indices[2];
   uint32_t hash_slots[4];

   assert(d12_umd_residency_hash_capacity(0) == 32);
   assert(d12_umd_residency_hash_capacity(16) == 32);
   assert(d12_umd_residency_hash_capacity(17) == 64);
   assert(!d12_umd_residency_set_init(
      NULL, allocations, object_indices, 2, hash_slots, 4));
   assert(!d12_umd_residency_set_init(
      &set, allocations, object_indices, 2, hash_slots, 3));
   assert(!d12_umd_residency_set_init(
      &set, allocations, object_indices, 2, hash_slots, 2));
   assert(d12_umd_residency_set_init(
      &set, allocations, object_indices, 2, hash_slots, 4));
   assert(!d12_umd_residency_set_add(&set, 2, 1));
}

int
main(void)
{
   test_aliases_and_collisions();
   test_reclaim_fanout();
   test_validation();
   puts("d12_umd_residency_core_tests: PASS");
   return 0;
}
