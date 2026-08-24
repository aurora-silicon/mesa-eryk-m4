/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_vdm_index_list_core.h"

#include <string.h>

#define AGX_VDM_INDEX_LIST_NONINDEXED_G15_HEADER UINT32_C(0x61c40000)
#define AGX_VDM_INDEX_LIST_NONINDEXED_HAL300_HEADER UINT32_C(0x69c40000)
#define AGX_VDM_INDEX_LIST_INDEXED_G15_FIXED UINT32_C(0x61f00000)
#define AGX_VDM_INDEX_LIST_INDEXED_HAL300_FIXED UINT32_C(0x69f00000)
#define AGX_VDM_INDEX_LIST_HAL300_OPAQUE_WORD UINT32_C(7)

static void
agx_vdm_index_list_store_le32(uint8_t *dst, uint32_t value)
{
   dst[0] = (uint8_t)value;
   dst[1] = (uint8_t)(value >> 8);
   dst[2] = (uint8_t)(value >> 16);
   dst[3] = (uint8_t)(value >> 24);
}

static uint32_t
agx_vdm_index_list_load_le32(const uint8_t *src)
{
   return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
          ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static bool
agx_vdm_index_list_direct_is_hal300(enum agx_chip chip)
{
   return chip == AGX_CHIP_G17G || chip == AGX_CHIP_G17S ||
          chip == AGX_CHIP_G17C;
}

static bool
agx_vdm_index_list_primitive_supported(uint8_t primitive)
{
   switch (primitive) {
   case 0:
   case 1:
   case 3:
   case 5:
   case 6:
   case 9:
   case 11:
   case 13:
   case 14:
   case 15:
      return true;
   default:
      return false;
   }
}

static bool
agx_vdm_index_list_index_size_supported(
   enum agx_vdm_index_list_index_size index_size)
{
   return index_size == AGX_VDM_INDEX_LIST_INDEX_SIZE_U16 ||
          index_size == AGX_VDM_INDEX_LIST_INDEX_SIZE_U32;
}

bool
agx_vdm_index_list_direct_target_supported(enum agx_chip chip)
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
agx_vdm_index_list_direct_nonindexed_pack(
   enum agx_chip chip,
   const struct agx_vdm_index_list_direct_nonindexed_info *info,
   struct agx_vdm_index_list_direct_nonindexed *out)
{
   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !info || !agx_vdm_index_list_direct_target_supported(chip) ||
       !agx_vdm_index_list_primitive_supported(info->primitive) ||
       info->instances == 0)
      return false;

   const bool hal300 = agx_vdm_index_list_direct_is_hal300(chip);
   const uint32_t header =
      (hal300 ? AGX_VDM_INDEX_LIST_NONINDEXED_HAL300_HEADER
              : AGX_VDM_INDEX_LIST_NONINDEXED_G15_HEADER) |
      ((uint32_t)info->primitive << 8);

   agx_vdm_index_list_store_le32(&out->bytes[0], header);
   agx_vdm_index_list_store_le32(&out->bytes[4], info->count);
   agx_vdm_index_list_store_le32(&out->bytes[8], info->instances);
   agx_vdm_index_list_store_le32(&out->bytes[12], info->start);

   if (hal300) {
      agx_vdm_index_list_store_le32(&out->bytes[16],
                                    AGX_VDM_INDEX_LIST_HAL300_OPAQUE_WORD);
      out->length = 20;
   } else {
      out->length = 16;
   }

   return true;
}

bool
agx_vdm_index_list_direct_nonindexed_unpack(
   enum agx_chip chip, const void *bytes, size_t size,
   struct agx_vdm_index_list_direct_nonindexed_info *info)
{
   if (info)
      memset(info, 0, sizeof(*info));

   if (!bytes || !info || !agx_vdm_index_list_direct_target_supported(chip))
      return false;

   const bool hal300 = agx_vdm_index_list_direct_is_hal300(chip);
   const size_t expected_size = hal300 ? 20 : 16;
   if (size != expected_size)
      return false;

   const uint8_t *raw = bytes;
   const uint32_t header = agx_vdm_index_list_load_le32(&raw[0]);
   const uint32_t expected_header =
      hal300 ? AGX_VDM_INDEX_LIST_NONINDEXED_HAL300_HEADER
             : AGX_VDM_INDEX_LIST_NONINDEXED_G15_HEADER;
   const uint8_t primitive = (uint8_t)(header >> 8);

   if ((header & UINT32_C(0xffff00ff)) != expected_header ||
       !agx_vdm_index_list_primitive_supported(primitive) ||
       agx_vdm_index_list_load_le32(&raw[8]) == 0 ||
       (hal300 && agx_vdm_index_list_load_le32(&raw[16]) !=
                     AGX_VDM_INDEX_LIST_HAL300_OPAQUE_WORD))
      return false;

   info->primitive = primitive;
   info->count = agx_vdm_index_list_load_le32(&raw[4]);
   info->instances = agx_vdm_index_list_load_le32(&raw[8]);
   info->start = agx_vdm_index_list_load_le32(&raw[12]);
   return true;
}

bool
agx_vdm_index_list_direct_indexed_pack(
   enum agx_chip chip,
   const struct agx_vdm_index_list_direct_indexed_info *info,
   struct agx_vdm_index_list_direct_indexed *out)
{
   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !info || !agx_vdm_index_list_direct_target_supported(chip) ||
       !agx_vdm_index_list_primitive_supported(info->primitive) ||
       !agx_vdm_index_list_index_size_supported(info->index_size) ||
       info->index_buffer >= (UINT64_C(1) << 48) || info->instances == 0)
      return false;

   const bool hal300 = agx_vdm_index_list_direct_is_hal300(chip);
   uint32_t header =
      (hal300 ? AGX_VDM_INDEX_LIST_INDEXED_HAL300_FIXED
              : AGX_VDM_INDEX_LIST_INDEXED_G15_FIXED) |
      (uint32_t)((info->index_buffer >> 32) & UINT64_C(0xff)) |
      ((uint32_t)info->primitive << 8) |
      ((uint32_t)info->index_size << 17);
   if (info->restart_enable)
      header |= UINT32_C(1) << 16;

   agx_vdm_index_list_store_le32(&out->bytes[0], header);
   agx_vdm_index_list_store_le32(&out->bytes[4],
                                 (uint32_t)info->index_buffer);
   agx_vdm_index_list_store_le32(&out->bytes[8], info->count);
   agx_vdm_index_list_store_le32(&out->bytes[12], info->instances);
   agx_vdm_index_list_store_le32(&out->bytes[16], info->start);

   unsigned cursor = 20;
   if (hal300) {
      agx_vdm_index_list_store_le32(&out->bytes[cursor],
                                    AGX_VDM_INDEX_LIST_HAL300_OPAQUE_WORD);
      cursor += 4;
   }

   agx_vdm_index_list_store_le32(&out->bytes[cursor],
                                 info->index_buffer_size_word);
   cursor += 4;
   agx_vdm_index_list_store_le32(
      &out->bytes[cursor], (uint32_t)((info->index_buffer >> 40) & 0xff));
   cursor += 4;
   out->length = (uint8_t)cursor;
   return true;
}

bool
agx_vdm_index_list_direct_indexed_unpack(
   enum agx_chip chip, const void *bytes, size_t size,
   struct agx_vdm_index_list_direct_indexed_info *info)
{
   if (info)
      memset(info, 0, sizeof(*info));

   if (!bytes || !info || !agx_vdm_index_list_direct_target_supported(chip))
      return false;

   const bool hal300 = agx_vdm_index_list_direct_is_hal300(chip);
   const size_t expected_size = hal300 ? 32 : 28;
   if (size != expected_size)
      return false;

   const uint8_t *raw = bytes;
   const uint32_t header = agx_vdm_index_list_load_le32(&raw[0]);
   const uint32_t expected_fixed =
      hal300 ? AGX_VDM_INDEX_LIST_INDEXED_HAL300_FIXED
             : AGX_VDM_INDEX_LIST_INDEXED_G15_FIXED;
   const uint8_t primitive = (uint8_t)(header >> 8);
   const enum agx_vdm_index_list_index_size index_size =
      (enum agx_vdm_index_list_index_size)((header >> 17) & 0x7);
   const unsigned opaque_offset = 20;
   const unsigned size_offset = hal300 ? 24 : 20;
   const unsigned high_offset = hal300 ? 28 : 24;
   const uint32_t high_word = agx_vdm_index_list_load_le32(&raw[high_offset]);

   if ((header & UINT32_C(0xfff00000)) != expected_fixed ||
       !agx_vdm_index_list_primitive_supported(primitive) ||
       !agx_vdm_index_list_index_size_supported(index_size) ||
       agx_vdm_index_list_load_le32(&raw[12]) == 0 || high_word > 0xff ||
       (hal300 && agx_vdm_index_list_load_le32(&raw[opaque_offset]) !=
                     AGX_VDM_INDEX_LIST_HAL300_OPAQUE_WORD))
      return false;

   info->primitive = primitive;
   info->restart_enable = (header & (UINT32_C(1) << 16)) != 0;
   info->index_size = index_size;
   info->index_buffer =
      (uint64_t)agx_vdm_index_list_load_le32(&raw[4]) |
      ((uint64_t)(header & 0xff) << 32) | ((uint64_t)high_word << 40);
   info->count = agx_vdm_index_list_load_le32(&raw[8]);
   info->instances = agx_vdm_index_list_load_le32(&raw[12]);
   info->start = agx_vdm_index_list_load_le32(&raw[16]);
   info->index_buffer_size_word =
      agx_vdm_index_list_load_le32(&raw[size_offset]);
   return true;
}
