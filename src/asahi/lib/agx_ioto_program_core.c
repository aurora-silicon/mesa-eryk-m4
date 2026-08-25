/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_ioto_program_core.h"

#include "agx_ioto_core.h"

#include <string.h>

#define AGX_IOTO_GPU0_INLINE_LDDEV_SIZE 14u

static bool
agx_ioto_program_target_gpu0(enum agx_chip chip)
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
      return true;
   default:
      return false;
   }
}

/* GPU0 EslBuilder appends the AGX3b LDDEV instruction directly rather than
 * through an AGXIotoInstruction_LDDEV object.  Its low eleven opcode bits and
 * fourteen-byte boundary are exact, but it deliberately remains outside the
 * public Ioto record-kind namespace. */
static bool
agx_ioto_program_gpu0_inline_lddev(enum agx_chip chip, const uint8_t *bytes,
                                   size_t available)
{
   if (!agx_ioto_program_target_gpu0(chip) ||
       available < AGX_IOTO_GPU0_INLINE_LDDEV_SIZE)
      return false;

   const uint16_t word = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
   return (word & UINT16_C(0x07ff)) == UINT16_C(0x0067);
}

static bool
agx_ioto_straight_line_body_kind(enum agx_ioto_kind kind)
{
   switch (kind) {
   case AGX_IOTO_KIND_NOP_EP0:
   case AGX_IOTO_KIND_LDIMM_EP0:
   case AGX_IOTO_KIND_LDIMM_EP1:
   case AGX_IOTO_KIND_LDIMM_EP2:
   case AGX_IOTO_KIND_LDIMM_EP3:
   case AGX_IOTO_KIND_COPY_EP0:
   case AGX_IOTO_KIND_COPY_EP1:
   case AGX_IOTO_KIND_RLD_EP0:
   case AGX_IOTO_KIND_RLD_EP1:
   case AGX_IOTO_KIND_IADD_EP0:
   case AGX_IOTO_KIND_LDDEV_EP0:
   case AGX_IOTO_KIND_LDDEV_EP1:
   case AGX_IOTO_KIND_LDDEV_EP2:
   case AGX_IOTO_KIND_LDDEV_EP3:
   case AGX_IOTO_KIND_LDDEV_EP4:
   case AGX_IOTO_KIND_LDDEV_EP5:
      return true;
   default:
      return false;
   }
}

static bool
agx_ioto_straight_line_body_validate(enum agx_chip chip, const void *body,
                                     size_t body_size)
{
   if (body_size == 0)
      return true;
   if (!body)
      return false;

   const uint8_t *bytes = body;
   size_t offset = 0;
   while (offset < body_size) {
      if (agx_ioto_program_gpu0_inline_lddev(chip, bytes + offset,
                                             body_size - offset)) {
         offset += AGX_IOTO_GPU0_INLINE_LDDEV_SIZE;
         continue;
      }

      struct agx_ioto_record record;
      size_t consumed;
      if (!agx_ioto_decode_one(chip, bytes + offset, body_size - offset,
                               &record, &consumed) ||
          consumed == 0 || !agx_ioto_straight_line_body_kind(record.kind))
         return false;

      offset += consumed;
   }

   return offset == body_size;
}

bool
agx_ioto_program_compose_no_pixrel(
   enum agx_chip chip, uint64_t program_va, const void *body, size_t body_size,
   const struct agx_ioto_state_tail *tail,
   const struct agx_ioto_profile_control *profile,
   const struct agx_ioto_preshader_desc *preshader, void *output,
   size_t capacity, size_t *written)
{
   if (!output || !written || !tail || !profile || !preshader ||
       !agx_ioto_target_supported(chip) || profile->chip != chip ||
       program_va > UINT64_C(0xffffffffffff) ||
       (program_va & (AGX_IOTO_PROGRAM_ALIGNMENT - 1)) != 0 ||
       body_size > capacity ||
       !agx_ioto_straight_line_body_validate(chip, body, body_size))
      return false;

   struct agx_ioto_state_tail_output packed_tail;
   if (!agx_ioto_state_tail_pack(chip, tail, &packed_tail))
      return false;

   uint8_t packed_profile[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
   if (!agx_ioto_profile_control_pack(profile, packed_profile))
      return false;

   struct agx_ioto_preshader_output packed_preshader;
   if (!agx_ioto_preshader_emit(chip, preshader, &packed_preshader))
      return false;

   const size_t profile_size = agx_ioto_profile_control_size(chip);
   if (profile_size == 0 || body_size > SIZE_MAX - packed_tail.length ||
       body_size + packed_tail.length > SIZE_MAX - profile_size ||
       body_size + packed_tail.length + profile_size >
          SIZE_MAX - packed_preshader.length)
      return false;

   const size_t total =
      body_size + packed_tail.length + profile_size + packed_preshader.length;
   if (capacity < total)
      return false;

   uint8_t *dst = output;
   if (body_size)
      memmove(dst, body, body_size);
   memcpy(dst + body_size, packed_profile, profile_size);
   memcpy(dst + body_size + profile_size, packed_tail.bytes,
          packed_tail.length);
   memcpy(dst + body_size + profile_size + packed_tail.length,
          packed_preshader.bytes, packed_preshader.length);
   *written = total;
   return true;
}

bool
agx_ioto_straight_line_program_compose(
   enum agx_chip chip, uint64_t program_va, const void *body, size_t body_size,
   const struct agx_ioto_state_tail *tail,
   const struct agx_ioto_profile_control *profile, void *output,
   size_t capacity, size_t *written)
{
   const struct agx_ioto_preshader_desc absent = {
      .external_va = AGX_IOTO_PRESHADER_NO_VA,
   };

   return agx_ioto_program_compose_no_pixrel(chip, program_va, body, body_size,
                                             tail, profile, &absent, output,
                                             capacity, written);
}
