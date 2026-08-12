/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Resource-level private data preserved by dxgkrnl between
 * CreateHeapAndResource and OpenHeapAndResource.  The AppleAgxWddm prefix is
 * intentionally first so the KMD can validate its existing contract without
 * knowing anything about the D3D12 frontend tail.
 */
#pragma once

#include "../d12_frontend.h"
#include "agxw_contract.h"

#include <stdbool.h>
#include <stdint.h>

#define D12_UMD_RESOURCE_PRIVATE_MAGIC UINT32_C(0x52583241) /* 'A2XR' */
#define D12_UMD_RESOURCE_PRIVATE_VERSION 1u

enum d12_umd_resource_private_flags {
   D12_UMD_RESOURCE_PRIVATE_HAS_RESOURCE = 1u << 0,
   D12_UMD_RESOURCE_PRIVATE_HAS_CLEAR_VALUE = 1u << 1,
   D12_UMD_RESOURCE_PRIVATE_HAS_ROW_MAJOR_LAYOUT = 1u << 2,
};

struct d12_umd_resource_private {
   struct agxw_resource_private kmd;
   uint32_t magic;
   uint32_t version;
   uint32_t struct_size;
   uint32_t flags;
   struct d12_frontend_heap_desc heap;
   struct d12_frontend_resource_desc resource;
   struct d12_frontend_clear_value clear_value;
   uint32_t row_pitch;
   uint32_t slice_pitch;
   uint64_t reserved[2];
};

bool d12_umd_resource_private_pack(
   const struct d12_frontend_heap_desc *heap,
   const struct d12_frontend_resource_desc *resource,
   const struct d12_frontend_clear_value *clear_value, uint32_t row_pitch,
   uint32_t slice_pitch, struct d12_umd_resource_private *data);

bool d12_umd_resource_private_parse(
   const void *blob, uint32_t blob_size,
   struct d12_umd_resource_private *data);
