/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 bridge for bounded AGX3 direct VDM Index List records.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_DIRECT_NONINDEXED_INDEX_LIST_MAX_LENGTH 20u
#define D12_AGX3_DIRECT_INDEXED_INDEX_LIST_MAX_LENGTH 32u

enum d12_agx3_index_size {
   D12_AGX3_INDEX_SIZE_U16 = 1,
   D12_AGX3_INDEX_SIZE_U32 = 2,
};

struct d12_agx3_direct_nonindexed_index_list_info {
   uint8_t primitive;
   uint32_t count;
   uint32_t instances;
   uint32_t start;
};

struct d12_agx3_direct_indexed_index_list_info {
   uint8_t primitive;
   bool restart_enable;
   enum d12_agx3_index_size index_size;
   uint64_t index_buffer;
   uint32_t count;
   uint32_t instances;
   uint32_t start;
   uint32_t index_buffer_size_word;
};

struct d12_agx3_direct_nonindexed_index_list {
   uint8_t bytes[D12_AGX3_DIRECT_NONINDEXED_INDEX_LIST_MAX_LENGTH];
   uint8_t length;
};

struct d12_agx3_direct_indexed_index_list {
   uint8_t bytes[D12_AGX3_DIRECT_INDEXED_INDEX_LIST_MAX_LENGTH];
   uint8_t length;
};

/*
 * Research/validation bridge only.  These functions reuse the shared Mesa
 * packer and decoder but are not called by D3D draw lowering or command
 * submission and do not change AGX3 admission or D3D12 capabilities.
 */
bool d12_agx3_encode_direct_nonindexed_index_list(
   enum agx_chip chip,
   const struct d12_agx3_direct_nonindexed_index_list_info *info,
   struct d12_agx3_direct_nonindexed_index_list *out);

bool d12_agx3_decode_direct_nonindexed_index_list(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_direct_nonindexed_index_list_info *info);

bool d12_agx3_encode_direct_indexed_index_list(
   enum agx_chip chip,
   const struct d12_agx3_direct_indexed_index_list_info *info,
   struct d12_agx3_direct_indexed_index_list *out);

bool d12_agx3_decode_direct_indexed_index_list(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_direct_indexed_index_list_info *info);

#ifdef __cplusplus
}
#endif
