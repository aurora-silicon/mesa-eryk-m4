/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Frontend-neutral calls into the shared renderer. DDI private objects own
 * opaque renderer references; no COM or Mesa-private type crosses this seam.
 */
#pragma once

#include "d12_umd_command_core.h"
#include "../d12_frontend.h"

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

struct d12_umd_renderer_ops {
   HRESULT (*create_command_queue)(
      void *device, enum d12_umd_renderer_command_type type,
      void **out_queue);
   void (*destroy_command_queue)(void *queue);
   HRESULT (*execute_command_lists)(void *queue, uint32_t count,
                                    void *const *lists);
   HRESULT (*update_tile_mappings)(
      void *queue, void *resource, uint32_t region_count,
      const struct d12_frontend_tiled_resource_coordinate *region_starts,
      const struct d12_frontend_tile_region_size *region_sizes, void *heap,
      uint32_t range_count, const uint32_t *range_flags,
      const uint32_t *heap_start_offsets, const uint32_t *range_tile_counts,
      uint32_t flags);
   HRESULT (*copy_tile_mappings)(
      void *queue, void *destination_resource,
      const struct d12_frontend_tiled_resource_coordinate *destination_start,
      void *source_resource,
      const struct d12_frontend_tiled_resource_coordinate *source_start,
      const struct d12_frontend_tile_region_size *region_size,
      uint32_t flags);
   HRESULT (*trim_memory)(void *device, bool periodic,
                          bool restart_periodic, uint64_t bytes_to_trim);
   uint32_t (*paging_queue)(void *device);
   HRESULT (*wait_paging)(void *device, uint64_t value);
   HRESULT (*rotate_resource_identities)(void *device,
                                         void *const *resources,
                                         uint32_t count);
   HRESULT (*create_fence)(
      void *device, uint32_t count,
      const struct d12_frontend_fence_placement *placements,
      void **out_fence);
   /* A non-NULL fence returned by create_fence owns each placement lifetime
    * callback, even when the HRESULT reports failure and the caller
    * immediately destroys that partial object. */
   void (*destroy_fence)(void *fence);
   HRESULT (*signal_fence)(void *queue, void *fence, uint64_t value);
   HRESULT (*wait_fence)(void *queue, void *fence, uint64_t value);
   uint32_t (*command_queue_context_token)(void *queue);
   HRESULT (*create_command_allocator)(
      void *device, enum d12_umd_renderer_command_type type,
      void **out_allocator);
   HRESULT (*reset_command_allocator)(void *allocator);
   void (*destroy_command_allocator)(void *allocator);

   HRESULT (*create_command_list)(
      void *device, enum d12_umd_renderer_command_type type, void *allocator,
      void **out_list);
   HRESULT (*close_command_list)(void *list);
   HRESULT (*reset_command_list)(void *list, void *allocator);
   void (*destroy_command_list)(void *list);
   void (*execute_bundle)(void *list, void *bundle);
   void (*clear_root_arguments)(void *list);
   HRESULT (*create_command_signature)(
      void *device, const struct d12_frontend_command_signature_desc *desc,
      void **out_signature);
   void (*destroy_command_signature)(void *signature);
   void (*execute_indirect)(void *list, void *signature,
                            uint32_t maximum_count, void *argument_buffer,
                            uint64_t argument_offset, void *count_buffer,
                            uint64_t count_offset);
   void (*draw_instanced)(void *list, uint32_t vertex_count_per_instance,
                          uint32_t instance_count, uint32_t start_vertex,
                          uint32_t start_instance);
   void (*draw_indexed_instanced)(
      void *list, uint32_t index_count_per_instance, uint32_t instance_count,
      uint32_t start_index, int32_t base_vertex, uint32_t start_instance);
   void (*dispatch)(void *list, uint32_t group_count_x,
                    uint32_t group_count_y, uint32_t group_count_z);
   void (*set_topology)(void *list, uint32_t topology);
   void (*set_viewports)(void *list, uint32_t count,
                         const float (*viewports)[6]);
   void (*set_scissors)(void *list, uint32_t count,
                        const int32_t (*rects)[4]);
   void (*set_blend_factor)(void *list, const float factor[4]);
   void (*set_stencil_ref)(void *list, uint32_t reference);
   void (*set_front_and_back_stencil_ref)(void *list,
                                          uint32_t front_reference,
                                          uint32_t back_reference);
   void (*set_depth_bias)(void *list, float depth_bias,
                          float depth_bias_clamp,
                          float slope_scaled_depth_bias);
   void (*set_index_buffer_strip_cut)(void *list, uint32_t strip_cut);
   void (*set_vertex_buffers)(
      void *list, uint32_t start_slot, uint32_t count,
      const struct d12_frontend_vertex_buffer_view *views);
   void (*set_index_buffer)(
      void *list, const struct d12_frontend_index_buffer_view *view);
   void (*set_stream_output_targets)(
      void *list, uint32_t start_slot, uint32_t count,
      const struct d12_frontend_stream_output_buffer_view *views);
   void (*set_render_targets)(void *list, uint32_t count,
                              const uint64_t *render_targets,
                              bool contiguous, const uint64_t *depth_stencil);
   void (*resource_barriers)(
      void *list, uint32_t count,
      const struct d12_frontend_resource_barrier *barriers);
   void (*copy_buffer_region)(void *list, void *destination,
                              uint64_t destination_offset, void *source,
                              uint64_t source_offset, uint64_t bytes);
   void (*copy_resource)(void *list, void *destination, void *source);
   HRESULT (*copy_tiles)(
      void *list, void *resource,
      const struct d12_frontend_tiled_resource_coordinate *region_start,
      const struct d12_frontend_tile_region_size *region_size, void *buffer,
      uint64_t buffer_offset, uint32_t flags);
   void (*copy_texture_region)(
      void *list,
      const struct d12_frontend_texture_copy_location *destination,
      uint32_t destination_x, uint32_t destination_y, uint32_t destination_z,
      const struct d12_frontend_texture_copy_location *source,
      const struct d12_frontend_box *source_box);
   void (*resolve_subresource)(void *list, void *destination,
                               uint32_t destination_subresource,
                               void *source, uint32_t source_subresource,
                               uint32_t format);
   HRESULT (*resolve_subresource_region)(
      void *list, void *destination, uint32_t destination_subresource,
      uint32_t destination_x, uint32_t destination_y, void *source,
      uint32_t source_subresource,
      const struct d12_frontend_rect *source_rect, uint32_t format,
      uint32_t resolve_mode);
   HRESULT (*write_buffer_immediate)(
      void *list, uint32_t count,
      const struct d12_frontend_write_buffer_immediate_parameter *parameters,
      const uint32_t *modes);
   HRESULT (*present_blt)(
      void *list, void *destination, void *source,
      const struct d12_frontend_present_blt_desc *desc);
   void (*clear_render_target)(
      void *list, uint64_t descriptor, const float color[4], uint32_t count,
      const struct d12_frontend_rect *rects);
   void (*clear_depth_stencil)(
      void *list, uint64_t descriptor, uint32_t flags, float depth,
      uint8_t stencil, uint32_t count,
      const struct d12_frontend_rect *rects);
   void (*clear_uav_uint)(
      void *list, uint64_t gpu_descriptor, uint64_t cpu_descriptor,
      void *resource, const uint32_t values[4], uint32_t count,
      const struct d12_frontend_rect *rects);
   void (*clear_uav_float)(
      void *list, uint64_t gpu_descriptor, uint64_t cpu_descriptor,
      void *resource, const float values[4], uint32_t count,
      const struct d12_frontend_rect *rects);

   HRESULT (*create_root_signature)(
      void *device, const struct d12_frontend_root_signature_desc *desc,
      void **out_root_signature);
   void (*destroy_root_signature)(void *root_signature);
   void (*set_root_signature)(void *list,
                              enum d12_frontend_root_pipeline pipeline,
                              void *root_signature);
   void (*set_root_descriptor_table)(
      void *list, enum d12_frontend_root_pipeline pipeline,
      uint32_t parameter_index, uint64_t base_descriptor);
   void (*set_root_constants)(
      void *list, enum d12_frontend_root_pipeline pipeline,
      uint32_t parameter_index, uint32_t count, const void *data,
      uint32_t destination_offset);
   void (*set_root_buffer_view)(
      void *list, enum d12_frontend_root_pipeline pipeline,
      enum d12_frontend_root_buffer_type buffer_type,
      uint32_t parameter_index, uint64_t address);

   HRESULT (*create_compute_pipeline_state)(
      void *device, const struct d12_frontend_compute_pipeline_desc *desc,
      void **out_pipeline_state);
   HRESULT (*create_graphics_pipeline_state)(
      void *device, const struct d12_frontend_graphics_pipeline_desc *desc,
      void **out_pipeline_state);
   void (*retain_pipeline_state)(void *pipeline_state);
   void (*destroy_pipeline_state)(void *pipeline_state);
   HRESULT (*get_pipeline_state_comment)(
      void *pipeline_state, char **out_comment, size_t *out_size);
   void (*set_pipeline_state)(void *list, void *pipeline_state);

   HRESULT (*create_heap)(
      void *device, const struct d12_frontend_heap_desc *desc,
      void *runtime_resource, const void *resource_driver_data,
      uint32_t resource_driver_data_size, uint64_t standard_descriptor,
      void **out_heap,
      struct d12_frontend_heap_allocation *out_allocation);
   HRESULT (*open_heap)(
      void *device, const struct d12_frontend_heap_desc *desc,
      void *runtime_resource, uint32_t allocation, uint32_t resource,
      uint32_t allocation_flags, void **out_heap,
      struct d12_frontend_heap_allocation *out_allocation);
   void (*destroy_heap)(void *heap);
   HRESULT (*create_placed_resource)(
      void *device, void *heap, uint64_t offset,
      const struct d12_frontend_resource_desc *desc,
      const struct d12_frontend_clear_value *clear_value,
      void **out_resource);
   HRESULT (*create_reused_resource)(
      void *device, void *source_resource, uint64_t source_offset,
      const struct d12_frontend_resource_desc *desc,
      const struct d12_frontend_clear_value *clear_value,
      void **out_resource);
   HRESULT (*create_reserved_resource)(
      void *device, const struct d12_frontend_resource_desc *desc,
      const struct d12_frontend_clear_value *clear_value,
      void **out_resource);
   HRESULT (*validate_reserved_resource)(
      const struct d12_frontend_resource_desc *desc);
   void (*destroy_resource)(void *resource);
   uint64_t (*resource_gpu_virtual_address)(void *resource);
   HRESULT (*resource_mip_packing)(void *resource, uint32_t *packed_mips,
                                   uint32_t *packed_tiles);
   HRESULT (*get_resource_allocation_info)(
      void *device, const struct d12_frontend_resource_desc *desc,
      uint32_t visible_node_mask,
      struct d12_frontend_resource_allocation_info *info);
   uint64_t (*format_support)(void *device, uint32_t format);
   uint32_t (*multisample_quality_levels)(
      void *device, uint32_t format, uint32_t sample_count, uint32_t flags);
   HRESULT (*resource_subresource_info)(
      void *resource, uint32_t subresource,
      struct d12_frontend_subresource_info *info);

   HRESULT (*create_descriptor_heap)(
      void *device, const struct d12_frontend_descriptor_heap_desc *desc,
      void **out_heap);
   void (*destroy_descriptor_heap)(void *heap);
   uint64_t (*descriptor_heap_cpu_start)(void *heap);
   uint64_t (*descriptor_heap_gpu_start)(void *heap);
   void (*set_descriptor_heaps)(void *list, uint32_t count,
                                void *const *heaps);
   void (*create_shader_resource_view)(
      void *device, void *resource,
      const struct d12_frontend_srv_desc *desc, uint64_t destination);
   void (*create_unordered_access_view)(
      void *device, void *resource, void *counter_resource,
      const struct d12_frontend_uav_desc *desc, uint64_t destination);
   void (*create_render_target_view)(
      void *device, void *resource,
      const struct d12_frontend_rtv_desc *desc, uint64_t destination);
   void (*create_depth_stencil_view)(
      void *device, void *resource,
      const struct d12_frontend_dsv_desc *desc, uint64_t destination);
   void (*create_constant_buffer_view)(void *device, uint64_t address,
                                       uint32_t size,
                                       uint64_t destination);
   void (*create_sampler)(void *device,
                          const struct d12_frontend_sampler_desc *desc,
                          uint64_t destination);
   void (*copy_descriptors_simple)(void *device, uint32_t count,
                                   uint64_t destination, uint64_t source,
                                   uint32_t type);

   HRESULT (*create_query_heap)(void *device, uint32_t type, uint32_t count,
                                uint32_t node_mask, void **out_heap);
   void (*destroy_query_heap)(void *heap);
   void (*begin_query)(void *list, void *heap, uint32_t type,
                       uint32_t index);
   void (*end_query)(void *list, void *heap, uint32_t type,
                     uint32_t index);
   void (*resolve_query_data)(void *list, void *heap, uint32_t type,
                              uint32_t first, uint32_t count,
                              void *destination, uint64_t offset);
   void (*set_predication)(void *list, void *resource, uint64_t offset,
                           uint32_t operation);
};
