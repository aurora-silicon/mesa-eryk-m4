/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_vdm_tessellate_core.h"

#include <string.h>

#define AGX_VDM_TESSELLATE_DIRECT_FIXED UINT32_C(0xb2f00c00)
#define AGX_VDM_TESSELLATE_INSTANCE_STRIDE_MAX UINT32_C(0x7ffff)

static void
agx_vdm_tessellate_store_le32(uint8_t *dst, uint32_t value)
{
   dst[0] = (uint8_t)value;
   dst[1] = (uint8_t)(value >> 8);
   dst[2] = (uint8_t)(value >> 16);
   dst[3] = (uint8_t)(value >> 24);
}

static uint32_t
agx_vdm_tessellate_load_le32(const uint8_t *src)
{
   return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
          ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

bool
agx_vdm_tessellate_direct_target_supported(enum agx_chip chip)
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

   case AGX_CHIP_G13G:
   case AGX_CHIP_G13X:
   case AGX_CHIP_G14G:
   case AGX_CHIP_G14X:
   case AGX_CHIP_G15P:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G17A:
   case AGX_CHIP_UNKNOWN:
   default:
      return false;
   }
}

bool
agx_vdm_tessellate_direct_pack(
   enum agx_chip chip, const struct agx_vdm_tessellate_direct_info *info,
   struct agx_vdm_tessellate_direct *out)
{
   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !info || !agx_vdm_tessellate_direct_target_supported(chip) ||
       info->factor_buffer >= (UINT64_C(1) << 48) ||
       info->instance_stride_word > AGX_VDM_TESSELLATE_INSTANCE_STRIDE_MAX)
      return false;

   const uint32_t header =
      AGX_VDM_TESSELLATE_DIRECT_FIXED |
      (uint32_t)((info->factor_buffer >> 32) & UINT64_C(0xff));

   agx_vdm_tessellate_store_le32(&out->bytes[0], header);
   agx_vdm_tessellate_store_le32(&out->bytes[4],
                                 (uint32_t)info->factor_buffer);
   agx_vdm_tessellate_store_le32(&out->bytes[8], info->patch_count);
   agx_vdm_tessellate_store_le32(&out->bytes[12], info->instance_count);
   agx_vdm_tessellate_store_le32(&out->bytes[16], info->base_patch);
   agx_vdm_tessellate_store_le32(&out->bytes[20], info->base_instance);
   agx_vdm_tessellate_store_le32(&out->bytes[24],
                                 info->instance_stride_word);
   agx_vdm_tessellate_store_le32(&out->bytes[28],
                                 info->factor_buffer_size_minus_one);
   agx_vdm_tessellate_store_le32(
      &out->bytes[32],
      (uint32_t)((info->factor_buffer >> 40) & UINT64_C(0xff)));
   out->length = AGX_VDM_TESSELLATE_DIRECT_LENGTH;
   return true;
}

bool
agx_vdm_tessellate_direct_unpack(
   enum agx_chip chip, const void *bytes, size_t size,
   struct agx_vdm_tessellate_direct_info *info)
{
   if (info)
      memset(info, 0, sizeof(*info));

   if (!bytes || !info || !agx_vdm_tessellate_direct_target_supported(chip) ||
       size != AGX_VDM_TESSELLATE_DIRECT_LENGTH)
      return false;

   const uint8_t *raw = bytes;
   const uint32_t header = agx_vdm_tessellate_load_le32(&raw[0]);
   const uint32_t high_word = agx_vdm_tessellate_load_le32(&raw[32]);
   const uint32_t instance_stride =
      agx_vdm_tessellate_load_le32(&raw[24]);

   if ((header & UINT32_C(0xffffff00)) !=
          AGX_VDM_TESSELLATE_DIRECT_FIXED ||
       high_word > UINT32_C(0xff) ||
       instance_stride > AGX_VDM_TESSELLATE_INSTANCE_STRIDE_MAX)
      return false;

   info->factor_buffer =
      (uint64_t)agx_vdm_tessellate_load_le32(&raw[4]) |
      ((uint64_t)(header & UINT32_C(0xff)) << 32) |
      ((uint64_t)high_word << 40);
   info->patch_count = agx_vdm_tessellate_load_le32(&raw[8]);
   info->instance_count = agx_vdm_tessellate_load_le32(&raw[12]);
   info->base_patch = agx_vdm_tessellate_load_le32(&raw[16]);
   info->base_instance = agx_vdm_tessellate_load_le32(&raw[20]);
   info->instance_stride_word = instance_stride;
   info->factor_buffer_size_minus_one =
      agx_vdm_tessellate_load_le32(&raw[28]);
   return true;
}
