/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#ifndef D12_UMD_RESIDENCY_H
#define D12_UMD_RESIDENCY_H

#include "d12_umd_residency_core.h"

#include <windows.h>
#include <d3dkmthk.h>

typedef bool (*d12_umd_residency_resolver)(void *context, UINT object_index,
                                           D3DKMT_HANDLE *allocation);

struct d12_umd_residency_translation {
   struct d12_umd_residency_set set;
   void *spill;
   D3DKMT_HANDLE stack_allocations[16];
   UINT stack_object_allocation_indices[16];
   UINT stack_hash_slots[32];
};

HRESULT d12_umd_residency_translate(
   UINT object_count, d12_umd_residency_resolver resolver, void *context,
   struct d12_umd_residency_translation *translation);

void d12_umd_residency_translation_cleanup(
   struct d12_umd_residency_translation *translation);

#endif
