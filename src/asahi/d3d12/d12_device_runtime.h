/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_d3dkmt_runtime.h"
#include "d12_device_removal_core.h"
#include "d12_frontend.h"

struct d12_device;
struct agxw_adapter_identity;

enum d12_device_runtime_command_type {
   D12_DEVICE_RUNTIME_COMMAND_DIRECT,
   D12_DEVICE_RUNTIME_COMMAND_BUNDLE,
   D12_DEVICE_RUNTIME_COMMAND_COMPUTE,
   D12_DEVICE_RUNTIME_COMMAND_COPY,
};

HRESULT d12_device_runtime_command_queue_create(
   struct d12_device *device, enum d12_device_runtime_command_type type,
   void **out_queue);
void d12_device_runtime_command_queue_destroy(void *queue);
HRESULT d12_device_runtime_command_queue_execute(
   void *queue, uint32_t count, void *const *lists);
HRESULT d12_device_runtime_command_queue_update_tile_mappings(
   void *queue, void *resource, uint32_t region_count,
   const struct d12_frontend_tiled_resource_coordinate *region_starts,
   const struct d12_frontend_tile_region_size *region_sizes, void *heap,
   uint32_t range_count, const uint32_t *range_flags,
   const uint32_t *heap_start_offsets, const uint32_t *range_tile_counts,
   uint32_t flags);
HRESULT d12_device_runtime_command_queue_copy_tile_mappings(
   void *queue, void *destination_resource,
   const struct d12_frontend_tiled_resource_coordinate *destination_start,
   void *source_resource,
   const struct d12_frontend_tiled_resource_coordinate *source_start,
   const struct d12_frontend_tile_region_size *region_size, uint32_t flags);
HRESULT d12_device_runtime_fence_create(
   struct d12_device *device, uint32_t count,
   const struct d12_frontend_fence_placement *placements,
   void **out_fence);
void d12_device_runtime_fence_destroy(void *fence);
HRESULT d12_device_runtime_command_queue_signal_fence(
   void *queue, void *fence, uint64_t value);
HRESULT d12_device_runtime_command_queue_wait_fence(
   void *queue, void *fence, uint64_t value);
uint32_t d12_device_runtime_command_queue_context_token(void *queue);

bool d12_device_resolve_d3dkmt_adapter_luid(
   const struct agxw_adapter_identity *identity,
   struct agx_kmt_luid *luid);

HRESULT d12_device_create_d3dkmt_runtime(
   const struct agx_d3dkmt_runtime_device *runtime_device,
   struct d12_device **out_device, uint64_t *out_shader_base);
void d12_device_destroy_d3dkmt_runtime(struct d12_device *device);
bool d12_device_runtime_set_removal_callback(
   struct d12_device *device, d12_device_removal_callback callback,
   void *userdata);
HRESULT d12_device_runtime_trim_memory(struct d12_device *device,
                                       bool periodic,
                                       bool restart_periodic,
                                       uint64_t bytes_to_trim);
uint32_t d12_device_runtime_paging_queue(struct d12_device *device);
HRESULT d12_device_runtime_wait_paging(struct d12_device *device,
                                       uint64_t value);
HRESULT d12_device_runtime_rotate_resource_identities(
   struct d12_device *device, void *const *resources, uint32_t count);

HRESULT d12_device_runtime_command_allocator_create(
   struct d12_device *device, enum d12_device_runtime_command_type type,
   void **out_allocator);
HRESULT d12_device_runtime_command_allocator_reset(void *allocator);
void d12_device_runtime_command_allocator_destroy(void *allocator);

HRESULT d12_device_runtime_command_list_create(
   struct d12_device *device, enum d12_device_runtime_command_type type,
   void *allocator, void **out_list);
HRESULT d12_device_runtime_command_list_close(void *list);
HRESULT d12_device_runtime_command_list_reset(void *list, void *allocator);
void d12_device_runtime_command_list_destroy(void *list);
void d12_device_runtime_command_list_execute_bundle(void *list,
                                                    void *bundle);
void d12_device_runtime_command_list_clear_root_arguments(void *list);
HRESULT d12_device_runtime_command_signature_create(
   struct d12_device *device,
   const struct d12_frontend_command_signature_desc *desc,
   void **out_signature);
void d12_device_runtime_command_signature_destroy(void *signature);
void d12_device_runtime_command_list_execute_indirect(
   void *list, void *signature, uint32_t maximum_count,
   void *argument_buffer, uint64_t argument_offset, void *count_buffer,
   uint64_t count_offset);
void d12_device_runtime_command_list_draw_instanced(
   void *list, uint32_t vertex_count_per_instance, uint32_t instance_count,
   uint32_t start_vertex, uint32_t start_instance);
void d12_device_runtime_command_list_draw_indexed_instanced(
   void *list, uint32_t index_count_per_instance, uint32_t instance_count,
   uint32_t start_index, int32_t base_vertex, uint32_t start_instance);
void d12_device_runtime_command_list_dispatch(
   void *list, uint32_t group_count_x, uint32_t group_count_y,
   uint32_t group_count_z);
void d12_device_runtime_command_list_set_topology(void *list,
                                                  uint32_t topology);
void d12_device_runtime_command_list_set_viewports(
   void *list, uint32_t count, const float (*viewports)[6]);
void d12_device_runtime_command_list_set_scissors(
   void *list, uint32_t count, const int32_t (*rects)[4]);
void d12_device_runtime_command_list_set_blend_factor(
   void *list, const float factor[4]);
void d12_device_runtime_command_list_set_stencil_ref(void *list,
                                                     uint32_t reference);
void d12_device_runtime_command_list_set_vertex_buffers(
   void *list, uint32_t start_slot, uint32_t count,
   const struct d12_frontend_vertex_buffer_view *views);
void d12_device_runtime_command_list_set_index_buffer(
   void *list, const struct d12_frontend_index_buffer_view *view);
void d12_device_runtime_command_list_set_stream_output_targets(
   void *list, uint32_t start_slot, uint32_t count,
   const struct d12_frontend_stream_output_buffer_view *views);
void d12_device_runtime_command_list_set_render_targets(
   void *list, uint32_t count, const uint64_t *render_targets,
   bool contiguous, const uint64_t *depth_stencil);
void d12_device_runtime_command_list_resource_barriers(
   void *list, uint32_t count,
   const struct d12_frontend_resource_barrier *barriers);
void d12_device_runtime_command_list_copy_buffer_region(
   void *list, void *destination, uint64_t destination_offset, void *source,
   uint64_t source_offset, uint64_t bytes);
void d12_device_runtime_command_list_copy_resource(
   void *list, void *destination, void *source);
HRESULT d12_device_runtime_command_list_copy_tiles(
   void *list, void *resource,
   const struct d12_frontend_tiled_resource_coordinate *region_start,
   const struct d12_frontend_tile_region_size *region_size, void *buffer,
   uint64_t buffer_offset, uint32_t flags);
void d12_device_runtime_command_list_copy_texture_region(
   void *list,
   const struct d12_frontend_texture_copy_location *destination,
   uint32_t destination_x, uint32_t destination_y, uint32_t destination_z,
   const struct d12_frontend_texture_copy_location *source,
   const struct d12_frontend_box *source_box);
void d12_device_runtime_command_list_resolve_subresource(
   void *list, void *destination, uint32_t destination_subresource,
   void *source, uint32_t source_subresource, uint32_t format);
HRESULT d12_device_runtime_command_list_present_blt(
   void *list, void *destination, void *source,
   const struct d12_frontend_present_blt_desc *desc);
void d12_device_runtime_command_list_clear_render_target(
   void *list, uint64_t descriptor, const float color[4], uint32_t count,
   const struct d12_frontend_rect *rects);
void d12_device_runtime_command_list_clear_depth_stencil(
   void *list, uint64_t descriptor, uint32_t flags, float depth,
   uint8_t stencil, uint32_t count,
   const struct d12_frontend_rect *rects);
void d12_device_runtime_command_list_clear_uav_uint(
   void *list, uint64_t gpu_descriptor, uint64_t cpu_descriptor,
   void *resource, const uint32_t values[4], uint32_t count,
   const struct d12_frontend_rect *rects);
void d12_device_runtime_command_list_clear_uav_float(
   void *list, uint64_t gpu_descriptor, uint64_t cpu_descriptor,
   void *resource, const float values[4], uint32_t count,
   const struct d12_frontend_rect *rects);

HRESULT d12_device_runtime_root_signature_create(
   struct d12_device *device,
   const struct d12_frontend_root_signature_desc *desc,
   void **out_root_signature);
void d12_device_runtime_root_signature_destroy(void *root_signature);
void d12_device_runtime_command_list_set_root_signature(
   void *list, enum d12_frontend_root_pipeline pipeline,
   void *root_signature);
void d12_device_runtime_command_list_set_root_descriptor_table(
   void *list, enum d12_frontend_root_pipeline pipeline,
   uint32_t parameter_index, uint64_t base_descriptor);
void d12_device_runtime_command_list_set_root_constants(
   void *list, enum d12_frontend_root_pipeline pipeline,
   uint32_t parameter_index, uint32_t count, const void *data,
   uint32_t destination_offset);
void d12_device_runtime_command_list_set_root_buffer_view(
   void *list, enum d12_frontend_root_pipeline pipeline,
   enum d12_frontend_root_buffer_type buffer_type,
   uint32_t parameter_index, uint64_t address);

HRESULT d12_device_runtime_pipeline_state_create_compute(
   struct d12_device *device,
   const struct d12_frontend_compute_pipeline_desc *desc,
   void **out_pipeline_state);
HRESULT d12_device_runtime_pipeline_state_create_graphics(
   struct d12_device *device,
   const struct d12_frontend_graphics_pipeline_desc *desc,
   void **out_pipeline_state);
void d12_device_runtime_pipeline_state_destroy(void *pipeline_state);
void d12_device_runtime_pipeline_state_retain(void *pipeline_state);
HRESULT d12_device_runtime_pipeline_state_comment(
   void *pipeline_state, char **out_comment, size_t *out_size);
void d12_device_runtime_command_list_set_pipeline_state(
   void *list, void *pipeline_state);

HRESULT d12_device_runtime_heap_create(
   struct d12_device *device, const struct d12_frontend_heap_desc *desc,
   void *runtime_resource, const void *resource_driver_data,
   uint32_t resource_driver_data_size, uint64_t standard_descriptor,
   void **out_heap,
   struct d12_frontend_heap_allocation *out_allocation);
HRESULT d12_device_runtime_heap_open(
   struct d12_device *device, const struct d12_frontend_heap_desc *desc,
   void *runtime_resource, uint32_t allocation, uint32_t resource,
   uint32_t allocation_flags, void **out_heap,
   struct d12_frontend_heap_allocation *out_allocation);
void d12_device_runtime_heap_destroy(void *heap);
HRESULT d12_device_runtime_resource_create_placed(
   struct d12_device *device, void *heap, uint64_t offset,
   const struct d12_frontend_resource_desc *desc,
   const struct d12_frontend_clear_value *clear_value,
   void **out_resource);
HRESULT d12_device_runtime_resource_create_reused(
   struct d12_device *device, void *source_resource, uint64_t source_offset,
   const struct d12_frontend_resource_desc *desc,
   const struct d12_frontend_clear_value *clear_value,
   void **out_resource);
HRESULT d12_device_runtime_resource_create_reserved(
   struct d12_device *device,
   const struct d12_frontend_resource_desc *desc,
   const struct d12_frontend_clear_value *clear_value,
   void **out_resource);
HRESULT d12_device_runtime_resource_validate_reserved(
   const struct d12_frontend_resource_desc *desc);
void d12_device_runtime_resource_destroy(void *resource);
uint64_t d12_device_runtime_resource_gpu_virtual_address(void *resource);
HRESULT d12_device_runtime_resource_mip_packing(
   void *resource, uint32_t *packed_mips, uint32_t *packed_tiles);
HRESULT d12_device_runtime_resource_allocation_info(
   struct d12_device *device,
   const struct d12_frontend_resource_desc *desc,
   uint32_t visible_node_mask,
   struct d12_frontend_resource_allocation_info *info);
uint64_t d12_device_runtime_format_support(struct d12_device *device,
                                           uint32_t format);
uint32_t d12_device_runtime_multisample_quality_levels(
   struct d12_device *device, uint32_t format, uint32_t sample_count,
   uint32_t flags);
HRESULT d12_device_runtime_resource_subresource_info(
   void *resource, uint32_t subresource,
   struct d12_frontend_subresource_info *info);

HRESULT d12_device_runtime_descriptor_heap_create(
   struct d12_device *device,
   const struct d12_frontend_descriptor_heap_desc *desc,
   void **out_heap);
void d12_device_runtime_descriptor_heap_destroy(void *heap);
uint64_t d12_device_runtime_descriptor_heap_cpu_start(void *heap);
uint64_t d12_device_runtime_descriptor_heap_gpu_start(void *heap);
void d12_device_runtime_command_list_set_descriptor_heaps(
   void *list, uint32_t count, void *const *heaps);
void d12_device_runtime_create_shader_resource_view(
   struct d12_device *device, void *resource,
   const struct d12_frontend_srv_desc *desc, uint64_t destination);
void d12_device_runtime_create_unordered_access_view(
   struct d12_device *device, void *resource, void *counter_resource,
   const struct d12_frontend_uav_desc *desc, uint64_t destination);
void d12_device_runtime_create_render_target_view(
   struct d12_device *device, void *resource,
   const struct d12_frontend_rtv_desc *desc, uint64_t destination);
void d12_device_runtime_create_depth_stencil_view(
   struct d12_device *device, void *resource,
   const struct d12_frontend_dsv_desc *desc, uint64_t destination);
void d12_device_runtime_create_constant_buffer_view(
   struct d12_device *device, uint64_t address, uint32_t size,
   uint64_t destination);
void d12_device_runtime_create_sampler(
   struct d12_device *device,
   const struct d12_frontend_sampler_desc *desc, uint64_t destination);
void d12_device_runtime_copy_descriptors_simple(
   struct d12_device *device, uint32_t count, uint64_t destination,
   uint64_t source, uint32_t type);

HRESULT d12_device_runtime_query_heap_create(
   struct d12_device *device, uint32_t type, uint32_t count,
   uint32_t node_mask, void **out_heap);
void d12_device_runtime_query_heap_destroy(void *heap);
void d12_device_runtime_command_list_begin_query(
   void *list, void *heap, uint32_t type, uint32_t index);
void d12_device_runtime_command_list_end_query(
   void *list, void *heap, uint32_t type, uint32_t index);
void d12_device_runtime_command_list_resolve_query_data(
   void *list, void *heap, uint32_t type, uint32_t first, uint32_t count,
   void *destination, uint64_t offset);
void d12_device_runtime_command_list_set_predication(
   void *list, void *resource, uint64_t offset, uint32_t operation);
