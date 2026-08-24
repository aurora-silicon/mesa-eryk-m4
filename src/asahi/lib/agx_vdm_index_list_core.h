/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Bounded, target-aware AGX3 direct VDM Index List representations.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_VDM_INDEX_LIST_DIRECT_NONINDEXED_MAX_LENGTH 20u
#define AGX_VDM_INDEX_LIST_DIRECT_INDEXED_MAX_LENGTH 32u

enum agx_vdm_index_list_index_size {
   AGX_VDM_INDEX_LIST_INDEX_SIZE_U16 = 1,
   AGX_VDM_INDEX_LIST_INDEX_SIZE_U32 = 2,
};

struct agx_vdm_index_list_direct_nonindexed_info {
   uint8_t primitive;
   uint32_t count;
   uint32_t instances;
   uint32_t start;
};

struct agx_vdm_index_list_direct_indexed_info {
   uint8_t primitive;
   bool restart_enable;
   enum agx_vdm_index_list_index_size index_size;
   uint64_t index_buffer;
   uint32_t count;
   uint32_t instances;
   uint32_t start;

   /*
    * Preserved exactly as emitted after Apple's range computation.  Although
    * this word is dword-scaled, its complete byte-range semantics are not
    * independently grounded for every API edge case, so this helper does not
    * invent a byte-size conversion.
    */
   uint32_t index_buffer_size_word;
};

struct agx_vdm_index_list_direct_nonindexed {
   uint8_t bytes[AGX_VDM_INDEX_LIST_DIRECT_NONINDEXED_MAX_LENGTH];
   uint8_t length;
};

struct agx_vdm_index_list_direct_indexed {
   uint8_t bytes[AGX_VDM_INDEX_LIST_DIRECT_INDEXED_MAX_LENGTH];
   uint8_t length;
};

/*
 * These helpers cover only the exact direct Index List records recovered from
 * Apple's G15, HAL200, and HAL300 VDM encoders.  HAL300 has header bit 27 set
 * and carries an additional opaque word whose only witnessed value is 7.
 * Indexed AGX3 records split a 48-bit index-buffer address across header[7:0],
 * buffer-lo, and an additional zero-extended VA[47:40] word.
 *
 * The primitive values accepted here are exactly the ten entries present in
 * all inspected direct-draw tables.  Only U16 and U32 indexed forms are
 * represented because those are the two forms selected by the inspected
 * public Metal paths.
 *
 * This is a host representation/validation surface.  It does not emit into a
 * live VDM stream or enable scheduling, submission, or device admission.
 */
bool agx_vdm_index_list_direct_target_supported(enum agx_chip chip);

bool agx_vdm_index_list_direct_nonindexed_pack(
   enum agx_chip chip,
   const struct agx_vdm_index_list_direct_nonindexed_info *info,
   struct agx_vdm_index_list_direct_nonindexed *out);

bool agx_vdm_index_list_direct_nonindexed_unpack(
   enum agx_chip chip, const void *bytes, size_t size,
   struct agx_vdm_index_list_direct_nonindexed_info *info);

bool agx_vdm_index_list_direct_indexed_pack(
   enum agx_chip chip,
   const struct agx_vdm_index_list_direct_indexed_info *info,
   struct agx_vdm_index_list_direct_indexed *out);

bool agx_vdm_index_list_direct_indexed_unpack(
   enum agx_chip chip, const void *bytes, size_t size,
   struct agx_vdm_index_list_direct_indexed_info *info);

#ifdef __cplusplus
}
#endif
