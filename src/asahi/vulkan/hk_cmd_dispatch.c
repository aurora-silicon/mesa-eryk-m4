/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "libagx/query.h"
#include "vulkan/vulkan_core.h"
#include "agx_helpers.h"
#include "agx_linker.h"
#include "agx_pack.h"
#include "agx_scratch.h"
#include "agx_tilebuffer.h"
#include "hk_buffer.h"
#include "hk_cmd_buffer.h"
#include "hk_descriptor_set.h"
#include "hk_device.h"
#include "hk_entrypoints.h"
#include "hk_agx3_ioto_program_core.h"
#include "hk_physical_device.h"
#include "hk_shader.h"
#include "libagx_dgc.h"
#include "libagx_shaders.h"
#include "asahi/genxml/agx3_cmdstream.h"
#include "pool.h"

void
hk_cmd_buffer_begin_compute(struct hk_cmd_buffer *cmd,
                            const VkCommandBufferBeginInfo *pBeginInfo)
{
}

void
hk_cmd_invalidate_compute_state(struct hk_cmd_buffer *cmd)
{
   memset(&cmd->state.cs, 0, sizeof(cmd->state.cs));
}

void
hk_cmd_bind_compute_shader(struct hk_cmd_buffer *cmd,
                           struct hk_api_shader *shader)
{
   cmd->state.cs.shader = shader;
}

void
hk_cdm_cache_flush(struct hk_device *dev, struct hk_cs *cs)
{
   assert(cs->type == HK_CS_CDM);
   assert(cs->current + AGX_CDM_BARRIER_LENGTH < cs->end &&
          "caller must ensure space");

   switch (agx_chip_command_stream_generation(dev->dev.chip)) {
   case AGX_CS_GENERATION_AGX2:
      cs->current = agx_cdm_barrier(cs->current, dev->dev.chip);
      break;

   case AGX_CS_GENERATION_G15:
   case AGX_CS_GENERATION_HAL200:
   case AGX_CS_GENERATION_HAL300: {
      /* Apple's CommandEncodingNext emitter uses flags=5 for its strong
       * post-dispatch cache flush on every grounded G15--G17 identity.  Keep
       * Honeykrisp's deliberately conservative flush-after-every-launch
       * policy, but encode it in the target command-stream ISA. */
      const struct agx3_cdm_barrier_info info = {
         .chip = dev->dev.chip,
         .flags = 0x5,
      };
      uint8_t packed[AGX3_CDM_BARRIER_SIZE];
      if (!agx3_cdm_barrier_pack(&info, packed)) {
         mesa_loge("no grounded AGX3 CDM cache flush for %s",
                   agx_chip_name(dev->dev.chip));
         vk_command_buffer_set_error(&cs->cmd->vk,
                                     VK_ERROR_FEATURE_NOT_PRESENT);
         return;
      }

      memcpy(cs->current, packed, sizeof(packed));
      cs->current = (uint32_t *)((uint8_t *)cs->current + sizeof(packed));
      break;
   }

   case AGX_CS_GENERATION_UNKNOWN:
   default:
      mesa_loge("unknown CDM command-stream generation for %s",
                agx_chip_name(dev->dev.chip));
      vk_command_buffer_set_error(&cs->cmd->vk,
                                  VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   cs->stats.flushes++;
}

void
hk_dispatch_with_usc_launch(struct hk_device *dev, struct hk_cs *cs,
                            struct agx_cdm_launch_word_0_packed launch,
                            uint32_t usc, struct agx_grid grid,
                            struct agx_workgroup wg)
{
   switch (agx_chip_command_stream_generation(dev->dev.chip)) {
   case AGX_CS_GENERATION_AGX2:
      break;

   case AGX_CS_GENERATION_G15:
   case AGX_CS_GENERATION_HAL200:
   case AGX_CS_GENERATION_HAL300:
      /* `usc` addresses an AGX2 USC block.  AGX3 direct launches instead
       * require a dispatch-local, 64-byte-aligned Ioto program VA.  Reject
       * this legacy entry point until the caller supplies that joined object;
       * never feed an AGX2 launch to CommandEncodingNext. */
      mesa_loge("legacy USC compute launch rejected for %s",
                agx_chip_name(dev->dev.chip));
      vk_command_buffer_set_error(&cs->cmd->vk,
                                  VK_ERROR_FEATURE_NOT_PRESENT);
      return;

   case AGX_CS_GENERATION_UNKNOWN:
   default:
      mesa_loge("unknown compute command-stream generation for %s",
                agx_chip_name(dev->dev.chip));
      vk_command_buffer_set_error(&cs->cmd->vk,
                                  VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   hk_ensure_cs_has_space(cs->cmd, cs, 0x2000 /* TODO */);
   cs->stats.cmds++;
   cs->current =
      agx_cdm_launch(cs->current, dev->dev.chip, grid, wg, launch, usc);

   hk_cdm_cache_flush(dev, cs);
}

void
hk_dispatch_with_usc(struct hk_device *dev, struct hk_cs *cs,
                     struct agx_shader_info *info, uint32_t usc,
                     struct agx_grid grid, struct agx_workgroup local_size)
{
   struct agx_cdm_launch_word_0_packed launch;
   agx_pack(&launch, CDM_LAUNCH_WORD_0, cfg) {
      cfg.texture_state_register_count = info->texture_state_count;
      cfg.sampler_state_register_count =
         agx_translate_sampler_state_count(info->sampler_state_count, false);
      cfg.uniform_register_count = info->push_count;
      cfg.preshader_register_count = info->nr_preamble_gprs;
   }

   hk_dispatch_with_usc_launch(dev, cs, launch, usc, grid, local_size);
}

static bool
hk_g17p_compute_shader_supported(struct hk_shader *s,
                                 struct hk_linked_shader *linked)
{
   return s && linked && s->info.stage == MESA_SHADER_COMPUTE &&
          linked->b.bo && linked->b.bo->va && linked->b.size != 0 &&
          linked->b.size <= UINT32_MAX && linked->b.nr_gprs != 0 &&
          !s->b.info.has_preamble && s->b.info.preamble_scratch_size == 0 &&
          s->b.info.scratch_size == 0 && linked->b.scratch_size == 0 &&
          s->b.info.local_size == 0 && s->b.info.texture_state_count == 0 &&
          s->b.info.sampler_state_count == 0 &&
          !s->b.info.uses_sampler_heap && !linked->b.uses_txf &&
          s->data_size == 0 &&
          s->info.set_count <= HK_AGX3_G17P_MAX_ROOT_SETS;
}

static bool
hk_g17p_compute_uniform_ranges(
   struct hk_shader *s, uint64_t root_ptr,
   struct agx3_ioto_uniform_range ranges[2], size_t *range_count)
{
   if (!s || !ranges || !range_count ||
       !hk_agx3_g17p_root_range(0, s->info.set_count, root_ptr, &ranges[0]))
      return false;

   size_t count = 1;
   const struct agx_rodata *rodata = &s->b.info.rodata;
   if (rodata->size_16 != 0) {
      const uint64_t size_B = (uint64_t)rodata->size_16 * 2;
      if (!s->bo || !s->bo->va || (rodata->base_uniform & 1) != 0 ||
          (rodata->size_16 & 1) != 0 || rodata->size_16 > 512 ||
          rodata->offset > s->bo->size ||
          size_B > s->bo->size - rodata->offset)
         return false;

      ranges[count++] = (struct agx3_ioto_uniform_range){
         .start_halfs = rodata->base_uniform,
         .size_halfs = rodata->size_16,
         .address = s->bo->va->addr + rodata->offset,
      };
   }

   *range_count = count;
   return true;
}

void
hk_dispatch_g17p_native(struct hk_cmd_buffer *cmd, struct hk_cs *cs,
                        struct hk_shader *s, struct agx_grid grid,
                        struct agx_workgroup local_size)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   struct hk_linked_shader *linked = s ? s->only_linked : NULL;
   static_assert(HK_MAX_SETS == HK_AGX3_G17P_MAX_ROOT_SETS,
                 "G17P root range must cover every Honeykrisp set");

   if (dev->dev.chip != AGX_CHIP_G17P || cs->type != HK_CS_CDM ||
       !hk_g17p_compute_shader_supported(s, linked) ||
       agx_is_indirect(grid)) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   if (grid.count[0] == 0 || grid.count[1] == 0 || grid.count[2] == 0)
      return;

   const uint64_t root_ptr =
      hk_cmd_buffer_upload_root(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
   if (!root_ptr)
      return;

   struct agx3_ioto_uniform_range ranges[2];
   size_t range_count;
   if (!hk_g17p_compute_uniform_ranges(s, root_ptr, ranges, &range_count)) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   struct agx3_cdm_direct_template_sources launch_sources;
   if (!agx3_cdm_direct_template_sources_compute(
          s->b.info.uses_resource_info_query,
          s->b.info.uses_ray_traversal,
          s->b.info.ray_traversal_depth_nonzero, &launch_sources)) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   struct agx_bo *program_bo = agx_bo_create(
      &dev->dev, HK_AGX3_G17P_COMPUTE_PROGRAM_MAX_SIZE,
      AGX_IOTO_PROGRAM_ALIGNMENT, AGX_BO_EXEC | AGX_BO_LOW_VA,
      "G17P compute Ioto program");
   if (!program_bo) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }
   util_dynarray_append(&cmd->large_bos, program_bo);

   void *program_map = agx_bo_map(program_bo);
   if (!program_map) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_MEMORY_MAP_FAILED);
      return;
   }

   const struct hk_agx3_g17p_compute_program_desc desc = {
      .program_va = program_bo->va->addr,
      .shader_va = linked->b.bo->va->addr,
      .uniform_ranges = ranges,
      .uniform_range_count = range_count,
      .shader_binary_size = linked->b.size,
      .scratch_size = linked->b.scratch_size,
      .nr_gprs = linked->b.nr_gprs,
      .launch_sources = launch_sources,
   };
   struct hk_agx3_g17p_compute_program program;
   if (!hk_agx3_g17p_compute_program_generate(dev->dev.chip, &desc,
                                               &program)) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   memcpy(program_map, program.bytes, program.length);
   if (agx_bo_sync_for_device(program_bo, 0, program.length)) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_DEVICE_LOST);
      return;
   }

   const uint8_t dimensions =
      grid.count[2] != 1 || local_size.z != 1
         ? 3
         : grid.count[1] != 1 || local_size.y != 1 ? 2 : 1;
   const struct agx3_compute_direct_grid direct = {
      .source = AGX3_CDM_DIRECT_GRID_GLOBAL_THREADS,
      .size = {grid.count[0], grid.count[1], grid.count[2]},
      .local_size = {local_size.x, local_size.y, local_size.z},
      .dimensions = dimensions,
   };

   hk_ensure_cs_has_space(cmd, cs, 0x2000);
   struct agx3_compute_dispatch_sink sink = {
      .data = (uint8_t *)cs->current,
      .capacity = (uint8_t *)cs->end - (uint8_t *)cs->current,
   };
   if (!hk_agx3_g17p_native_compute_dispatch_append(&program.launch, &direct,
                                                     &sink)) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   cs->current = (uint32_t *)((uint8_t *)cs->current + sink.length);
   cs->stats.cmds++;
}

static void
dispatch(struct hk_cmd_buffer *cmd, struct agx_grid grid)
{
   struct hk_shader *s = hk_only_variant(cmd->state.cs.shader);
   if (agx_is_shader_empty(&s->b))
      return;

   struct hk_cs *cs = hk_cmd_buffer_get_cs(cmd, true /* compute */);
   if (!cs)
      return;

   struct agx_workgroup local_size =
      agx_workgroup(s->b.info.workgroup_size[0], s->b.info.workgroup_size[1],
                    s->b.info.workgroup_size[2]);

   uint64_t stat = hk_pipeline_stat_addr(
      cmd, VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT);

   if (hk_stat_enabled(stat)) {
      perf_debug(cmd, "CS invocation statistic");
      uint64_t grid = cmd->state.cs.descriptors.root.cs.group_count_addr;

      libagx_increment_cs_invocations(cmd, agx_1d(1), AGX_BARRIER_ALL, grid,
                                      stat, agx_workgroup_threads(local_size));
   }

   if (!agx_is_indirect(grid)) {
      grid.count[0] *= local_size.x;
      grid.count[1] *= local_size.y;
      grid.count[2] *= local_size.z;
   }

   hk_dispatch_with_local_size(cmd, cs, s, grid, local_size);
   cs->stats.calls++;
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX,
                   uint32_t baseGroupY, uint32_t baseGroupZ,
                   uint32_t groupCountX, uint32_t groupCountY,
                   uint32_t groupCountZ)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   struct hk_descriptor_state *desc = &cmd->state.cs.descriptors;
   if (desc->push_dirty)
      hk_cmd_buffer_flush_push_descriptors(cmd, desc);

   desc->root.cs.base_group[0] = baseGroupX;
   desc->root.cs.base_group[1] = baseGroupY;
   desc->root.cs.base_group[2] = baseGroupZ;

   /* We don't want to key the shader to whether we're indirectly dispatching,
    * so treat everything as indirect.
    */
   VkDispatchIndirectCommand group_count = {
      .x = groupCountX,
      .y = groupCountY,
      .z = groupCountZ,
   };

   desc->root.cs.group_count_addr =
      hk_pool_upload(cmd, &group_count, sizeof(group_count), 8);

   dispatch(cmd, agx_3d(groupCountX, groupCountY, groupCountZ));
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                       VkDeviceSize offset)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(hk_buffer, buffer, _buffer);
   struct hk_descriptor_state *desc = &cmd->state.cs.descriptors;
   if (desc->push_dirty)
      hk_cmd_buffer_flush_push_descriptors(cmd, desc);

   desc->root.cs.base_group[0] = 0;
   desc->root.cs.base_group[1] = 0;
   desc->root.cs.base_group[2] = 0;

   uint64_t dispatch_addr = hk_buffer_address(buffer, offset, true);
   assert(dispatch_addr != 0);

   desc->root.cs.group_count_addr = dispatch_addr;
   dispatch(cmd, agx_grid_indirect(dispatch_addr));
}
