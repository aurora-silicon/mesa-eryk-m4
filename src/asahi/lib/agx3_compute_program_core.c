/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_compute_program_core.h"

#include <string.h>

bool
agx3_compute_program_prepare(const struct agx3_compute_program_desc *desc,
                             void *output, size_t capacity, size_t *written,
                             struct agx3_compute_program *program)
{
   if (!desc || !output || !written || !program ||
       desc->profile.chip != desc->chip ||
       !agx3_cdm_direct_launch_target_grounded(desc->chip))
      return false;

   struct agx3_cdm_direct_template launch_template;
   if (!agx3_cdm_direct_template_build_from_sources(&desc->launch_sources,
                                                    &launch_template))
      return false;

   size_t staged_written = 0;
   if (!agx_ioto_program_compose_no_pixrel(
          desc->chip, desc->program_va, desc->body, desc->body_size,
          &desc->tail, &desc->profile, &desc->preshader, output, capacity,
          &staged_written))
      return false;

   *program = (struct agx3_compute_program){
      .chip = desc->chip,
      .program_va = desc->program_va,
      .launch_template = launch_template,
   };
   *written = staged_written;
   return true;
}

bool
agx3_g17s_minimal_compute_program_prepare(
   const struct agx3_g17s_minimal_compute_program_desc *desc, void *output,
   size_t capacity, size_t *written, struct agx3_compute_program *program)
{
   if (!desc || !output || !written || !program)
      return false;

   /* G17S live witness, ProgramVariantESLState::ProgramVariantESLState
    * +12508 -> EslBuilder::loadFromUserBuffer.  Exact arguments are
    * (w1..w6) = (4, 1, 0, 0x20000001, 0, 0).  The binding tag is constructed
    * from user-buffer index one; keep only caller-visible/raw UniformData
    * axes here and fail closed on every other layout. */
   if (desc->root_uniform.user_buffer_binding != 1 ||
       desc->root_uniform.byte_offset != 0 ||
       desc->root_uniform.uniform_data_destination != 4 ||
       desc->root_uniform.uniform_data_count != 1 ||
       desc->root_uniform.temporary_base != 0)
      return false;

   struct agx3_cdm_direct_template launch_template;
   if (!agx3_cdm_direct_template_build_from_sources(&desc->launch_sources,
                                                    &launch_template))
      return false;

   size_t staged_written;
   if (!agx_ioto_g17s_minimal_compute_program_compose(
          desc->program_va, desc->shader_va, output, capacity,
          &staged_written))
      return false;

   *program = (struct agx3_compute_program){
      .chip = AGX_CHIP_G17S,
      .program_va = desc->program_va,
      .launch_template = launch_template,
   };
   *written = staged_written;
   return true;
}

bool
agx3_compute_program_pack_direct_launch(
   const struct agx3_compute_program *program,
   const struct agx3_compute_direct_grid *grid,
   uint8_t packed[AGX3_CDM_DIRECT_LAUNCH_SIZE])
{
   if (!packed)
      return false;

   memset(packed, 0, AGX3_CDM_DIRECT_LAUNCH_SIZE);
   if (!program || !grid ||
       !agx3_cdm_direct_launch_target_grounded(program->chip) ||
       program->program_va > UINT64_C(0xffffffffffff) ||
       (program->program_va & (AGX_IOTO_PROGRAM_ALIGNMENT - 1)) != 0)
      return false;

   uint32_t global_size[3];
   if (!agx3_cdm_direct_grid_resolve(grid->source, grid->size, grid->local_size,
                                     global_size))
      return false;

   const struct agx3_cdm_direct_launch_info launch = {
      .chip = program->chip,
      .template_words = program->launch_template,
      .pipeline = program->program_va,
      .dimensions = grid->dimensions,
      .g15x_has_multiple_agcs = grid->g15x_has_multiple_agcs,
      .global_size = {global_size[0], global_size[1], global_size[2]},
      .local_size = {grid->local_size[0], grid->local_size[1],
                     grid->local_size[2]},
   };

   return agx3_cdm_direct_launch_pack(&launch, packed);
}
