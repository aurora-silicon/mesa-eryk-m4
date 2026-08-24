/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_vdm_decode_core.h"

#include "agx_vdm_barrier_core.h"
#include "agx_vdm_ppp_state_core.h"
#include "agx_vdm_stream_link_core.h"
#include "agx_vdm_stream_terminate_core.h"
#include "agx_vdm_tessellate_core.h"

#include <string.h>

static uint32_t
agx_vdm_decode_load_le32(const uint8_t *src)
{
   return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
          ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static bool
agx_vdm_decode_is_hal300(enum agx_chip chip)
{
   return chip == AGX_CHIP_G17G || chip == AGX_CHIP_G17S ||
          chip == AGX_CHIP_G17C;
}

bool
agx_vdm_decode_one(enum agx_chip chip, const void *bytes, size_t available,
                   struct agx_vdm_decoded_record *record)
{
   if (record)
      memset(record, 0, sizeof(*record));

   if (!bytes || !record || available < sizeof(uint32_t))
      return false;

   const uint32_t word0 = agx_vdm_decode_load_le32(bytes);
   const uint8_t block_type = word0 >> 29;

   switch (block_type) {
   case 0: {
      uint64_t pointer;
      uint32_t size_words;
      if (available < AGX_VDM_PPP_STATE_CORE_LENGTH ||
          !agx_vdm_ppp_state_core_unpack(
             chip, bytes, AGX_VDM_PPP_STATE_CORE_LENGTH, &pointer, &size_words))
         return false;

      record->kind = AGX_VDM_RECORD_PPP_STATE_UPDATE;
      record->length = AGX_VDM_PPP_STATE_CORE_LENGTH;
      record->chip = chip;
      record->data.ppp_state_update.pointer = pointer;
      record->data.ppp_state_update.size_words = size_words;
      return true;
   }

   case 1:
      if (available < AGX_VDM_BARRIER_CORE_LENGTH ||
          !agx_vdm_barrier_core_unpack(chip, bytes,
                                       AGX_VDM_BARRIER_CORE_LENGTH))
         return false;

      record->kind = AGX_VDM_RECORD_BARRIER;
      record->length = AGX_VDM_BARRIER_CORE_LENGTH;
      record->chip = chip;
      return true;

   case 2:
      if (available < AGX_VDM_STATE_UPDATE_LENGTH ||
          !agx_vdm_state_update_unpack(chip, bytes, AGX_VDM_STATE_UPDATE_LENGTH,
                                       &record->data.state_update))
         return false;

      record->kind = AGX_VDM_RECORD_STATE_UPDATE;
      record->length = AGX_VDM_STATE_UPDATE_LENGTH;
      record->chip = chip;
      return true;

   case 3: {
      const bool hal300 = agx_vdm_decode_is_hal300(chip);
      const size_t indexed_length = hal300 ? 32 : 28;
      const size_t nonindexed_length = hal300 ? 20 : 16;

      if (available >= indexed_length &&
          agx_vdm_index_list_direct_indexed_unpack(
             chip, bytes, indexed_length, &record->data.index_list_indexed)) {
         record->kind = AGX_VDM_RECORD_INDEX_LIST_INDEXED;
         record->length = (uint8_t)indexed_length;
         record->chip = chip;
         return true;
      }

      if (available >= nonindexed_length &&
          agx_vdm_index_list_direct_nonindexed_unpack(
             chip, bytes, nonindexed_length,
             &record->data.index_list_nonindexed)) {
         record->kind = AGX_VDM_RECORD_INDEX_LIST_NONINDEXED;
         record->length = (uint8_t)nonindexed_length;
         record->chip = chip;
         return true;
      }

      memset(record, 0, sizeof(*record));
      return false;
   }

   case 4: {
      uint64_t target;
      if (available < AGX_VDM_STREAM_LINK_CORE_LENGTH ||
          !agx_vdm_stream_link_core_unpack(
             chip, bytes, AGX_VDM_STREAM_LINK_CORE_LENGTH, &target))
         return false;

      record->kind = AGX_VDM_RECORD_STREAM_LINK;
      record->length = AGX_VDM_STREAM_LINK_CORE_LENGTH;
      record->chip = chip;
      record->data.stream_link.target = target;
      return true;
   }

   case 5:
      if (available < AGX_VDM_TESSELLATE_DIRECT_LENGTH ||
          !agx_vdm_tessellate_direct_unpack(
             chip, bytes, AGX_VDM_TESSELLATE_DIRECT_LENGTH,
             &record->data.tessellate_direct))
         return false;

      record->kind = AGX_VDM_RECORD_TESSELLATE_DIRECT;
      record->length = AGX_VDM_TESSELLATE_DIRECT_LENGTH;
      record->chip = chip;
      return true;

   case 6:
      if (available < AGX_VDM_STREAM_TERMINATE_CORE_LENGTH ||
          !agx_vdm_stream_terminate_core_unpack(
             chip, bytes, AGX_VDM_STREAM_TERMINATE_CORE_LENGTH))
         return false;

      record->kind = AGX_VDM_RECORD_STREAM_TERMINATE;
      record->length = AGX_VDM_STREAM_TERMINATE_CORE_LENGTH;
      record->chip = chip;
      return true;

   case 7:
   default:
      return false;
   }
}
