/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "util/macros.h"

#include "util/list.h"
#include "agx_abi.h"
#include "agx_helpers.h"
#include "agx_linker.h"
#include "agx_pack.h"
#include "agx_tilebuffer.h"
#include "agx_uvs.h"
#include "libagx_dgc.h"
#include "pool.h"
#include "shader_enums.h"

#include "hk_private.h"
#include "hk_shader.h"

#include "hk_cmd_pool.h"
#include "hk_cs_root_size.h"
#include "hk_descriptor_set.h"
#include "hk_drm_render_core.h"
#include "hk_g17p_draw_state_upload.h"

#include "asahi/lib/agx_nir_lower_vbo.h"
#include "util/u_dynarray.h"
#include "vulkan/vulkan_core.h"

#include "vk_command_buffer.h"

#include <stdio.h>

struct hk_native_program_cache;
struct hk_buffer;
struct hk_cmd_bo;
struct hk_cmd_pool;
struct hk_image_view;
struct hk_push_descriptor_set;
struct hk_shader;
struct hk_linked_shader;
struct agx_usc_builder;
struct vk_shader;

/** Root descriptor table. */
struct hk_root_descriptor_table {
   /* Address of this descriptor itself. Must be first for reflection. */
   uint64_t root_desc_addr;

   /* Descriptor set base addresses. Must follow root_desc_addr to match our
    * push layout.
    */
   uint64_t sets[HK_MAX_SETS];

   union {
      struct {
         uint32_t view_index;
         uint32_t ppp_multisamplectl;

         /* Vertex input state */
         uint64_t attrib_base[AGX_MAX_VBUFS];
         uint32_t attrib_clamps[AGX_MAX_VBUFS];
         uint32_t attrib_strides[AGX_MAX_VBUFS];

         /* Mask of outputs flowing VS->TCS, VS->GS, or TES->GS . */
         uint64_t vertex_outputs;

         /* Address of vertex param buffer if geom/tess is used, else 0 */
         uint64_t vertex_params;

         /* Address of tessellation param buffer if tessellation used, else 0 */
         uint64_t tess_params;

         /* Address of geometry param buffer if GS is used, else 0 */
         uint64_t geometry_params;

         /* Pipeline statistics queries. This is a base address with flags. */
         uint64_t pipeline_stats;
         VkQueryPipelineStatisticFlags pipeline_stats_flags;

         float blend_constant[4];
         uint16_t no_epilog_discard;
         uint16_t clip_z_coeff; /* half: 0 for [0,1], 0.5 for [-1,1] */
         uint16_t api_sample_mask;
         uint16_t samples_log2;
         uint16_t force_never_in_shader;
         uint16_t _pad3;
         uint16_t provoking;
         uint16_t _pad4;

         /* True if there is an API geometry shader. If false, there may still
          * be a geometry shader in use (notably for transform feedback) but it
          * should not contribute to pipeline statistics.
          */
         uint16_t api_gs;
         uint16_t _pad5;

         uint16_t rasterization_stream;
         uint16_t _pad6;

         /* Alpha-to-coverage enable for fragment shaders that write the
          * sample mask; they apply it before their own coverage update.
          * Sysval fields must sit at 4-byte offsets (root table loads). */
         uint16_t alpha_to_coverage;
         uint16_t _pad7;

         /* Mapping from varying slots written by the last vertex stage to UVS
          * indices. This mapping must be compatible with the fragment shader.
          */
         uint8_t uvs_index[VARYING_SLOT_MAX];
      } draw;
      struct {
         /* GPU address of the immutable NIR constant-data blob for the
          * currently bound compute shader. */
         uint64_t constant_data_addr;
         uint64_t group_count_addr;
         uint32_t base_group[3];
      } cs;
   };

   /* Client push constants */
   uint8_t push[HK_MAX_PUSH_SIZE];

   /* Dynamic buffer bindings */
   struct hk_buffer_address dynamic_buffers[HK_MAX_DYNAMIC_BUFFERS];

   /* Start index in dynamic_buffers where each set starts */
   uint8_t set_dynamic_buffer_start[HK_MAX_SETS];

   /* Process-local on G17P; loaded through the root instead of cached code. */
   uint64_t printf_buffer_address;
   /* Process-local sparse map and VA geometry, never embedded in cached code. */
   uint64_t sparse_residency_map;
   uint64_t sparse_va_base;
   uint64_t sparse_va_size;
   uint64_t atomic64_lock;
   uint64_t buffer_alias_table;
   /* Immutable NIR constant table for the currently bound fragment shader. */
   uint64_t fragment_constant_data_addr;
};

/* helper macro for computing root descriptor byte offsets */
#define hk_root_descriptor_offset(member)                                      \
   offsetof(struct hk_root_descriptor_table, member)

struct hk_descriptor_state {
   bool root_dirty;
   struct hk_root_descriptor_table root;

   uint32_t set_sizes[HK_MAX_SETS];
   struct hk_descriptor_set *sets[HK_MAX_SETS];
   uint32_t sets_dirty;

   struct hk_push_descriptor_set *push[HK_MAX_SETS];
   uint32_t push_dirty;
};

/* A hardware fragment shader cannot consume vertex-input buffer state. The
 * root's self address may differ while its fragment-visible fields are equal;
 * a reused program keeps the previous immutable root alive in the same CB. */
static inline bool
hk_multidraw_fragment_root_equal(const struct hk_root_descriptor_table *a,
                                 const struct hk_root_descriptor_table *b)
{
   const size_t begin = sizeof(a->root_desc_addr);
   const size_t vertex_begin = offsetof(struct hk_root_descriptor_table,
                                       draw.attrib_base);
   const size_t vertex_end = offsetof(struct hk_root_descriptor_table,
                                     draw.vertex_outputs);
   return !memcmp((const uint8_t *)a + begin, (const uint8_t *)b + begin,
                  vertex_begin - begin) &&
          !memcmp((const uint8_t *)a + vertex_end,
                  (const uint8_t *)b + vertex_end, sizeof(*a) - vertex_end);
}

struct hk_multidraw_resource_cache {
   bool fragment_enabled, ubo_enabled;
   struct hk_cs *cs;
   struct hk_shader *shader;
   struct hk_linked_shader *linked;
   uint64_t images, samplers, program;
   struct hk_root_descriptor_table root;
   struct {
      struct hk_api_shader *api;
      unsigned variant;
      bool valid, direct;
   } ubo[2];
};

static inline bool
hk_prefetch_buffer_address(const struct hk_descriptor_state *state,
                           const struct hk_ubo_prefetch *load,
                           struct hk_buffer_address *address)
{
   const uint8_t *data;
   uint32_t size;
   if (load->set == HK_UBO_PREFETCH_ROOT_SET) {
      data = (const uint8_t *)&state->root;
      size = sizeof(state->root);
   } else {
      assert(load->set < HK_MAX_SETS);
      const struct hk_descriptor_set *set = state->sets[load->set];
      data = set ? set->mapped_ptr :
         state->push[load->set] ? state->push[load->set]->data : NULL;
      /* Push data is already available before its first GPU upload. The
       * cached set size is only filled by flush_push_descriptors later. */
      size = set ? state->set_sizes[load->set] :
         state->push[load->set] ? sizeof(state->push[load->set]->data) : 0;
   }
   if (!data || load->descriptor_offset_B > size ||
       sizeof(*address) > size - load->descriptor_offset_B)
      return false;
   memcpy(address, data + load->descriptor_offset_B, sizeof(*address));
   address->base_addr &= ~HK_BUFFER_ADDRESS_DIRECT_BIT;
   return true;
}

struct hk_attachment {
   VkFormat vk_format;
   struct hk_image_view *iview;

   VkResolveModeFlagBits resolve_mode;
   struct hk_image_view *resolve_iview;

   bool clear;
   uint32_t clear_colour[4];
};

struct hk_bg_eot {
   uint64_t usc;
   union {
      struct agx_counts_packed counts;
      uint64_t rsrc_spec;
   };
};

struct hk_render_registers {
   uint32_t width, height, layers;
   uint32_t zls_width, zls_height;
   uint32_t isp_bgobjdepth;
   uint32_t isp_bgobjvals;
   struct agx_zls_control_packed zls_control, zls_control_partial;
   uint32_t depth_dimensions;
   bool process_empty_tiles;
   /* NONE/NONE attachments must remain untouched when no draw uses them. */
   bool depth_none, stencil_none;
   enum u_tristate dbias_is_int;

   struct {
      uint32_t dimensions;
      uint64_t buffer, meta;
      uint32_t stride, meta_stride;
   } depth;

   struct {
      uint64_t buffer, meta;
      uint32_t stride, meta_stride;
   } stencil;

   struct {
      struct hk_bg_eot main;
      struct hk_bg_eot partial;
   } bg;

   struct {
      struct hk_bg_eot main;
      struct hk_bg_eot partial;
   } eot;
};

struct hk_rendering_state {
   VkRenderingFlagBits flags;

   VkRect2D area;
   uint32_t layer_count;
   uint32_t view_mask;

   uint32_t color_att_count;
   struct hk_attachment color_att[HK_MAX_RTS];
   struct hk_attachment depth_att;
   struct hk_attachment stencil_att;

   struct agx_tilebuffer_layout tilebuffer;
   struct hk_render_registers cr;
};

struct hk_index_buffer_state {
   struct hk_addr_range buffer;
   enum agx_index_size size;
   uint32_t restart;
};

/* Dirty tracking bits for state not tracked by vk_dynamic_graphics_state or
 * shaders_dirty.
 */
enum hk_dirty {
   HK_DIRTY_VB = BITFIELD_BIT(0),
   HK_DIRTY_OCCLUSION = BITFIELD_BIT(1),
   HK_DIRTY_PROVOKING = BITFIELD_BIT(2),
   HK_DIRTY_VARYINGS = BITFIELD_BIT(3),
};

struct hk_graphics_state {
   struct hk_rendering_state render;
   struct hk_descriptor_state descriptors;

   enum hk_dirty dirty;

   uint64_t root;
   uint64_t draw_params;
   uint64_t draw_id_ptr;

   bool fs_ubo_direct;
   bool vs_ubo_direct;
   uint32_t shaders_dirty;
   struct hk_api_shader *shaders[MESA_SHADER_MESH + 1];

   /* Vertex buffers */
   struct hk_addr_range vb[AGX_MAX_VBUFS];

   /* Transform feedback buffers */
   struct hk_addr_range xfb[4];

   /* Is transform feedback enabled? */
   bool xfb_enabled;

   /* Internal transform feedback offset vec4.
    *
    * TODO: Strictly could be global.
    */
   uint64_t xfb_offsets;

   /* Pointer to the GPU memory backing active transform feedback queries,
    * per-stream. Zero if no query is bound.
    */
   uint64_t xfb_query[4];

   struct hk_index_buffer_state index;
   enum agx_primitive topology;
   enum agx_object_type object_type;

   /* Provoking vertex 0, 1, or 2. Usually 0 or 2 for FIRST/LAST. 1 can only be
    * set for tri fans.
    */
   uint8_t provoking;

   struct {
      enum agx_visibility_mode mode;

      /* If enabled, index of the current occlusion query in the occlusion heap.
       * There can only be one active at a time (hardware contraint).
       */
      uint16_t index;
   } occlusion;

   /* Fast linked shader data structures */
   uint64_t varyings;
   struct agx_varyings_vs linked_varyings;

   uint32_t linked_dirty;
   struct hk_linked_shader *linked[MESA_SHADER_STAGES];
   bool generate_primitive_id;

   /* Whether blend constants are required by the active blend state */
   bool uses_blend_constant;

   /* Tessellation state */
   struct {
      uint64_t out_draws;
      uint64_t grids;
      struct hk_tess_info info;
      enum mesa_prim prim;
   } tess;

   /* Needed by vk_command_buffer::dynamic_graphics_state */
   struct vk_vertex_input_state _dynamic_vi;
   struct vk_sample_locations_state _dynamic_sl;
};

struct hk_compute_state {
   struct hk_descriptor_state descriptors;
   struct hk_api_shader *shader;
};

struct hk_cmd_push {
   void *map;
   uint64_t addr;
   uint32_t range;
   bool no_prefetch;
};

struct hk_scratch_req {
   bool main;
   bool preamble;
};

/*
 * Represents a firmware timestamp request.  Handle is a kernel timestamp object
 * handle, not a GEM handle.
 *
 * The kernel/firmware uses the handle/offset_B to write. We use the address to
 * read the results back. We could deduplicate this, but this is convenient.
 */
struct agx_timestamp_req {
   uint64_t addr;
   uint32_t handle;
   uint32_t offset_B;
};

/*
 * hk_cs represents a single control stream, to be enqueued either to the
 * CDM or VDM for compute/3D respectively.
 */
enum hk_cs_type {
   HK_CS_CDM,
   HK_CS_VDM,
};

struct hk_cs {
   struct list_head node;

   /* Parent command buffer. Convenience. */
   struct hk_cmd_buffer *cmd;

   /* Data master */
   enum hk_cs_type type;

   /* Address of the root control stream for the job */
   uint64_t addr;

   /* Fat pointer to the start of the current chunk of the control stream */
   struct agx_ptr chunk;

   /* Start pointer of the root control stream */
   void *start;

   /* Current pointer within the control stream */
   void *current;

   /* End pointer of the current chunk of the control stream */
   void *end;

   /* Whether there is more than just the root chunk */
   bool stream_linked;

   /* GPU timestamp resets must retire before firmware writes query results. */
   bool ends_timestamp_reset;

   /* One no-op dispatch created solely to obtain a firmware timestamp. */
   bool timestamp_only;

   /* Memory dependency: publication is required before merging a reader. */
   bool ends_memory_dependency;

   /* Exact native wide-visibility operation is at the end of this CDM. */
   bool ends_wide_visibility;

   /* Active-context selector consumed by AGX3 VertexProgramVariant. */
   bool agx3_active_context_selector_valid;
   uint32_t agx3_raw_active_context_selector;

   /* Whether the sampler heap is required. Although we always must maintain the
    * heap for correctness, it's often not necessary since we can push lots of
    * samplers (especially for GL/DX11-era engines).
    */
   bool uses_sampler_heap;

   /* Accumulated across all draws, including replayed secondaries and meta. */
   bool depth_test_used, stencil_test_used;

   /* Scratch requirements */
   struct {
      union {
         struct hk_scratch_req vs;
         struct hk_scratch_req cs;
      };

      struct hk_scratch_req fs;
   } scratch;

   /* Immediate writes, type libagx_imm_write. These happen at the end of the
    * control stream (parallel on AGX2, native ordered stores on G17P). This
    * accelerates queries. Implies CDM.
    */
   struct util_dynarray imm_writes;

   /* Statistics */
   struct {
      uint32_t calls, cmds, flushes, merged;
      uint32_t fills, timestamp_copies, query_copies, precomp_calls, precomp_id;
   } stats;

   /* Native start timestamps can absorb a preceding timestamp-only command. */
   struct {
      struct agx_timestamp_req start, end;
      struct agx_timestamp_req end_copies[8];
      unsigned end_copy_count;
   } timestamp;

   /* Writable destinations known while recording meta compute dispatches. */
   struct hk_drm_attachment_set compute_attachments;

   /* Whole private image-plane ranges, including compression metadata.
    * The count must cover every dispatch before clear-only merging is safe. */
   struct hk_drm_attachment_set clear_destinations;
   unsigned clear_dispatches;

   /* Internal address fills have an exact write footprint, unlike arbitrary
    * application compute. Finalize only if these account for every dispatch. */
   struct hk_drm_attachment_set fill_destinations;
   unsigned fill_dispatches;

   /* Buffer copies have exact destination ranges, including sparse VAs.
    * Dependency barriers remain in the linked native stream. */
   struct hk_drm_attachment_set copy_destinations;
   unsigned copy_dispatches;

   /* Query copies read private query-pool storage and write these ranges.
    * Only a stream consisting entirely of these dispatches is covered. */
   struct hk_drm_attachment_set query_destinations;
   unsigned query_dispatches;

   /* Remaining state is for graphics only, ignored for compute */
   struct agx_tilebuffer_layout tib;

   /* One immutable bind record owned by this command pool. Never reuse an
    * allocation across reset, and never overwrite an earlier draw's bytes. */
   struct agx_ptr native_bind_state;
   size_t native_bind_size;
   struct hk_g17p_bind_state_upload native_bind_upload;
   unsigned native_bind_uploads, native_bind_reuses, native_bind_elisions;

   struct util_dynarray scissor, depth_bias;
   uint64_t uploaded_scissor, uploaded_zbias;

   /* We can only set ppp_multisamplectl once per batch. has_sample_locations
    * tracks if we've committed to a set of sample locations yet. vk_meta
    * operations do not set has_sample_locations since they don't care and it
    * would interfere with the app-provided samples.
    *
    */
   bool has_sample_locations;
   uint32_t ppp_multisamplectl;

   struct hk_render_registers cr;

   /* Snapshot of the writable image ranges for this exact VDM command. Render
    * state may change before queue submission, so the synthetic fragment
    * attachment command must travel with the control stream it describes. */
   struct hk_drm_attachment_set fragment_attachments;
   bool writes_memory;
   bool writes_statistics;
   bool fragment_only_dependency;
   bool independent_tiling;

   /* Active restart index if one is set. Zero if there is no restart index set
    * yet, since Vulkan does not allow zero restart indices (unlike OpenGL).
    * This is used in place of dirty tracking, because dirty tracking
    * restart indices is complicated and just checking the saved value is cheap.
    */
   uint32_t restart_index;
};

void *hk_cmd_buffer_emit_cs_jump(struct hk_cmd_buffer *cmd, void *current,
                                 uint64_t target, bool vdm);

/*
 * Helper to merge two compute control streams, concatenating the second control
 * stream to the first one. Must sync with hk_cs.
 */
static inline void
hk_cs_merge_cdm(struct hk_cs *a, const struct hk_cs *b)
{
   assert(a->type == HK_CS_CDM && b->type == HK_CS_CDM);
   assert(a->cmd == b->cmd);
   assert(!a->timestamp.end.handle && !b->timestamp.start.handle);

   void *link_end =
      hk_cmd_buffer_emit_cs_jump(a->cmd, a->current, b->addr, false);
   const bool linked = link_end != a->current;
   a->current = b->current;
   a->stream_linked |= linked || b->stream_linked;

   a->uses_sampler_heap |= b->uses_sampler_heap;
   a->writes_memory |= b->writes_memory;
   a->scratch.cs.main |= b->scratch.cs.main;
   a->scratch.cs.preamble |= b->scratch.cs.preamble;

   a->timestamp.end = b->timestamp.end;
   assert(a->timestamp.end_copy_count == 0);
   a->timestamp.end_copy_count = b->timestamp.end_copy_count;
   memcpy(a->timestamp.end_copies, b->timestamp.end_copies,
          sizeof(a->timestamp.end_copies));
   a->timestamp_only = false;
   a->ends_timestamp_reset |= b->ends_timestamp_reset;
   a->ends_memory_dependency |= b->ends_memory_dependency;
   a->ends_wide_visibility = b->ends_wide_visibility;

   a->clear_dispatches += b->clear_dispatches;
   a->fill_dispatches += b->fill_dispatches;
   a->copy_dispatches += b->copy_dispatches;
   if (!hk_drm_attachment_set_merge(&a->copy_destinations,
                                    &b->copy_destinations,
                                    HK_DRM_MAX_FRAGMENT_ATTACHMENTS))
      a->copy_destinations.complete = false;
   a->query_dispatches += b->query_dispatches;
   if (!hk_drm_attachment_set_merge(&a->query_destinations,
                                    &b->query_destinations,
                                    HK_DRM_MAX_FRAGMENT_ATTACHMENTS))
      a->query_destinations.complete = false;
   if (!hk_drm_attachment_set_merge(&a->fill_destinations,
                                     &b->fill_destinations,
                                     HK_DRM_MAX_FRAGMENT_ATTACHMENTS))
      a->fill_destinations.complete = false;
   if (!hk_drm_attachment_set_merge(&a->clear_destinations,
                                     &b->clear_destinations,
                                     HK_DRM_MAX_FRAGMENT_ATTACHMENTS))
      a->clear_destinations.complete = false;


   a->stats.calls += b->stats.calls;
   a->stats.cmds += b->stats.cmds;
   a->stats.flushes += b->stats.flushes;
   a->stats.merged++;
   a->stats.fills += b->stats.fills;
   a->stats.timestamp_copies += b->stats.timestamp_copies;
   a->stats.query_copies += b->stats.query_copies;
   if (b->stats.precomp_calls) {
      if (!a->stats.precomp_calls)
         a->stats.precomp_id = b->stats.precomp_id;
      else if (a->stats.precomp_id != b->stats.precomp_id)
         a->stats.precomp_id = UINT32_MAX;
   }
   a->stats.precomp_calls += b->stats.precomp_calls;
}

static inline uint64_t
hk_cs_current_addr(struct hk_cs *cs)
{
   return cs->chunk.gpu + ((uint8_t *)cs->current - (uint8_t *)cs->chunk.cpu);
}

struct hk_uploader {
   /** List of hk_cmd_bo */
   struct list_head bos;

   /* Current addresses */
   uint8_t *map;
   uint64_t base;
   uint32_t offset;
};

/* Opt-in covered-fragment counts, accumulated over all executions before reset.
 * The command pool owns the mapped counters until the CB may legally reset. */
struct hk_fragment_count {
   uint64_t *words;
   uint64_t cs;
   unsigned width, height;
};

struct hk_cmd_buffer {
   struct vk_command_buffer vk;
   struct agx3_ioto_uniform_template *uniform_templates[2];
   /* Recording-only, owns no shader pointers. Invalidated before upload reset. */
   struct hk_native_program_cache *native_programs[2];
   uint64_t native_program_hits[2], native_program_misses[2];
   struct {
      uint32_t params[2], draw_id;
      uint64_t params_va, draw_id_va;
   } native_draw;
   /* Stack-owned only during one indexed indirect multi-draw API call. */
   struct hk_multidraw_resource_cache *multidraw_resources;

   /* Immutable native heap/query pointer uploads. Small direct-mapped cache;
    * contents and size, never descriptor object identity, determine reuse. */
   struct {
      uint64_t words[4];
      uint64_t address;
      unsigned size;
   } native_pointer_uploads[4];
   uint64_t native_prefetch_eligible[2];

   struct {
      struct hk_graphics_state gfx;
      struct hk_compute_state cs;
      uint64_t conditional;
   } state;

   bool conditional_meta_clear;

   struct {
      struct hk_uploader main, usc;
   } uploader;

   /* Recording-only locality cache. Submission may read it but never updates
    * it, including simultaneous-use command buffers. Cleared before BO reset. */
   struct hk_cmd_bo *vdm_lookup_bo;

   /* List of all recorded control streams */
   struct list_head control_streams;

   /* Current recorded control stream */
   struct {
      /* VDM stream for 3D */
      struct hk_cs *gfx;

      /* CDM stream for compute */
      struct hk_cs *cs;

      /* CDM stream that executes immediately before the current graphics
       * control stream. Used for geometry shading, tessellation, etc.
       */
      struct hk_cs *pre_gfx;

      /* CDM stream that will execute after the current graphics control stream
       * finishes. Used for queries.
       */
      struct hk_cs *post_gfx;
   } current_cs;

   /* Are we currently inside a vk_meta operation? This alters sample location
    * behaviour.
    */
   bool in_meta;

   /* XXX: move me?
    *
    * Indirect draw generated by the indirect GS translator.
    */
   uint64_t geom_index_buffer;
   uint32_t geom_index_count;
   uint32_t geom_instance_count;

   /* Does the command buffer use the geometry heap? */
   bool uses_heap;

   /* Execution destinations accumulated since the preceding graphics CS. */
   VkPipelineStageFlags2 graphics_dependency_stages;

   /* Owned BOs and retained references needed by recorded commands. */
   struct util_dynarray large_bos;
   struct util_dynarray fragment_counts;
};

VK_DEFINE_HANDLE_CASTS(hk_cmd_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

extern const struct vk_command_buffer_ops hk_cmd_buffer_ops;

static inline struct hk_cmd_pool *
hk_cmd_buffer_pool(struct hk_cmd_buffer *cmd)
{
   return (struct hk_cmd_pool *)cmd->vk.pool;
}

/*
 * The hardware vertex shader is supplied by the last geometry stage. The
 * geometry pipeline is vertex->tess->geometry so we search backwards.
 */
static inline struct hk_shader *
hk_bound_fs(struct hk_graphics_state *gfx)
{
   struct hk_api_shader *fs = gfx->shaders[MESA_SHADER_FRAGMENT];
   return fs ? &fs->variants[gfx->fs_ubo_direct &&
                             fs->variants[1].info.ubo_prefetch_direct] : NULL;
}

static inline struct hk_shader *
hk_bound_hw_vs(struct hk_graphics_state *gfx)
{
   struct hk_api_shader *vs = gfx->shaders[MESA_SHADER_VERTEX];
   struct hk_api_shader *tes = gfx->shaders[MESA_SHADER_TESS_EVAL];
   struct hk_api_shader *gs = gfx->shaders[MESA_SHADER_GEOMETRY];

   if (gs)
      return &gs->variants[HK_GS_VARIANT_RAST];
   else if (tes)
      return &tes->variants[HK_VS_VARIANT_HW];
   else
      return &vs->variants[gfx->vs_ubo_direct &&
                          vs->variants[HK_VS_VARIANT_HW_UBO].info.ubo_prefetch_direct
                             ? HK_VS_VARIANT_HW_UBO : HK_VS_VARIANT_HW];
}

static inline struct hk_shader *
hk_bound_sw_vs(struct hk_graphics_state *gfx)
{
   struct hk_api_shader *vs = gfx->shaders[MESA_SHADER_VERTEX];
   struct hk_shader *hw_vs = hk_bound_hw_vs(gfx);

   if (hw_vs == &vs->variants[HK_VS_VARIANT_HW] ||
       hw_vs == &vs->variants[HK_VS_VARIANT_HW_UBO])
      return hw_vs;
   else
      return &vs->variants[HK_VS_VARIANT_SW];
}

static inline struct hk_shader *
hk_bound_sw_vs_before_gs(struct hk_graphics_state *gfx)
{
   struct hk_api_shader *vs = gfx->shaders[MESA_SHADER_VERTEX];
   struct hk_api_shader *tes = gfx->shaders[MESA_SHADER_TESS_EVAL];
   struct hk_api_shader *api = tes ?: vs;

   return &api->variants[HK_VS_VARIANT_SW];
}

struct agx_ptr hk_pool_alloc_internal(struct hk_cmd_buffer *cmd, uint32_t size,
                                      uint32_t alignment, bool usc);

uint64_t hk_g17p_vdm_compact(struct hk_cmd_buffer *cmd, uint64_t primary);
uint64_t hk_g17p_vdm_alias(struct hk_cmd_buffer *cmd, uint64_t primary);

uint64_t hk_pool_upload(struct hk_cmd_buffer *cmd, const void *data,
                        uint32_t size, uint32_t alignment);
uint64_t hk_pool_usc_upload(struct hk_cmd_buffer *cmd, const void *data,
                            uint32_t size, uint32_t alignment);

/* Keep externally owned storage referenced by recorded GPU commands alive for
 * the command-buffer lifetime. Repeated uses of the same BO are deduplicated.
 */
void hk_cmd_buffer_retain_bo(struct hk_cmd_buffer *cmd, struct agx_bo *bo);
void hk_cmd_buffer_release_bos(struct hk_cmd_buffer *cmd,
                               struct agx_device *dev);

static inline struct agx_ptr
hk_pool_alloc(struct hk_cmd_buffer *cmd, uint32_t size, uint32_t alignment)
{
   return hk_pool_alloc_internal(cmd, size, alignment, false);
}

static inline struct agx_ptr
hk_pool_usc_alloc(struct hk_cmd_buffer *cmd, uint32_t size, uint32_t alignment)
{
   return hk_pool_alloc_internal(cmd, size, alignment, true);
}

/*
 * Allocate a payload that a VDM record references through its 40-bit pointer.
 * G17P's ordinary main heap has bit 39 set, which selects the upper UAT root
 * in the VDM state-fetch path. Main command-pool BOs are also bound in a
 * device-global low alias aperture. Internal VDM packets and HAL200 bind pairs
 * carry that context-relative compact offset; hardware adds 0x1000000000
 * before fetching the alias. Retain the same CPU mapping and backing storage.
 */
static inline struct agx_ptr
hk_pool_vdm_alloc(struct hk_cmd_buffer *cmd, uint32_t size, uint32_t alignment)
{
   struct agx_ptr T = hk_pool_alloc(cmd, size, alignment);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   if (T.gpu && hk_g17p_vdm_low_window_enabled(&dev->dev)) {
      T.gpu = hk_g17p_vdm_compact(cmd, T.gpu);
      if (!T.gpu) {
         vk_command_buffer_set_error(&cmd->vk,
                                     VK_ERROR_OUT_OF_DEVICE_MEMORY);
         T.cpu = NULL;
      }
   }

   return T;
}

void hk_cs_init_graphics(struct hk_cmd_buffer *cmd, struct hk_cs *cs);
uint32_t hk_default_sample_positions(unsigned nr_samples);

static inline struct hk_cs *
hk_cmd_buffer_get_cs_general(struct hk_cmd_buffer *cmd, struct hk_cs **ptr,
                             bool compute)
{
   if ((*ptr) == NULL) {
      /* Allocate root control stream */
      struct hk_device *dev = hk_cmd_buffer_device(cmd);
      size_t initial_size = hk_cs_root_size(dev->dev.chip, compute);
      struct agx_ptr root = hk_pool_alloc(cmd, initial_size, 1024);
      if (!root.cpu)
         return NULL;

      /* Allocate hk_cs for the new stream */
      struct hk_cs *cs = malloc(sizeof(*cs));
      *cs = (struct hk_cs){
         .cmd = cmd,
         .type = compute ? HK_CS_CDM : HK_CS_VDM,
         .addr = root.gpu,
         .start = root.cpu,
         .chunk = root,
         .current = root.cpu,
         .end = root.cpu + initial_size,
      };

      if (compute && debug_get_bool_option("HK_G17P_COMPUTE_TRACE", false))
         fprintf(stderr, "HK_COMPUTE_BEGIN cs=%" PRIx64 "\n", cs->addr);

      list_inithead(&cs->node);
      hk_drm_attachment_set_init(&cs->compute_attachments);
      hk_drm_attachment_set_init(&cs->clear_destinations);
      hk_drm_attachment_set_init(&cs->fill_destinations);
      hk_drm_attachment_set_init(&cs->copy_destinations);
      hk_drm_attachment_set_init(&cs->query_destinations);
      /* Only known meta destinations are collected so far. */
      cs->compute_attachments.complete = false;

      bool before_gfx = (ptr == &cmd->current_cs.pre_gfx);

      /* Insert into the command buffer. We usually append to the end of the
       * command buffer, except for pre-graphics streams which go right before
       * the graphics workload. (This implies a level of out-of-order processing
       * that's allowed by Vulkan and required for efficient
       * geometry/tessellation shaders.)
       */
      if (before_gfx && cmd->current_cs.gfx) {
         list_addtail(&cs->node, &cmd->current_cs.gfx->node);
      } else {
         list_addtail(&cs->node, &cmd->control_streams);
      }

      *ptr = cs;

      if (!compute) {
         const VkPipelineStageFlags2 fragment_stages =
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
         /* A graphics stream has no compute stage. A barrier targeting both
          * fragment and compute must still order compute work, but does not
          * introduce a vertex/tiling dependency. Compute streams retain their
          * separate dependencies; queue submission checks the predecessor
          * engine and kernel capability before requesting any overlap. */
         const VkPipelineStageFlags2 graphics_stages =
            cmd->graphics_dependency_stages & ~VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
         cs->independent_tiling = !(graphics_stages & ~fragment_stages);
         cs->fragment_only_dependency = graphics_stages &&
            !(graphics_stages & ~fragment_stages);
         cmd->graphics_dependency_stages = 0;
         hk_cs_init_graphics(cmd, cs);
      }
   }

   assert(*ptr != NULL);
   return *ptr;
}

static inline struct hk_cs *
hk_cmd_buffer_get_cs(struct hk_cmd_buffer *cmd, bool compute)
{
   struct hk_cs **ptr = compute ? &cmd->current_cs.cs : &cmd->current_cs.gfx;
   return hk_cmd_buffer_get_cs_general(cmd, ptr, compute);
}

void hk_ensure_cs_has_space(struct hk_cmd_buffer *cmd, struct hk_cs *cs,
                            size_t space);

static inline uint64_t
hk_cs_alloc_for_indirect(struct hk_cs *cs, size_t size_B)
{
   hk_ensure_cs_has_space(cs->cmd, cs, size_B);

   uint64_t addr = hk_cs_current_addr(cs);
   cs->current += size_B;
   return addr;
}

static void
hk_cmd_buffer_dirty_all(struct hk_cmd_buffer *cmd)
{
   struct vk_dynamic_graphics_state *dyn = &cmd->vk.dynamic_graphics_state;
   struct hk_graphics_state *gfx = &cmd->state.gfx;

   vk_dynamic_graphics_state_dirty_all(dyn);
   gfx->dirty = ~0;
   gfx->shaders_dirty = ~0;
   gfx->linked_dirty = ~0;
   gfx->descriptors.root_dirty = true;
}

static inline void
hk_cs_destroy(struct hk_cs *cs)
{
   if (cs->type == HK_CS_VDM) {
      util_dynarray_fini(&cs->scissor);
      util_dynarray_fini(&cs->depth_bias);
   } else {
      util_dynarray_fini(&cs->imm_writes);
   }

   free(cs);
}

void hk_dispatch_imm_writes(struct hk_cmd_buffer *cmd, struct hk_cs *cs);

bool hk_g17p_smooth_varyings_supported(
   struct hk_cmd_buffer *cmd, const struct hk_linked_shader *linked);
bool hk_g17p_launch_trace_enabled(void);
void hk_g17p_trace_hexdump(const char *label, uint64_t base, const void *data,
                           size_t len);
void hk_g17p_trace_line(const char *label, uint64_t base, const void *data,
                        size_t len);

void *hk_cmd_buffer_emit_cdm_terminate(struct hk_cmd_buffer *cmd,
                                       void *current);
void *hk_cmd_buffer_emit_vdm_terminate(struct hk_cmd_buffer *cmd,
                                       void *current);

void hk_cdm_cache_flush(struct hk_device *dev, struct hk_cs *cs);

static void
hk_cmd_buffer_end_compute_internal(struct hk_cmd_buffer *cmd,
                                   struct hk_cs **ptr)
{
   if (*ptr) {
      struct hk_cs *cs = *ptr;

      /* This control stream may write immediates as it ends. Queue the writes
       * now that we're done emitting everything else.
       */
      if (cs->imm_writes.size) {
         hk_dispatch_imm_writes(cmd, cs);
      }
      if ((hk_cmd_buffer_device(cmd)->dev.chip == AGX_CHIP_G16G ||
           hk_cmd_buffer_device(cmd)->dev.chip == AGX_CHIP_G15S) &&
          debug_get_bool_option("HK_M4_CDM_END_BARRIER",
             debug_get_bool_option("HK_M4_CDM_VISIBILITY_BARRIER", true))) {
         hk_ensure_cs_has_space(cmd, cs, 0x2000);
         hk_cdm_cache_flush(hk_cmd_buffer_device(cmd), cs);
         cs->ends_wide_visibility =
            debug_get_bool_option("HK_M4_CDM_VISIBILITY_BARRIER", true);
      }
   }

   *ptr = NULL;
}

static void
hk_cmd_buffer_end_compute(struct hk_cmd_buffer *cmd)
{
   hk_cmd_buffer_end_compute_internal(cmd, &cmd->current_cs.cs);
}

/* Return true when the VDM was replaced/removed, false when it needs normal
 * graphics finalization (for example, a native background clear). */
bool hk_optimize_empty_vdm(struct hk_cmd_buffer *cmd);


static void
hk_cmd_buffer_end_graphics(struct hk_cmd_buffer *cmd)
{
   struct hk_cs *cs = cmd->current_cs.gfx;

   if (cs && (cs->cr.depth_none || cs->cr.stencil_none)) {
      agx_unpack(NULL, &cs->cr.zls_control, ZLS_CONTROL, zls);
      /* Forced partial-render stores must not turn an unused NONE attachment
       * into a clear. If a draw actually uses the aspect, preserve its initial
       * contents so a read-only/conditionally writing stream also preserves
       * memory when no samples write. Otherwise issue no load or store at all.
       */
      if (cs->cr.depth_none)
         zls.z_load = zls.z_store = cs->depth_test_used;
      if (cs->cr.stencil_none)
         zls.s_load = zls.s_store = cs->stencil_test_used;
      agx_pack(&cs->cr.zls_control, ZLS_CONTROL, cfg) {
         cfg = zls;
      }
   }

   if (cs && cs->stats.cmds == 0 && hk_optimize_empty_vdm(cmd))
      cs = NULL;

   if (cs) {
      struct hk_device *dev = hk_cmd_buffer_device(cmd);
      const bool full_usc_state = agx_native_graphics(&dev->dev);
      /* Scissor and depth bias arrays are staged to dynamic arrays on the
       * CPU. When we end the control stream, they're done growing and are
       * ready for upload. G17P's ISP consumes these as full bit-40-set GART
       * addresses, matching native's USC-window allocations; a main-heap
       * address has bit 39 set and incorrectly selects the upper root.
       */
      cs->uploaded_scissor =
         full_usc_state
            ? hk_pool_usc_upload(cmd, cs->scissor.data, cs->scissor.size, 64)
            : hk_pool_upload(cmd, cs->scissor.data, cs->scissor.size, 64);

      cs->uploaded_zbias =
         full_usc_state
            ? hk_pool_usc_upload(cmd, cs->depth_bias.data,
                                 cs->depth_bias.size, 64)
            : hk_pool_upload(cmd, cs->depth_bias.data,
                             cs->depth_bias.size, 64);

      /* TODO: maybe free scissor/depth_bias now? */
      cs->current = hk_cmd_buffer_emit_vdm_terminate(cmd, cs->current);
   }

   cmd->current_cs.gfx = NULL;

   hk_cmd_buffer_end_compute_internal(cmd, &cmd->current_cs.pre_gfx);
   hk_cmd_buffer_end_compute_internal(cmd, &cmd->current_cs.post_gfx);

   assert(cmd->current_cs.pre_gfx == NULL);
   assert(cmd->current_cs.post_gfx == NULL);

   /* We just flushed out the heap use. If we want to use it again, we'll need
    * to queue a free for it again.
    */
   cmd->uses_heap = false;

   /* Render retirement alone does not make shader stores visible to a later
    * graphics shader on G16G. The native strong CDM barrier covers that cache
    * domain; order it after this render and before any subsequent consumer.
    * Framebuffer-only renders retain their normal attachment handling. */
   if (cs && (cs->writes_memory ||
              (cs->writes_statistics &&
               !debug_get_bool_option("HK_M4_DEFER_STATISTICS_FLUSH", true))) &&
       hk_cmd_buffer_device(cmd)->dev.chip == AGX_CHIP_G16G) {
      struct hk_cs *flush = hk_cmd_buffer_get_cs(cmd, true);
      hk_cdm_cache_flush(hk_cmd_buffer_device(cmd), flush);
      hk_cmd_buffer_end_compute(cmd);
   }
}

static inline uint64_t
hk_pipeline_stat_addr(struct hk_cmd_buffer *cmd,
                      VkQueryPipelineStatisticFlagBits stat)
{
   struct hk_root_descriptor_table *root = &cmd->state.gfx.descriptors.root;
   VkQueryPipelineStatisticFlags flags = root->draw.pipeline_stats_flags;

   if (flags & stat) {
      assert(!cmd->in_meta && "queries paused for meta");
      assert(util_bitcount(stat) == 1 && "by construction");

      /* Prefix sum to determine the compacted index in the query pool */
      uint32_t index = util_bitcount(flags & (stat - 1));

      return root->draw.pipeline_stats + (sizeof(uint64_t) * index);
   } else {
      /* Query disabled */
      return AGX_SCRATCH_PAGE_ADDRESS;
   }
}

static inline bool
hk_stat_enabled(uint64_t addr)
{
   return addr != AGX_SCRATCH_PAGE_ADDRESS;
}

void hk_cmd_buffer_begin_graphics(struct hk_cmd_buffer *cmd,
                                  const VkCommandBufferBeginInfo *pBeginInfo);
void hk_cmd_buffer_begin_compute(struct hk_cmd_buffer *cmd,
                                 const VkCommandBufferBeginInfo *pBeginInfo);

void hk_cmd_invalidate_graphics_state(struct hk_cmd_buffer *cmd);
void hk_cmd_invalidate_compute_state(struct hk_cmd_buffer *cmd);

void hk_cmd_bind_shaders(struct vk_command_buffer *vk_cmd, uint32_t stage_count,
                         const mesa_shader_stage *stages,
                         struct vk_shader **const shaders);

void hk_cmd_bind_graphics_shader(struct hk_cmd_buffer *cmd,
                                 const mesa_shader_stage stage,
                                 struct hk_api_shader *shader);

void hk_cmd_bind_compute_shader(struct hk_cmd_buffer *cmd,
                                struct hk_api_shader *shader);

void hk_cmd_bind_vertex_buffer(struct hk_cmd_buffer *cmd, uint32_t vb_idx,
                               struct hk_addr_range addr_range);

static inline struct hk_descriptor_state *
hk_get_descriptors_state(struct hk_cmd_buffer *cmd,
                         VkPipelineBindPoint bind_point)
{
   switch (bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      return &cmd->state.gfx.descriptors;
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      return &cmd->state.cs.descriptors;
   default:
      UNREACHABLE("Unhandled bind point");
   }
};

void hk_cmd_buffer_flush_push_descriptors(struct hk_cmd_buffer *cmd,
                                          struct hk_descriptor_state *desc);

void hk_meta_resolve_rendering(struct hk_cmd_buffer *cmd,
                               const VkRenderingInfo *pRenderingInfo);

uint64_t hk_cmd_buffer_upload_root(struct hk_cmd_buffer *cmd,
                                   VkPipelineBindPoint bind_point);

void hk_reserve_scratch(struct hk_cmd_buffer *cmd, struct hk_cs *cs,
                        struct hk_shader *s);

/* AGX2 returns its shader-base-relative USC offset.  G17P returns the absolute
 * 48-bit Ioto program VA consumed by HAL200 VDM state. */
uint64_t hk_upload_usc_words(struct hk_cmd_buffer *cmd, struct hk_shader *s,
                             struct hk_linked_shader *linked);

void hk_usc_upload_spilled_rt_descs(struct agx_usc_builder *b,
                                    struct hk_cmd_buffer *cmd);


void hk_dispatch_with_usc_launch(struct hk_device *dev, struct hk_cs *cs,
                                 struct agx_cdm_launch_word_0_packed launch,
                                 uint32_t usc, struct agx_grid grid,
                                 struct agx_workgroup local_size);

void hk_dispatch_with_usc(struct hk_device *dev, struct hk_cs *cs,
                          struct agx_shader_info *info, uint32_t usc,
                          struct agx_grid grid,
                          struct agx_workgroup local_size);

struct agx3_ioto_uniform_range;
bool hk_g17p_graphics_uniform_ranges(struct hk_cmd_buffer *cmd,
                                     struct hk_shader *s,
                                     const struct hk_linked_shader *linked,
                                     struct agx3_ioto_uniform_range *ranges,
                                     size_t *range_count);
void hk_dispatch_g17p_native_linked(struct hk_cmd_buffer *cmd, struct hk_cs *cs,
                                    struct hk_shader *s,
                                    struct hk_linked_shader *linked,
                                    struct agx_grid grid,
                                    struct agx_workgroup local_size);

void hk_dispatch_g17p_native(struct hk_cmd_buffer *cmd, struct hk_cs *cs,
                             struct hk_shader *s, struct agx_grid grid,
                             struct agx_workgroup local_size);

static inline void
hk_dispatch_with_local_size(struct hk_cmd_buffer *cmd, struct hk_cs *cs,
                            struct hk_shader *s, struct agx_grid grid,
                            struct agx_workgroup local_size)
{
   if (agx_is_shader_empty(&s->b))
      return;

   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   if (agx_chip_uses_hal200_ioto(dev->dev.chip)) {
      hk_reserve_scratch(cmd, cs, s);
      hk_dispatch_g17p_native(cmd, cs, s, grid, local_size);
      return;
   }

   uint64_t uploaded = hk_upload_usc_words(cmd, s, s->only_linked);
   if (uploaded > UINT32_MAX) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_FEATURE_NOT_PRESENT);
      return;
   }
   uint32_t usc = uploaded;

   hk_reserve_scratch(cmd, cs, s);
   hk_dispatch_with_usc(dev, cs, &s->b.info, usc, grid, local_size);
}

void hk_dispatch_precomp(struct hk_cmd_buffer *cmd, struct agx_grid grid,
                         enum agx_barrier barrier, enum libagx_program idx,
                         void *data, size_t data_size);

#define MESA_DISPATCH_PRECOMP hk_dispatch_precomp

void hk_queue_write(struct hk_cmd_buffer *cmd, uint64_t address, uint32_t value,
                    bool after_gfx);

/* Dispatch the native G17P raw-address fill meta shader into an explicitly
 * selected compute stream.  The target may be the ordinary or post-graphics
 * stream; the latter preserves query-availability ordering. */
void hk_g17p_meta_fill_addr(struct hk_cmd_buffer *cmd, struct hk_cs **target,
                            uint64_t address, uint64_t size, uint32_t value);

/* Poll a naturally aligned 32-bit device address from a native G17P compute
 * stream until it reaches value. */
void hk_g17p_meta_wait_addr(struct hk_cmd_buffer *cmd, uint64_t address,
                            uint32_t value);

/* Copy a retired graphics timestamp in the dependent post-graphics stream. */
void hk_native_meta_copy_timestamp(struct hk_cmd_buffer *cmd,
                                   uint64_t destination, uint64_t source);
void hk_native_meta_timestamp_dispatch(struct hk_cmd_buffer *cmd);
void hk_native_meta_copy_query(struct hk_cmd_buffer *cmd, uint64_t destination,
                               uint64_t source, uint64_t availability,
                               VkQueryResultFlags flags, bool availability_64);

void agx_fill_velem_keys(const struct vk_vertex_input_state *vi,
                         uint64_t attribs_read, struct agx_velem_key *keys);

struct agx_robustness hk_prolog_robustness(struct hk_device *dev);
