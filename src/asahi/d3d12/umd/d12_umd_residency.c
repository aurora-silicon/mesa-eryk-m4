/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_residency.h"

#include <stdint.h>

_Static_assert(sizeof(D3DKMT_HANDLE) == sizeof(uint32_t),
               "D3DKMT allocation handles must remain 32-bit");

void
d12_umd_residency_translation_cleanup(
   struct d12_umd_residency_translation *translation)
{
   if (!translation)
      return;
   HeapFree(GetProcessHeap(), 0, translation->spill);
   translation->set = (struct d12_umd_residency_set){0};
   translation->spill = NULL;
}

HRESULT
d12_umd_residency_translate(
   UINT object_count, d12_umd_residency_resolver resolver, void *context,
   struct d12_umd_residency_translation *translation)
{
   size_t allocation_bytes;
   size_t object_index_bytes;
   size_t hash_bytes;
   size_t spill_bytes;
   const size_t hash_capacity =
      d12_umd_residency_hash_capacity(object_count);
   uint32_t *allocations;
   uint32_t *object_allocation_indices;
   uint32_t *hash_slots;

   if (!translation || (object_count && !resolver))
      return E_INVALIDARG;
   *translation = (struct d12_umd_residency_translation){0};
   if (!hash_capacity)
      return E_OUTOFMEMORY;

   allocations = translation->stack_allocations;
   object_allocation_indices =
      translation->stack_object_allocation_indices;
   hash_slots = translation->stack_hash_slots;
   if (object_count > ARRAYSIZE(translation->stack_allocations)) {
      allocation_bytes = (size_t)object_count * sizeof(*allocations);
      object_index_bytes =
         (size_t)object_count * sizeof(*object_allocation_indices);
      hash_bytes = hash_capacity * sizeof(*hash_slots);
      if (allocation_bytes > SIZE_MAX - object_index_bytes ||
          allocation_bytes + object_index_bytes > SIZE_MAX - hash_bytes)
         return E_OUTOFMEMORY;
      spill_bytes = allocation_bytes + object_index_bytes + hash_bytes;
      translation->spill = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                     spill_bytes);
      if (!translation->spill)
         return E_OUTOFMEMORY;
      allocations = translation->spill;
      object_allocation_indices =
         (UINT *)((uint8_t *)translation->spill + allocation_bytes);
      hash_slots =
         (UINT *)((uint8_t *)object_allocation_indices +
                  object_index_bytes);
   }

   if (!d12_umd_residency_set_init(
          &translation->set, allocations, object_allocation_indices,
          object_count, hash_slots, hash_capacity)) {
      d12_umd_residency_translation_cleanup(translation);
      return E_FAIL;
   }

   for (UINT index = 0; index < object_count; ++index) {
      D3DKMT_HANDLE allocation;

      if (!resolver(context, index, &allocation)) {
         d12_umd_residency_translation_cleanup(translation);
         return E_INVALIDARG;
      }
      if (!d12_umd_residency_set_add(&translation->set, index, allocation)) {
         d12_umd_residency_translation_cleanup(translation);
         return E_FAIL;
      }
   }
   return S_OK;
}
