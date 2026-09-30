/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_cmd_buffer.h"

#include "agx_abi.h"
#include "agx_bo.h"
#include "agx_device.h"
#include "agx_linker.h"
#include "agx_tilebuffer.h"
#include "agx_usc.h"
#include "hk_agx3_ioto_program_core.h"
#include "hk_agx3_vdm_decode.h"
#include "hk_buffer.h"
#include "hk_cmd_pool.h"
#include "hk_descriptor_prefetch.h"
#include "hk_descriptor_set.h"
#include "hk_descriptor_set_layout.h"
#include "hk_device.h"
#include "hk_device_memory.h"
#include "hk_entrypoints.h"
#include "hk_image.h"
#include "hk_image_view.h"
#include "hk_physical_device.h"
#include "hk_shader.h"
#include "nir_serialize.h"
#include "nir_builder.h"

#include "util/log.h"
#include "util/hash_table.h"
#include "util/u_debug.h"

#include <inttypes.h>
#include <stdio.h>

#include "asahi/genxml/agx3_cmdstream.h"
#include "asahi/lib/agx_ppp_hal300_depth_stencil_core.h"
#include "asahi/lib/agx_ppp_w_clamp_cull2_core.h"
#include "asahi/lib/agx_vdm_barrier_core.h"
#include "asahi/lib/agx_vdm_stream_link_core.h"
#include "asahi/lib/agx_vdm_stream_terminate_core.h"
#include "asahi/lib/agx_va_runtime_hole.h"
#include "libagx_dgc.h"
#include "pool.h"
#include "shader_enums.h"
#include "vk_pipeline_layout.h"
#include "vk_common_entrypoints.h"
#include "vk_synchronization.h"

#include "util/list.h"
#include "util/macros.h"
#include "util/u_dynarray.h"
#include "vulkan/vulkan_core.h"

/* These process options are immutable during command recording. */
DEBUG_GET_ONCE_BOOL_OPTION(hk_m4_program_profile, "HK_M4_PROGRAM_PROFILE", false)
DEBUG_GET_ONCE_BOOL_OPTION(hk_m4_shader_profile, "HK_M4_SHADER_PROFILE", false)
DEBUG_GET_ONCE_BOOL_OPTION(hk_m4_native_preamble, "HK_M4_NATIVE_PREAMBLE", false)

static uint64_t
hk_g17p_vdm_lookup(struct hk_cmd_buffer *cmd, uint64_t primary, bool bind)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   if (!hk_g17p_vdm_low_window_enabled(&dev->dev))
      return primary;

   struct hk_cmd_bo *found = cmd->vdm_lookup_bo;
   if (!found || primary < found->bo->va->addr ||
       primary - found->bo->va->addr >= found->bo->size) {
      found = NULL;
      list_for_each_entry(struct hk_cmd_bo, bo, &cmd->uploader.main.bos, link) {
         if (primary >= bo->bo->va->addr &&
             primary - bo->bo->va->addr < bo->bo->size) {
            found = bo;
            break;
         }
      }
   }

   if (found) {
      struct hk_cmd_bo *bo = found;
      const uint64_t start = bo->bo->va->addr;

      if (bind) {
         const VkResult result =
            hk_cmd_bo_ensure_vdm_alias(hk_cmd_buffer_pool(cmd), bo);
         if (result != VK_SUCCESS) {
            vk_command_buffer_set_error(&cmd->vk, result);
            return 0;
         }
         cmd->vdm_lookup_bo = bo;
      }

      const uint64_t alias = bo->g17p_vdm_alias + (primary - start);
      uint32_t compact = 0;
      if (agx_g17p_vdm_alias_compact(alias, &compact))
         return compact;
   }

   mesa_loge("G17P VDM address %#" PRIx64 " has no low command-BO alias",
             primary);
   return 0;
}

uint64_t
hk_g17p_vdm_compact(struct hk_cmd_buffer *cmd, uint64_t primary)
{
   return hk_g17p_vdm_lookup(cmd, primary, true);
}

uint64_t
hk_g17p_vdm_alias(struct hk_cmd_buffer *cmd, uint64_t primary)
{
   /* Submission may read a simultaneous-use CB from multiple queues. All
    * VDM roots/links were mapped during recording; never mutate them here. */
   const uint64_t compact = hk_g17p_vdm_lookup(cmd, primary, false);
   return compact ? AGX_G17P_VDM_USER_BASE + compact : 0;
}

static void
hk_descriptor_state_fini(struct hk_cmd_buffer *cmd,
                         struct hk_descriptor_state *desc)
{
   struct hk_cmd_pool *pool = hk_cmd_buffer_pool(cmd);

   for (unsigned i = 0; i < HK_MAX_SETS; i++) {
      vk_free(&pool->vk.alloc, desc->push[i]);
      desc->push[i] = NULL;
   }
}

static void
hk_free_resettable_cmd_buffer(struct hk_cmd_buffer *cmd)
{
   cmd->vdm_lookup_bo = NULL;
   memset(cmd->native_pointer_uploads, 0, sizeof(cmd->native_pointer_uploads));
   memset(cmd->native_prefetch_eligible, 0, sizeof(cmd->native_prefetch_eligible));
   struct hk_cmd_pool *pool = hk_cmd_buffer_pool(cmd);
   struct hk_device *dev = hk_cmd_pool_device(pool);

   /* Vulkan requires these commands to have retired before reset/destruction.
    * Counts are cumulative, including repeated submissions; zero does not
    * distinguish unsubmitted commands from draws with no covered fragments. */
   unsigned draw = 0;
   util_dynarray_foreach(&cmd->fragment_counts, struct hk_fragment_count, item) {
      const uint64_t guard = UINT64_C(0xc017c017c017c017);
      fprintf(stderr, "HK_FRAGMENT_COUNT cb=%p draw=%u cs=%" PRIx64
              " width=%u height=%u covered=%" PRIu64 " guard=%s\n",
              (void *)cmd, ++draw, item->cs, item->width, item->height,
              item->words[1], item->words[0] == guard &&
              item->words[2] == guard && item->words[3] == guard ? "PASS" : "FAIL");
   }
   util_dynarray_clear(&cmd->fragment_counts);

   hk_descriptor_state_fini(cmd, &cmd->state.gfx.descriptors);
   hk_descriptor_state_fini(cmd, &cmd->state.cs.descriptors);

   hk_cmd_pool_free_bo_list(pool, &cmd->uploader.main.bos);
   hk_cmd_pool_free_usc_bo_list(pool, &cmd->uploader.usc.bos);

   list_for_each_entry_safe(struct hk_cs, it, &cmd->control_streams, node) {
      list_del(&it->node);
      hk_cs_destroy(it);
   }

   hk_cmd_buffer_release_bos(cmd, &dev->dev);
}

static void
hk_destroy_cmd_buffer(struct vk_command_buffer *vk_cmd_buffer)
{
   struct hk_cmd_buffer *cmd =
      container_of(vk_cmd_buffer, struct hk_cmd_buffer, vk);
   struct hk_cmd_pool *pool = hk_cmd_buffer_pool(cmd);

   hk_free_resettable_cmd_buffer(cmd);
   for (unsigned i = 0; i < 2; ++i)
      vk_free(&pool->vk.alloc, cmd->uniform_templates[i]);
   util_dynarray_fini(&cmd->large_bos);
   util_dynarray_fini(&cmd->fragment_counts);
   vk_command_buffer_finish(&cmd->vk);
   vk_free(&pool->vk.alloc, cmd);
}

static VkResult
hk_create_cmd_buffer(struct vk_command_pool *vk_pool,
                     VkCommandBufferLevel level,
                     struct vk_command_buffer **cmd_buffer_out)
{
   struct hk_cmd_pool *pool = container_of(vk_pool, struct hk_cmd_pool, vk);
   struct hk_device *dev = hk_cmd_pool_device(pool);
   struct hk_cmd_buffer *cmd;
   VkResult result;

   cmd = vk_zalloc(&pool->vk.alloc, sizeof(*cmd), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = vk_command_buffer_init_with_params(&cmd->vk,
                                               &(struct vk_command_buffer_init_params) {
                                                  .pool = &pool->vk,
                                                  .ops = &hk_cmd_buffer_ops,
                                                  .level = level,
                                                  .needs_cmd_queue = level == VK_COMMAND_BUFFER_LEVEL_SECONDARY,
                                               });
   if (result != VK_SUCCESS) {
      vk_free(&pool->vk.alloc, cmd);
      return result;
   }

   cmd->large_bos = UTIL_DYNARRAY_INIT;
   cmd->fragment_counts = UTIL_DYNARRAY_INIT;

   cmd->vk.dynamic_graphics_state.vi = &cmd->state.gfx._dynamic_vi;
   cmd->vk.dynamic_graphics_state.ms.sample_locations =
      &cmd->state.gfx._dynamic_sl;

   list_inithead(&cmd->uploader.main.bos);
   list_inithead(&cmd->uploader.usc.bos);
   list_inithead(&cmd->control_streams);

   *cmd_buffer_out = &cmd->vk;

   return VK_SUCCESS;
}

static void
hk_reset_cmd_buffer(struct vk_command_buffer *vk_cmd_buffer,
                    UNUSED VkCommandBufferResetFlags flags)
{
   struct hk_cmd_buffer *cmd =
      container_of(vk_cmd_buffer, struct hk_cmd_buffer, vk);

   vk_command_buffer_reset(&cmd->vk);
   hk_free_resettable_cmd_buffer(cmd);

   cmd->uploader.main.map = NULL;
   cmd->uploader.main.base = 0;
   cmd->uploader.main.offset = 0;
   cmd->uploader.usc.map = NULL;
   cmd->uploader.usc.base = 0;
   cmd->uploader.usc.offset = 0;

   cmd->current_cs.gfx = NULL;
   cmd->current_cs.cs = NULL;
   cmd->current_cs.post_gfx = NULL;
   cmd->current_cs.pre_gfx = NULL;

   assert(!cmd->in_meta);
   cmd->geom_index_buffer = 0;
   cmd->geom_index_count = 0;
   cmd->geom_instance_count = 0;
   cmd->uses_heap = false;
   cmd->graphics_dependency_stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

   memset(&cmd->state, 0, sizeof(cmd->state));
}

const struct vk_command_buffer_ops hk_cmd_buffer_ops = {
   .create = hk_create_cmd_buffer,
   .reset = hk_reset_cmd_buffer,
   .destroy = hk_destroy_cmd_buffer,
};

VKAPI_ATTR void VKAPI_CALL
hk_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                      uint32_t commandBufferCount,
                      const VkCommandBuffer *pCommandBuffers)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);

   /* Secondary command buffers are materialized by replaying their common
    * command queues into the primary.  Their last bound state is therefore
    * also the driver's cached state in the primary after replay.  Vulkan
    * makes that state undefined (except for the active render pass), so an
    * application is required to bind it again.  Invalidate here so binding
    * the same object used by the secondary cannot be incorrectly elided.
    */
   /* Inherited conditional rendering is not advertised. A secondary can
    * establish its own condition, but does not inherit the primary's. */
   uint64_t conditional = cmd->state.conditional;
   cmd->state.conditional = 0;
   vk_common_CmdExecuteCommands(commandBuffer, commandBufferCount,
                                pCommandBuffers);
   cmd->state.conditional = conditional;
   hk_cmd_invalidate_graphics_state(cmd);
   hk_cmd_invalidate_compute_state(cmd);
}

static VkResult
hk_cmd_buffer_alloc_bo(struct hk_cmd_buffer *cmd, bool usc,
                       struct hk_cmd_bo **bo_out)
{
   VkResult result = hk_cmd_pool_alloc_bo(hk_cmd_buffer_pool(cmd), usc, bo_out);
   if (result != VK_SUCCESS)
      return result;

   if (usc)
      list_addtail(&(*bo_out)->link, &cmd->uploader.usc.bos);
   else
      list_addtail(&(*bo_out)->link, &cmd->uploader.main.bos);

   return VK_SUCCESS;
}

struct agx_ptr
hk_pool_alloc_internal(struct hk_cmd_buffer *cmd, uint32_t size,
                       uint32_t alignment, bool usc)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   struct hk_uploader *uploader =
      usc ? &cmd->uploader.usc : &cmd->uploader.main;

   /* Specially handle large allocations owned by the command buffer, e.g. used
    * for statically allocated vertex output buffers with geometry shaders.
    */
   if (size > HK_CMD_BO_SIZE) {
      uint32_t flags = usc ? AGX_BO_LOW_VA : 0;
      struct agx_bo *bo =
         agx_bo_create(&dev->dev, size, 0, flags, "Large pool allocation");

      if (unlikely(bo == NULL)) {
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         return (struct agx_ptr){0};
      }

      void *map = agx_bo_map(bo);
      if (unlikely(map == NULL)) {
         agx_bo_unreference(&dev->dev, bo);
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
         return (struct agx_ptr){0};
      }

      util_dynarray_append(&cmd->large_bos, bo);
      return (struct agx_ptr){
         .gpu = bo->va->addr,
         .cpu = map,
      };
   }

   assert(size <= HK_CMD_BO_SIZE);
   assert(alignment > 0);

   uint32_t offset = align(uploader->offset, alignment);

   assert(offset <= HK_CMD_BO_SIZE);
   if (uploader->map != NULL && size <= HK_CMD_BO_SIZE - offset) {
      uploader->offset = offset + size;

      return (struct agx_ptr){
         .gpu = uploader->base + offset,
         .cpu = uploader->map + offset,
      };
   }

   struct hk_cmd_bo *bo;
   VkResult result = hk_cmd_buffer_alloc_bo(cmd, usc, &bo);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return (struct agx_ptr){0};
   }

   /* Pick whichever of the current upload BO and the new BO will have more
    * room left to be the BO for the next upload.  If our upload size is
    * bigger than the old offset, we're better off burning the whole new
    * upload BO on this one allocation and continuing on the current upload
    * BO.
    */
   if (uploader->map == NULL || size < uploader->offset) {
      uploader->map = agx_bo_map(bo->bo);
      uploader->base = bo->bo->va->addr;
      uploader->offset = size;
   }

   return (struct agx_ptr){
      .gpu = bo->bo->va->addr,
      .cpu = bo->map,
   };
}

uint64_t
hk_pool_upload(struct hk_cmd_buffer *cmd, const void *data, uint32_t size,
               uint32_t alignment)
{
   struct agx_ptr T = hk_pool_alloc(cmd, size, alignment);
   if (unlikely(T.cpu == NULL))
      return 0;

   memcpy(T.cpu, data, size);
   return T.gpu;
}

uint64_t
hk_pool_usc_upload(struct hk_cmd_buffer *cmd, const void *data, uint32_t size,
                   uint32_t alignment)
{
   struct agx_ptr T = hk_pool_usc_alloc(cmd, size, alignment);
   if (unlikely(T.cpu == NULL))
      return 0;

   memcpy(T.cpu, data, size);
   return T.gpu;
}

void *
hk_cmd_buffer_emit_cs_jump(struct hk_cmd_buffer *cmd, void *current,
                           uint64_t target, bool vdm)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   if (vdm && hk_g17p_vdm_low_window_enabled(&dev->dev))
      target = hk_g17p_vdm_compact(cmd, target);

   switch (agx_chip_command_stream_generation(dev->dev.chip)) {
   case AGX_CS_GENERATION_AGX2:
      return agx_cs_jump(current, target, vdm);

   case AGX_CS_GENERATION_G15:
   case AGX_CS_GENERATION_HAL200:
   case AGX_CS_GENERATION_HAL300:
      if (vdm) {
         struct agx_vdm_stream_link_core link;
         if (agx_vdm_stream_link_core_pack(dev->dev.chip, target, &link)) {
            memcpy(current, link.bytes, link.length);
            return (uint8_t *)current + link.length;
         }
      } else {
         const struct agx3_cdm_stream_link_info info = {
            .chip = dev->dev.chip,
            .target = target,
            .with_return = false,
         };
         uint8_t link[AGX3_CDM_STREAM_LINK_SIZE];
         if (agx3_cdm_stream_link_pack(&info, link)) {
            memcpy(current, link, sizeof(link));
            return (uint8_t *)current + sizeof(link);
         }
      }
      break;

   case AGX_CS_GENERATION_UNKNOWN:
   default:
      break;
   }

   mesa_loge("no grounded %s stream link for %s", vdm ? "VDM" : "CDM",
             agx_chip_name(dev->dev.chip));
   vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
   return current;
}

/* Diagnostics for the G17P native compute/graphics route.
 *
 * The raw add3 probe and a Honeykrisp dispatch drive the *same* kernel path
 * with the same 44-byte launch + 4-byte terminate shape, yet only the
 * Honeykrisp one makes the firmware request a stamp-blamed recovery. The two
 * objects that differ are the CDM stream as a whole and the composed Ioto
 * program; both are small, so dump them verbatim and diff against the
 * known-good 48-byte add3 stream instead of reasoning about them.
 */
bool
hk_g17p_launch_trace_enabled(void)
{
   static int enabled = -1;
   if (enabled < 0)
      enabled = debug_get_bool_option("HK_G17P_LAUNCH_TRACE", false);
   return enabled != 0;
}

/* Single line, distinct tag. The multi-line dump below has twice been reported
 * as "only the header printed", so anything that must not be lost in a log
 * filter goes through this instead. */
void
hk_g17p_trace_line(const char *label, uint64_t base, const void *data,
                   size_t len)
{
   const uint8_t *bytes = data;
   fprintf(stderr, "G17P %s: base=%" PRIx64 " len=%zu bytes=", label, base,
           len);
   for (size_t i = 0; i < len; ++i)
      fprintf(stderr, "%02x", bytes[i]);
   fprintf(stderr, "\n");
   fflush(stderr);
}

void
hk_g17p_trace_hexdump(const char *label, uint64_t base, const void *data,
                      size_t len)
{
   const uint8_t *bytes = data;
   fprintf(stderr, "G17P dump: %s base=%" PRIx64 " len=%zu\n", label, base,
           len);
   for (size_t off = 0; off < len; off += 16) {
      fprintf(stderr, "G17P dump:   +%04zx ", off);
      for (size_t i = 0; i < 16 && off + i < len; ++i)
         fprintf(stderr, "%02x ", bytes[off + i]);
      fprintf(stderr, "\n");
   }
   fflush(stderr);
}

void *
hk_cmd_buffer_emit_cdm_terminate(struct hk_cmd_buffer *cmd, void *current)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   switch (agx_chip_command_stream_generation(dev->dev.chip)) {
   case AGX_CS_GENERATION_AGX2:
      return agx_cdm_terminate(current);

   case AGX_CS_GENERATION_G15:
   case AGX_CS_GENERATION_HAL200:
   case AGX_CS_GENERATION_HAL300: {
      uint8_t terminate[AGX3_CDM_STREAM_TERMINATE_SIZE];
      if (agx3_cdm_stream_terminate_pack(dev->dev.chip, terminate)) {
         memcpy(current, terminate, sizeof(terminate));
         return (uint8_t *)current + sizeof(terminate);
      }
      break;
   }

   case AGX_CS_GENERATION_UNKNOWN:
   default:
      break;
   }

   mesa_loge("no grounded CDM stream terminator for %s",
             agx_chip_name(dev->dev.chip));
   vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
   return current;
}

void *
hk_cmd_buffer_emit_vdm_terminate(struct hk_cmd_buffer *cmd, void *current)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   /* Draw emission leaves the stream open for further state, draws or links.
    * EndCommandStream supplies exactly one terminal record here. Do not infer
    * termination from the preceding dword: it may be a draw's vertex_start.
    */
   switch (agx_chip_command_stream_generation(dev->dev.chip)) {
   case AGX_CS_GENERATION_AGX2:
      return agx_vdm_terminate(current);

   case AGX_CS_GENERATION_G15:
   case AGX_CS_GENERATION_HAL200:
   case AGX_CS_GENERATION_HAL300: {
      struct agx_vdm_stream_terminate_core terminate;
      if (agx_vdm_stream_terminate_core_pack(dev->dev.chip, &terminate)) {
         memcpy(current, terminate.bytes, terminate.length);
         return (uint8_t *)current + terminate.length;
      }
      break;
   }

   case AGX_CS_GENERATION_UNKNOWN:
   default:
      break;
   }

   mesa_loge("no grounded VDM stream terminator for %s",
             agx_chip_name(dev->dev.chip));
   vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
   return current;
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                      const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   hk_reset_cmd_buffer(&cmd->vk, 0);

   perf_debug(cmd, "Begin command buffer");
   hk_cmd_buffer_begin_compute(cmd, pBeginInfo);
   hk_cmd_buffer_begin_graphics(cmd, pBeginInfo);

   return VK_SUCCESS;
}

/*
 * Merge adjacent compute control streams. Except for reading timestamps, there
 * is no reason to submit two CDM streams back-to-back in the same command
 * buffer. However, it is challenging to avoid constructing such sequences due
 * to the gymnastics required to reorder compute around graphics. Merging at
 * EndCommandBuffer is cheap O(# of control streams) and lets us get away with
 * the sloppiness.
 */
static void
merge_control_streams(struct hk_cmd_buffer *cmd)
{
   struct hk_cs *last = NULL;
   const bool batch_clears =
      hk_cmd_buffer_device(cmd)->dev.chip == AGX_CHIP_G16G &&
      debug_get_bool_option("HK_M4_BATCH_DISJOINT_CLEARS", false);
   const bool batch_fills =
      agx_chip_uses_g15_g16_shader_abi(hk_cmd_buffer_device(cmd)->dev.chip) &&
      debug_get_bool_option("HK_M4_BATCH_META_FILLS",
         debug_get_bool_option("HK_M4_CDM_VISIBILITY_BARRIER", true));
   const bool batch_copies =
      agx_chip_uses_g15_g16_shader_abi(hk_cmd_buffer_device(cmd)->dev.chip) &&
      debug_get_bool_option("HK_M4_BATCH_META_COPIES", true);
   const bool batch_queries =
      hk_cmd_buffer_device(cmd)->dev.chip == AGX_CHIP_G16G &&
      debug_get_bool_option("HK_M4_BATCH_QUERY_COPIES", false);

   /* Most dispatches have an incomplete destination ledger. Only after all
    * recording is finished can we prove this stream contains clears alone
    * and replace that ledger with their complete whole-plane footprints. */
   list_for_each_entry(struct hk_cs, cs, &cmd->control_streams, node) {
      if (batch_queries && cs->type == HK_CS_CDM && cs->query_dispatches &&
          cs->query_dispatches == cs->stats.cmds)
         cs->compute_attachments = cs->query_destinations;
      if (batch_copies && cs->type == HK_CS_CDM && cs->copy_dispatches &&
          cs->copy_dispatches == cs->stats.cmds)
         cs->compute_attachments = cs->copy_destinations;
      if (batch_fills && cs->type == HK_CS_CDM && cs->fill_dispatches &&
          cs->fill_dispatches == cs->stats.cmds)
         cs->compute_attachments = cs->fill_destinations;
      if (batch_clears && cs->type == HK_CS_CDM && cs->clear_dispatches &&
          cs->clear_dispatches == cs->stats.cmds)
         cs->compute_attachments = cs->clear_destinations;
   }

   list_for_each_entry_safe(struct hk_cs, cs, &cmd->control_streams, node) {
      /* A timestamp-only command immediately before a render measures the
       * same ordered boundary as that render's tiling-start timestamp. The
       * native runtime completes all preceding commands before starting TA.
       * G15S/G16G can optionally use a following compute's start in the same
       * way. Both kernels expose native compute start/end timestamps; this
       * does not require the separate timestamp-copy capability.
       * Preserve resets and dependency boundaries, and never discard work
       * that was subsequently appended to the no-op stream. A reset successor
       * must not overwrite a query result just published at its start. Query
       * copies also need the completed producer's visibility boundary: the
       * firmware start write is not visible to that same CDM's query reads.
       */
      if (agx_native_graphics(&hk_cmd_buffer_device(cmd)->dev) && last &&
          last->timestamp_only && last->stats.cmds == 1 &&
          last->timestamp.end.handle && !last->timestamp.start.handle &&
          !last->timestamp.end_copy_count &&
          !last->ends_timestamp_reset && !last->ends_memory_dependency &&
          !cs->timestamp.start.handle &&
          (cs->type == HK_CS_VDM ||
           (agx_chip_uses_g15_g16_shader_abi(hk_cmd_buffer_device(cmd)->dev.chip) &&
            debug_get_bool_option("HK_M4_COMPUTE_START_TIMESTAMP", false) &&
            cs->type == HK_CS_CDM && cs->stats.cmds > 0 &&
            !cs->ends_timestamp_reset && !cs->stats.query_copies))) {
         cs->timestamp.start = last->timestamp.end;
         list_del(&last->node);
         hk_cs_destroy(last);
         last = cs;
         continue;
      }
      if (cs->type == HK_CS_CDM && last && last->type == HK_CS_CDM &&
          !last->timestamp.end.handle && !cs->timestamp.start.handle &&
          /* Reset-only successors can share the same completion flush. Keep
           * the boundary before the subsequent timestamp producer/consumer,
           * which runs through a different firmware memory agent. Zink emits
           * many adjacent single-query resets in its reordered command buffer.
           */
          (!last->ends_timestamp_reset || cs->ends_timestamp_reset) &&
          (!last->ends_memory_dependency ||
           /* Keep the actual native visibility operation in the linked
            * stream. A memory dependency no longer needs a separate firmware
            * job solely to publish aliased writes. Reset/timestamp ownership
            * and complete attachment ledgers still gate merging. */
           (last->ends_wide_visibility &&
            debug_get_bool_option("HK_M4_BATCH_VISIBLE_COMPUTE", true)) ||
           /* A clear cannot consume another disjoint clear's writes. Keep
            * ordered CDM launches and publish both at the merged completion
            * boundary, before any subsequent reader. Whole-plane canonical
            * ranges exclude aliases, imports and sparse mappings. */
           (batch_clears && last->clear_dispatches == last->stats.cmds &&
            cs->clear_dispatches == cs->stats.cmds &&
            !last->ends_timestamp_reset && !cs->ends_timestamp_reset &&
            hk_drm_attachment_sets_disjoint(&last->clear_destinations,
                                             &cs->clear_destinations))) &&
          hk_drm_attachment_set_merge(&last->compute_attachments,
                                       &cs->compute_attachments,
                                       hk_cmd_buffer_device(cmd)->dev.params.max_attachments)) {

         hk_cs_merge_cdm(last, cs);
         list_del(&cs->node);
         hk_cs_destroy(cs);
      } else {
         last = cs;
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);

   assert(cmd->current_cs.gfx == NULL && cmd->current_cs.pre_gfx == NULL &&
          "must end rendering before ending the command buffer");

   perf_debug(cmd, "End command buffer");
   hk_cmd_buffer_end_compute(cmd);
   hk_cmd_buffer_end_compute_internal(cmd, &cmd->current_cs.post_gfx);

   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   if (dev->dev.chip == AGX_CHIP_G16G &&
       debug_get_bool_option("HK_M4_DEFER_STATISTICS_FLUSH", true)) {
      bool statistics = false;
      list_for_each_entry(struct hk_cs, cs, &cmd->control_streams, node)
         statistics |= cs->type == HK_CS_VDM && cs->writes_statistics;

      /* Query counters are private, non-aliased storage accessed by atomics
       * while active. EndQuery still publishes them before availability.
       * Also publish at the CB boundary for split queries and diagnostics.
       * Ordinary shader memory dependencies retain their per-pass flush. */
      const struct hk_cs *tail = list_is_empty(&cmd->control_streams) ? NULL :
         list_last_entry(&cmd->control_streams, struct hk_cs, node);
      /* A final compute already publishes all preceding fragment atomics
       * when its exact wide-visibility operation is retained. Do not add a
       * second firmware job solely to repeat that command-buffer boundary. */
      const bool published = tail && tail->type == HK_CS_CDM &&
                             tail->ends_wide_visibility;
      if (statistics && !published) {
         struct hk_cs *flush = hk_cmd_buffer_get_cs(cmd, true);
         if (!flush)
            return vk_command_buffer_get_record_result(&cmd->vk);
         hk_cdm_cache_flush(dev, flush);
         hk_cmd_buffer_end_compute(cmd);
      }
   }

   if (likely(!(dev->dev.debug & AGX_DBG_NOMERGE))) {
      merge_control_streams(cmd);
   }

   /* We cannot terminate CDM control streams until after merging, since merging
    * needs to append stream links late. Now that we've merged, insert all the
    * missing stream terminates.
    */
   list_for_each_entry(struct hk_cs, cs, &cmd->control_streams, node) {
      if (cs->type == HK_CS_CDM) {
         cs->current = hk_cmd_buffer_emit_cdm_terminate(cmd, cs->current);
      }
   }

   /* Publish pooled executable uploads once after recording. Linux uses WC
    * mappings; retain the platform synchronization hook for other backends. */
   if (dev->dev.chip == AGX_CHIP_G16G) {
      list_for_each_entry(struct hk_cmd_bo, bo, &cmd->uploader.usc.bos, link) {
         if (agx_bo_sync_for_device(bo->bo, 0, bo->bo->size))
            vk_command_buffer_set_error(&cmd->vk, VK_ERROR_DEVICE_LOST);
      }
   }

   return vk_command_buffer_get_record_result(&cmd->vk);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                       const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   if (HK_PERF(dev, NOBARRIER))
      return;

   perf_debug(cmd, "Pipeline barrier");

   VkPipelineStageFlags2 destinations = 0;
   for (unsigned i = 0; i < pDependencyInfo->memoryBarrierCount; i++)
      destinations |= pDependencyInfo->pMemoryBarriers[i].dstStageMask;
   for (unsigned i = 0; i < pDependencyInfo->bufferMemoryBarrierCount; i++)
      destinations |= pDependencyInfo->pBufferMemoryBarriers[i].dstStageMask;
   for (unsigned i = 0; i < pDependencyInfo->imageMemoryBarrierCount; i++)
      destinations |= pDependencyInfo->pImageMemoryBarriers[i].dstStageMask;

   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip)) {
      VkAccessFlags2 writes = 0;
      for (unsigned i = 0; i < pDependencyInfo->memoryBarrierCount; i++) {
         const VkMemoryBarrier2 *b = &pDependencyInfo->pMemoryBarriers[i];
         writes |= vk_filter_src_access_flags2(b->srcStageMask, b->srcAccessMask);
      }
      for (unsigned i = 0; i < pDependencyInfo->bufferMemoryBarrierCount; i++) {
         const VkBufferMemoryBarrier2 *b = &pDependencyInfo->pBufferMemoryBarriers[i];
         writes |= vk_filter_src_access_flags2(b->srcStageMask, b->srcAccessMask);
      }
      for (unsigned i = 0; i < pDependencyInfo->imageMemoryBarrierCount; i++) {
         const VkImageMemoryBarrier2 *b = &pDependencyInfo->pImageMemoryBarriers[i];
         writes |= vk_filter_src_access_flags2(b->srcStageMask, b->srcAccessMask);
      }
      /* CDM's post-launch barrier orders accesses through the same VA but
       * does not publish writes to another VA of the same physical page.
       * Preserve the firmware completion flush for memory dependencies.
       * Execution-only dependencies can still be merged. */
      if (writes) {
         struct hk_cs *streams[] = {cmd->current_cs.cs, cmd->current_cs.pre_gfx,
                                    cmd->current_cs.post_gfx};
         for (unsigned i = 0; i < ARRAY_SIZE(streams); i++) {
            if (streams[i])
               streams[i]->ends_memory_dependency = true;
         }
      }
   }

   /* The big hammer. We end both compute and graphics batches. Ending compute
    * here is necessary to properly handle graphics->compute dependencies.
    *
    * XXX: perf. */
   hk_cmd_buffer_end_compute(cmd);
   hk_cmd_buffer_end_graphics(cmd);
   cmd->graphics_dependency_stages |= destinations;
}

void
hk_cmd_bind_shaders(struct vk_command_buffer *vk_cmd, uint32_t stage_count,
                    const mesa_shader_stage *stages,
                    struct vk_shader **const shaders)
{
   struct hk_cmd_buffer *cmd = container_of(vk_cmd, struct hk_cmd_buffer, vk);

   for (uint32_t i = 0; i < stage_count; i++) {
      struct hk_api_shader *shader =
         container_of(shaders[i], struct hk_api_shader, vk);

      if (stages[i] == MESA_SHADER_COMPUTE || stages[i] == MESA_SHADER_KERNEL)
         hk_cmd_bind_compute_shader(cmd, shader);
      else
         hk_cmd_bind_graphics_shader(cmd, stages[i], shader);
   }
}

static void
hk_bind_descriptor_sets(UNUSED struct hk_cmd_buffer *cmd,
                        struct hk_descriptor_state *desc,
                        const VkBindDescriptorSetsInfoKHR *info)
{
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout, info->layout);

   /* Fro the Vulkan 1.3.275 spec:
    *
    *    "When binding a descriptor set (see Descriptor Set Binding) to
    *    set number N...
    *
    *    If, additionally, the previously bound descriptor set for set
    *    N was bound using a pipeline layout not compatible for set N,
    *    then all bindings in sets numbered greater than N are
    *    disturbed."
    *
    * This means that, if some earlier set gets bound in such a way that
    * it changes set_dynamic_buffer_start[s], this binding is implicitly
    * invalidated.
    */
   uint8_t dyn_buffer_start =
      pipeline_layout->dynamic_descriptor_offset[info->firstSet];

   uint32_t next_dyn_offset = 0;
   for (uint32_t i = 0; i < info->descriptorSetCount; ++i) {
      unsigned s = i + info->firstSet;
      VK_FROM_HANDLE(hk_descriptor_set, set, info->pDescriptorSets[i]);

      if (desc->sets[s] != set) {
         if (set != NULL) {
            desc->root.sets[s] = hk_descriptor_set_addr(set);
            desc->set_sizes[s] = set->size;
         } else {
            desc->root.sets[s] = 0;
            desc->set_sizes[s] = 0;
         }
         desc->sets[s] = set;
         desc->sets_dirty |= BITFIELD_BIT(s);

         /* Binding descriptors invalidates push descriptors */
         desc->push_dirty &= ~BITFIELD_BIT(s);
      }

      desc->root.set_dynamic_buffer_start[s] = dyn_buffer_start;

      if (pipeline_layout->set_layouts[s] != NULL) {
         const struct hk_descriptor_set_layout *set_layout =
            vk_to_hk_descriptor_set_layout(pipeline_layout->set_layouts[s]);

         if (set != NULL && set_layout->vk.dynamic_descriptor_count > 0) {
            for (uint32_t j = 0; j < set_layout->vk.dynamic_descriptor_count; j++) {
               struct hk_buffer_address addr = set->dynamic_buffers[j];
               addr.base_addr += info->pDynamicOffsets[next_dyn_offset + j];
               desc->root.dynamic_buffers[dyn_buffer_start + j] = addr;
            }
            next_dyn_offset += set->layout->vk.dynamic_descriptor_count;
         }

         dyn_buffer_start += set_layout->vk.dynamic_descriptor_count;
      } else {
         assert(set == NULL);
      }
   }
   assert(dyn_buffer_start <= HK_MAX_DYNAMIC_BUFFERS);
   assert(next_dyn_offset <= info->dynamicOffsetCount);

   desc->root_dirty = true;
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdBindDescriptorSets2KHR(
   VkCommandBuffer commandBuffer,
   const VkBindDescriptorSetsInfoKHR *pBindDescriptorSetsInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);

   if (pBindDescriptorSetsInfo->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS) {
      hk_bind_descriptor_sets(cmd, &cmd->state.gfx.descriptors,
                              pBindDescriptorSetsInfo);
   }

   if (pBindDescriptorSetsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      hk_bind_descriptor_sets(cmd, &cmd->state.cs.descriptors,
                              pBindDescriptorSetsInfo);
   }
}

static void
hk_push_constants(UNUSED struct hk_cmd_buffer *cmd,
                  struct hk_descriptor_state *desc,
                  const VkPushConstantsInfoKHR *info)
{
   memcpy(desc->root.push + info->offset, info->pValues, info->size);
   desc->root_dirty = true;
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdPushConstants2KHR(VkCommandBuffer commandBuffer,
                        const VkPushConstantsInfoKHR *pPushConstantsInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS)
      hk_push_constants(cmd, &cmd->state.gfx.descriptors, pPushConstantsInfo);

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT)
      hk_push_constants(cmd, &cmd->state.cs.descriptors, pPushConstantsInfo);
}

static struct hk_push_descriptor_set *
hk_cmd_push_descriptors(struct hk_cmd_buffer *cmd,
                        struct hk_descriptor_state *desc, uint32_t set)
{
   assert(set < HK_MAX_SETS);
   if (unlikely(desc->push[set] == NULL)) {
      desc->push[set] =
         vk_zalloc(&cmd->vk.pool->alloc, sizeof(*desc->push[set]), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (unlikely(desc->push[set] == NULL)) {
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
         return NULL;
      }
   }

   /* Pushing descriptors replaces whatever sets are bound */
   desc->sets[set] = NULL;
   desc->push_dirty |= BITFIELD_BIT(set);

   return desc->push[set];
}

static void
hk_push_descriptor_set(struct hk_cmd_buffer *cmd,
                       struct hk_descriptor_state *desc,
                       const VkPushDescriptorSetInfoKHR *info)
{
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout, info->layout);

   struct hk_push_descriptor_set *push_set =
      hk_cmd_push_descriptors(cmd, desc, info->set);
   if (unlikely(push_set == NULL))
      return;

   struct hk_descriptor_set_layout *set_layout =
      vk_to_hk_descriptor_set_layout(pipeline_layout->set_layouts[info->set]);

   hk_push_descriptor_set_update(push_set, set_layout,
                                 info->descriptorWriteCount,
                                 info->pDescriptorWrites);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdPushDescriptorSet2KHR(
   VkCommandBuffer commandBuffer,
   const VkPushDescriptorSetInfoKHR *pPushDescriptorSetInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);

   if (pPushDescriptorSetInfo->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS) {
      hk_push_descriptor_set(cmd, &cmd->state.gfx.descriptors,
                             pPushDescriptorSetInfo);
   }

   if (pPushDescriptorSetInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      hk_push_descriptor_set(cmd, &cmd->state.cs.descriptors,
                             pPushDescriptorSetInfo);
   }
}

void
hk_cmd_buffer_flush_push_descriptors(struct hk_cmd_buffer *cmd,
                                     struct hk_descriptor_state *desc)
{
   u_foreach_bit(set_idx, desc->push_dirty) {
      struct hk_push_descriptor_set *push_set = desc->push[set_idx];
      uint64_t push_set_addr = hk_pool_upload(
         cmd, push_set->data, sizeof(push_set->data), HK_MIN_UBO_ALIGNMENT);

      desc->root.sets[set_idx] = push_set_addr;
      desc->set_sizes[set_idx] = sizeof(push_set->data);
   }

   desc->root_dirty = true;
   desc->push_dirty = 0;
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdPushDescriptorSetWithTemplate2KHR(
   VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfoKHR
                                     *pPushDescriptorSetWithTemplateInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(vk_descriptor_update_template, template,
                  pPushDescriptorSetWithTemplateInfo->descriptorUpdateTemplate);
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout,
                  pPushDescriptorSetWithTemplateInfo->layout);

   struct hk_descriptor_state *desc =
      hk_get_descriptors_state(cmd, template->bind_point);
   struct hk_push_descriptor_set *push_set = hk_cmd_push_descriptors(
      cmd, desc, pPushDescriptorSetWithTemplateInfo->set);
   if (unlikely(push_set == NULL))
      return;

   struct hk_descriptor_set_layout *set_layout = vk_to_hk_descriptor_set_layout(
      pipeline_layout->set_layouts[pPushDescriptorSetWithTemplateInfo->set]);

   hk_push_descriptor_set_update_template(
      push_set, set_layout, template,
      pPushDescriptorSetWithTemplateInfo->pData);
}

uint64_t
hk_cmd_buffer_upload_root(struct hk_cmd_buffer *cmd,
                          VkPipelineBindPoint bind_point)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   struct hk_descriptor_state *desc = hk_get_descriptors_state(cmd, bind_point);
   struct hk_root_descriptor_table *root = &desc->root;

   struct agx_ptr root_ptr = hk_pool_alloc(cmd, sizeof(*root), 8);
   if (!root_ptr.gpu)
      return 0;

   root->root_desc_addr = root_ptr.gpu;
   root->printf_buffer_address = dev->dev.printf_buffer_address;
   root->sparse_residency_map = dev->sparse_residency
      ? dev->sparse_residency->va->addr : 0;
   root->sparse_va_base = dev->dev.main_heap_base;
   root->sparse_va_size = dev->dev.main_heap_size;
   root->buffer_alias_table = dev->buffer_alias_table ? dev->buffer_alias_table->va->addr : 0;
   root->atomic64_lock = dev->atomic64_lock ? dev->atomic64_lock->va->addr : 0;

   memcpy(root_ptr.cpu, root, sizeof(*root));
   return root_ptr.gpu;
}

static uint64_t
hk_upload_native_spilled_rt_descs(struct hk_cmd_buffer *cmd)
{
   const struct hk_rendering_state *render = &cmd->state.gfx.render;
   uint8_t entries[HK_MAX_RTS][AGX_G17P_EMRT_DESCRIPTOR_SIZE] = {0};
   for (unsigned rt = 0; rt < render->color_att_count; ++rt) {
      if (!render->tilebuffer.spilled[rt])
         continue;
      const struct hk_image_view *view = render->color_att[rt].iview;
      if (!view || !view->planes[0].emrt_software_valid ||
          !view->planes[0].emrt_read_heap_index ||
          !view->planes[0].emrt_write_heap_index) {
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FORMAT_NOT_SUPPORTED);
         return 0;
      }
      memcpy(entries[rt], &view->planes[0].emrt_texture, 32);
      memcpy(entries[rt] + 32, &view->planes[0].emrt_software, 32);
      memcpy(entries[rt] + AGX_G17P_STORAGE_HEAP_INDEX_OFFSET,
             &view->planes[0].emrt_write_heap_index, 4);
      memcpy(entries[rt] + AGX_G17P_SAMPLED_HEAP_INDEX_OFFSET,
             &view->planes[0].emrt_read_heap_index, 4);
   }
   return hk_pool_upload(cmd, entries, render->color_att_count * sizeof(entries[0]), 32);
}

void
hk_usc_upload_spilled_rt_descs(struct agx_usc_builder *b,
                               struct hk_cmd_buffer *cmd)
{
   struct hk_rendering_state *render = &cmd->state.gfx.render;
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   const unsigned generation =
      agx_chip_descriptor_generation(dev->dev.chip);

   if (!hk_native_pbe_hardware_supported(generation)) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   const unsigned texture_size = hk_texture_descriptor_size(generation);
   const unsigned pbe_size = hk_pbe_descriptor_size(generation);
   const unsigned pair_size = texture_size + pbe_size;

   /* Upload texture/PBE descriptors for each render target so we can clear
    * spilled render targets.
    */
   struct agx_ptr descs =
      hk_pool_alloc(cmd, pair_size * render->color_att_count, 64);
   uint8_t *desc = descs.cpu;
   if (!desc)
      return;

   for (unsigned i = 0; i < render->color_att_count; ++i) {
      struct hk_image_view *iview = render->color_att[i].iview;
      if (!iview) {
         /* XXX: probably should emit a null descriptor here...? */
         continue;
      }

      if (!iview->planes[0].emrt_pbe_valid) {
         vk_command_buffer_set_error(&cmd->vk,
                                     VK_ERROR_FORMAT_NOT_SUPPORTED);
         return;
      }

      memcpy(desc + i * pair_size, &iview->planes[0].emrt_texture,
             texture_size);
      memcpy(desc + i * pair_size + texture_size,
             &iview->planes[0].emrt_pbe, pbe_size);
   }

   /* Bind the base as u0_u1 for bindless access */
   agx_usc_uniform(b, 0, 4, hk_pool_upload(cmd, &descs.gpu, 8, 8));
}

void
hk_reserve_scratch(struct hk_cmd_buffer *cmd, struct hk_cs *cs,
                   struct hk_shader *s)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   uint32_t max_scratch_size =
      MAX2(s->b.info.scratch_size, s->b.info.preamble_scratch_size);

   /* Not scratch but this is the most convenient place for this... */
   cs->uses_sampler_heap |= s->b.info.uses_sampler_heap;

   /* G15S/G16G firmware owns the USC private-memory pool. Native IOTO
    * declares scratch size; AGX2 helper programs are not part of this ABI. */
   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip))
      return;

   if (max_scratch_size == 0)
      return;

   unsigned preamble_size = (s->b.info.preamble_scratch_size > 0) ? 1 : 0;

   /* Note: this uses the hardware stage, not the software stage */
   hk_device_alloc_scratch(dev, s->b.info.stage, max_scratch_size);
   perf_debug(cmd, "Reserving %u (%u) bytes of scratch for stage %s",
              s->b.info.scratch_size, s->b.info.preamble_scratch_size,
              _mesa_shader_stage_to_abbrev(s->b.info.stage));

   switch (s->b.info.stage) {
   case MESA_SHADER_FRAGMENT:
      cs->scratch.fs.main = true;
      cs->scratch.fs.preamble = MAX2(cs->scratch.fs.preamble, preamble_size);
      break;
   case MESA_SHADER_VERTEX:
      cs->scratch.vs.main = true;
      cs->scratch.vs.preamble = MAX2(cs->scratch.vs.preamble, preamble_size);
      break;
   default:
      cs->scratch.cs.main = true;
      cs->scratch.cs.preamble = MAX2(cs->scratch.cs.preamble, preamble_size);
      break;
   }
}

static bool
hk_g17p_basic_fragment_layout(struct hk_cmd_buffer *cmd)
{
   struct agx_tilebuffer_layout tib = cmd->state.gfx.render.tilebuffer;
   if (!tib.nr_samples && !tib.sample_size_B &&
       cmd->vk.dynamic_graphics_state.rs.rasterizer_discard_enable) {
      /* The native null fragment program needs a valid launch descriptor.
       * Keep the render state's sample count deferred for later draws. */
      agx_tilebuffer_set_samples(&tib, 1);
   }
   return hk_agx3_g17p_rt0_tile_layout_supported(&tib);
}

/* Keep native interpolation bounded to the f32 UVS layout already supported
 * by hk_agx3_g17p_vertex_program_info, including M4's larger UVS footprint.
 * HAL200 updateVaryingTable (0x3efa58) uses the same CF entry encoding for
 * smooth, linear and flat inputs, including their mixture in one shader.
 * Flat inputs support points, lines and non-fan triangles. The compiler
 * selects C and therefore must not admit the first-fan layout. Position Z,
 * reciprocal W and point coordinates use native system coefficient sources.
 */
bool
hk_g17p_smooth_varyings_supported(struct hk_cmd_buffer *cmd,
                                 const struct hk_linked_shader *linked)
{
   if (!linked)
      return false;
   const struct agx_varyings_fs *cf = &linked->b.cf;
   if (cf->nr_bindings == 0)
      return cf->nr_cf == 0;

   const struct hk_graphics_state *gfx = &cmd->state.gfx;
   const struct hk_shader *vs = gfx->shaders[MESA_SHADER_VERTEX]
      ? hk_bound_hw_vs(&cmd->state.gfx) : NULL;
   const unsigned max_user_words =
      hk_cmd_buffer_device(cmd)->dev.chip == AGX_CHIP_G16G ?
         HK_AGX3_G16G_MAX_USER_WORDS : HK_AGX3_G17P_MAX_USER_WORDS;
   if (!vs ||
       cf->nr_bindings > max_user_words + 2 ||
       cf->nr_cf > max_user_words + 2 ||
       vs->agx3_vdm_state_update.source_layout !=
          AGX_VDM_STATE_UPDATE_SOURCE_LAYOUT_AGX3_VERTEX_PROGRAM_VARIANT ||
       vs->info.uvs.user_size > max_user_words)
      return false;

   unsigned next_cf = 0;
   bool has_rhw = false, has_z = false, has_primitive_id = false;
   unsigned user_bindings = 0;
   for (unsigned i = 0; i < cf->nr_bindings; ++i) {
      const struct agx_cf_binding *b = &cf->bindings[i];
      if (b->count == 0 || b->count > 4 || b->cf_base != next_cf)
         return false;
      /* Flat inputs: the fragment shader selects B for the first-fan
       * provoking mode (draw.provoking == 1, only set for fans) and C
       * otherwise, matching the legacy interpolation path. */
      if (!b->smooth &&
          (b->perspective ||
           (gfx->topology != AGX_PRIMITIVE_POINTS &&
            gfx->topology != AGX_PRIMITIVE_TRIANGLES &&
            gfx->topology != AGX_PRIMITIVE_TRIANGLE_STRIP &&
            gfx->topology != AGX_PRIMITIVE_TRIANGLE_FAN &&
            gfx->topology != AGX_PRIMITIVE_LINES &&
            gfx->topology != AGX_PRIMITIVE_LINE_STRIP) ||
           (gfx->provoking == 1 &&
            gfx->topology != AGX_PRIMITIVE_TRIANGLE_FAN)))
         return false;
      if (b->slot == VARYING_SLOT_POS) {
         /* System coefficients precede user coefficients: optional W,
          * then optional Z. agx_link_varyings assigns native Z source1. */
         if (user_bindings || b->count != 1 || b->perspective || !b->smooth)
            return false;
         if (b->offset == 3) {
            if (i != 0 || has_rhw || has_z)
               return false;
            has_rhw = true;
         } else if (b->offset == 2) {
            if (has_z || !cf->reads_z)
               return false;
            has_z = true;
         } else {
            return false;
         }
      } else if (b->slot == VARYING_SLOT_PRIMITIVE_ID &&
                 !gfx->linked_varyings.slots[VARYING_SLOT_PRIMITIVE_ID]) {
         /* The coefficient linker selects the rasterizer's primitive-ID
          * source and enables generation in CULL_2; no VS export is needed. */
         if (b->offset != 0 || b->count != 1 || b->smooth || b->perspective ||
             !gfx->generate_primitive_id)
            return false;
         has_primitive_id = true;
      } else if (b->slot == VARYING_SLOT_PNTC) {
         /* The rasterizer supplies point coordinates; they have no vertex
          * UVS slot. The common linker selects POINT_COORD coefficients.
          * Reading them for other primitives is legal with undefined values,
          * and must not cause command-buffer recording to fail.
          */
         if (b->offset != 0 || b->count != 2 || !b->smooth ||
             !b->perspective || !has_rhw)
            return false;
      } else {
         /* Clip distances read by the fragment shader have ordinary UVS
          * copies in addition to their clipper outputs. Cull lowering also
          * carries sign indicators in ordinary interpolated UVS slots. */
         const bool distance = b->slot == VARYING_SLOT_CLIP_DIST0 ||
                               b->slot == VARYING_SLOT_CLIP_DIST1 ||
                               b->slot == VARYING_SLOT_CULL_PRIMITIVE ||
                               b->slot == VARYING_SLOT_CULL_PRIMITIVE + 1;
         if ((!distance && b->slot != VARYING_SLOT_LAYER &&
              b->slot != VARYING_SLOT_VIEWPORT &&
              (b->slot < VARYING_SLOT_VAR0 || b->slot > VARYING_SLOT_VAR31)) ||
             (b->perspective && !has_rhw) ||
             b->offset + b->count > vs->info.uvs.components[b->slot])
            return false;
         ++user_bindings;
         const unsigned slot = gfx->linked_varyings.slots[b->slot];
         if (slot < 4 || slot + b->offset + b->count > vs->info.uvs.size)
            return false;
      }
      next_cf += b->count;
   }
   return (user_bindings || has_rhw || has_z || has_primitive_id) &&
          has_z == cf->reads_z && next_cf == cf->nr_cf;
}

static bool
hk_g17p_basic_graphics_program_supported(struct hk_cmd_buffer *cmd,
                                         struct hk_shader *s,
                                         struct hk_linked_shader *linked)
{
   const bool native_spills = s->info.stage == MESA_SHADER_FRAGMENT &&
      agx_tilebuffer_spills(&cmd->state.gfx.render.tilebuffer);
   /* Main-shader TPS storage is declared by the native SPECTPR builder.
    * Atomics with returned values use it even without register spilling.
    * The legacy preamble/scratch-helper ABI remains unsupported here. */
   if (!linked || !linked->b.bo || !linked->b.bo->va || linked->b.size == 0 ||
       linked->b.size > UINT32_MAX ||
       (s->b.info.has_preamble &&
        !((s->info.stage == MESA_SHADER_FRAGMENT &&
           debug_get_option_hk_m4_native_preamble()) ||
          (s->info.stage == MESA_SHADER_VERTEX &&
           hk_vertex_preamble_enabled(hk_cmd_buffer_device(cmd)->dev.chip)))) ||
       s->b.info.preamble_scratch_size != 0 ||
       s->b.info.texture_state_count != 0 ||
       (s->b.info.sampler_state_count != 0 &&
        !(s->info.agx3_indexed_sampler_valid &&
          s->b.info.sampler_state_count == 1)) ||
       s->b.info.uses_sampler_heap ||
       (linked->b.uses_txf && !s->info.agx3_indexed_sampler_valid && !native_spills) ||
       (s->data_size != 0 && s->info.stage != MESA_SHADER_FRAGMENT) ||
       s->info.set_count > HK_AGX3_G17P_MAX_ROOT_SETS) {
      mesa_loge("HAL200 resources: preamble=%u preamble-scratch=%u tex-state=%u "
                "sampler-state=%u heap=%u txf=%u indexed=%u data=%zu sets=%u",
                s->b.info.has_preamble, s->b.info.preamble_scratch_size,
                s->b.info.texture_state_count, s->b.info.sampler_state_count,
                s->b.info.uses_sampler_heap, linked ? linked->b.uses_txf : 0,
                s->info.agx3_indexed_sampler_valid, (size_t)s->data_size,
                s->info.set_count);
      return false;
   }

   if (s->b.info.stage == MESA_SHADER_VERTEX) {
      const unsigned nr_attribs = s->info.stage == MESA_SHADER_VERTEX ? DIV_ROUND_UP(
         BITSET_LAST_BIT(s->info.vs.attrib_components_read), 4) : 0;

      return !linked->sw_indexing &&
             nr_attribs <= AGX_MAX_VBUFS &&
             s->agx3_vdm_state_update.source_layout ==
                AGX_VDM_STATE_UPDATE_SOURCE_LAYOUT_AGX3_VERTEX_PROGRAM_VARIANT;
   }

   bool varyings = hk_g17p_smooth_varyings_supported(cmd, linked);
   bool tile = hk_g17p_basic_fragment_layout(cmd);
   if (s->info.stage == MESA_SHADER_FRAGMENT && (!varyings || !tile)) {
      struct agx_tilebuffer_layout *tib = &cmd->state.gfx.render.tilebuffer;
      mesa_loge("HAL200 fragment rejected varyings=%u tile=%u cf=%u/%u "
                "formats=%u/%u/%u samples=%u layered=%u size=%u",
                varyings, tile, linked->b.cf.nr_bindings, linked->b.cf.nr_cf,
                tib->logical_format[0], tib->logical_format[1],
                tib->logical_format[2], tib->nr_samples, tib->layered,
                tib->sample_size_B);
      struct hk_graphics_state *gfx = &cmd->state.gfx;
      const struct hk_shader *vs = gfx->shaders[MESA_SHADER_VERTEX]
         ? hk_bound_hw_vs(gfx) : NULL;
      mesa_loge("HAL200 varying state: topology=%u provoking=%u tess=%u "
                "vs-layout=%u uvs=%u user=%u reads-z=%u",
                gfx->topology, gfx->provoking,
                !!gfx->shaders[MESA_SHADER_TESS_EVAL],
                vs ? vs->agx3_vdm_state_update.source_layout : 0,
                vs ? vs->info.uvs.size : 0, vs ? vs->info.uvs.user_size : 0,
                linked->b.cf.reads_z);
      for (unsigned i = 0; i < linked->b.cf.nr_bindings; ++i) {
         const struct agx_cf_binding *b = &linked->b.cf.bindings[i];
         mesa_loge("HAL200 varying %u: slot=%u offset=%u count=%u base=%u "
                   "smooth=%u perspective=%u vertex-components=%u linked-slot=%u",
                   i, b->slot, b->offset, b->count, b->cf_base, b->smooth,
                   b->perspective, vs ? vs->info.uvs.components[b->slot] : 0,
                   gfx->linked_varyings.slots[b->slot]);
      }
   }
   return s->info.stage == MESA_SHADER_FRAGMENT && varyings && tile;
}

static uint64_t
hk_native_pointer_upload(struct hk_cmd_buffer *cmd, const uint64_t *words,
                         unsigned size)
{
   assert(size && size <= 32 && !(size & 7));
   const unsigned index = size / 8 - 1;
   if (cmd->native_pointer_uploads[index].address &&
       cmd->native_pointer_uploads[index].size == size &&
       !memcmp(cmd->native_pointer_uploads[index].words, words, size))
      return cmd->native_pointer_uploads[index].address;

   const uint64_t address = hk_pool_upload(cmd, words, size, 8);
   if (address) {
      memcpy(cmd->native_pointer_uploads[index].words, words, size);
      cmd->native_pointer_uploads[index].size = size;
      cmd->native_pointer_uploads[index].address = address;
   }
   return address;
}

bool
hk_g17p_graphics_uniform_ranges(
   struct hk_cmd_buffer *cmd, struct hk_shader *s,
   const struct hk_linked_shader *linked,
   struct agx3_ioto_uniform_range *ranges,
   size_t *range_count)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   const uint64_t root_ptr = cmd->state.gfx.root;
   size_t count = 0;
   const unsigned root_uniform = s->info.agx3_root_uniform;
   /* A vertex shader lowered to compute still consumes the vertex ABI. */
   const unsigned vertex_prefix = s->info.stage == MESA_SHADER_VERTEX
      ? AGX_ABI_VUNI_NATIVE_PREFIX : 0;

   if (s->info.stage == MESA_SHADER_VERTEX) {
      const unsigned nr_attribs = DIV_ROUND_UP(
         BITSET_LAST_BIT(s->info.vs.attrib_components_read), 4);
      if (nr_attribs > AGX_MAX_VBUFS)
         return false;

      if (nr_attribs != 0) {
         if (!root_ptr)
            return false;

         ranges[count++] = (struct agx3_ioto_uniform_range){
            .start_halfs = vertex_prefix + AGX_ABI_VUNI_VBO_BASE(0),
            .size_halfs = 4 * nr_attribs,
            .address =
               root_ptr + hk_root_descriptor_offset(draw.attrib_base),
         };
         ranges[count++] = (struct agx3_ioto_uniform_range){
            .start_halfs = vertex_prefix + AGX_ABI_VUNI_VBO_CLAMP(nr_attribs, 0),
            .size_halfs = 2 * nr_attribs,
            .address =
               root_ptr + hk_root_descriptor_offset(draw.attrib_clamps),
         };
      }

      /* The compiler's vertex ABI reserves these independent ranges after
       * attribute bases/clamps. Direct draws already upload baseVertex and
       * firstInstance together; native Ioto can bind them without a prolog
       * specialization, shader patch or pre-render compute dispatch. */
      if (cmd->state.gfx.draw_params) {
         ranges[count++] = (struct agx3_ioto_uniform_range){
            .start_halfs = vertex_prefix + AGX_ABI_VUNI_FIRST_VERTEX(nr_attribs),
            .size_halfs = 4,
            .address = cmd->state.gfx.draw_params,
         };
      }
      if (cmd->state.gfx.draw_id_ptr) {
         ranges[count++] = (struct agx3_ioto_uniform_range){
            .start_halfs = vertex_prefix + AGX_ABI_VUNI_DRAW_ID(nr_attribs),
            /* DrawID is a 32-bit value (two uniform halves), also when
             * the shader only consumes its low half after optimization. */
            .size_halfs = 2,
            .address = cmd->state.gfx.draw_id_ptr,
         };
      }

      if (s->b.info.stage == MESA_SHADER_COMPUTE) {
         ranges[count++] = (struct agx3_ioto_uniform_range){
            .start_halfs = vertex_prefix + AGX_ABI_VUNI_VERTEX_PARAMS(nr_attribs),
            .size_halfs = 4,
            .address = root_ptr + hk_root_descriptor_offset(draw.vertex_params),
         };
      }
   } else if (s->info.stage == MESA_SHADER_FRAGMENT) {
      if (agx_tilebuffer_spills(&cmd->state.gfx.render.tilebuffer)) {
         const uint64_t table = hk_upload_native_spilled_rt_descs(cmd);
         const uint64_t pointer = table ? hk_pool_upload(cmd, &table, 8, 8) : 0;
         if (!pointer)
            return false;
         ranges[count++] = (struct agx3_ioto_uniform_range){
            .start_halfs = AGX_ABI_FUNI_NATIVE_PREFIX + AGX_ABI_FUNI_EMRT_HEAP,
            .size_halfs = 4, .address = pointer,
         };
      }
      if (cmd->state.gfx.uses_blend_constant) {
         if (!root_ptr)
            return false;
         ranges[count++] = (struct agx3_ioto_uniform_range){
            .start_halfs = AGX_ABI_FUNI_NATIVE_PREFIX + AGX_ABI_FUNI_BLEND_R,
            .size_halfs = 8,
            .address = root_ptr + hk_root_descriptor_offset(draw.blend_constant),
         };
      }
   } else if (s->info.stage != MESA_SHADER_GEOMETRY &&
              s->info.stage != MESA_SHADER_TESS_CTRL &&
              s->info.stage != MESA_SHADER_TESS_EVAL) {
      return false;
   }

   if (s->info.agx3_combined_sampler_valid) {
      if (s->info.stage != MESA_SHADER_FRAGMENT || s->info.set_count != 0)
         return false;

      struct hk_descriptor_set *set = cmd->state.gfx.descriptors.sets[0];
      const struct agx_g17p_combined_sampler_binding *binding =
         &s->info.agx3_combined_sampler;
      if (!set || !set->mapped_ptr ||
          binding->texture_offset_B > set->size ||
          set->size - binding->texture_offset_B < 32 ||
          binding->sampler_index_offset_B > set->size ||
          set->size - binding->sampler_index_offset_B < sizeof(uint16_t))
         return false;

      uint16_t sampler_index;
      memcpy(&sampler_index,
             (const uint8_t *)set->mapped_ptr +
                binding->sampler_index_offset_B,
             sizeof(sampler_index));

      const unsigned sampler_element_size =
         agx_chip_sampler_heap_element_size(dev->dev.chip);
      if (!dev->samplers.table.bo || !dev->samplers.table.bo->va ||
          dev->samplers.table.alloc > UINT16_MAX ||
          sampler_element_size > UINT16_MAX)
         return false;

      const struct hk_agx3_g17p_combined_sampler_heap heap = {
         .descriptor_set_va = hk_descriptor_set_addr(set),
         .sampler_heap_va = dev->samplers.table.bo->va->addr,
         .texture_offset_B = binding->texture_offset_B,
         .sampler_index = sampler_index,
         .sampler_count = dev->samplers.table.alloc,
         .sampler_element_size_B = sampler_element_size,
      };
      uint64_t pointers[2];
      if (!hk_agx3_g17p_combined_sampler_pointers(&heap, pointers))
         return false;

      /* HAL200 FragmentProgramVariant::finalize (374968..3749c0) maps
       * compiler bindpoints 0x31/0x35 to driver qwords +0xc0/+0xe0.
       * encodeAndEmitRenderState writes the raw texture/sampler table VAs
       * there (4c5580/4c5598); ESL copies them to uniform dwords 0..3.
       * Keep this upload immutable for the command-buffer lifetime, just
       * like the per-draw graphics state. Both shader indices are zero.
       */
      const uint64_t pointers_va =
         hk_native_pointer_upload(cmd, pointers, sizeof(pointers));
      if (!pointers_va)
         return false;

      ranges[count++] = (struct agx3_ioto_uniform_range){
         .start_halfs = 0,
         .size_halfs = 8,
         .address = pointers_va,
      };
   }

   const bool statistics_uniform = s->info.stage == MESA_SHADER_FRAGMENT &&
      linked && linked->prolog_statistics_uniform;
   const uint64_t statistics_address = statistics_uniform ?
      hk_pipeline_stat_addr(cmd,
         VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT) : 0;
   static_assert(AGX_ABI_FUNI_NATIVE_STAT_QUERY == 8, "native prefix slot");
   if (statistics_uniform && (!statistics_address || (statistics_address & 7) ||
                              !hk_stat_enabled(statistics_address)))
      return false;

   const bool native_spills = s->info.stage == MESA_SHADER_FRAGMENT &&
      agx_tilebuffer_spills(&cmd->state.gfx.render.tilebuffer);
   if (native_spills && s->info.agx3_combined_sampler_valid) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return false;
   }
   if (s->info.agx3_indexed_sampler_valid || native_spills) {
      /* Geometry and tessellation shaders run as compute with the same
       * texture/sampler table pointers at uniform halves 0..7 and their
       * root at AGX_G17P_TXF_ROOT_HALFS (geometry.basic.output_vary_by_texture). */
      if ((s->b.info.stage != MESA_SHADER_FRAGMENT &&
           s->b.info.stage != MESA_SHADER_VERTEX &&
           s->b.info.stage != MESA_SHADER_COMPUTE &&
           s->b.info.stage != MESA_SHADER_GEOMETRY &&
           s->b.info.stage != MESA_SHADER_TESS_CTRL &&
           s->b.info.stage != MESA_SHADER_TESS_EVAL) ||
          !dev->sampled_images.bo || !dev->samplers.table.bo)
         return false;
      const uint64_t pointers[4] = {
         dev->sampled_images.bo->va->addr,
         dev->samplers.table.bo->va->addr,
         statistics_address,
         0xffff, /* Native image coordinate routing reads the clamp at u12. */
      };
      const unsigned pointer_halfs =
         (s->info.agx3_indexed_images_valid || native_spills) ? 16 :
         statistics_uniform ? 12 : 8;
      const uint64_t pointers_va =
         hk_native_pointer_upload(cmd, pointers, pointer_halfs * 2);
      if (!pointers_va)
         return false;
      ranges[count++] = (struct agx3_ioto_uniform_range){
         .start_halfs = 0, .size_halfs = pointer_halfs, .address = pointers_va,
      };
   }

   if (statistics_uniform && !s->info.agx3_indexed_sampler_valid && !native_spills) {
      uint64_t pointer = hk_native_pointer_upload(cmd, &statistics_address,
                                                  sizeof(statistics_address));
      if (!pointer)
         return false;
      ranges[count++] = (struct agx3_ioto_uniform_range){
         .start_halfs = AGX_ABI_FUNI_NATIVE_STAT_QUERY,
         .size_halfs = 4, .address = pointer,
      };
   }

   if (s->info.agx3_uses_root || s->info.set_count != 0 ||
       (linked && linked->prolog_uses_root)) {
      if (!root_ptr)
         return false;

      if (!hk_agx3_g17p_root_range(root_uniform, s->info.set_count, root_ptr,
                                   &ranges[count]))
         return false;
      count++;
   }

   static_assert(HK_MAX_SETS == 8, "descriptor prefetch range budget");
   static_assert(HK_DESCRIPTOR_PREFETCH_MAX == 32, "compact descriptor range budget");
   static_assert(HK_UBO_PREFETCH_MAX == 128, "uniform prefetch range capacity");
   for (unsigned i = 0; i < s->info.ubo_prefetch_count; ++i) {
      const struct hk_ubo_prefetch *load = &s->info.ubo_prefetch[i];
      const struct hk_descriptor_state *state = &cmd->state.gfx.descriptors;
      struct hk_buffer_address address;
      if (!hk_prefetch_buffer_address(state, load, &address))
         return false;
      const bool valid = load->offset_B <= address.size &&
                         load->size_B <= address.size - load->offset_B;
      /* UBO contents are loaded by the GPU at execution. The CPU only reads
       * immutable descriptor metadata and implements the original bound test. */
      const bool direct = !agx_addr_is_ro(&dev->dev, address.base_addr);
      uint64_t source = valid && direct ? address.base_addr + load->offset_B :
                                          AGX_ZERO_PAGE_ADDRESS;
      if (valid && direct && source < address.base_addr)
         return false;
      /* Consecutive scalar uniforms often come from consecutive UBO bytes.
       * Let Ioto issue one contiguous load instead of a separate resource
       * round for each scalar. Bounds were checked independently above. Zero
       * page fallbacks do not combine unless their addresses are contiguous. */
      struct agx3_ioto_uniform_range *previous = count ? &ranges[count - 1] : NULL;
      if (previous && previous->start_halfs + previous->size_halfs == load->uniform_half &&
          previous->address + previous->size_halfs * 2 == source) {
         previous->size_halfs += load->size_B / 2;
      } else {
         ranges[count++] = (struct agx3_ioto_uniform_range){
            .start_halfs = load->uniform_half, .size_halfs = load->size_B / 2,
            .address = source,
         };
      }
      if (load->direct_half == load->uniform_half + 2) {
         const uint32_t eligible = direct;
         if (!cmd->native_prefetch_eligible[eligible])
            cmd->native_prefetch_eligible[eligible] =
               hk_pool_upload(cmd, &eligible, sizeof(eligible), 4);
         uint64_t eligible_va = cmd->native_prefetch_eligible[eligible];
         if (!eligible_va)
            return false;
         ranges[count++] = (struct agx3_ioto_uniform_range){
            .start_halfs = load->direct_half, .size_halfs = 2,
            .address = eligible_va,
         };
      }
   }

   for (unsigned set = 0; set < s->info.set_count; ++set) {
      const struct hk_descriptor_prefetch *span = &s->info.descriptor_prefetch[set];
      if (!span->size_B)
         continue;
      /* The uploaded root covers both allocated and push descriptor sets.
       * Push sets deliberately have no hk_descriptor_set object. */
      const struct hk_descriptor_state *descriptors = &cmd->state.gfx.descriptors;
      uint64_t address = descriptors->root.sets[set];
      uint32_t size = descriptors->set_sizes[set];
      if (!address || span->offset_B > size ||
          span->size_B > size - span->offset_B ||
          count >= HK_AGX3_G17P_GRAPHICS_MAX_RANGES)
         return false;
      ranges[count++] = (struct agx3_ioto_uniform_range){
         .start_halfs = span->uniform_half,
         .size_halfs = span->size_B / 2,
         .address = address + span->offset_B,
      };
   }

   for (unsigned i = 0; i < s->info.descriptor_prefetch_compact_count; ++i) {
      const struct hk_descriptor_prefetch *span = &s->info.descriptor_prefetch_compact[i];
      const struct hk_descriptor_state *descriptors = &cmd->state.gfx.descriptors;
      if (span->set >= s->info.set_count)
         return false;
      uint64_t address = descriptors->root.sets[span->set];
      uint32_t size = descriptors->set_sizes[span->set];
      if (!address || span->offset_B > size ||
          span->size_B > size - span->offset_B ||
          count >= HK_AGX3_G17P_GRAPHICS_MAX_RANGES)
         return false;
      ranges[count++] = (struct agx3_ioto_uniform_range){
         .start_halfs = span->uniform_half,
         .size_halfs = span->size_B / 2,
         .address = address + span->offset_B,
      };
   }

   /*
    * Promoted constants. agx_opt_promote_constants() parks a shader's
    * constant pool in the binary's rodata section and addresses it through a
    * uniform, so the driver has to publish that address as a uniform range --
    * exactly as the compute path does (hk_cmd_dispatch.c). Without it a
    * fragment shader as simple as a constant colour is refused, because the
    * compiler promotes its three floats.
    *
    * The data is read through the uniform, not the program counter, so it is
    * addressed in the shader's own BO even though graphics executes from the
    * linked binary. Same bounds checks as compute: the range must lie inside
    * that BO, and both halves must be even.
    */
   if (count < HK_AGX3_G17P_GRAPHICS_MAX_RANGES && s->b.info.rodata.size_16 != 0) {
      const struct agx_rodata *rodata = &s->b.info.rodata;
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
   } else if (s->b.info.rodata.size_16 != 0) {
      /* No room left in the program's range budget. */
      return false;
   }

   *range_count = count;
   return count <= HK_AGX3_G17P_GRAPHICS_MAX_RANGES;
}

/* Bounded selected-executable diagnostics. Compiler logs contain both ordinary
 * and fallback variants; report the actual linked program used for recording.
 * This is an address sample, not a complete lifetime/coverage ledger. */
static void
hk_profile_graphics_program(struct hk_cmd_buffer *cmd, struct hk_shader *s,
                            struct hk_linked_shader *linked)
{
   if (!debug_get_option_hk_m4_program_profile())
      return;
   static _Thread_local uint64_t reported[128];
   static _Thread_local unsigned count;
   uint64_t code = linked->b.bo->va->addr + linked->b.entry_offset;
   for (unsigned i = 0; i < count; ++i) {
      if (reported[i] == code)
         return;
   }
   if (count == ARRAY_SIZE(reported))
      return;
   reported[count++] = code;
   fprintf(stderr, "HK_PROGRAM_PROFILE stage=%u code=%" PRIx64
           " bytes=%zu gprs=%u scratch=%u instrs=%u loops=%u"
           " ordinary_fs=%u prolog_root=%u\n",
           s->info.stage, code, linked->b.size, linked->b.nr_gprs,
           linked->b.scratch_size, s->b.info.stats.instrs, s->b.info.stats.loops,
           s->info.stage == MESA_SHADER_FRAGMENT && cmd->state.gfx.fs_ubo_direct,
           linked->prolog_uses_root);
}

uint64_t
hk_upload_usc_words(struct hk_cmd_buffer *cmd, struct hk_shader *s,
                    struct hk_linked_shader *linked)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   if (s->info.stage == MESA_SHADER_FRAGMENT &&
       debug_get_option_hk_m4_shader_profile()) {
      const struct agx2_stats *stats = &s->b.info.stats;
      fprintf(stderr, "HK_SHADER_PROFILE code=%08x cs=%" PRIx64 " width=%u height=%u direct=%u "
              "instrs=%u regs=%u scratch=%u loops=%u preamble=%u preamble_regs=%u\n",
              _mesa_hash_data(s->b.binary, s->b.info.binary_size),
              cmd->current_cs.gfx
                 ? (hk_g17p_vdm_low_window_enabled(&dev->dev)
                       ? hk_g17p_vdm_alias(cmd, cmd->current_cs.gfx->addr)
                       : cmd->current_cs.gfx->addr)
                 : 0,
              cmd->state.gfx.render.area.extent.width,
              cmd->state.gfx.render.area.extent.height,
              s->info.ubo_prefetch_direct, stats->instrs, s->b.info.nr_gprs,
              s->b.info.scratch_size, stats->loops, stats->preamble_inst,
              s->b.info.nr_preamble_gprs);
   }


   if (agx_native_graphics(&dev->dev)) {
      if (!agx_native_graphics(&dev->dev) ||
          !hk_g17p_basic_graphics_program_supported(cmd, s, linked)) {
         mesa_loge("HAL200 program rejected stage=%u layout=%u outputs=%#llx "
                   "uvs=%u user=%u groups=%u/%u/%u/%u/%u indexed-sampler=%u "
                   "sets=%u prefetch=%u linked=%p size=%zu",
                   s->info.stage, s->agx3_vdm_state_update.source_layout,
                   (unsigned long long)s->b.info.outputs,
                   s->info.uvs.size, s->info.uvs.user_size,
                   s->info.uvs.group_offs[UVS_POSITION],
                   s->info.uvs.group_offs[UVS_VARYINGS],
                   s->info.uvs.group_offs[UVS_PSIZ],
                   s->info.uvs.group_offs[UVS_LAYER_VIEWPORT],
                   s->info.uvs.group_offs[UVS_CLIP_DIST],
                   s->info.agx3_indexed_sampler_valid, s->info.set_count,
                   s->info.ubo_prefetch_count, (void *)linked,
                   linked ? linked->b.size : 0);
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
         return 0;
      }

      hk_profile_graphics_program(cmd, s, linked);

      struct agx3_ioto_uniform_range ranges[HK_AGX3_G17P_GRAPHICS_MAX_RANGES];
      size_t range_count;
      if (!hk_g17p_graphics_uniform_ranges(cmd, s, linked, ranges,
                                           &range_count)) {
         mesa_loge("HAL200 uniform ranges rejected stage=%u sets=%u prefetch=%u",
                   s->info.stage, s->info.set_count, s->info.ubo_prefetch_count);
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
         return 0;
      }

      size_t program_capacity;
      if (!hk_agx3_g17p_graphics_program_bound(dev->dev.chip, ranges,
                                              range_count, &program_capacity)) {
         mesa_loge("HAL200 program bounds rejected stage=%u ranges=%zu",
                   s->info.stage, range_count);
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
         return 0;
      }

      /* The Ioto program embeds the linked executable address, and promoted
       * constants embed the shader BO address. Pipelines may be destroyed
       * after recording, so retain both until this command buffer is reset.
       */
      hk_cmd_buffer_retain_bo(cmd, linked->b.bo);
      if (s->b.info.rodata.size_16 != 0)
         hk_cmd_buffer_retain_bo(cmd, s->bo);
      size_t preamble_size = 0;
      if (s->b.info.has_preamble) {
         const unsigned end = s->b.info.main_offset > s->b.info.preamble_offset
            ? s->b.info.main_offset : s->b.info.binary_size;
         if (end > s->b.info.binary_size ||
             s->b.info.preamble_offset >= end ||
             end - s->b.info.preamble_offset < 4 ||
             program_capacity > SIZE_MAX - HK_AGX3_FRAGMENT_PREAMBLE_DRAIN_SIZE ||
             end - s->b.info.preamble_offset > SIZE_MAX - program_capacity -
                HK_AGX3_FRAGMENT_PREAMBLE_DRAIN_SIZE) {
            vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
            return 0;
         }
         preamble_size = end - s->b.info.preamble_offset;
         program_capacity += preamble_size + HK_AGX3_FRAGMENT_PREAMBLE_DRAIN_SIZE;
      }

      struct agx_bo *program_bo = NULL;
      struct agx_ptr upload = {0};
      const bool pooled = agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) &&
                          program_capacity <= HK_CMD_BO_SIZE;
      if (pooled) {
         /* The command buffer owns these immutable bytes until reset, just
          * like its other uploads. A draw does not need its own GEM object. */
         upload = hk_pool_usc_alloc(cmd, program_capacity,
                                    AGX_IOTO_PROGRAM_ALIGNMENT);
      } else {
         program_bo = agx_bo_create(&dev->dev, program_capacity,
                                    AGX_IOTO_PROGRAM_ALIGNMENT,
                                    AGX_BO_EXEC | AGX_BO_LOW_VA,
                                    "G17P Ioto program");
         if (!program_bo) {
            vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
            return 0;
         }
         util_dynarray_append(&cmd->large_bos, program_bo);
         upload.gpu = program_bo->va->addr;
         upload.cpu = agx_bo_map(program_bo);
      }
      if (!upload.cpu) {
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_MEMORY_MAP_FAILED);
         return 0;
      }
      void *program_map = upload.cpu;

      /* Same fail-closed USC-window containment check as the compute path in
       * hk_cmd_dispatch.c: usc_exec_base is fixed per queue, so an exec
       * pointer outside [shader_base, +4 GiB) can only end as an unattributed
       * GPU page fault.
       */
      if (!agx_va_in_usc_window(&dev->dev, upload.gpu) ||
          !agx_va_in_usc_window(&dev->dev,
                             linked->b.bo->va->addr + linked->b.entry_offset)) {
         mesa_loge("G17P graphics: exec pointer outside the USC window "
                   "[%" PRIx64 ", %" PRIx64 "): program=%" PRIx64
                   " shader=%" PRIx64 "\n",
                   dev->dev.shader_base,
                   dev->dev.shader_base + AGX_USC_WINDOW_SIZE,
                   upload.gpu,
                linked->b.bo->va->addr + linked->b.entry_offset);
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_UNKNOWN);
         return 0;
      }

      struct agx3_ioto_uniform_template *uniform_template = NULL;
      if (hk_ioto_templates_enabled(dev->dev.chip) &&
          (s->info.stage == MESA_SHADER_VERTEX || s->info.stage == MESA_SHADER_FRAGMENT)) {
         const unsigned index = s->info.stage == MESA_SHADER_FRAGMENT;
         if (!cmd->uniform_templates[index])
            cmd->uniform_templates[index] = vk_zalloc(&hk_cmd_buffer_pool(cmd)->vk.alloc,
               sizeof(*uniform_template), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
         uniform_template = cmd->uniform_templates[index];
      }

      const struct hk_agx3_g17p_graphics_program_desc desc = {
         .uniform_template = uniform_template,
         .sample_size_B = s->info.stage == MESA_SHADER_FRAGMENT
            ? cmd->state.gfx.render.tilebuffer.sample_size_B : 0,
         .tile_size = cmd->state.gfx.render.tilebuffer.tile_size,
         .nr_samples = MAX2(cmd->state.gfx.render.tilebuffer.nr_samples, 1),
         .program_va = upload.gpu,
         /* The container's entry point, not the allocation base: the
       * shader loader reads the block-size dword at entry - 0x80. */
      .shader_va = linked->b.bo->va->addr + linked->b.entry_offset,
         .uniform_ranges = ranges,
         .uniform_range_count = range_count,
         .shader_binary_size = linked->b.size,
         .scratch_size = linked->b.scratch_size,
         .nr_gprs = linked->b.nr_gprs,
         .compiler_d_valid = linked->b.g17p_compiler_d_valid,
         .compiler_d = linked->b.g17p_compiler_d,
         .fragment = s->info.stage == MESA_SHADER_FRAGMENT,
         .writes_sample_mask = s->info.stage == MESA_SHADER_FRAGMENT &&
            ((linked->b.fragment_control.opaque[0] >> 29) == AGX_PASS_TYPE_PUNCH_THROUGH ||
             (linked->b.fragment_control.opaque[0] >> 29) == AGX_PASS_TYPE_TRANSLUCENT_PUNCH_THROUGH),
      };
      struct hk_agx3_g17p_graphics_program program;
      if (!hk_agx3_g17p_basic_graphics_program_generate(dev->dev.chip, &desc,
                                                        &program)) {
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
         return 0;
      }

      size_t program_length = program.length;
      memcpy(program_map, program.bytes, program.length);
      if (preamble_size) {
         const void *preamble = (const uint8_t *)s->b.binary + s->b.info.preamble_offset;
         const bool appended = desc.fragment ? hk_agx3_fragment_preamble_append(
                dev->dev.chip, desc.writes_sample_mask, program_map,
                program_capacity, &program_length, preamble, preamble_size) :
            hk_agx3_vertex_preamble_append(dev->dev.chip, program_map,
                program_capacity, &program_length, preamble, preamble_size);
         if (!appended) {
            vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
            return 0;
         }
      }
      if (hk_g17p_launch_trace_enabled()) {
         const char *label = s->info.stage == MESA_SHADER_FRAGMENT
                                ? "fragment-ioto"
                                : "vertex-ioto";
         hk_g17p_trace_line(label, upload.gpu, program_map,
                            program_length);
      }
      if (!pooled && agx_bo_sync_for_device(program_bo, 0, program_length)) {
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_DEVICE_LOST);
         return 0;
      }
      return upload.gpu;
   }

   if (agx_chip_command_stream_generation(dev->dev.chip) !=
       AGX_CS_GENERATION_AGX2) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return 0;
   }

   mesa_shader_stage sw_stage = s->info.stage;

   unsigned constant_push_ranges = DIV_ROUND_UP(s->b.info.rodata.size_16, 64);
   unsigned push_ranges = 2;
   unsigned stage_ranges = 3;

   size_t usc_size =
      agx_usc_size(constant_push_ranges + push_ranges + stage_ranges + 4);
   struct agx_ptr t = hk_pool_usc_alloc(cmd, usc_size, 64);
   if (!t.cpu)
      return 0;

   struct agx_usc_builder b = agx_usc_builder(t.cpu, usc_size);

   uint64_t root_ptr;

   if (sw_stage == MESA_SHADER_COMPUTE) {
      root_ptr = hk_cmd_buffer_upload_root(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
   } else {
      root_ptr = cmd->state.gfx.root;
   }

   static_assert(offsetof(struct hk_root_descriptor_table, root_desc_addr) == 0,
                 "self-reflective");

   unsigned root_unif = 0;
   if (sw_stage == MESA_SHADER_VERTEX) {
      unsigned count =
         DIV_ROUND_UP(BITSET_LAST_BIT(s->info.vs.attrib_components_read), 4);

      if (count) {
         agx_usc_uniform(
            &b, AGX_ABI_VUNI_VBO_BASE(0), 4 * count,
            root_ptr + hk_root_descriptor_offset(draw.attrib_base));

         agx_usc_uniform(
            &b, AGX_ABI_VUNI_VBO_CLAMP(count, 0), 2 * count,
            root_ptr + hk_root_descriptor_offset(draw.attrib_clamps));
      }

      if (cmd->state.gfx.draw_params) {
         agx_usc_uniform(&b, AGX_ABI_VUNI_FIRST_VERTEX(count), 4,
                         cmd->state.gfx.draw_params);
      }

      if (cmd->state.gfx.draw_id_ptr) {
         agx_usc_uniform(&b, AGX_ABI_VUNI_DRAW_ID(count), 1,
                         cmd->state.gfx.draw_id_ptr);
      }

      if (linked->sw_indexing) {
         agx_usc_uniform(
            &b, AGX_ABI_VUNI_VERTEX_PARAMS(count), 4,
            root_ptr + hk_root_descriptor_offset(draw.vertex_params));
      }

      root_unif = AGX_ABI_VUNI_COUNT_VK(count);
   } else if (sw_stage == MESA_SHADER_FRAGMENT) {
      if (agx_tilebuffer_spills(&cmd->state.gfx.render.tilebuffer)) {
         hk_usc_upload_spilled_rt_descs(&b, cmd);
      }

      if (cmd->state.gfx.uses_blend_constant) {
         agx_usc_uniform(
            &b, 4, 8,
            root_ptr + hk_root_descriptor_offset(draw.blend_constant));
      }

      /* The SHARED state is baked into linked->usc for non-fragment shaders. We
       * don't pass around the information to bake the tilebuffer layout.
       *
       * TODO: We probably could with some refactor.
       */
      agx_usc_push_packed(&b, SHARED, &cmd->state.gfx.render.tilebuffer.usc);
      root_unif = AGX_ABI_FUNI_ROOT;
   }

   /* Address for the root and each set */
   agx_usc_uniform(&b, root_unif, 4 * (1 + s->info.set_count), root_ptr);

   agx_usc_push_blob(&b, linked->usc.data, linked->usc.size);
   return agx_usc_addr(&dev->dev, t.gpu);
}

static bool
hk_precomp_lower_input(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_preamble)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *address = nir_iadd_imm(b, nir_load_root_agx(b),
      hk_root_descriptor_offset(push) + nir_intrinsic_base(intr) * 2);
   nir_def *value = nir_load_global_constant(b, intr->def.num_components,
      intr->def.bit_size, address, .align_mul = 8,
      .align_offset = (hk_root_descriptor_offset(push) +
                       nir_intrinsic_base(intr) * 2) % 8);
   nir_def_replace(&intr->def, value);
   return true;
}

static void
hk_build_precomp_nir(nir_builder *b, const void *key)
{
   enum libagx_program idx = *(const enum libagx_program *)key;
   const uint32_t *serialized = libagx_nir[idx];
   struct blob_reader reader;
   blob_reader_init(&reader, serialized + 1, serialized[0]);
   ralloc_free(b->shader);
   nir_shader *nir = nir_deserialize(NULL, &agx_nir_options, &reader);
   assert(!reader.overrun);
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, hk_precomp_lower_input,
            nir_metadata_control_flow, NULL);
   *b = nir_builder_at(nir_after_impl(nir_shader_get_entrypoint(nir)));
}

void
hk_dispatch_precomp(struct hk_cmd_buffer *cmd, struct agx_grid grid,
                    enum agx_barrier barrier, enum libagx_program idx,
                    void *data, size_t data_size)
{
   if (vk_command_buffer_has_error(&cmd->vk))
      return;
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   if (agx_chip_uses_hal200_ioto(dev->dev.chip)) {
      struct hk_api_shader *api =
         hk_meta_shader(dev, hk_build_precomp_nir, &idx, sizeof(idx));
      if (!api) {
         mesa_loge("native precompiled kernel %u failed to compile", idx);
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
         return;
      }
      struct hk_shader *s = hk_only_variant(api);
      struct hk_cs **target = (barrier & AGX_POSTGFX) ? &cmd->current_cs.post_gfx
         : (barrier & AGX_PREGFX) ? &cmd->current_cs.pre_gfx : &cmd->current_cs.cs;
      struct hk_cs *cs = hk_cmd_buffer_get_cs_general(cmd, target, true);
      if (!cs)
         return;

      if (!cs->stats.precomp_calls)
         cs->stats.precomp_id = idx;
      else if (cs->stats.precomp_id != idx)
         cs->stats.precomp_id = UINT32_MAX;
      cs->stats.precomp_calls++;

      /* Internal kernels must not overwrite the application's compute push
       * constants. The native dispatch uploads its root before returning. */
      struct hk_root_descriptor_table saved = cmd->state.cs.descriptors.root;
      assert(data_size <= sizeof(saved.push));
      memcpy(cmd->state.cs.descriptors.root.push, data, data_size);
      hk_dispatch_with_local_size(cmd, cs, s, grid,
         agx_workgroup(s->b.info.workgroup_size[0], s->b.info.workgroup_size[1],
                       s->b.info.workgroup_size[2]));
      cmd->state.cs.descriptors.root = saved;
      return;
   }
   struct agx_precompiled_shader *prog = agx_get_precompiled(&dev->bg_eot, idx);
   if (!prog) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   struct hk_cs **target = (barrier & AGX_POSTGFX)  ? &cmd->current_cs.post_gfx
                           : (barrier & AGX_PREGFX) ? &cmd->current_cs.pre_gfx
                                                    : &cmd->current_cs.cs;

   struct hk_cs *cs = hk_cmd_buffer_get_cs_general(cmd, target, true);
   if (!cs)
      return;

   struct agx_ptr t = hk_pool_usc_alloc(cmd, agx_usc_size(15), 64);
   uint64_t uploaded_data = hk_pool_upload(cmd, data, data_size, 4);

   agx_usc_words_precomp(t.cpu, &prog->b, uploaded_data, data_size);

   hk_dispatch_with_usc_launch(dev, cs, prog->b.launch,
                               agx_usc_addr(&dev->dev, t.gpu), grid,
                               prog->b.workgroup);
}

void
hk_cs_init_graphics(struct hk_cmd_buffer *cmd, struct hk_cs *cs)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   struct hk_rendering_state *render = &cmd->state.gfx.render;
   uint8_t *map = cs->current;

   /* Even a clear-only stream needs a stable root alias before EndCommandBuffer.
    * Linked chunks acquire theirs in hk_cmd_buffer_emit_cs_jump. */
   if (hk_g17p_vdm_low_window_enabled(&dev->dev) &&
       !hk_g17p_vdm_compact(cmd, cs->addr))
      return;

   if (debug_get_option_hk_m4_shader_profile())
      fprintf(stderr, "HK_SHADER_BEGIN cs=%" PRIx64 "\n",
              hk_g17p_vdm_low_window_enabled(&dev->dev)
                 ? hk_g17p_vdm_alias(cmd, cs->addr) : cs->addr);

   hk_drm_attachment_set_init(&cs->fragment_attachments);

   /* Retain the image allocation spans now. A command buffer may contain
    * several VDM commands with different render state, while queue submission
    * happens only after all of them have been recorded. */
   struct hk_attachment_view_ref {
      struct hk_image_view *view;
      VkImageAspectFlags aspect;
   } views[(2 * HK_MAX_RTS) + 4];
   static_assert(ARRAY_SIZE(views) == HK_DRM_MAX_FRAGMENT_ATTACHMENTS,
                 "fragment attachment capture capacity");
   unsigned view_count = 0;

   for (unsigned i = 0; i < render->color_att_count; ++i) {
      views[view_count++] = (struct hk_attachment_view_ref){
         .view = render->color_att[i].iview,
         .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
      };
      views[view_count++] = (struct hk_attachment_view_ref){
         .view = render->color_att[i].resolve_iview,
         .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
      };
   }

   views[view_count++] = (struct hk_attachment_view_ref){
      .view = render->depth_att.iview,
      .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
   };
   views[view_count++] = (struct hk_attachment_view_ref){
      .view = render->depth_att.resolve_iview,
      .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
   };
   views[view_count++] = (struct hk_attachment_view_ref){
      .view = render->stencil_att.iview,
      .aspect = VK_IMAGE_ASPECT_STENCIL_BIT,
   };
   views[view_count++] = (struct hk_attachment_view_ref){
      .view = render->stencil_att.resolve_iview,
      .aspect = VK_IMAGE_ASPECT_STENCIL_BIT,
   };

   assert(view_count <= ARRAY_SIZE(views));
   for (unsigned i = 0; i < view_count; ++i) {
      struct hk_image_view *view = views[i].view;
      if (!view)
         continue;

      struct hk_image *image =
         container_of(view->vk.image, struct hk_image, vk);
      const uint8_t image_plane =
         views[i].aspect == VK_IMAGE_ASPECT_COLOR_BIT
            ? view->planes[0].image_plane
            : hk_image_aspects_to_plane(image, views[i].aspect);
      if (image_plane >= image->plane_count) {
         cs->fragment_attachments.complete = false;
         continue;
      }

      const struct hk_image_plane *plane = &image->planes[image_plane];
      const uint64_t size = plane->layout.size_B;
      if (!plane->addr || !size) {
         cs->fragment_attachments.complete = false;
         continue;
      }

      hk_drm_attachment_set_add(&cs->fragment_attachments, plane->addr, size);
   }

   if (agx_native_graphics(&dev->dev)) {
      /* The kext preserves a zero final VDM word when this selector is the
       * UINT32_MAX sentinel.  This reproduces m1n1's direct-triangle prefix. */
      cs->agx3_active_context_selector_valid = true;
      cs->agx3_raw_active_context_selector = UINT32_MAX;
   }

   cs->tib = render->tilebuffer;

   /* Assume this is not the first control stream of the render pass, so
    * initially use the partial background/EOT program and ZLS control.
    * hk_BeginRendering/hk_EndRendering will override.
    */
   cs->cr = render->cr;
   cs->cr.bg.main = render->cr.bg.partial;
   cs->cr.eot.main = render->cr.eot.partial;
   cs->cr.zls_control = render->cr.zls_control_partial;

   /* Barrier to enforce GPU-CPU coherency, in case this batch is back to back
    * with another that caused stale data to be cached and the CPU wrote to it
    * in the meantime.
    */
   switch (agx_chip_command_stream_generation(dev->dev.chip)) {
   case AGX_CS_GENERATION_AGX2:
      agx_push(map, VDM_BARRIER, cfg) {
         cfg.usc_cache_inval = true;
      }
      break;

   case AGX_CS_GENERATION_G15:
   case AGX_CS_GENERATION_HAL200:
   case AGX_CS_GENERATION_HAL300: {
      struct agx_vdm_barrier_core barrier;
      if (!agx_vdm_barrier_core_pack(dev->dev.chip, &barrier)) {
         mesa_loge("no grounded AGX3 initial VDM barrier for %s",
                   agx_chip_name(dev->dev.chip));
         vk_command_buffer_set_error(&cmd->vk,
                                     VK_ERROR_FEATURE_NOT_PRESENT);
         return;
      }

      memcpy(map, barrier.bytes, barrier.length);
      map += barrier.length;
      break;
   }

   case AGX_CS_GENERATION_UNKNOWN:
   default:
      mesa_loge("unknown VDM command-stream generation for %s",
                agx_chip_name(dev->dev.chip));
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }

   if (agx_chip_command_stream_generation(dev->dev.chip) ==
       AGX_CS_GENERATION_HAL300) {
      struct agx_ppp_w_clamp_cull2_core initial;
      const struct agx_ppp_w_clamp_cull2_fields fields = {
         /* Exact G17G/G17X default: W clamp 0.0, clamp-W enabled. */
         .w_clamp_raw = 0,
         .cull2_raw = UINT32_C(0x80),
      };
      struct agx_ppp_hal300_depth_stencil_core depth_stencil;
      const struct agx_ppp_hal300_depth_stencil_fields depth_stencil_fields = {
         /* Exact G17G/G17X fixed-layout initial render-state witness. */
         .fragment_control_raw = UINT32_C(0x000007aa),
         .fragment_front_face_raw = UINT32_C(0xe2e00000),
         .fragment_back_face_raw = UINT32_C(0xf0c00000),
         .fragment_front_stencil_raw = UINT32_C(0x00023020),
         .fragment_back_stencil_raw = UINT32_C(0x0001ffff),
      };
      if (!agx_ppp_hal300_depth_stencil_core_pack(
             dev->dev.chip, &depth_stencil_fields, &depth_stencil) ||
          !agx_ppp_w_clamp_cull2_core_pack(dev->dev.chip, &fields,
                                            &initial)) {
         mesa_loge("no grounded HAL300 initial PPP payload for %s",
                   agx_chip_name(dev->dev.chip));
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
         return;
      }

      const uint64_t depth_stencil_gpu = hk_pool_upload(
         cmd, depth_stencil.bytes, depth_stencil.length,
         AGX_PPP_HEADER_ALIGN);
      const uint64_t gpu = hk_pool_upload(
         cmd, initial.bytes, initial.length, AGX_PPP_HEADER_ALIGN);
      uint8_t records[2 * AGX_PPP_STATE_LENGTH];
      uint8_t *record = records;
      if (!depth_stencil_gpu || !gpu ||
          !agx_ppp_emit_state(dev->dev.chip, &record, depth_stencil_gpu,
                              depth_stencil.length) ||
          !agx_ppp_emit_state(dev->dev.chip, &record, gpu, initial.length) ||
          record != records + sizeof(records)) {
         mesa_loge("failed to emit HAL300 initial PPP state for %s",
                   agx_chip_name(dev->dev.chip));
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         return;
      }

      memcpy(map, records, sizeof(records));
      map += sizeof(records);
   } else if (agx_native_graphics(&dev->dev)) {
      /* PPPEncoderGen6's inspected G17P fixed-layout opening is one exact
       * W-clamp/Cull-2 payload, not the legacy AGX2 selector graph below.
       * Keep the payload in the compact VDM window just like every other
       * nested HAL200 PPP pointer.
       */
      struct agx_ppp_w_clamp_cull2_core initial;
      const struct agx_ppp_w_clamp_cull2_fields fields = {
         .w_clamp_raw = 0,
         .cull2_raw = UINT32_C(0x80),
      };

      if (!agx_ppp_w_clamp_cull2_core_pack(dev->dev.chip, &fields,
                                            &initial)) {
         mesa_loge("no grounded G17P initial PPP payload for %s",
                   agx_chip_name(dev->dev.chip));
         vk_command_buffer_set_error(&cmd->vk,
                                     VK_ERROR_FEATURE_NOT_PRESENT);
         return;
      }

      struct agx_ptr T =
         hk_pool_vdm_alloc(cmd, initial.length, AGX_PPP_HEADER_ALIGN);
      if (!T.cpu)
         return;

      memcpy(T.cpu, initial.bytes, initial.length);
      if (!agx_ppp_emit_state(dev->dev.chip, &map, T.gpu, initial.length)) {
         mesa_loge("failed to emit G17P initial PPP state for %s",
                   agx_chip_name(dev->dev.chip));
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         return;
      }

      if (hk_g17p_launch_trace_enabled())
         hk_g17p_trace_line("ppp-initial", T.gpu, T.cpu, initial.length);
   } else {
      struct AGX_PPP_HEADER present = {
         .w_clamp = true,
         .occlusion_query_2 = true,
         .output_unknown = true,
         .varying_word_2 = true,
         .viewport_count = 1, /* irrelevant */
      };

      size_t size = agx_ppp_update_size(&present);
      struct agx_ptr T = hk_pool_vdm_alloc(cmd, size, AGX_PPP_HEADER_ALIGN);
      if (!T.cpu)
         return;

      struct agx_ppp_update ppp = agx_new_ppp_update(T, size, &present);

      /* clang-format off */
      agx_ppp_push(&ppp, W_CLAMP, cfg) cfg.w_clamp = 1e-10;
      agx_ppp_push(&ppp, FRAGMENT_OCCLUSION_QUERY_2, cfg);
      agx_ppp_push(&ppp, OUTPUT_UNKNOWN, cfg);
      agx_ppp_push(&ppp, VARYING_2, cfg);
      /* clang-format on */

      const bool ppp_ok = agx_ppp_legacy_payload_supported(dev->dev.chip) &&
                          agx_ppp_fini(dev->dev.chip, &map, &ppp);
      if (!ppp_ok) {
         mesa_loge("no grounded legacy PPP payload/finalizer for %s",
                   agx_chip_name(dev->dev.chip));
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
         return;
      }

      if (hk_g17p_launch_trace_enabled())
         hk_g17p_trace_line("ppp-initial", ppp.gpu_base, ppp.cpu_base,
                            ppp.total_size);
   }
   cs->current = map;

   cs->scissor = UTIL_DYNARRAY_INIT;
   cs->depth_bias = UTIL_DYNARRAY_INIT;

   /* All graphics state must be reemited in each control stream */
   hk_cmd_buffer_dirty_all(cmd);

   if (agx_native_graphics(&dev->dev))
      assert((cs->current - cs->start) == HK_G17P_VDM_INITIAL_SIZE);
}

void
hk_ensure_cs_has_space(struct hk_cmd_buffer *cmd, struct hk_cs *cs,
                       size_t space)
{
   bool vdm = cs->type == HK_CS_VDM;

   size_t link_length =
      vdm ? AGX_VDM_STREAM_LINK_LENGTH : AGX_CDM_STREAM_LINK_LENGTH;

   /* Assert that we have space for a link tag */
   assert((cs->current + link_length) <= cs->end && "Encoder overflowed");

   /* Always leave room for a link tag, in case we run out of space later,
    * plus padding because VDM apparently overreads?
    *
    * 0x200 is not enough. 0x400 seems to work. 0x800 for safety.
    */
   /* If there is room in the command buffer, we're done */
   if (likely(hk_cs_space_fits(cs->end - cs->current, space, link_length)))
      return;

   /* Otherwise, we need to allocate a new command buffer. We use memory owned
    * by the batch to simplify lifetime management for the BO.
    */
   size_t size = 65536;
   struct agx_ptr T = hk_pool_alloc(cmd, size, 256);

   /* Jump from the old control stream to the new control stream. */
   void *link_end =
      hk_cmd_buffer_emit_cs_jump(cmd, cs->current, T.gpu, vdm);
   const bool linked = link_end != cs->current;

   /* Swap out the control stream */
   cs->current = T.cpu;
   cs->end = cs->current + size;
   cs->chunk = T;
   cs->stream_linked |= linked;
}

static void
clear_attachment_as_image(struct hk_cmd_buffer *cmd,
                          struct hk_rendering_state *render,
                          struct hk_attachment *att, unsigned aspect)
{
   struct hk_image_view *view = att->iview;
   if (!att->clear || !view || !(view->vk.aspects & aspect))
      return;

   const uint32_t layer_count = render->view_mask
                                   ? util_last_bit(render->view_mask)
                                   : render->layer_count;

   const VkImageSubresourceRange range = {
      .layerCount = layer_count,
      .levelCount = 1,
      .baseArrayLayer = view->vk.base_array_layer,
      .baseMipLevel = view->vk.base_mip_level,
      .aspectMask = view->vk.aspects & aspect,
   };

   assert(util_bitcount(range.aspectMask) == 1);
   VkFormat format = att->vk_format;

   if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
      format = vk_format_depth_only(format);
   } else if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
      format = vk_format_stencil_only(format);
   }

   struct hk_image *image = container_of(view->vk.image, struct hk_image, vk);
   hk_clear_image(cmd, image, vk_format_to_pipe_format(format),
                  att->clear_colour, &range, false /* partially clear 3D */);
}

bool
hk_optimize_empty_vdm(struct hk_cmd_buffer *cmd)
{
   struct hk_cs *cs = cmd->current_cs.gfx;
   struct hk_rendering_state *render = &cmd->state.gfx.render;
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   /* Native libagx kernels are now compiled through hk_dispatch_precomp,
    * so an unpopulated AGX2 precompiled cache no longer forbids compute
    * clears. Keep a real VDM when its firmware timestamp has already been
    * selected: deleting it would discard that query write.
    */
   if (agx_native_graphics(&dev->dev) && cs->cr.process_empty_tiles &&
       cs->timestamp.end.addr) {
      /* Normally supplied by the first draw. A clear has no application
       * sample-location state; use the attachment sample count and leave
       * has_sample_locations uncommitted, just like other meta operations.
       */
      cs->ppp_multisamplectl =
         hk_default_sample_positions(MAX2(cs->tib.nr_samples, 1));

      /* The render submission still binds these arrays even without PPP draw
       * state referencing them. Give it an owned, neutral full-frame entry
       * rather than a zero-byte upload. Render-area handling and empty-tile
       * selection remain in the existing render setup.
       */
      if (cs->scissor.size == 0) {
         uint8_t *scissor =
            util_dynarray_grow_bytes(&cs->scissor, 1, AGX_SCISSOR_LENGTH);
         agx_pack(scissor, SCISSOR, cfg) {
            cfg.min_x = cfg.min_y = 0;
            cfg.max_x = cs->cr.width;
            cfg.max_y = cs->cr.height;
            cfg.min_z = 0.0f;
            cfg.max_z = 1.0f;
         }
      }

      if (cs->depth_bias.size == 0) {
         void *bias = util_dynarray_grow_bytes(&cs->depth_bias, 1,
                                              AGX_DEPTH_BIAS_LENGTH);
         memset(bias, 0, AGX_DEPTH_BIAS_LENGTH);
      }

      if (hk_g17p_launch_trace_enabled()) {
         fprintf(stderr, "G17P clear-only graphics: %ux%u samples=%u "
                         "sample_control=%#x process_empty_tiles=1 "
                         "scissor=%u zbias=%u\n",
                 cs->cr.width, cs->cr.height, MAX2(cs->tib.nr_samples, 1),
                 cs->ppp_multisamplectl, cs->scissor.size,
                 cs->depth_bias.size);
      }
      return false;
   }

   for (unsigned i = 0; i < render->color_att_count; ++i) {
      clear_attachment_as_image(cmd, render, &render->color_att[i], ~0);
   }

   clear_attachment_as_image(cmd, render, &render->depth_att,
                             VK_IMAGE_ASPECT_DEPTH_BIT);

   clear_attachment_as_image(cmd, render, &render->stencil_att,
                             VK_IMAGE_ASPECT_STENCIL_BIT);

   /* Remove the VDM control stream from the command buffer, now that it is
    * replaced by equivalent other operations.
    */
   list_del(&cs->node);
   hk_cs_destroy(cs);
   return true;
}
