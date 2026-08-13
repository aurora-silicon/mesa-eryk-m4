/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#ifndef D12_UMD_RESIDENCY_CORE_H
#define D12_UMD_RESIDENCY_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define D12_UMD_RESIDENCY_NO_ALLOCATION UINT32_MAX

struct d12_umd_residency_set {
   uint32_t *allocations;
   uint32_t *object_allocation_indices;
   uint32_t *hash_slots;
   uint32_t allocation_count;
   uint32_t object_count;
   size_t hash_capacity;
};

bool d12_umd_residency_set_init(
   struct d12_umd_residency_set *set, uint32_t *allocations,
   uint32_t *object_allocation_indices, uint32_t object_count,
   uint32_t *hash_slots, size_t hash_capacity);

bool d12_umd_residency_set_add(struct d12_umd_residency_set *set,
                               uint32_t object_index,
                               uint32_t allocation);

#endif
