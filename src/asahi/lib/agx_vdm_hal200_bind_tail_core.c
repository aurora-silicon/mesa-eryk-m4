/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_vdm_hal200_bind_tail_core.h"

#include <string.h>

static const uint32_t controls[AGX_VDM_HAL200_BIND_COUNT] = {
   UINT32_C(0x700), UINT32_C(0x500), UINT32_C(0x700), UINT32_C(0x500),
   UINT32_C(0xa00), UINT32_C(0x300), UINT32_C(0x200), UINT32_C(0x200),
};

static void
store_le32(uint8_t *dst, uint32_t value)
{
   dst[0] = (uint8_t)value;
   dst[1] = (uint8_t)(value >> 8);
   dst[2] = (uint8_t)(value >> 16);
   dst[3] = (uint8_t)(value >> 24);
}

static uint32_t
load_le32(const uint8_t *src)
{
   return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
          ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

bool
agx_vdm_hal200_bind_tail_pack(
   enum agx_chip chip,
   const struct agx_vdm_hal200_bind_pair pairs[AGX_VDM_HAL200_BIND_COUNT],
   struct agx_vdm_hal200_bind_tail *out)
{
   if (out)
      memset(out, 0, sizeof(*out));

   if (!out || !pairs || !agx_chip_uses_hal200_ioto(chip))
      return false;

   store_le32(out->bytes, AGX_VDM_HAL200_BIND_HEADER_CONTROL);
   for (unsigned i = 0; i < AGX_VDM_HAL200_BIND_COUNT; ++i) {
      if (pairs[i].context_offset == 0 ||
          (pairs[i].context_offset & UINT32_C(3)) != 0 ||
          pairs[i].control != controls[i]) {
         memset(out, 0, sizeof(*out));
         return false;
      }

      const size_t offset = 4 + (8 * i);
      store_le32(&out->bytes[offset], pairs[i].context_offset);
      store_le32(&out->bytes[offset + 4], pairs[i].control);
   }

   out->length = AGX_VDM_HAL200_BIND_TAIL_LENGTH;
   return true;
}

bool
agx_vdm_hal200_bind_tail_unpack(
   enum agx_chip chip, const void *bytes, size_t size,
   struct agx_vdm_hal200_bind_pair pairs[AGX_VDM_HAL200_BIND_COUNT])
{
   if (pairs)
      memset(pairs, 0,
             sizeof(struct agx_vdm_hal200_bind_pair) *
                AGX_VDM_HAL200_BIND_COUNT);

   if (!bytes || !pairs || !agx_chip_uses_hal200_ioto(chip) ||
       size != AGX_VDM_HAL200_BIND_TAIL_LENGTH)
      return false;

   const uint8_t *raw = bytes;
   if (load_le32(raw) != AGX_VDM_HAL200_BIND_HEADER_CONTROL)
      return false;

   for (unsigned i = 0; i < AGX_VDM_HAL200_BIND_COUNT; ++i) {
      const size_t offset = 4 + (8 * i);
      pairs[i].context_offset = load_le32(&raw[offset]);
      pairs[i].control = load_le32(&raw[offset + 4]);
      if (pairs[i].context_offset == 0 ||
          (pairs[i].context_offset & UINT32_C(3)) != 0 ||
          pairs[i].control != controls[i]) {
         memset(pairs, 0,
                sizeof(struct agx_vdm_hal200_bind_pair) *
                   AGX_VDM_HAL200_BIND_COUNT);
         return false;
      }
   }

   return true;
}
