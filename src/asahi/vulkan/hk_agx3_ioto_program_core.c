/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ioto_program_core.h"

#include "asahi/lib/agx_ioto_core.h"

#include <string.h>

bool
hk_agx3_ioto_program_generate(
   enum agx_chip chip, const struct hk_agx3_ioto_program_desc *desc,
   void *output, size_t capacity, size_t *written)
{
   if (!desc || !agx_ioto_target_supported(chip))
      return false;

   const struct agx_ioto_profile_control profile = {
      .chip = chip,
      .payload0 = desc->profile_payload0,
      .payload1 = desc->profile_payload1,
   };

   return agx_ioto_straight_line_program_compose(
      chip, desc->program_va, desc->body, desc->body_size, &desc->tail,
      &profile, output, capacity, written);
}

bool
hk_agx3_ioto_program_generate_no_pixrel(
   enum agx_chip chip, const struct hk_agx3_ioto_program_desc *desc,
   const struct agx_ioto_preshader_desc *preshader, void *output,
   size_t capacity, size_t *written)
{
   if (!desc || !agx_ioto_target_supported(chip))
      return false;

   const struct agx_ioto_profile_control profile = {
      .chip = chip,
      .payload0 = desc->profile_payload0,
      .payload1 = desc->profile_payload1,
   };

   return agx_ioto_program_compose_no_pixrel(
      chip, desc->program_va, desc->body, desc->body_size, &desc->tail,
      &profile, preshader, output, capacity, written);
}

bool
hk_agx3_g17p_no_resource_program_generate(
   enum agx_chip chip, uint64_t program_va,
   const struct agx_ioto_state_tail_output *packed_tail, void *output,
   size_t capacity, size_t *written)
{
   if (chip != AGX_CHIP_G17P)
      return false;

   return agx_ioto_g17p_no_resource_program_compose(program_va, packed_tail,
                                                    output, capacity, written);
}

static bool
hk_agx3_g17p_graphics_state_tail(
   const struct hk_agx3_g17p_graphics_program_desc *desc,
   struct agx_ioto_state_tail_output *tail)
{
   if (!desc || !tail || desc->shader_binary_size == 0 ||
       desc->shader_binary_size > 256 || desc->nr_gprs == 0)
      return false;

   static const uint8_t vertex_speclm[AGX_IOTO_SPECLM_SIZE] = {
      0x77, 0x00, 0x2a, 0x41, 0x00, 0x00, 0x00, 0x00,
   };
   static const uint8_t fragment_speclm[AGX_IOTO_SPECLM_SIZE] = {
      0x77, 0x00, 0x2a, 0x43, 0x4b, 0x00, 0x00, 0x00,
   };

   struct agx_ioto_state ldshdr = {0};
   ldshdr.f[0] = desc->shader_va;
   ldshdr.f[1] = (desc->shader_binary_size + 63) / 64 - 1;
   ldshdr.f[23] = 1;

   struct agx_ioto_state spectpr;
   if (!agx_ioto_g17p_compiler_spectpr_build(desc->scratch_size, desc->nr_gprs,
                                             &spectpr))
      return false;

   struct agx_ioto_state_tail_output staged = {
      .length = AGX_IOTO_STATE_TAIL_TYPE0_SIZE,
   };
   memcpy(staged.bytes, desc->fragment ? fragment_speclm : vertex_speclm,
          AGX_IOTO_SPECLM_SIZE);
   if (!agx_ioto_state_pack_ldshdr(AGX_CHIP_G17P, &ldshdr,
                                   staged.bytes + AGX_IOTO_SPECLM_SIZE,
                                   AGX_IOTO_LDSHDR_SIZE) ||
       !agx_ioto_state_pack_spectpr(
          AGX_CHIP_G17P, &spectpr,
          staged.bytes + AGX_IOTO_SPECLM_SIZE + AGX_IOTO_LDSHDR_SIZE,
          AGX_IOTO_SPECTPR_SIZE))
      return false;

   *tail = staged;
   return true;
}

bool
hk_agx3_g17p_basic_graphics_program_generate(
   enum agx_chip chip, const struct hk_agx3_g17p_graphics_program_desc *desc,
   struct hk_agx3_g17p_graphics_program *program)
{
   if (!program || chip != AGX_CHIP_G17P)
      return false;

   struct agx_ioto_state_tail_output tail;
   if (!hk_agx3_g17p_graphics_state_tail(desc, &tail))
      return false;

   struct hk_agx3_g17p_graphics_program staged;
   size_t written = 0;
   if (!agx_ioto_g17p_no_resource_program_compose(
          desc->program_va, &tail, staged.bytes, sizeof(staged.bytes),
          &written) ||
       written != sizeof(staged.bytes))
      return false;

   staged.length = (uint8_t)written;
   *program = staged;
   return true;
}

bool
hk_agx3_g17p_rt0_eot_program_generate(
   enum agx_chip chip, const struct hk_agx3_g17p_rt0_eot_program_desc *desc,
   struct hk_agx3_g17p_rt0_eot_program *program)
{
   if (!program || chip != AGX_CHIP_G17P || !desc)
      return false;

   const struct hk_agx3_g17p_graphics_program_desc shader = {
      .program_va = desc->program_va,
      .shader_va = desc->shader_va,
      .shader_binary_size = desc->shader_binary_size,
      .scratch_size = desc->scratch_size,
      .nr_gprs = desc->nr_gprs,
      .fragment = true,
   };
   struct agx_ioto_state_tail_output tail;
   if (!hk_agx3_g17p_graphics_state_tail(&shader, &tail))
      return false;

   struct hk_agx3_g17p_rt0_eot_program staged;
   size_t written = 0;
   if (!agx_ioto_g17p_rt0_eot_program_compose(
          desc->program_va, desc->pbe_argument_va, &tail, staged.bytes,
          sizeof(staged.bytes), &written) ||
       written != sizeof(staged.bytes))
      return false;

   staged.length = (uint16_t)written;
   *program = staged;
   return true;
}

bool
hk_agx3_g17s_minimal_compute_program_prepare(
   enum agx_chip chip,
   const struct agx3_g17s_minimal_compute_program_desc *desc, void *output,
   size_t capacity, size_t *written, struct agx3_compute_program *program)
{
   if (chip != AGX_CHIP_G17S)
      return false;

   return agx3_g17s_minimal_compute_program_prepare(
      desc, output, capacity, written, program);
}

bool
hk_agx3_g17s_minimal_compute_program_upload_prepare(
   enum agx_chip chip,
   const struct agx3_g17s_minimal_compute_program_desc *desc,
   const struct agx3_compute_program_upload *upload, size_t *written,
   struct agx3_compute_program *program)
{
   return agx3_g17s_minimal_compute_program_upload_prepare(
      chip, desc, upload, written, program);
}

bool
hk_agx3_g17s_minimal_compute_program_live_upload_prepare(
   enum agx_chip chip,
   const struct agx3_g17s_minimal_compute_program_desc *desc,
   const struct agx_ioto_g17s_minimal_compute_resources *resources,
   const struct agx3_compute_program_upload *upload, size_t *written,
   struct agx3_compute_program *program)
{
   return agx3_g17s_minimal_compute_program_live_upload_prepare(
      chip, desc, resources, upload, written, program);
}

bool
hk_agx3_g17s_minimal_compute_dispatch_append(
   const struct agx3_compute_program *program,
   const struct agx3_compute_direct_grid *grid,
   struct agx3_compute_dispatch_sink *sink)
{
   return agx3_g17s_minimal_compute_dispatch_append(program, grid, sink);
}
