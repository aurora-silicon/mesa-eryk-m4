/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_residency_core.h"

#include <string.h>

bool
d12_umd_residency_set_init(
   struct d12_umd_residency_set *set, uint32_t *allocations,
   uint32_t *object_allocation_indices, uint32_t object_count,
   uint32_t *hash_slots, size_t hash_capacity)
{
   if (!set || !hash_slots || !hash_capacity ||
       (hash_capacity & (hash_capacity - 1)) ||
       hash_capacity > SIZE_MAX / sizeof(*hash_slots) ||
       hash_capacity <= object_count ||
       (object_count && (!allocations || !object_allocation_indices)))
      return false;

   *set = (struct d12_umd_residency_set){
      .allocations = allocations,
      .object_allocation_indices = object_allocation_indices,
      .hash_slots = hash_slots,
      .object_count = object_count,
      .hash_capacity = hash_capacity,
   };
   memset(hash_slots, 0, hash_capacity * sizeof(*hash_slots));
   for (uint32_t index = 0; index < object_count; ++index)
      object_allocation_indices[index] = D12_UMD_RESIDENCY_NO_ALLOCATION;
   return true;
}

bool
d12_umd_residency_set_add(struct d12_umd_residency_set *set,
                          uint32_t object_index, uint32_t allocation)
{
   size_t slot;
   const size_t mask = set ? set->hash_capacity - 1 : 0;

   if (!set || object_index >= set->object_count || !set->hash_capacity)
      return false;
   if (!allocation)
      return true;

   slot = ((size_t)allocation * UINT32_C(2654435761)) & mask;
   for (size_t probe = 0; probe < set->hash_capacity; ++probe) {
      if (!set->hash_slots[slot]) {
         uint32_t index;

         if (set->allocation_count >= set->object_count)
            return false;
         index = set->allocation_count++;
         set->allocations[index] = allocation;
         set->hash_slots[slot] = index + 1;
         set->object_allocation_indices[object_index] = index;
         return true;
      }

      const uint32_t index = set->hash_slots[slot] - 1;
      if (set->allocations[index] == allocation) {
         set->object_allocation_indices[object_index] = index;
         return true;
      }
      slot = (slot + 1) & mask;
   }
   return false;
}
