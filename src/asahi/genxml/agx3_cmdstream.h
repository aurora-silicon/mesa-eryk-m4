/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Exact, bounded AGX3 CDM command-stream helpers.
 *
 * These helpers cover only records whose byte layout is established by the
 * pinned Apple encoder corpus.  They do not enable device admission, queue
 * submission, execution, or any API capability.
 */

#pragma once

#include "agx_pack.h"
#include "../lib/agx_chip.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define AGX3_CDM_LAUNCH_SIZE 40u
#define AGX3_CDM_STREAM_LINK_SIZE 8u

struct agx3_cdm_launch_info {
   enum agx_chip chip;
   uint32_t word0_payload;
   uint32_t word1_payload;
   uint64_t pipeline;
   uint32_t opaque_word3;
   uint32_t global_size[3];
   uint32_t local_size[3];
};

struct agx3_cdm_stream_link_info {
   enum agx_chip chip;
   uint64_t target;
   bool with_return;
};

static inline void
agx3_cmdstream_store_le32(uint8_t *dst, uint32_t value)
{
   dst[0] = (uint8_t)value;
   dst[1] = (uint8_t)(value >> 8);
   dst[2] = (uint8_t)(value >> 16);
   dst[3] = (uint8_t)(value >> 24);
}

static inline uint32_t
agx3_cmdstream_load_le32(const uint8_t *src)
{
   return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
          ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

/* Exact launch emitters were inspected in the base and aggregate Mac plugin
 * classes listed here.  G16P and G17A stay closed because no corresponding
 * launch emitter was present in the pinned corpus. */
static inline bool
agx3_cdm_launch_target_grounded(enum agx_chip chip)
{
   switch (chip) {
   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15C:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G16C:
   case AGX_CHIP_G17P:
   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
   case AGX_CHIP_G17C:
      return true;
   default:
      return false;
   }
}

/* The pinned patchCDMControlStreamAndReset sites prove the 48-bit link form
 * in the aggregate G15X, G16X and G17X plugins.  Keep base/phone identities
 * closed until their exact emitter is independently pinned. */
static inline bool
agx3_cdm_stream_link_target_grounded(enum agx_chip chip)
{
   switch (chip) {
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15C:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G16C:
   case AGX_CHIP_G17S:
   case AGX_CHIP_G17C:
      return true;
   default:
      return false;
   }
}

static inline bool
agx3_cdm_launch_pack(const struct agx3_cdm_launch_info *info,
                     uint8_t packed[AGX3_CDM_LAUNCH_SIZE])
{
   if (!packed)
      return false;

   memset(packed, 0, AGX3_CDM_LAUNCH_SIZE);
   if (!info || !agx3_cdm_launch_target_grounded(info->chip) ||
       info->word0_payload >= (UINT32_C(1) << 29) ||
       info->word1_payload >= (UINT32_C(1) << 22) ||
       info->pipeline >= (UINT64_C(1) << 48) ||
       (info->pipeline & UINT64_C(0x3f)) != 0)
      return false;

   for (unsigned axis = 0; axis < 3; ++axis) {
      if (info->local_size[axis] > 0x7ff)
         return false;
   }

   struct AGX_CDM_LAUNCH_AGX3 header = {
      .word_0_payload = info->word0_payload,
      .word_1_payload = info->word1_payload,
      .pipeline_hi = info->pipeline >> 38,
      .pipeline_lo = (uint32_t)(info->pipeline >> 6),
      .opaque_word_3 = info->opaque_word3,
   };
   uint32_t header_words[AGX_CDM_LAUNCH_AGX3_LENGTH / 4] = {0};
   AGX_CDM_LAUNCH_AGX3_pack(header_words, &header);
   memcpy(packed, header_words, sizeof(header_words));

   for (unsigned axis = 0; axis < 3; ++axis) {
      agx3_cmdstream_store_le32(packed + 16 + axis * 4,
                                info->global_size[axis]);
      agx3_cmdstream_store_le32(packed + 28 + axis * 4,
                                info->local_size[axis]);
   }

   return true;
}

static inline bool
agx3_cdm_launch_unpack(enum agx_chip chip, const void *packed, size_t available,
                       struct agx3_cdm_launch_info *info)
{
   if (!info)
      return false;

   memset(info, 0, sizeof(*info));
   if (!packed || available < AGX3_CDM_LAUNCH_SIZE ||
       !agx3_cdm_launch_target_grounded(chip))
      return false;

   const uint8_t *bytes = (const uint8_t *)packed;
   const uint32_t word0 = agx3_cmdstream_load_le32(bytes);
   const uint32_t word1 = agx3_cmdstream_load_le32(bytes + 4);
   if ((word0 >> 29) != AGX_CDM_BLOCK_TYPE_LAUNCH)
      return false;

   info->chip = chip;
   info->word0_payload = word0 & ((UINT32_C(1) << 29) - 1);
   info->word1_payload = word1 & ((UINT32_C(1) << 22) - 1);
   info->pipeline = ((uint64_t)(word1 >> 22) << 38) |
                    ((uint64_t)agx3_cmdstream_load_le32(bytes + 8) << 6);
   info->opaque_word3 = agx3_cmdstream_load_le32(bytes + 12);

   for (unsigned axis = 0; axis < 3; ++axis) {
      info->global_size[axis] = agx3_cmdstream_load_le32(bytes + 16 + axis * 4);
      info->local_size[axis] = agx3_cmdstream_load_le32(bytes + 28 + axis * 4);
      if (info->local_size[axis] > 0x7ff) {
         memset(info, 0, sizeof(*info));
         return false;
      }
   }

   return true;
}

static inline bool
agx3_cdm_stream_link_pack(
   const struct agx3_cdm_stream_link_info *info,
   uint8_t packed[AGX3_CDM_STREAM_LINK_SIZE])
{
   if (!packed)
      return false;

   memset(packed, 0, AGX3_CDM_STREAM_LINK_SIZE);
   if (!info || !agx3_cdm_stream_link_target_grounded(info->chip) ||
       info->target >= (UINT64_C(1) << 48))
      return false;

   struct AGX_CDM_STREAM_LINK_AGX3 link = {
      .target_hi = info->target >> 32,
      .with_return = info->with_return,
      .target_lo = (uint32_t)info->target,
   };
   uint32_t words[AGX_CDM_STREAM_LINK_AGX3_LENGTH / 4] = {0};
   AGX_CDM_STREAM_LINK_AGX3_pack(words, &link);
   memcpy(packed, words, sizeof(words));
   return true;
}

static inline bool
agx3_cdm_stream_link_unpack(
   enum agx_chip chip, const void *packed, size_t available,
   struct agx3_cdm_stream_link_info *info)
{
   if (!info)
      return false;

   memset(info, 0, sizeof(*info));
   if (!packed || available < AGX3_CDM_STREAM_LINK_SIZE ||
       !agx3_cdm_stream_link_target_grounded(chip))
      return false;

   const uint8_t *bytes = (const uint8_t *)packed;
   const uint32_t word0 = agx3_cmdstream_load_le32(bytes);
   if ((word0 >> 29) != AGX_CDM_BLOCK_TYPE_STREAM_LINK ||
       (word0 & UINT32_C(0x0fff0000)) != 0)
      return false;

   info->chip = chip;
   info->target = ((uint64_t)(word0 & 0xffff) << 32) |
                  agx3_cmdstream_load_le32(bytes + 4);
   info->with_return = (word0 >> 28) & 1;
   return true;
}
