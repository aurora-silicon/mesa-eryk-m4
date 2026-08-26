/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_ioto_program_core.h"

#include "agx_ioto_core.h"

#include <string.h>

#define AGX_IOTO_GPU0_INLINE_LDDEV_SIZE 14u

static const uint8_t g17s_minimal_compute_resource_body[] = {
   /* First-use gather base at half4/6, then two-dword gather load to half20. */
   0x2c, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x3c, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
   /* Runtime-patched buffer(1) pointer at half24/26. */
   0xcc, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
   0xdc, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
   /* Constructor-owned absolute UniformData base at half28/30. */
   0xec, 0xa0, 0x02, 0x00, 0x00, 0x00, 0x0c, 0x00,
   0xfc, 0x80, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00,
   /* gather(2), buffer(1) root uniform(1), absolute(3), base-sorted. */
   0xaf, 0x04, 0x03, 0x00, 0xa8, 0x0c, 0x00, 0x40,
   0xcf, 0x18, 0x03, 0x00, 0x88, 0x04, 0x00, 0xc0,
   0x01, 0x00, 0x00, 0x00, 0x00, 0x80,
   0xef, 0x1c, 0x03, 0x00, 0xc8, 0x0c, 0x00, 0x40,
};

static const uint8_t g17s_minimal_compute_copies[] = {
   /* The pointer loaded by loadImmediateUserBuffer's kind-3 Register zero is
    * copied into CProg dwords 2..3. */
   0x2b, 0x00, 0x01, 0x04,
   0x3b, 0x00, 0x01, 0x04,
   /* Gather half20/22 -> CProg half0/2 (first COPY waits on slot 1). */
   0x0b, 0x14, 0x09, 0x44,
   0x1b, 0x16, 0x09, 0x04,
   /* Root uniform half24 -> CProg half8. */
   0x4b, 0x18, 0x09, 0x04,
   /* Absolute result half28/30/32 -> CProg half10/12/14. */
   0x5b, 0x1c, 0x09, 0x04,
   0x6b, 0x1e, 0x09, 0x04,
   0x7b, 0x20, 0x09, 0x04,
};

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

static bool
agx_ioto_g17s_ldimm_ep2_pack(uint16_t dest_half, uint32_t immediate,
                            uint8_t output[8])
{
   if (!output || (dest_half & 1) != 0 || dest_half >= 256)
      return false;

   /* Exact HAL300 LDIMM ep2 equations.  This deliberately does not select
    * the two-byte short form: emitUscStateLoad patches the pre-existing
    * sixteen-byte address pair in place. */
   const uint16_t w0 = UINT16_C(0x800c) |
                       (uint16_t)(((dest_half >> 1) & 0xf) << 4) |
                       (uint16_t)((immediate & 0x7f) << 8);
   const uint16_t w1 = UINT16_C(0x0002) |
                       (uint16_t)(((dest_half >> 5) & 3) << 6) |
                       (uint16_t)(((dest_half >> 7) & 1) << 3) |
                       (uint16_t)(((immediate >> 25) & 0x7f) << 9);
   const uint16_t w2 = (uint16_t)(((immediate >> 7) & 0xf) << 1) |
                       (uint16_t)(((immediate >> 11) & 3) << 10);
   const uint16_t w3 = (uint16_t)((immediate >> 13) & 0xf) |
                       (uint16_t)(((immediate >> 17) & 0xff) << 4);

   output[0] = (uint8_t)w0;
   output[1] = (uint8_t)(w0 >> 8);
   output[2] = (uint8_t)w1;
   output[3] = (uint8_t)(w1 >> 8);
   output[4] = (uint8_t)w2;
   output[5] = (uint8_t)(w2 >> 8);
   output[6] = (uint8_t)w3;
   output[7] = (uint8_t)(w3 >> 8);
   return true;
}

static bool
agx_ioto_g17s_minimal_compute_program_compose_internal(
   uint64_t program_va, uint64_t shader_va,
   const struct agx_ioto_g17s_minimal_compute_resources *resources,
   void *output, size_t capacity, size_t *written)
{
   if (!output || !written ||
       program_va > UINT64_C(0xffffffffffff) ||
       (program_va & (AGX_IOTO_PROGRAM_ALIGNMENT - 1)) != 0 ||
       capacity < AGX_IOTO_G17S_MINIMAL_COMPUTE_PROGRAM_SIZE ||
       (resources &&
        (resources->gather_buffer_va == 0 ||
         resources->user_compute_argument_gpu_va_1 == 0 ||
         resources->user_compute_argument_gpu_va_1 >
            UINT64_MAX -
               AGX_IOTO_G17S_MINIMAL_COMPUTE_USER_BUFFER_ADDEND ||
         resources->absolute_uniform_data_va == 0)))
      return false;

   struct agx_ioto_state_tail tail;
   if (!agx_ioto_g17s_minimal_compute_tail_build(shader_va, &tail))
      return false;

   struct agx_ioto_state_tail_output packed_tail;
   if (!agx_ioto_state_tail_pack(AGX_CHIP_G17S, &tail, &packed_tail) ||
       packed_tail.length != AGX_IOTO_STATE_TAIL_TYPE0_SIZE)
      return false;

   uint8_t staged[AGX_IOTO_G17S_MINIMAL_COMPUTE_PROGRAM_SIZE];
   size_t offset = 0;
   memcpy(staged + offset, g17s_minimal_compute_resource_body,
          sizeof(g17s_minimal_compute_resource_body));
   const uint64_t user_buffer_va =
      resources ? resources->user_compute_argument_gpu_va_1 +
                     AGX_IOTO_G17S_MINIMAL_COMPUTE_USER_BUFFER_ADDEND
                : 0;
   if (resources &&
       (!agx_ioto_g17s_ldimm_ep2_pack(
           4, (uint32_t)resources->gather_buffer_va, staged + offset) ||
        !agx_ioto_g17s_ldimm_ep2_pack(
           6, (uint32_t)(resources->gather_buffer_va >> 32),
           staged + offset + 8) ||
        !agx_ioto_g17s_ldimm_ep2_pack(
           24, (uint32_t)user_buffer_va, staged + offset + 16) ||
        !agx_ioto_g17s_ldimm_ep2_pack(
           26, (uint32_t)(user_buffer_va >> 32),
           staged + offset + 24) ||
        !agx_ioto_g17s_ldimm_ep2_pack(
           28, (uint32_t)resources->absolute_uniform_data_va,
           staged + offset + 32) ||
        !agx_ioto_g17s_ldimm_ep2_pack(
           30, (uint32_t)(resources->absolute_uniform_data_va >> 32),
           staged + offset + 40)))
      return false;
   offset += sizeof(g17s_minimal_compute_resource_body);
   memcpy(staged + offset, packed_tail.bytes, packed_tail.length);
   offset += packed_tail.length;
   if (!agx_ioto_g17s_minimal_profile_loader_pack(
          staged + offset, sizeof(staged) - offset))
      return false;
   offset += AGX_IOTO_G17S_MINIMAL_PROFILE_LOADER_SIZE;
   memcpy(staged + offset, g17s_minimal_compute_copies,
          sizeof(g17s_minimal_compute_copies));
   offset += sizeof(g17s_minimal_compute_copies);
   static const uint8_t terminal[AGX_IOTO_PROGRAM_TERMINATE_SIZE] = {
      0x0e, 0x00, 0x00, 0x00,
   };
   memcpy(staged + offset, terminal, sizeof(terminal));
   offset += sizeof(terminal);

   if (offset != sizeof(staged))
      return false;

   memcpy(output, staged, sizeof(staged));
   *written = sizeof(staged);
   return true;
}

bool
agx_ioto_g17s_minimal_compute_program_compose(
   uint64_t program_va, uint64_t shader_va, void *output, size_t capacity,
   size_t *written)
{
   return agx_ioto_g17s_minimal_compute_program_compose_internal(
      program_va, shader_va, NULL, output, capacity, written);
}

bool
agx_ioto_g17s_minimal_compute_program_compose_resources(
   uint64_t program_va, uint64_t shader_va,
   const struct agx_ioto_g17s_minimal_compute_resources *resources,
   void *output, size_t capacity, size_t *written)
{
   if (!resources)
      return false;

   return agx_ioto_g17s_minimal_compute_program_compose_internal(
      program_va, shader_va, resources, output, capacity, written);
}
