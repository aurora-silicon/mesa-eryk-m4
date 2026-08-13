/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Runtime-owned construction of the native Mesa AGX D3D12 backend.  This is
 * The opaque constructor keeps renderer-private headers and objects in the
 * shared core archive while this strict wrapper replaces public-COM platform
 * discovery with the WDDM callback transport.
 */

#include "d12_umd_backend.h"

#include "../d12_device_runtime.h"
#include "d12_umd_internal.h"

static bool
d12_umd_backend_command_type(
   enum d12_umd_renderer_command_type renderer_type,
   enum d12_device_runtime_command_type *runtime_type)
{
   if (!runtime_type)
      return false;

   switch (renderer_type) {
   case D12_UMD_RENDERER_COMMAND_DIRECT:
      *runtime_type = D12_DEVICE_RUNTIME_COMMAND_DIRECT;
      return true;
   case D12_UMD_RENDERER_COMMAND_BUNDLE:
      *runtime_type = D12_DEVICE_RUNTIME_COMMAND_BUNDLE;
      return true;
   case D12_UMD_RENDERER_COMMAND_COMPUTE:
      *runtime_type = D12_DEVICE_RUNTIME_COMMAND_COMPUTE;
      return true;
   case D12_UMD_RENDERER_COMMAND_COPY:
      *runtime_type = D12_DEVICE_RUNTIME_COMMAND_COPY;
      return true;
   default:
      return false;
   }
}

static HRESULT
d12_umd_backend_create_command_queue(
   void *backend, enum d12_umd_renderer_command_type type, void **out_queue)
{
   enum d12_device_runtime_command_type runtime_type;

   if (!d12_umd_backend_command_type(type, &runtime_type))
      return E_INVALIDARG;
   return d12_device_runtime_command_queue_create(
      (struct d12_device *)backend, runtime_type, out_queue);
}

static void
d12_umd_backend_destroy_command_queue(void *queue)
{
   d12_device_runtime_command_queue_destroy(queue);
}

static HRESULT
d12_umd_backend_execute_command_lists(void *queue, uint32_t count,
                                      void *const *lists)
{
   return d12_device_runtime_command_queue_execute(queue, count, lists);
}

static HRESULT
d12_umd_backend_update_tile_mappings(
   void *queue, void *resource, uint32_t region_count,
   const struct d12_frontend_tiled_resource_coordinate *region_starts,
   const struct d12_frontend_tile_region_size *region_sizes, void *heap,
   uint32_t range_count, const uint32_t *range_flags,
   const uint32_t *heap_start_offsets, const uint32_t *range_tile_counts,
   uint32_t flags)
{
   return d12_device_runtime_command_queue_update_tile_mappings(
      queue, resource, region_count, region_starts, region_sizes, heap,
      range_count, range_flags, heap_start_offsets, range_tile_counts, flags);
}

static HRESULT
d12_umd_backend_copy_tile_mappings(
   void *queue, void *destination_resource,
   const struct d12_frontend_tiled_resource_coordinate *destination_start,
   void *source_resource,
   const struct d12_frontend_tiled_resource_coordinate *source_start,
   const struct d12_frontend_tile_region_size *region_size, uint32_t flags)
{
   return d12_device_runtime_command_queue_copy_tile_mappings(
      queue, destination_resource, destination_start, source_resource,
      source_start, region_size, flags);
}

static HRESULT
d12_umd_backend_trim_memory(void *device, bool periodic,
                            bool restart_periodic, uint64_t bytes_to_trim)
{
   return d12_device_runtime_trim_memory(
      (struct d12_device *)device, periodic, restart_periodic,
      bytes_to_trim);
}

static uint32_t
d12_umd_backend_paging_queue(void *device)
{
   return d12_device_runtime_paging_queue((struct d12_device *)device);
}

static HRESULT
d12_umd_backend_wait_paging(void *device, uint64_t value)
{
   return d12_device_runtime_wait_paging((struct d12_device *)device, value);
}

static HRESULT
d12_umd_backend_rotate_resource_identities(void *device,
                                           void *const *resources,
                                           uint32_t count)
{
   return d12_device_runtime_rotate_resource_identities(
      (struct d12_device *)device, resources, count);
}

static HRESULT
d12_umd_backend_create_fence(
   void *device, uint32_t count,
   const struct d12_frontend_fence_placement *placements, void **out_fence)
{
   return d12_device_runtime_fence_create(device, count, placements,
                                          out_fence);
}

static void
d12_umd_backend_destroy_fence(void *fence)
{
   d12_device_runtime_fence_destroy(fence);
}

static HRESULT
d12_umd_backend_signal_fence(void *queue, void *fence, uint64_t value)
{
   return d12_device_runtime_command_queue_signal_fence(queue, fence, value);
}

static HRESULT
d12_umd_backend_wait_fence(void *queue, void *fence, uint64_t value)
{
   return d12_device_runtime_command_queue_wait_fence(queue, fence, value);
}

static uint32_t
d12_umd_backend_command_queue_context_token(void *queue)
{
   return d12_device_runtime_command_queue_context_token(queue);
}

static HRESULT
d12_umd_backend_create_command_allocator(
   void *backend, enum d12_umd_renderer_command_type type,
   void **out_allocator)
{
   enum d12_device_runtime_command_type runtime_type;

   if (!d12_umd_backend_command_type(type, &runtime_type))
      return E_INVALIDARG;
   return d12_device_runtime_command_allocator_create(
      (struct d12_device *)backend, runtime_type, out_allocator);
}

static HRESULT
d12_umd_backend_reset_command_allocator(void *allocator)
{
   return d12_device_runtime_command_allocator_reset(allocator);
}

static void
d12_umd_backend_destroy_command_allocator(void *allocator)
{
   d12_device_runtime_command_allocator_destroy(allocator);
}

static HRESULT
d12_umd_backend_create_command_list(
   void *backend, enum d12_umd_renderer_command_type type, void *allocator,
   void **out_list)
{
   enum d12_device_runtime_command_type runtime_type;

   if (!d12_umd_backend_command_type(type, &runtime_type))
      return E_INVALIDARG;
   return d12_device_runtime_command_list_create(
      (struct d12_device *)backend, runtime_type, allocator, out_list);
}

static HRESULT
d12_umd_backend_close_command_list(void *list)
{
   return d12_device_runtime_command_list_close(list);
}

static HRESULT
d12_umd_backend_reset_command_list(void *list, void *allocator)
{
   return d12_device_runtime_command_list_reset(list, allocator);
}

static void
d12_umd_backend_destroy_command_list(void *list)
{
   d12_device_runtime_command_list_destroy(list);
}

static void
d12_umd_backend_execute_bundle(void *list, void *bundle)
{
   d12_device_runtime_command_list_execute_bundle(list, bundle);
}

static void
d12_umd_backend_clear_root_arguments(void *list)
{
   d12_device_runtime_command_list_clear_root_arguments(list);
}

static HRESULT
d12_umd_backend_create_command_signature(
   void *backend, const struct d12_frontend_command_signature_desc *desc,
   void **out_signature)
{
   return d12_device_runtime_command_signature_create(
      (struct d12_device *)backend, desc, out_signature);
}

static void
d12_umd_backend_destroy_command_signature(void *signature)
{
   d12_device_runtime_command_signature_destroy(signature);
}

static void
d12_umd_backend_execute_indirect(
   void *list, void *signature, uint32_t maximum_count,
   void *argument_buffer, uint64_t argument_offset, void *count_buffer,
   uint64_t count_offset)
{
   d12_device_runtime_command_list_execute_indirect(
      list, signature, maximum_count, argument_buffer, argument_offset,
      count_buffer, count_offset);
}

static void
d12_umd_backend_draw_instanced(
   void *list, uint32_t vertex_count_per_instance, uint32_t instance_count,
   uint32_t start_vertex, uint32_t start_instance)
{
   d12_device_runtime_command_list_draw_instanced(
      list, vertex_count_per_instance, instance_count, start_vertex,
      start_instance);
}

static void
d12_umd_backend_draw_indexed_instanced(
   void *list, uint32_t index_count_per_instance, uint32_t instance_count,
   uint32_t start_index, int32_t base_vertex, uint32_t start_instance)
{
   d12_device_runtime_command_list_draw_indexed_instanced(
      list, index_count_per_instance, instance_count, start_index, base_vertex,
      start_instance);
}

static void
d12_umd_backend_dispatch(void *list, uint32_t group_count_x,
                         uint32_t group_count_y, uint32_t group_count_z)
{
   d12_device_runtime_command_list_dispatch(
      list, group_count_x, group_count_y, group_count_z);
}

static void
d12_umd_backend_set_topology(void *list, uint32_t topology)
{
   d12_device_runtime_command_list_set_topology(list, topology);
}

static void
d12_umd_backend_set_viewports(void *list, uint32_t count,
                              const float (*viewports)[6])
{
   d12_device_runtime_command_list_set_viewports(list, count, viewports);
}

static void
d12_umd_backend_set_scissors(void *list, uint32_t count,
                             const int32_t (*rects)[4])
{
   d12_device_runtime_command_list_set_scissors(list, count, rects);
}

static void
d12_umd_backend_set_blend_factor(void *list, const float factor[4])
{
   d12_device_runtime_command_list_set_blend_factor(list, factor);
}

static void
d12_umd_backend_set_stencil_ref(void *list, uint32_t reference)
{
   d12_device_runtime_command_list_set_stencil_ref(list, reference);
}

static void
d12_umd_backend_set_vertex_buffers(
   void *list, uint32_t start_slot, uint32_t count,
   const struct d12_frontend_vertex_buffer_view *views)
{
   d12_device_runtime_command_list_set_vertex_buffers(
      list, start_slot, count, views);
}

static void
d12_umd_backend_set_index_buffer(
   void *list, const struct d12_frontend_index_buffer_view *view)
{
   d12_device_runtime_command_list_set_index_buffer(list, view);
}

static void
d12_umd_backend_set_stream_output_targets(
   void *list, uint32_t start_slot, uint32_t count,
   const struct d12_frontend_stream_output_buffer_view *views)
{
   d12_device_runtime_command_list_set_stream_output_targets(
      list, start_slot, count, views);
}

static void
d12_umd_backend_set_render_targets(void *list, uint32_t count,
                                   const uint64_t *render_targets,
                                   bool contiguous,
                                   const uint64_t *depth_stencil)
{
   d12_device_runtime_command_list_set_render_targets(
      list, count, render_targets, contiguous, depth_stencil);
}

static void
d12_umd_backend_resource_barriers(
   void *list, uint32_t count,
   const struct d12_frontend_resource_barrier *barriers)
{
   d12_device_runtime_command_list_resource_barriers(list, count, barriers);
}

static void
d12_umd_backend_copy_buffer_region(void *list, void *destination,
                                   uint64_t destination_offset, void *source,
                                   uint64_t source_offset, uint64_t bytes)
{
   d12_device_runtime_command_list_copy_buffer_region(
      list, destination, destination_offset, source, source_offset, bytes);
}

static void
d12_umd_backend_copy_resource(void *list, void *destination, void *source)
{
   d12_device_runtime_command_list_copy_resource(list, destination, source);
}

static HRESULT
d12_umd_backend_copy_tiles(
   void *list, void *resource,
   const struct d12_frontend_tiled_resource_coordinate *region_start,
   const struct d12_frontend_tile_region_size *region_size, void *buffer,
   uint64_t buffer_offset, uint32_t flags)
{
   return d12_device_runtime_command_list_copy_tiles(
      list, resource, region_start, region_size, buffer, buffer_offset,
      flags);
}

static void
d12_umd_backend_copy_texture_region(
   void *list,
   const struct d12_frontend_texture_copy_location *destination,
   uint32_t destination_x, uint32_t destination_y, uint32_t destination_z,
   const struct d12_frontend_texture_copy_location *source,
   const struct d12_frontend_box *source_box)
{
   d12_device_runtime_command_list_copy_texture_region(
      list, destination, destination_x, destination_y, destination_z, source,
      source_box);
}

static void
d12_umd_backend_resolve_subresource(
   void *list, void *destination, uint32_t destination_subresource,
   void *source, uint32_t source_subresource, uint32_t format)
{
   d12_device_runtime_command_list_resolve_subresource(
      list, destination, destination_subresource, source, source_subresource,
      format);
}

static HRESULT
d12_umd_backend_present_blt(
   void *list, void *destination, void *source,
   const struct d12_frontend_present_blt_desc *desc)
{
   return d12_device_runtime_command_list_present_blt(
      list, destination, source, desc);
}

static void
d12_umd_backend_clear_render_target(
   void *list, uint64_t descriptor, const float color[4], uint32_t count,
   const struct d12_frontend_rect *rects)
{
   d12_device_runtime_command_list_clear_render_target(
      list, descriptor, color, count, rects);
}

static void
d12_umd_backend_clear_depth_stencil(
   void *list, uint64_t descriptor, uint32_t flags, float depth,
   uint8_t stencil, uint32_t count,
   const struct d12_frontend_rect *rects)
{
   d12_device_runtime_command_list_clear_depth_stencil(
      list, descriptor, flags, depth, stencil, count, rects);
}

static void
d12_umd_backend_clear_uav_uint(
   void *list, uint64_t gpu_descriptor, uint64_t cpu_descriptor,
   void *resource, const uint32_t values[4], uint32_t count,
   const struct d12_frontend_rect *rects)
{
   d12_device_runtime_command_list_clear_uav_uint(
      list, gpu_descriptor, cpu_descriptor, resource, values, count, rects);
}

static void
d12_umd_backend_clear_uav_float(
   void *list, uint64_t gpu_descriptor, uint64_t cpu_descriptor,
   void *resource, const float values[4], uint32_t count,
   const struct d12_frontend_rect *rects)
{
   d12_device_runtime_command_list_clear_uav_float(
      list, gpu_descriptor, cpu_descriptor, resource, values, count, rects);
}

static HRESULT
d12_umd_backend_create_root_signature(
   void *backend, const struct d12_frontend_root_signature_desc *desc,
   void **out_root_signature)
{
   return d12_device_runtime_root_signature_create(
      (struct d12_device *)backend, desc, out_root_signature);
}

static void
d12_umd_backend_destroy_root_signature(void *root_signature)
{
   d12_device_runtime_root_signature_destroy(root_signature);
}

static void
d12_umd_backend_set_root_signature(
   void *list, enum d12_frontend_root_pipeline pipeline,
   void *root_signature)
{
   d12_device_runtime_command_list_set_root_signature(
      list, pipeline, root_signature);
}

static void
d12_umd_backend_set_root_descriptor_table(
   void *list, enum d12_frontend_root_pipeline pipeline,
   uint32_t parameter_index, uint64_t base_descriptor)
{
   d12_device_runtime_command_list_set_root_descriptor_table(
      list, pipeline, parameter_index, base_descriptor);
}

static void
d12_umd_backend_set_root_constants(
   void *list, enum d12_frontend_root_pipeline pipeline,
   uint32_t parameter_index, uint32_t count, const void *data,
   uint32_t destination_offset)
{
   d12_device_runtime_command_list_set_root_constants(
      list, pipeline, parameter_index, count, data, destination_offset);
}

static void
d12_umd_backend_set_root_buffer_view(
   void *list, enum d12_frontend_root_pipeline pipeline,
   enum d12_frontend_root_buffer_type buffer_type,
   uint32_t parameter_index, uint64_t address)
{
   d12_device_runtime_command_list_set_root_buffer_view(
      list, pipeline, buffer_type, parameter_index, address);
}

static HRESULT
d12_umd_backend_create_compute_pipeline_state(
   void *backend, const struct d12_frontend_compute_pipeline_desc *desc,
   void **out_pipeline_state)
{
   return d12_device_runtime_pipeline_state_create_compute(
      (struct d12_device *)backend, desc, out_pipeline_state);
}

static HRESULT
d12_umd_backend_create_graphics_pipeline_state(
   void *backend, const struct d12_frontend_graphics_pipeline_desc *desc,
   void **out_pipeline_state)
{
   return d12_device_runtime_pipeline_state_create_graphics(
      (struct d12_device *)backend, desc, out_pipeline_state);
}

static void
d12_umd_backend_retain_pipeline_state(void *pipeline_state)
{
   d12_device_runtime_pipeline_state_retain(pipeline_state);
}

static void
d12_umd_backend_destroy_pipeline_state(void *pipeline_state)
{
   d12_device_runtime_pipeline_state_destroy(pipeline_state);
}

static HRESULT
d12_umd_backend_get_pipeline_state_comment(void *pipeline_state,
                                           char **out_comment,
                                           size_t *out_size)
{
   return d12_device_runtime_pipeline_state_comment(
      pipeline_state, out_comment, out_size);
}

static void
d12_umd_backend_set_pipeline_state(void *list, void *pipeline_state)
{
   d12_device_runtime_command_list_set_pipeline_state(list, pipeline_state);
}

static HRESULT
d12_umd_backend_create_heap(
   void *backend, const struct d12_frontend_heap_desc *desc,
   void *runtime_resource, const void *resource_driver_data,
   uint32_t resource_driver_data_size, void **out_heap,
   struct d12_frontend_heap_allocation *out_allocation)
{
   return d12_device_runtime_heap_create(
      (struct d12_device *)backend, desc, runtime_resource,
      resource_driver_data, resource_driver_data_size, out_heap,
      out_allocation);
}

static HRESULT
d12_umd_backend_open_heap(
   void *backend, const struct d12_frontend_heap_desc *desc,
   void *runtime_resource, uint32_t allocation, uint32_t resource,
   uint32_t allocation_flags, void **out_heap,
   struct d12_frontend_heap_allocation *out_allocation)
{
   return d12_device_runtime_heap_open(
      (struct d12_device *)backend, desc, runtime_resource, allocation,
      resource, allocation_flags, out_heap, out_allocation);
}

static void
d12_umd_backend_destroy_heap(void *heap)
{
   d12_device_runtime_heap_destroy(heap);
}

static HRESULT
d12_umd_backend_create_placed_resource(
   void *backend, void *heap, uint64_t offset,
   const struct d12_frontend_resource_desc *desc,
   const struct d12_frontend_clear_value *clear_value, void **out_resource)
{
   return d12_device_runtime_resource_create_placed(
      (struct d12_device *)backend, heap, offset, desc, clear_value,
      out_resource);
}

static HRESULT
d12_umd_backend_create_reserved_resource(
   void *backend, const struct d12_frontend_resource_desc *desc,
   const struct d12_frontend_clear_value *clear_value, void **out_resource)
{
   return d12_device_runtime_resource_create_reserved(
      (struct d12_device *)backend, desc, clear_value, out_resource);
}

static void
d12_umd_backend_destroy_resource(void *resource)
{
   d12_device_runtime_resource_destroy(resource);
}

static uint64_t
d12_umd_backend_resource_gpu_virtual_address(void *resource)
{
   return d12_device_runtime_resource_gpu_virtual_address(resource);
}

static HRESULT
d12_umd_backend_resource_mip_packing(void *resource, uint32_t *packed_mips,
                                     uint32_t *packed_tiles)
{
   return d12_device_runtime_resource_mip_packing(resource, packed_mips,
                                                  packed_tiles);
}

static HRESULT
d12_umd_backend_get_resource_allocation_info(
   void *backend, const struct d12_frontend_resource_desc *desc,
   uint32_t visible_node_mask,
   struct d12_frontend_resource_allocation_info *info)
{
   return d12_device_runtime_resource_allocation_info(
      (struct d12_device *)backend, desc, visible_node_mask, info);
}

static uint64_t
d12_umd_backend_format_support(void *backend, uint32_t format)
{
   return d12_device_runtime_format_support((struct d12_device *)backend,
                                            format);
}

static uint32_t
d12_umd_backend_multisample_quality_levels(
   void *backend, uint32_t format, uint32_t sample_count, uint32_t flags)
{
   return d12_device_runtime_multisample_quality_levels(
      (struct d12_device *)backend, format, sample_count, flags);
}

static HRESULT
d12_umd_backend_resource_subresource_info(
   void *resource, uint32_t subresource,
   struct d12_frontend_subresource_info *info)
{
   return d12_device_runtime_resource_subresource_info(resource, subresource,
                                                       info);
}

static HRESULT
d12_umd_backend_create_descriptor_heap(
   void *backend, const struct d12_frontend_descriptor_heap_desc *desc,
   void **out_heap)
{
   return d12_device_runtime_descriptor_heap_create(
      (struct d12_device *)backend, desc, out_heap);
}

static void
d12_umd_backend_destroy_descriptor_heap(void *heap)
{
   d12_device_runtime_descriptor_heap_destroy(heap);
}

static uint64_t
d12_umd_backend_descriptor_heap_cpu_start(void *heap)
{
   return d12_device_runtime_descriptor_heap_cpu_start(heap);
}

static uint64_t
d12_umd_backend_descriptor_heap_gpu_start(void *heap)
{
   return d12_device_runtime_descriptor_heap_gpu_start(heap);
}

static void
d12_umd_backend_set_descriptor_heaps(void *list, uint32_t count,
                                     void *const *heaps)
{
   d12_device_runtime_command_list_set_descriptor_heaps(list, count, heaps);
}

static void
d12_umd_backend_create_shader_resource_view(
   void *backend, void *resource, const struct d12_frontend_srv_desc *desc,
   uint64_t destination)
{
   d12_device_runtime_create_shader_resource_view(
      (struct d12_device *)backend, resource, desc, destination);
}

static void
d12_umd_backend_create_unordered_access_view(
   void *backend, void *resource, void *counter_resource,
   const struct d12_frontend_uav_desc *desc, uint64_t destination)
{
   d12_device_runtime_create_unordered_access_view(
      (struct d12_device *)backend, resource, counter_resource, desc,
      destination);
}

static void
d12_umd_backend_create_render_target_view(
   void *backend, void *resource, const struct d12_frontend_rtv_desc *desc,
   uint64_t destination)
{
   d12_device_runtime_create_render_target_view(
      (struct d12_device *)backend, resource, desc, destination);
}

static void
d12_umd_backend_create_depth_stencil_view(
   void *backend, void *resource, const struct d12_frontend_dsv_desc *desc,
   uint64_t destination)
{
   d12_device_runtime_create_depth_stencil_view(
      (struct d12_device *)backend, resource, desc, destination);
}

static void
d12_umd_backend_create_constant_buffer_view(
   void *backend, uint64_t address, uint32_t size, uint64_t destination)
{
   d12_device_runtime_create_constant_buffer_view(
      (struct d12_device *)backend, address, size, destination);
}

static void
d12_umd_backend_create_sampler(
   void *backend, const struct d12_frontend_sampler_desc *desc,
   uint64_t destination)
{
   d12_device_runtime_create_sampler(
      (struct d12_device *)backend, desc, destination);
}

static void
d12_umd_backend_copy_descriptors_simple(
   void *backend, uint32_t count, uint64_t destination, uint64_t source,
   uint32_t type)
{
   d12_device_runtime_copy_descriptors_simple(
      (struct d12_device *)backend, count, destination, source, type);
}

static HRESULT
d12_umd_backend_create_query_heap(void *backend, uint32_t type,
                                  uint32_t count, uint32_t node_mask,
                                  void **out_heap)
{
   return d12_device_runtime_query_heap_create(
      (struct d12_device *)backend, type, count, node_mask, out_heap);
}

static void
d12_umd_backend_destroy_query_heap(void *heap)
{
   d12_device_runtime_query_heap_destroy(heap);
}

static void
d12_umd_backend_begin_query(void *list, void *heap, uint32_t type,
                            uint32_t index)
{
   d12_device_runtime_command_list_begin_query(list, heap, type, index);
}

static void
d12_umd_backend_end_query(void *list, void *heap, uint32_t type,
                          uint32_t index)
{
   d12_device_runtime_command_list_end_query(list, heap, type, index);
}

static void
d12_umd_backend_resolve_query_data(
   void *list, void *heap, uint32_t type, uint32_t first, uint32_t count,
   void *destination, uint64_t offset)
{
   d12_device_runtime_command_list_resolve_query_data(
      list, heap, type, first, count, destination, offset);
}

static void
d12_umd_backend_set_predication(void *list, void *resource, uint64_t offset,
                                uint32_t operation)
{
   d12_device_runtime_command_list_set_predication(
      list, resource, offset, operation);
}

static const struct d12_umd_renderer_ops d12_umd_backend_renderer_ops = {
   .create_command_queue = d12_umd_backend_create_command_queue,
   .destroy_command_queue = d12_umd_backend_destroy_command_queue,
   .execute_command_lists = d12_umd_backend_execute_command_lists,
   .update_tile_mappings = d12_umd_backend_update_tile_mappings,
   .copy_tile_mappings = d12_umd_backend_copy_tile_mappings,
   .trim_memory = d12_umd_backend_trim_memory,
   .paging_queue = d12_umd_backend_paging_queue,
   .wait_paging = d12_umd_backend_wait_paging,
   .rotate_resource_identities =
      d12_umd_backend_rotate_resource_identities,
   .create_fence = d12_umd_backend_create_fence,
   .destroy_fence = d12_umd_backend_destroy_fence,
   .signal_fence = d12_umd_backend_signal_fence,
   .wait_fence = d12_umd_backend_wait_fence,
   .command_queue_context_token =
      d12_umd_backend_command_queue_context_token,
   .create_command_allocator = d12_umd_backend_create_command_allocator,
   .reset_command_allocator = d12_umd_backend_reset_command_allocator,
   .destroy_command_allocator = d12_umd_backend_destroy_command_allocator,
   .create_command_list = d12_umd_backend_create_command_list,
   .close_command_list = d12_umd_backend_close_command_list,
   .reset_command_list = d12_umd_backend_reset_command_list,
   .destroy_command_list = d12_umd_backend_destroy_command_list,
   .execute_bundle = d12_umd_backend_execute_bundle,
   .clear_root_arguments = d12_umd_backend_clear_root_arguments,
   .create_command_signature = d12_umd_backend_create_command_signature,
   .destroy_command_signature = d12_umd_backend_destroy_command_signature,
   .execute_indirect = d12_umd_backend_execute_indirect,
   .draw_instanced = d12_umd_backend_draw_instanced,
   .draw_indexed_instanced = d12_umd_backend_draw_indexed_instanced,
   .dispatch = d12_umd_backend_dispatch,
   .set_topology = d12_umd_backend_set_topology,
   .set_viewports = d12_umd_backend_set_viewports,
   .set_scissors = d12_umd_backend_set_scissors,
   .set_blend_factor = d12_umd_backend_set_blend_factor,
   .set_stencil_ref = d12_umd_backend_set_stencil_ref,
   .set_vertex_buffers = d12_umd_backend_set_vertex_buffers,
   .set_index_buffer = d12_umd_backend_set_index_buffer,
   .set_stream_output_targets =
      d12_umd_backend_set_stream_output_targets,
   .set_render_targets = d12_umd_backend_set_render_targets,
   .resource_barriers = d12_umd_backend_resource_barriers,
   .copy_buffer_region = d12_umd_backend_copy_buffer_region,
   .copy_resource = d12_umd_backend_copy_resource,
   .copy_tiles = d12_umd_backend_copy_tiles,
   .copy_texture_region = d12_umd_backend_copy_texture_region,
   .resolve_subresource = d12_umd_backend_resolve_subresource,
   .present_blt = d12_umd_backend_present_blt,
   .clear_render_target = d12_umd_backend_clear_render_target,
   .clear_depth_stencil = d12_umd_backend_clear_depth_stencil,
   .clear_uav_uint = d12_umd_backend_clear_uav_uint,
   .clear_uav_float = d12_umd_backend_clear_uav_float,
   .create_root_signature = d12_umd_backend_create_root_signature,
   .destroy_root_signature = d12_umd_backend_destroy_root_signature,
   .set_root_signature = d12_umd_backend_set_root_signature,
   .set_root_descriptor_table = d12_umd_backend_set_root_descriptor_table,
   .set_root_constants = d12_umd_backend_set_root_constants,
   .set_root_buffer_view = d12_umd_backend_set_root_buffer_view,
   .create_compute_pipeline_state =
      d12_umd_backend_create_compute_pipeline_state,
   .create_graphics_pipeline_state =
      d12_umd_backend_create_graphics_pipeline_state,
   .retain_pipeline_state = d12_umd_backend_retain_pipeline_state,
   .destroy_pipeline_state = d12_umd_backend_destroy_pipeline_state,
   .get_pipeline_state_comment =
      d12_umd_backend_get_pipeline_state_comment,
   .set_pipeline_state = d12_umd_backend_set_pipeline_state,
   .create_heap = d12_umd_backend_create_heap,
   .open_heap = d12_umd_backend_open_heap,
   .destroy_heap = d12_umd_backend_destroy_heap,
   .create_placed_resource = d12_umd_backend_create_placed_resource,
   .create_reserved_resource = d12_umd_backend_create_reserved_resource,
   .destroy_resource = d12_umd_backend_destroy_resource,
   .resource_gpu_virtual_address =
      d12_umd_backend_resource_gpu_virtual_address,
   .resource_mip_packing = d12_umd_backend_resource_mip_packing,
   .get_resource_allocation_info =
      d12_umd_backend_get_resource_allocation_info,
   .format_support = d12_umd_backend_format_support,
   .multisample_quality_levels =
      d12_umd_backend_multisample_quality_levels,
   .resource_subresource_info =
      d12_umd_backend_resource_subresource_info,
   .create_descriptor_heap = d12_umd_backend_create_descriptor_heap,
   .destroy_descriptor_heap = d12_umd_backend_destroy_descriptor_heap,
   .descriptor_heap_cpu_start =
      d12_umd_backend_descriptor_heap_cpu_start,
   .descriptor_heap_gpu_start =
      d12_umd_backend_descriptor_heap_gpu_start,
   .set_descriptor_heaps = d12_umd_backend_set_descriptor_heaps,
   .create_shader_resource_view =
      d12_umd_backend_create_shader_resource_view,
   .create_unordered_access_view =
      d12_umd_backend_create_unordered_access_view,
   .create_render_target_view = d12_umd_backend_create_render_target_view,
   .create_depth_stencil_view = d12_umd_backend_create_depth_stencil_view,
   .create_constant_buffer_view =
      d12_umd_backend_create_constant_buffer_view,
   .create_sampler = d12_umd_backend_create_sampler,
   .copy_descriptors_simple = d12_umd_backend_copy_descriptors_simple,
   .create_query_heap = d12_umd_backend_create_query_heap,
   .destroy_query_heap = d12_umd_backend_destroy_query_heap,
   .begin_query = d12_umd_backend_begin_query,
   .end_query = d12_umd_backend_end_query,
   .resolve_query_data = d12_umd_backend_resolve_query_data,
   .set_predication = d12_umd_backend_set_predication,
};

HRESULT
d12_umd_backend_create(struct d12_umd_device *device,
                       D3D12DDI_HRTADAPTER runtime_adapter,
                       const struct agxw_adapter_identity *identity)
{
   struct agx_d3dkmt_runtime_device runtime_device;
   struct agx_kmt_luid process_key;
   uint64_t shader_base;
   HRESULT result;

   if (!device || device->backend || !identity)
      return E_INVALIDARG;

   /* The runtime adapter handle is intentionally opaque and process-local.
    * Resolve the matching KMT adapter once so every UMD in this process uses
    * the same named GPU-VA broker key. Enumeration handles are immediately
    * closed; no second device is created. */
   if (!d12_device_resolve_d3dkmt_adapter_luid(identity, &process_key))
      return DXGI_ERROR_UNSUPPORTED;

   result = d12_umd_agx_transport_init(
      &device->agx_transport, runtime_adapter, device->runtime_device,
      &device->kernel_callbacks);
   if (FAILED(result))
      return result;

   runtime_device =
      d12_umd_agx_runtime_device(&device->agx_transport, process_key,
                                 identity->params_blob_size);
   result = d12_device_create_d3dkmt_runtime(
      &runtime_device, &device->backend, &shader_base);
   if (FAILED(result))
      goto fail_transport;

   device->usc_base = shader_base;
   device->usc_size = UINT64_C(0x100000000);
   device->renderer_ops = &d12_umd_backend_renderer_ops;
   return S_OK;

fail_transport:
   d12_umd_agx_transport_finish(&device->agx_transport);
   return result;
}

void
d12_umd_backend_destroy(struct d12_umd_device *device)
{
   struct d12_device *backend;

   if (!device)
      return;

   device->renderer_ops = NULL;
   if (!device->backend)
      return;

   backend = device->backend;
   device->backend = NULL;
   d12_device_destroy_d3dkmt_runtime(backend);
   d12_umd_agx_transport_finish(&device->agx_transport);
}
