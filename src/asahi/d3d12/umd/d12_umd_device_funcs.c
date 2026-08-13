/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Implemented baseline device-table entries.  These functions translate the
 * D3D12 DDI queue object into the existing AppleAgxWddm context, paging and VA
 * contracts exclusively through callbacks supplied by the runtime.
 */

#include "d12_umd_device_funcs.h"

#include "d12_umd_internal.h"
#include "d12_umd_resource_private.h"

#include <stdbool.h>
#include <string.h>

#define D12_UMD_DEFAULT_PLACEMENT_ALIGNMENT UINT64_C(0x10000)

static bool
d12_umd_queue_arguments_valid(
   const D3D12DDIARG_CREATECOMMANDQUEUE_0001 *arguments)
{
   const UINT supported = D3D12DDI_COMMAND_QUEUE_FLAG_3D |
                          D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE |
                          D3D12DDI_COMMAND_QUEUE_FLAG_COPY;
   const UINT flags = (UINT)arguments->QueueFlags;

   return (arguments->NodeMask == 0 || arguments->NodeMask == 1) && flags &&
          !(flags & ~supported) && !(flags & (flags - 1));
}

static enum d12_umd_renderer_command_type
d12_umd_queue_renderer_type(D3D12DDI_COMMAND_QUEUE_FLAGS flags)
{
   if (flags == D3D12DDI_COMMAND_QUEUE_FLAG_3D)
      return D12_UMD_RENDERER_COMMAND_DIRECT;
   if (flags == D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE)
      return D12_UMD_RENDERER_COMMAND_COMPUTE;
   return D12_UMD_RENDERER_COMMAND_COPY;
}

static void
d12_umd_set_device_error(struct d12_umd_device *device, HRESULT result)
{
   if (device && device->core_callbacks.pfnSetErrorCb)
      device->core_callbacks.pfnSetErrorCb(device->runtime_device, result);
}

static UINT APIENTRY
d12_umd_get_implicit_physical_adapter_mask(D3D12DDI_HDEVICE handle)
{
   return d12_umd_device_from_handle(handle) ? 1u : 0u;
}

static void APIENTRY
d12_umd_query_node_map(D3D12DDI_HDEVICE handle, UINT physical_adapter_count,
                       UINT *map)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);

   if (!device || (physical_adapter_count && !map)) {
      d12_umd_set_device_error(device, E_INVALIDARG);
      return;
   }

   /* Apple AGX is exposed as one physical adapter with one public D3D node.
    * If a newer runtime asks with a wider physical-adapter array, explicitly
    * hide every non-existent entry instead of aliasing it to node zero.
    */
   if (physical_adapter_count)
      map[0] = 0;
   for (UINT index = 1; index < physical_adapter_count; ++index)
      map[index] = D3D12DDI_NODE_MAP_HIDE_NODE;
}

static uint64_t
d12_umd_standalone_format_support(DXGI_FORMAT format)
{
   const uint64_t texture = D12_FRONTEND_FORMAT_TEXTURE_1D |
                            D12_FRONTEND_FORMAT_TEXTURE_2D |
                            D12_FRONTEND_FORMAT_TEXTURE_3D |
                            D12_FRONTEND_FORMAT_TEXTURE_CUBE |
                            D12_FRONTEND_FORMAT_SHADER_LOAD |
                            D12_FRONTEND_FORMAT_SHADER_SAMPLE |
                            D12_FRONTEND_FORMAT_MIP |
                            D12_FRONTEND_FORMAT_SHADER_GATHER;
   const uint64_t color = texture | D12_FRONTEND_FORMAT_BUFFER |
                          D12_FRONTEND_FORMAT_VERTEX_BUFFER |
                          D12_FRONTEND_FORMAT_RENDER_TARGET |
                          D12_FRONTEND_FORMAT_BLENDABLE |
                          D12_FRONTEND_FORMAT_MULTISAMPLE_RESOLVE |
                          D12_FRONTEND_FORMAT_MULTISAMPLE_RENDER_TARGET |
                          D12_FRONTEND_FORMAT_MULTISAMPLE_LOAD |
                          D12_FRONTEND_FORMAT_TYPED_UAV |
                          D12_FRONTEND_FORMAT_UAV_TYPED_LOAD |
                          D12_FRONTEND_FORMAT_UAV_TYPED_STORE;

   switch (format) {
   case DXGI_FORMAT_R8G8B8A8_UNORM:
      return color | D12_FRONTEND_FORMAT_DISPLAY;
   case DXGI_FORMAT_B8G8R8A8_UNORM:
   case DXGI_FORMAT_B8G8R8X8_UNORM:
      return texture | D12_FRONTEND_FORMAT_RENDER_TARGET |
             D12_FRONTEND_FORMAT_BLENDABLE |
             D12_FRONTEND_FORMAT_MULTISAMPLE_RESOLVE |
             D12_FRONTEND_FORMAT_MULTISAMPLE_RENDER_TARGET |
             D12_FRONTEND_FORMAT_MULTISAMPLE_LOAD |
             D12_FRONTEND_FORMAT_DISPLAY;
   case DXGI_FORMAT_R32_FLOAT:
   case DXGI_FORMAT_R32_SINT:
      return color;
   case DXGI_FORMAT_R32_UINT:
   case DXGI_FORMAT_R16_UINT:
      return color | D12_FRONTEND_FORMAT_INDEX_BUFFER;
   case DXGI_FORMAT_D32_FLOAT:
   case DXGI_FORMAT_D16_UNORM:
      return D12_FRONTEND_FORMAT_TEXTURE_1D |
             D12_FRONTEND_FORMAT_TEXTURE_2D |
             D12_FRONTEND_FORMAT_TEXTURE_CUBE |
             D12_FRONTEND_FORMAT_SHADER_LOAD |
             D12_FRONTEND_FORMAT_SHADER_SAMPLE |
             D12_FRONTEND_FORMAT_SHADER_SAMPLE_COMPARISON |
             D12_FRONTEND_FORMAT_SHADER_GATHER_COMPARISON |
             D12_FRONTEND_FORMAT_MIP |
             D12_FRONTEND_FORMAT_DEPTH_STENCIL |
             D12_FRONTEND_FORMAT_MULTISAMPLE_LOAD;
   default:
      return format == DXGI_FORMAT_UNKNOWN ? D12_FRONTEND_FORMAT_BUFFER : 0;
   }
}

static UINT
d12_umd_translate_format_support(uint64_t support)
{
   UINT result = 0;

#define D12_MAP_DDI_FORMAT(frontend_bit, ddi_bit)                           \
   do {                                                                      \
      if (support & (frontend_bit))                                          \
         result |= (ddi_bit);                                                \
   } while (0)
   D12_MAP_DDI_FORMAT(D12_FRONTEND_FORMAT_SHADER_SAMPLE,
                      D3D12DDI_FORMAT_SUPPORT_SHADER_SAMPLE);
   D12_MAP_DDI_FORMAT(D12_FRONTEND_FORMAT_RENDER_TARGET,
                      D3D12DDI_FORMAT_SUPPORT_RENDERTARGET);
   D12_MAP_DDI_FORMAT(D12_FRONTEND_FORMAT_BLENDABLE,
                      D3D12DDI_FORMAT_SUPPORT_BLENDABLE);
   D12_MAP_DDI_FORMAT(D12_FRONTEND_FORMAT_MULTISAMPLE_RENDER_TARGET,
                      D3D12DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET);
   D12_MAP_DDI_FORMAT(D12_FRONTEND_FORMAT_MULTISAMPLE_LOAD,
                      D3D12DDI_FORMAT_SUPPORT_MULTISAMPLE_LOAD);
   D12_MAP_DDI_FORMAT(D12_FRONTEND_FORMAT_VERTEX_BUFFER,
                      D3D12DDI_FORMAT_SUPPORT_VERTEX_BUFFER);
   D12_MAP_DDI_FORMAT(D12_FRONTEND_FORMAT_UAV_TYPED_STORE,
                      D3D12DDI_FORMAT_SUPPORT_UAV_WRITES);
   D12_MAP_DDI_FORMAT(D12_FRONTEND_FORMAT_BUFFER,
                      D3D12DDI_FORMAT_SUPPORT_BUFFER);
   D12_MAP_DDI_FORMAT(D12_FRONTEND_FORMAT_SHADER_GATHER,
                      D3D12DDI_FORMAT_SUPPORT_SHADER_GATHER);
   D12_MAP_DDI_FORMAT(D12_FRONTEND_FORMAT_UAV_TYPED_LOAD,
                      D3D12DDI_FORMAT_SUPPORT_UAV_READS);
   D12_MAP_DDI_FORMAT(D12_FRONTEND_FORMAT_DISPLAY,
                      D3D12DDI_FORMAT_SUPPORT_DISPLAY);
   if (support & D12_FRONTEND_FORMAT_TEXTURE_2D)
      result |= D3D12DDI_FORMAT_SUPPORT_TILED;
#undef D12_MAP_DDI_FORMAT
   return result;
}

static void APIENTRY
d12_umd_check_format_support(D3D12DDI_HDEVICE handle, DXGI_FORMAT format,
                             UINT *out_support)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   uint64_t support;

   if (!out_support)
      return;
   *out_support = 0;
   if (!device)
      return;
   support = device->backend && device->renderer_ops &&
                   device->renderer_ops->format_support
                ? device->renderer_ops->format_support(device->backend,
                                                       (uint32_t)format)
                : d12_umd_standalone_format_support(format);
   *out_support = d12_umd_translate_format_support(support);
   if (!*out_support &&
       format == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM)
      *out_support = D3D12DDI_FORMAT_SUPPORT_NOT_SUPPORTED;
}

static void APIENTRY
d12_umd_check_multisample_quality_levels(
   D3D12DDI_HDEVICE handle, DXGI_FORMAT format, UINT sample_count,
   D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags, UINT *quality_levels)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   const UINT supported_flags =
      D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAG_TILED_RESOURCE;
   uint64_t support;

   if (!quality_levels)
      return;
   *quality_levels = 0;
   if (!device)
      return;
   if ((UINT)flags & ~supported_flags) {
      d12_umd_set_device_error(device, E_INVALIDARG);
      return;
   }
   if (device->backend && device->renderer_ops &&
       device->renderer_ops->multisample_quality_levels) {
      *quality_levels = device->renderer_ops->multisample_quality_levels(
         device->backend, (uint32_t)format, sample_count, (uint32_t)flags);
      return;
   }
   if (!sample_count || format == DXGI_FORMAT_UNKNOWN)
      return;
   support = d12_umd_standalone_format_support(format);
   if (flags & D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAG_TILED_RESOURCE) {
      if (sample_count == 1 &&
          (support & D12_FRONTEND_FORMAT_TEXTURE_2D))
         *quality_levels = 1;
      return;
   }
   if (sample_count == 1 && support)
      *quality_levels = 1;
   else if ((sample_count == 2 || sample_count == 4) &&
            (support &
             (D12_FRONTEND_FORMAT_MULTISAMPLE_RENDER_TARGET |
              D12_FRONTEND_FORMAT_MULTISAMPLE_LOAD)))
      *quality_levels = 1;
}

static void APIENTRY
d12_umd_get_mip_packing(D3D12DDI_HDEVICE handle,
                        D3D12DDI_HRESOURCE resource_handle,
                        UINT *packed_mips, UINT *packed_tiles)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_resource *resource = resource_handle.pDrvPrivate;

   if (packed_mips)
      *packed_mips = 0;
   if (packed_tiles)
      *packed_tiles = 0;
   if (!device || !packed_mips || !packed_tiles || !resource ||
       resource->signature != D12_UMD_RESOURCE_SIGNATURE ||
       resource->device != device || !resource->reserved ||
       !resource->backend || !device->renderer_ops ||
       !device->renderer_ops->resource_mip_packing) {
      d12_umd_set_device_error(device, E_INVALIDARG);
      return;
   }
   HRESULT result = device->renderer_ops->resource_mip_packing(
      resource->backend, packed_mips, packed_tiles);
   if (FAILED(result))
      d12_umd_set_device_error(device, result);
}

static HRESULT
d12_umd_heap_retain_mapping(struct d12_umd_heap *heap, void **mapping)
{
   struct d12_umd_device *device;
   HRESULT result = S_OK;

   if (!heap || heap->signature != D12_UMD_HEAP_SIGNATURE || !mapping ||
       heap->cpu_page_property == D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
      return E_INVALIDARG;
   device = heap->device;
   if (!device)
      return E_INVALIDARG;

   AcquireSRWLockExclusive(&heap->map_lock);
   if (!heap->mapping)
      result = d12_umd_kmt_lock(&device->kmt, heap->allocation.allocation,
                                &heap->mapping);
   if (SUCCEEDED(result)) {
      if (heap->map_count == UINT32_MAX) {
         result = E_FAIL;
      } else {
         ++heap->map_count;
         *mapping = heap->mapping;
      }
   }
   ReleaseSRWLockExclusive(&heap->map_lock);
   return result;
}

static HRESULT
d12_umd_heap_release_mapping(struct d12_umd_heap *heap)
{
   struct d12_umd_device *device;
   HRESULT result = S_OK;

   if (!heap || heap->signature != D12_UMD_HEAP_SIGNATURE || !heap->device)
      return E_INVALIDARG;
   device = heap->device;
   AcquireSRWLockExclusive(&heap->map_lock);
   if (!heap->map_count) {
      result = E_INVALIDARG;
   } else if (--heap->map_count == 0) {
      result = d12_umd_kmt_unlock(&device->kmt, heap->allocation.allocation);
      if (SUCCEEDED(result))
         heap->mapping = NULL;
   }
   ReleaseSRWLockExclusive(&heap->map_lock);
   return result;
}

static SIZE_T APIENTRY
d12_umd_calc_private_command_queue_size(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATECOMMANDQUEUE_0001 *arguments)
{
   if (!d12_umd_device_from_handle(handle) || !arguments ||
       !d12_umd_queue_arguments_valid(arguments))
      return 0;
   return sizeof(struct d12_umd_command_queue);
}

static HRESULT APIENTRY
d12_umd_create_command_queue(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATECOMMANDQUEUE_0001 *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_command_queue *queue;
   enum d12_umd_renderer_command_type type;
   bool reserve_usc;
   HRESULT result;

   if (!device || !arguments || !d12_umd_queue_arguments_valid(arguments))
      return E_INVALIDARG;
   if (!arguments->hDrvCommandQueue.pDrvPrivate ||
       !arguments->hRTCommandQueue.handle)
      return E_INVALIDARG;

   queue = (struct d12_umd_command_queue *)
              arguments->hDrvCommandQueue.pDrvPrivate;
   memset(queue, 0, sizeof(*queue));
   InitializeSRWLock(&queue->dxgi_blt_lock);
   type = d12_umd_queue_renderer_type(arguments->QueueFlags);

   AcquireSRWLockExclusive(&device->queue_lock);
   reserve_usc = !device->backend && device->queue_count == 0;
   if (device->backend && device->renderer_ops &&
       device->renderer_ops->create_command_queue) {
      result = device->renderer_ops->create_command_queue(
         device->backend, type, &queue->backend);
   } else {
      result = d12_umd_queue_runtime_create(
         &device->core_callbacks, &device->kmt, arguments->hRTCommandQueue,
         AGXW_PRIORITY_MEDIUM, D12_UMD_USC_BASE, D12_UMD_USC_SIZE,
         reserve_usc, &queue->runtime);
   }
   if (SUCCEEDED(result)) {
      if (!queue->backend && reserve_usc) {
         device->usc_base = D12_UMD_USC_BASE;
         device->usc_size = D12_UMD_USC_SIZE;
         /* The reservation is shared by every queue and released by the last
          * one, so successful construction transfers ownership to device.
          */
         queue->runtime.owns_usc_reservation = false;
      }
      ++device->queue_count;
      queue->signature = D12_UMD_COMMAND_QUEUE_SIGNATURE;
      queue->device = device;
      queue->type = type;
      queue->next = device->queues;
      device->queues = queue;
   }
   ReleaseSRWLockExclusive(&device->queue_lock);
   return result;
}

static void APIENTRY
d12_umd_destroy_command_queue(D3D12DDI_HDEVICE handle,
                              D3D12DDI_HCOMMANDQUEUE queue_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_command_queue *queue =
      (struct d12_umd_command_queue *)queue_handle.pDrvPrivate;
   HRESULT result;

   if (!device || !queue || queue->signature != D12_UMD_COMMAND_QUEUE_SIGNATURE ||
       queue->device != device)
      return;

   AcquireSRWLockExclusive(&device->queue_lock);
   if (device->renderer_ops) {
      if (queue->dxgi_blt_list &&
          device->renderer_ops->destroy_command_list)
         device->renderer_ops->destroy_command_list(queue->dxgi_blt_list);
      if (queue->dxgi_blt_allocator &&
          device->renderer_ops->destroy_command_allocator)
         device->renderer_ops->destroy_command_allocator(
            queue->dxgi_blt_allocator);
      queue->dxgi_blt_list = NULL;
      queue->dxgi_blt_allocator = NULL;
   }
   if (queue->backend) {
      if (device->renderer_ops &&
          device->renderer_ops->destroy_command_queue)
         device->renderer_ops->destroy_command_queue(queue->backend);
      result = S_OK;
   } else {
      if (!device->backend && device->queue_count == 1) {
         queue->runtime.usc_base = device->usc_base;
         queue->runtime.usc_size = device->usc_size;
         queue->runtime.owns_usc_reservation = true;
      }
      result = d12_umd_queue_runtime_destroy(&device->core_callbacks,
                                             &device->kmt, &queue->runtime);
   }
   if (device->queue_count)
      --device->queue_count;
   {
      struct d12_umd_command_queue **link = &device->queues;
      while (*link && *link != queue)
         link = &(*link)->next;
      if (*link == queue)
         *link = queue->next;
   }
   if (!device->queue_count && !device->backend) {
      device->usc_base = 0;
      device->usc_size = 0;
   }
   ReleaseSRWLockExclusive(&device->queue_lock);

   if (FAILED(result))
      d12_umd_set_device_error(device, result);
   SecureZeroMemory(queue, sizeof(*queue));
}

static void APIENTRY
d12_umd_execute_command_lists(D3D12DDI_HCOMMANDQUEUE queue_handle,
                              UINT count,
                              const D3D12DDI_HCOMMANDLIST *list_handles)
{
   struct d12_umd_command_queue *queue = queue_handle.pDrvPrivate;
   struct d12_umd_device *device;
   void *local[64];
   void **lists = local;
   HRESULT result;

   if (!queue || queue->signature != D12_UMD_COMMAND_QUEUE_SIGNATURE)
      return;
   device = queue->device;
   if (!device || !queue->backend || !device->renderer_ops ||
       !device->renderer_ops->execute_command_lists ||
       (count && !list_handles)) {
      d12_umd_set_device_error(device, E_INVALIDARG);
      return;
   }
   if (count > ARRAYSIZE(local)) {
      if ((SIZE_T)count > SIZE_MAX / sizeof(*lists)) {
         d12_umd_set_device_error(device, E_OUTOFMEMORY);
         return;
      }
      lists = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)count * sizeof(*lists));
      if (!lists) {
         d12_umd_set_device_error(device, E_OUTOFMEMORY);
         return;
      }
   }
   for (UINT index = 0; index < count; ++index) {
      struct d12_umd_command_list *list =
         d12_umd_command_list_from_handle(list_handles[index]);
      if (!list || list->device != device || !list->backend ||
          list->type != queue->type) {
         if (lists != local)
            HeapFree(GetProcessHeap(), 0, lists);
         d12_umd_set_device_error(device, E_INVALIDARG);
         return;
      }
      lists[index] = list->backend;
   }
   result = device->renderer_ops->execute_command_lists(
      queue->backend, count, lists);
   if (lists != local)
      HeapFree(GetProcessHeap(), 0, lists);
   if (FAILED(result))
      d12_umd_set_device_error(device, result);
}

static struct d12_umd_device *
d12_umd_tiled_queue_device(D3D12DDI_HCOMMANDQUEUE queue_handle)
{
   struct d12_umd_command_queue *queue = queue_handle.pDrvPrivate;

   return queue && queue->signature == D12_UMD_COMMAND_QUEUE_SIGNATURE
             ? queue->device
             : NULL;
}

static void APIENTRY
d12_umd_update_tile_mappings(
   D3D12DDI_HCOMMANDQUEUE queue_handle, D3D12DDI_HRESOURCE resource,
   UINT region_count,
   const D3D12DDI_TILED_RESOURCE_COORDINATE *region_start_coordinates,
   const D3D12DDI_TILE_REGION_SIZE *region_sizes, D3D12DDI_HHEAP heap,
   UINT range_count, const D3D12DDI_TILE_RANGE_FLAGS *range_flags,
   const UINT *heap_start_offsets, const UINT *range_tile_counts,
   D3D12DDI_TILE_MAPPING_FLAGS flags)
{
   struct d12_umd_command_queue *queue = queue_handle.pDrvPrivate;
   struct d12_umd_resource *tiled = resource.pDrvPrivate;
   struct d12_umd_heap *backing = heap.pDrvPrivate;
   struct d12_frontend_tiled_resource_coordinate local_starts[16];
   struct d12_frontend_tile_region_size local_sizes[16];
   uint32_t local_range_flags[16];
   struct d12_frontend_tiled_resource_coordinate *starts = local_starts;
   struct d12_frontend_tile_region_size *sizes = local_sizes;
   uint32_t *translated_range_flags = local_range_flags;
   HRESULT result = E_INVALIDARG;

   if (!queue || queue->signature != D12_UMD_COMMAND_QUEUE_SIGNATURE ||
       !queue->device || !queue->backend || !tiled ||
       tiled->signature != D12_UMD_RESOURCE_SIGNATURE ||
       tiled->device != queue->device || !tiled->reserved ||
       !tiled->backend || (region_count && !region_start_coordinates) ||
       ((UINT)flags & ~(UINT)D3D12DDI_TILE_MAPPING_FLAG_NO_HAZARD) ||
       (backing &&
        (backing->signature != D12_UMD_HEAP_SIGNATURE ||
         backing->device != queue->device || !backing->backend)) ||
       !queue->device->renderer_ops ||
       !queue->device->renderer_ops->update_tile_mappings)
      goto out;

   if (region_count > ARRAYSIZE(local_starts)) {
      if ((SIZE_T)region_count > SIZE_MAX / sizeof(*starts)) {
         result = E_OUTOFMEMORY;
         goto out;
      }
      starts = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                         (SIZE_T)region_count * sizeof(*starts));
      if (!starts) {
         result = E_OUTOFMEMORY;
         goto out;
      }
      if (region_sizes) {
         sizes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           (SIZE_T)region_count * sizeof(*sizes));
         if (!sizes) {
            result = E_OUTOFMEMORY;
            goto out;
         }
      }
   }
   if (range_flags && range_count > ARRAYSIZE(local_range_flags)) {
      if ((SIZE_T)range_count > SIZE_MAX / sizeof(*translated_range_flags)) {
         result = E_OUTOFMEMORY;
         goto out;
      }
      translated_range_flags = HeapAlloc(
         GetProcessHeap(), HEAP_ZERO_MEMORY,
         (SIZE_T)range_count * sizeof(*translated_range_flags));
      if (!translated_range_flags) {
         result = E_OUTOFMEMORY;
         goto out;
      }
   }

   for (UINT i = 0; i < region_count; ++i) {
      starts[i] = (struct d12_frontend_tiled_resource_coordinate){
         .x = region_start_coordinates[i].X,
         .y = region_start_coordinates[i].Y,
         .z = region_start_coordinates[i].Z,
         .subresource = region_start_coordinates[i].Subresource,
      };
      if (region_sizes) {
         sizes[i] = (struct d12_frontend_tile_region_size){
            .num_tiles = region_sizes[i].NumTiles,
            .use_box = region_sizes[i].UseBox ? 1u : 0u,
            .width = region_sizes[i].Width,
            .height = region_sizes[i].Height,
            .depth = region_sizes[i].Depth,
         };
      }
   }
   for (UINT i = 0; range_flags && i < range_count; ++i)
      translated_range_flags[i] = (uint32_t)range_flags[i];

   result = queue->device->renderer_ops->update_tile_mappings(
      queue->backend, tiled->backend, region_count, starts,
      region_sizes ? sizes : NULL, backing ? backing->backend : NULL,
      range_count, range_flags ? translated_range_flags : NULL,
      heap_start_offsets, range_tile_counts, (uint32_t)flags);

out:
   if (starts != local_starts)
      HeapFree(GetProcessHeap(), 0, starts);
   if (sizes != local_sizes)
      HeapFree(GetProcessHeap(), 0, sizes);
   if (translated_range_flags != local_range_flags)
      HeapFree(GetProcessHeap(), 0, translated_range_flags);
   if (FAILED(result))
      d12_umd_set_device_error(d12_umd_tiled_queue_device(queue_handle),
                               result);
}

static void APIENTRY
d12_umd_copy_tile_mappings(
   D3D12DDI_HCOMMANDQUEUE queue_handle,
   D3D12DDI_HRESOURCE destination_resource,
   const D3D12DDI_TILED_RESOURCE_COORDINATE *destination_coordinate,
   D3D12DDI_HRESOURCE source_resource,
   const D3D12DDI_TILED_RESOURCE_COORDINATE *source_coordinate,
   const D3D12DDI_TILE_REGION_SIZE *region_size,
   D3D12DDI_TILE_MAPPING_FLAGS flags)
{
   struct d12_umd_command_queue *queue = queue_handle.pDrvPrivate;
   struct d12_umd_resource *destination =
      destination_resource.pDrvPrivate;
   struct d12_umd_resource *source = source_resource.pDrvPrivate;
   HRESULT result = E_INVALIDARG;

   if (!queue || queue->signature != D12_UMD_COMMAND_QUEUE_SIGNATURE ||
       !queue->device || !queue->backend || !destination || !source ||
       destination->signature != D12_UMD_RESOURCE_SIGNATURE ||
       source->signature != D12_UMD_RESOURCE_SIGNATURE ||
       destination->device != queue->device ||
       source->device != queue->device || !destination->reserved ||
       !source->reserved || !destination->backend || !source->backend ||
       !destination_coordinate || !source_coordinate || !region_size ||
       ((UINT)flags & ~(UINT)D3D12DDI_TILE_MAPPING_FLAG_NO_HAZARD) ||
       !queue->device->renderer_ops ||
       !queue->device->renderer_ops->copy_tile_mappings)
      goto out;

   const struct d12_frontend_tiled_resource_coordinate destination_start = {
      destination_coordinate->X, destination_coordinate->Y,
      destination_coordinate->Z, destination_coordinate->Subresource,
   };
   const struct d12_frontend_tiled_resource_coordinate source_start = {
      source_coordinate->X, source_coordinate->Y, source_coordinate->Z,
      source_coordinate->Subresource,
   };
   const struct d12_frontend_tile_region_size size = {
      region_size->NumTiles, region_size->UseBox ? 1u : 0u,
      region_size->Width,
      region_size->Height, region_size->Depth,
   };
   result = queue->device->renderer_ops->copy_tile_mappings(
      queue->backend, destination->backend, &destination_start,
      source->backend, &source_start, &size, (uint32_t)flags);

out:
   if (FAILED(result))
      d12_umd_set_device_error(d12_umd_tiled_queue_device(queue_handle),
                               result);
}

static bool
d12_umd_is_power_of_two(uint64_t value)
{
   return value && !(value & (value - 1));
}

static bool
d12_umd_heap_arguments_valid(const D3D12DDIARG_CREATEHEAP_0001 *arguments)
{
   const UINT supported_flags =
      D3D12DDI_HEAP_FLAG_NON_RT_DS_TEXTURES |
      D3D12DDI_HEAP_FLAG_BUFFERS |
      D3D12DDI_HEAP_FLAG_COHERENT_SYSTEMWIDE |
      D3D12DDI_HEAP_FLAG_PRIMARY |
      D3D12DDI_HEAP_FLAG_RT_DS_TEXTURES;
   const UINT category_flags =
      D3D12DDI_HEAP_FLAG_NON_RT_DS_TEXTURES |
      D3D12DDI_HEAP_FLAG_BUFFERS |
      D3D12DDI_HEAP_FLAG_RT_DS_TEXTURES;
   const uint64_t alignment =
      arguments->Alignment ? arguments->Alignment
                           : D12_UMD_DEFAULT_PLACEMENT_ALIGNMENT;

   return arguments->ByteSize &&
          ((UINT)arguments->Flags & category_flags) &&
          !((UINT)arguments->Flags & ~supported_flags) &&
          d12_umd_is_power_of_two(alignment) &&
          alignment >= AGXW_PAGE_SIZE &&
          (arguments->MemoryPool == D3D12DDI_MEMORY_POOL_L0 ||
           arguments->MemoryPool == D3D12DDI_MEMORY_POOL_L1) &&
          arguments->CPUPageProperty >=
             D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE &&
          arguments->CPUPageProperty <= D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK &&
          (arguments->CreationNodeMask == 0 ||
           arguments->CreationNodeMask == 1) &&
          (arguments->VisibleNodeMask == 0 || arguments->VisibleNodeMask == 1);
}

static bool
d12_umd_resource_arguments_valid(
   const D3D12DDIARG_CREATERESOURCE_0003 *arguments)
{
   const UINT supported_flags =
      D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET |
      D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL |
      D3D12DDI_RESOURCE_FLAG_0003_CROSS_ADAPTER |
      D3D12DDI_RESOURCE_FLAG_0003_SIMULTANEOUS_ACCESS |
      D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE |
      D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS;

   if (!arguments || !arguments->Width || !arguments->Height ||
       !arguments->DepthOrArraySize || !arguments->SampleDesc.Count ||
       arguments->ResourceType < D3D12DDI_RT_BUFFER ||
       arguments->ResourceType > D3D12DDI_RT_TEXTURE3D ||
       arguments->Layout < D3D12DDI_TL_UNDEFINED ||
       arguments->Layout > D3D12DDI_TL_64KB_TILE_STANDARD_SWIZZLE ||
       ((UINT)arguments->Flags & ~supported_flags) ||
       (arguments->Flags & D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET &&
        arguments->Flags & D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL) ||
       (!arguments->ReuseBufferGPUVA.BaseAddress.UMD.hResource.pDrvPrivate &&
        arguments->ReuseBufferGPUVA.BaseAddress.UMD.Offset) ||
       (arguments->pRowMajorLayout &&
        arguments->Layout != D3D12DDI_TL_ROW_MAJOR))
      return false;

   if (arguments->pRowMajorLayout) {
      const UINT row_pitch = arguments->pRowMajorLayout->RowPitch;
      const UINT slice_pitch = arguments->pRowMajorLayout->SlicePitch;
      if (arguments->ResourceType != D3D12DDI_RT_TEXTURE2D ||
          arguments->MipLevels != 1 || arguments->DepthOrArraySize != 1 ||
          arguments->SampleDesc.Count != 1 || !row_pitch || !slice_pitch ||
          (row_pitch & UINT32_C(0xff)) ||
          arguments->Height > UINT32_MAX / row_pitch ||
          slice_pitch != row_pitch * arguments->Height)
         return false;
   }

   if (arguments->ResourceType == D3D12DDI_RT_BUFFER)
      return arguments->Height == 1 && arguments->DepthOrArraySize == 1 &&
             arguments->MipLevels == 1 &&
             arguments->Format == DXGI_FORMAT_UNKNOWN &&
             arguments->SampleDesc.Count == 1 &&
             arguments->SampleDesc.Quality == 0 &&
             arguments->Layout == D3D12DDI_TL_ROW_MAJOR;

   if (arguments->Layout == D3D12DDI_TL_ROW_MAJOR &&
       (arguments->ResourceType != D3D12DDI_RT_TEXTURE2D ||
        arguments->MipLevels != 1 || arguments->DepthOrArraySize != 1 ||
        arguments->SampleDesc.Count != 1 ||
        !(arguments->Flags & D3D12DDI_RESOURCE_FLAG_0003_CROSS_ADAPTER) ||
        (arguments->Flags &
         (D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL |
          D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS))))
      return false;

   if (arguments->Format == DXGI_FORMAT_UNKNOWN)
      return false;
   if (arguments->ResourceType == D3D12DDI_RT_TEXTURE1D &&
       (arguments->Height != 1 || arguments->SampleDesc.Count != 1))
      return false;
   if (arguments->ResourceType == D3D12DDI_RT_TEXTURE3D &&
       arguments->SampleDesc.Count != 1)
      return false;
   return true;
}

static struct d12_frontend_heap_desc
d12_umd_frontend_heap_desc(const D3D12DDIARG_CREATEHEAP_0001 *arguments)
{
   uint32_t flags = 0;
   if (arguments->Flags & D3D12DDI_HEAP_FLAG_NON_RT_DS_TEXTURES)
      flags |= D12_FRONTEND_HEAP_ALLOW_NON_RT_DS_TEXTURES;
   if (arguments->Flags & D3D12DDI_HEAP_FLAG_BUFFERS)
      flags |= D12_FRONTEND_HEAP_ALLOW_BUFFERS;
   if (arguments->Flags & D3D12DDI_HEAP_FLAG_COHERENT_SYSTEMWIDE)
      flags |= D12_FRONTEND_HEAP_COHERENT_SYSTEMWIDE;
   if (arguments->Flags & D3D12DDI_HEAP_FLAG_PRIMARY)
      flags |= D12_FRONTEND_HEAP_PRIMARY;
   if (arguments->Flags & D3D12DDI_HEAP_FLAG_RT_DS_TEXTURES)
      flags |= D12_FRONTEND_HEAP_ALLOW_RT_DS_TEXTURES;
   return (struct d12_frontend_heap_desc){
      .size = arguments->ByteSize,
      .alignment = arguments->Alignment,
      .memory_pool = (enum d12_frontend_memory_pool)arguments->MemoryPool,
      .cpu_page_property =
         (enum d12_frontend_cpu_page_property)arguments->CPUPageProperty,
      .flags = flags,
      .creation_node_mask = arguments->CreationNodeMask,
      .visible_node_mask = arguments->VisibleNodeMask,
   };
}

static struct d12_frontend_resource_desc
d12_umd_frontend_resource_desc(
   const D3D12DDIARG_CREATERESOURCE_0003 *arguments)
{
   uint32_t flags = 0;
   if (arguments->Flags & D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET)
      flags |= D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET;
   if (arguments->Flags & D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL)
      flags |= D12_FRONTEND_RESOURCE_ALLOW_DEPTH_STENCIL;
   if (arguments->Flags & D3D12DDI_RESOURCE_FLAG_0003_CROSS_ADAPTER)
      flags |= D12_FRONTEND_RESOURCE_ALLOW_CROSS_ADAPTER;
   if (arguments->Flags & D3D12DDI_RESOURCE_FLAG_0003_SIMULTANEOUS_ACCESS)
      flags |= D12_FRONTEND_RESOURCE_ALLOW_SIMULTANEOUS_ACCESS;
   if (arguments->Flags & D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE)
      flags |= D12_FRONTEND_RESOURCE_ALLOW_SHADER_RESOURCE;
   if (arguments->Flags & D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS)
      flags |= D12_FRONTEND_RESOURCE_ALLOW_UNORDERED_ACCESS;
   return (struct d12_frontend_resource_desc){
      .dimension = (uint32_t)arguments->ResourceType,
      .width = arguments->Width,
      .height = arguments->Height,
      .depth_or_array_size = arguments->DepthOrArraySize,
      .mip_levels = arguments->MipLevels,
      .format = (uint32_t)arguments->Format,
      .sample_count = arguments->SampleDesc.Count,
      .sample_quality = arguments->SampleDesc.Quality,
      .layout = (uint32_t)arguments->Layout,
      .flags = flags,
      .initial_state = (uint32_t)arguments->InitialResourceState,
      .row_pitch = arguments->pRowMajorLayout
                      ? arguments->pRowMajorLayout->RowPitch
                      : 0,
      .slice_pitch = arguments->pRowMajorLayout
                        ? arguments->pRowMajorLayout->SlicePitch
                        : 0,
   };
}

struct d12_umd_reused_resource_info {
   struct d12_umd_resource *source;
   uint64_t source_offset;
   uint64_t heap_offset;
   uint64_t gpu_virtual_address;
};

static HRESULT
d12_umd_reused_resource_info(
   struct d12_umd_device *device,
   const D3D12DDIARG_CREATERESOURCE_0003 *arguments,
   struct d12_umd_reused_resource_info *info)
{
   const D3D12DDIARG_HRESOURCE_PLACEMENT *placement =
      &arguments->ReuseBufferGPUVA.BaseAddress.UMD;
   struct d12_umd_resource *source = placement->hResource.pDrvPrivate;
   struct d12_frontend_resource_allocation_info allocation_info;
   struct d12_frontend_resource_desc frontend;
   uint64_t source_address;
   HRESULT result;

   if (!device || !arguments || !info || !source ||
       source->signature != D12_UMD_RESOURCE_SIGNATURE ||
       source->device != device || !source->backend || source->reserved ||
       source->desc.ResourceType != D3D12DDI_RT_BUFFER ||
       !source->allocation.allocation || !source->allocation.size ||
       !device->backend || !device->renderer_ops ||
       !device->renderer_ops->create_reused_resource ||
       !device->renderer_ops->resource_gpu_virtual_address ||
       !device->renderer_ops->get_resource_allocation_info)
      return E_INVALIDARG;

   frontend = d12_umd_frontend_resource_desc(arguments);
   result = device->renderer_ops->get_resource_allocation_info(
      device->backend, &frontend, 1, &allocation_info);
   if (FAILED(result))
      return result;

   source_address = device->renderer_ops->resource_gpu_virtual_address(
      source->backend);
   if (!source->allocation.gpu_virtual_address ||
       source->allocation.gpu_virtual_address >
          UINT64_MAX - source->heap_offset ||
       source_address !=
          source->allocation.gpu_virtual_address + source->heap_offset)
      return E_INVALIDARG;
   if (!d12_umd_resource_reuse_range(
          source->desc.Width, source_address, source->heap_offset,
          source->allocation.size, placement->Offset, allocation_info.size,
          allocation_info.alignment, &info->gpu_virtual_address,
          &info->heap_offset))
      return E_INVALIDARG;

   info->source = source;
   info->source_offset = placement->Offset;
   return S_OK;
}

static uint32_t
d12_umd_heap_allocation_flags(
   const D3D12DDIARG_CREATEHEAP_0001 *arguments)
{
   if (arguments->Flags & D3D12DDI_HEAP_FLAG_PRIMARY)
      return AGXW_ALLOC_SHAREABLE | AGXW_ALLOC_PRIMARY;

   uint32_t flags = AGXW_ALLOC_VM_PRIVATE;
   if (arguments->CPUPageProperty == D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK)
      flags |= AGXW_ALLOC_WRITEBACK;
   if (arguments->CPUPageProperty == D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
      flags |= AGXW_ALLOC_NO_CPU_MAP;
   return flags;
}

static D3D12DDI_HEAP_AND_RESOURCE_SIZES APIENTRY
d12_umd_calc_private_heap_resource_sizes(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATEHEAP_0001 *heap_arguments,
   const D3D12DDIARG_CREATERESOURCE_0003 *resource_arguments)
{
   D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes = {0};
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   const bool reuses_gpu_va =
      resource_arguments &&
      resource_arguments->ReuseBufferGPUVA.BaseAddress.UMD.hResource
         .pDrvPrivate;
   struct d12_umd_reused_resource_info reuse_info;

   if (!device ||
       (!heap_arguments && !resource_arguments) ||
       (heap_arguments && !d12_umd_heap_arguments_valid(heap_arguments)) ||
       (resource_arguments &&
        !d12_umd_resource_arguments_valid(resource_arguments)) ||
       (reuses_gpu_va &&
        (heap_arguments ||
         FAILED(d12_umd_reused_resource_info(
            device, resource_arguments, &reuse_info)))) ||
       (heap_arguments && resource_arguments &&
        resource_arguments->Width > heap_arguments->ByteSize))
      return sizes;

   sizes.Heap = heap_arguments ? sizeof(struct d12_umd_heap) : 0;
   sizes.Resource = resource_arguments ? sizeof(struct d12_umd_resource) : 0;
   return sizes;
}

static HRESULT APIENTRY
d12_umd_create_heap_resource(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATEHEAP_0001 *heap_arguments,
   D3D12DDI_HHEAP heap_handle, D3D12DDI_HRTRESOURCE runtime_resource,
   const D3D12DDIARG_CREATERESOURCE_0003 *resource_arguments,
   const D3D12DDI_CLEAR_VALUES *clear_value,
   D3D12DDI_HRESOURCE resource_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_heap *heap = (struct d12_umd_heap *)heap_handle.pDrvPrivate;
   struct d12_umd_resource *resource =
      (struct d12_umd_resource *)resource_handle.pDrvPrivate;
   struct d12_frontend_heap_desc frontend_heap;
   struct d12_frontend_resource_desc frontend_resource;
   struct d12_frontend_clear_value frontend_clear;
   struct d12_umd_resource_private private_data;
   struct d12_frontend_heap_allocation frontend_allocation;
   const struct d12_frontend_clear_value *translated_clear = NULL;
   struct d12_umd_reused_resource_info reuse_info;
   uint64_t alignment;
   uint64_t standard_descriptor = 0;
   HRESULT result;

   if (!device)
      return E_INVALIDARG;

   /* A resource description without heap arguments either reserves new GPU
    * VA or captures the effective VA of ReuseBufferGPUVA. Neither path owns
    * a new runtime/KMD allocation. */
   if (!heap_arguments) {
      if (!resource_arguments || !resource || heap ||
          !d12_umd_resource_arguments_valid(resource_arguments) ||
          !device->renderer_ops || !device->backend)
         return E_INVALIDARG;

      memset(resource, 0, sizeof(*resource));
      frontend_resource = d12_umd_frontend_resource_desc(resource_arguments);
      if (clear_value) {
         frontend_clear.format = (uint32_t)clear_value->Format;
         if (resource_arguments->Flags &
             D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL) {
            frontend_clear.depth_stencil.depth =
               clear_value->DepthStencil.Depth;
            frontend_clear.depth_stencil.stencil =
               clear_value->DepthStencil.Stencil;
         } else {
            memcpy(frontend_clear.color, clear_value->Color,
                   sizeof(frontend_clear.color));
         }
         translated_clear = &frontend_clear;
      }

      if (resource_arguments->ReuseBufferGPUVA.BaseAddress.UMD.hResource
             .pDrvPrivate) {
         result = d12_umd_reused_resource_info(
            device, resource_arguments, &reuse_info);
         if (FAILED(result))
            return result;
         result = device->renderer_ops->create_reused_resource(
            device->backend, reuse_info.source->backend,
            reuse_info.source_offset, &frontend_resource, translated_clear,
            &resource->backend);
         if (SUCCEEDED(result) &&
             (!resource->backend ||
              device->renderer_ops->resource_gpu_virtual_address(
                 resource->backend) != reuse_info.gpu_virtual_address)) {
            if (resource->backend &&
                device->renderer_ops->destroy_resource)
               device->renderer_ops->destroy_resource(resource->backend);
            resource->backend = NULL;
            result = E_FAIL;
         }
      } else {
         if (!device->renderer_ops->create_reserved_resource)
            return E_NOTIMPL;
         result = device->renderer_ops->create_reserved_resource(
            device->backend, &frontend_resource, translated_clear,
            &resource->backend);
      }
      if (SUCCEEDED(result) && !resource->backend)
         result = E_FAIL;
      if (FAILED(result)) {
         SecureZeroMemory(resource, sizeof(*resource));
         return result;
      }
      resource->signature = D12_UMD_RESOURCE_SIGNATURE;
      resource->device = device;
      resource->reserved =
         !resource_arguments->ReuseBufferGPUVA.BaseAddress.UMD.hResource
             .pDrvPrivate;
      if (!resource->reserved) {
         resource->allocation = reuse_info.source->allocation;
         resource->heap_offset = reuse_info.heap_offset;
      }
      resource->desc = *resource_arguments;
      if (resource_arguments->pRowMajorLayout) {
         resource->row_major = *resource_arguments->pRowMajorLayout;
         resource->desc.pRowMajorLayout = &resource->row_major;
      }
      return S_OK;
   }

   if (!heap || !runtime_resource.handle ||
       !d12_umd_heap_arguments_valid(heap_arguments) ||
       (resource_arguments &&
        (!resource || !d12_umd_resource_arguments_valid(resource_arguments) ||
         resource_arguments->Width > heap_arguments->ByteSize)))
      return E_INVALIDARG;

   memset(heap, 0, sizeof(*heap));
   if (resource)
      memset(resource, 0, sizeof(*resource));

   frontend_heap = d12_umd_frontend_heap_desc(heap_arguments);
   if (resource_arguments) {
      frontend_resource = d12_umd_frontend_resource_desc(resource_arguments);
      if (clear_value) {
         frontend_clear.format = (uint32_t)clear_value->Format;
         if (resource_arguments->Flags &
             D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL) {
            frontend_clear.depth_stencil.depth =
               clear_value->DepthStencil.Depth;
            frontend_clear.depth_stencil.stencil =
               clear_value->DepthStencil.Stencil;
         } else {
            memcpy(frontend_clear.color, clear_value->Color,
                   sizeof(frontend_clear.color));
         }
         translated_clear = &frontend_clear;
      }
   }
   if (!d12_umd_resource_private_pack(
          &frontend_heap, resource_arguments ? &frontend_resource : NULL,
          translated_clear, &private_data))
      return E_INVALIDARG;
   if (resource_arguments)
      (void)d12_umd_resource_private_standard_desc_pack(
         &frontend_resource, &standard_descriptor);

   if (device->renderer_ops && device->backend &&
       device->renderer_ops->create_heap) {
      result = device->renderer_ops->create_heap(
         device->backend, &frontend_heap, runtime_resource.handle,
         &private_data, sizeof(private_data), standard_descriptor,
         &heap->backend,
         &frontend_allocation);
      if (FAILED(result))
         return result;
      heap->allocation = (struct d12_umd_allocation){
         .allocation = frontend_allocation.allocation,
         .resource = frontend_allocation.resource,
         .gpu_virtual_address = frontend_allocation.gpu_virtual_address,
         .size = frontend_allocation.size,
      };
   } else {
      alignment = heap_arguments->Alignment
                     ? heap_arguments->Alignment
                     : D12_UMD_DEFAULT_PLACEMENT_ALIGNMENT;
      result = d12_umd_kmt_allocate(
         &device->kmt, runtime_resource.handle, heap_arguments->ByteSize,
         alignment, d12_umd_heap_allocation_flags(heap_arguments),
         standard_descriptor, &private_data, sizeof(private_data),
         &heap->allocation);
      if (FAILED(result))
         return result;
   }

   heap->signature = D12_UMD_HEAP_SIGNATURE;
   heap->device = device;
   heap->runtime_resource = runtime_resource.handle;
   heap->cpu_page_property = heap_arguments->CPUPageProperty;
   InitializeSRWLock(&heap->map_lock);

   AcquireSRWLockExclusive(&device->heap_lock);
   heap->next = device->heaps;
   device->heaps = heap;
   ReleaseSRWLockExclusive(&device->heap_lock);

   if (resource_arguments) {
      if (heap->backend && device->renderer_ops &&
          device->renderer_ops->create_placed_resource) {
         result = device->renderer_ops->create_placed_resource(
            device->backend, heap->backend, 0, &frontend_resource,
            translated_clear, &resource->backend);
         if (FAILED(result)) {
            AcquireSRWLockExclusive(&device->heap_lock);
            if (device->heaps == heap) {
               device->heaps = heap->next;
            } else {
               struct d12_umd_heap *previous = device->heaps;
               while (previous && previous->next != heap)
                  previous = previous->next;
               if (previous)
                  previous->next = heap->next;
            }
            ReleaseSRWLockExclusive(&device->heap_lock);
            device->renderer_ops->destroy_heap(heap->backend);
            SecureZeroMemory(heap, sizeof(*heap));
            return result;
         }
      }
      resource->signature = D12_UMD_RESOURCE_SIGNATURE;
      resource->device = device;
      resource->allocation = heap->allocation;
      resource->runtime_heap_resource = heap->runtime_resource;
      resource->heap_offset = 0;
      resource->desc = *resource_arguments;
      if (resource_arguments->pRowMajorLayout) {
         resource->row_major = *resource_arguments->pRowMajorLayout;
         resource->desc.pRowMajorLayout = &resource->row_major;
      }
   }
   return S_OK;
}

static bool
d12_umd_open_allocation_read(
   const D3D12DDIARG_OPENHEAP_0003 *arguments,
   struct agxw_alloc_private *allocation_private)
{
   const D3DDDI_OPENALLOCATIONINFO *allocation;

   if (!arguments || !allocation_private ||
       arguments->NumAllocations != AGXW_MAX_ALLOCATIONS_PER_RESOURCE ||
       !arguments->pOpenAllocationInfo || !arguments->hKMResource.handle)
      return false;
   allocation = &arguments->pOpenAllocationInfo[0];
   if (!allocation->hAllocation || !allocation->pPrivateDriverData ||
       allocation->PrivateDriverDataSize != sizeof(*allocation_private))
      return false;
   memcpy(allocation_private, allocation->pPrivateDriverData,
          sizeof(*allocation_private));
   if (agxw_alloc_private_validate(allocation_private,
                                   sizeof(*allocation_private)) != 0 ||
       !allocation_private->size ||
       (allocation_private->align_log2 &&
        (allocation_private->align_log2 < AGXW_PAGE_BITS ||
         allocation_private->align_log2 > AGXW_WDDM_LARGE_PAGE_BITS)))
      return false;
   return true;
}

static bool
d12_umd_open_private_data(
   const D3D12DDIARG_OPENHEAP_0003 *arguments,
   struct d12_umd_resource_private *private_data,
   struct agxw_alloc_private *allocation_private)
{
   uint64_t standard_descriptor = 0;

   if (!arguments || !private_data ||
       !d12_umd_open_allocation_read(arguments, allocation_private))
      return false;

   if (d12_umd_resource_private_parse(arguments->pPrivateDriverData,
                                      arguments->PrivateDriverDataSize,
                                      private_data)) {
      if (allocation_private->size != private_data->heap.size ||
          !!(allocation_private->flags & AGXW_ALLOC_PRIMARY) !=
             !!(private_data->heap.flags & D12_FRONTEND_HEAP_PRIMARY))
         return false;
      if (allocation_private->reserved &&
          (!(private_data->flags &
             D12_UMD_RESOURCE_PRIVATE_HAS_RESOURCE) ||
           !d12_umd_resource_private_standard_desc_pack(
              &private_data->resource, &standard_descriptor) ||
           standard_descriptor != allocation_private->reserved))
         return false;
      return true;
   }

   return d12_umd_resource_private_parse_foreign(
      arguments->pPrivateDriverData, arguments->PrivateDriverDataSize,
      allocation_private, private_data);
}

static D3D12DDI_HEAP_AND_RESOURCE_SIZES APIENTRY
d12_umd_calc_private_opened_heap_resource_sizes(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_OPENHEAP_0003 *arguments)
{
   D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes = {0};
   struct d12_umd_resource_private private_data;
   struct agxw_alloc_private allocation_private;

   if (!d12_umd_device_from_handle(handle) || !arguments ||
       !d12_umd_open_private_data(arguments, &private_data,
                                  &allocation_private))
      return sizes;
   sizes.Heap = sizeof(struct d12_umd_heap);
   sizes.Resource =
      private_data.flags & D12_UMD_RESOURCE_PRIVATE_HAS_RESOURCE
         ? sizeof(struct d12_umd_resource)
         : 0;
   return sizes;
}

static D3D12DDIARG_CREATERESOURCE_0003
d12_umd_opened_resource_desc(
   const struct d12_umd_resource_private *private_data,
   D3D12DDI_RESOURCE_STATES initial_state,
   D3D12DDIARG_ROW_MAJOR_RESOURCE_LAYOUT *row_major)
{
   const struct d12_frontend_resource_desc *frontend =
      &private_data->resource;
   D3D12DDI_RESOURCE_FLAGS_0003 flags = 0;

   if (frontend->flags & D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET)
      flags |= D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET;
   if (frontend->flags & D12_FRONTEND_RESOURCE_ALLOW_DEPTH_STENCIL)
      flags |= D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL;
   if (frontend->flags & D12_FRONTEND_RESOURCE_ALLOW_CROSS_ADAPTER)
      flags |= D3D12DDI_RESOURCE_FLAG_0003_CROSS_ADAPTER;
   if (frontend->flags & D12_FRONTEND_RESOURCE_ALLOW_SIMULTANEOUS_ACCESS)
      flags |= D3D12DDI_RESOURCE_FLAG_0003_SIMULTANEOUS_ACCESS;
   if (frontend->flags & D12_FRONTEND_RESOURCE_ALLOW_SHADER_RESOURCE)
      flags |= D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE;
   if (frontend->flags & D12_FRONTEND_RESOURCE_ALLOW_UNORDERED_ACCESS)
      flags |= D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS;

   *row_major = (D3D12DDIARG_ROW_MAJOR_RESOURCE_LAYOUT){
      .RowPitch = frontend->row_pitch,
      .SlicePitch = frontend->slice_pitch,
   };
   return (D3D12DDIARG_CREATERESOURCE_0003){
      .ResourceType = (D3D12DDI_RESOURCE_TYPE)frontend->dimension,
      .Width = frontend->width,
      .Height = frontend->height,
      .DepthOrArraySize = frontend->depth_or_array_size,
      .MipLevels = frontend->mip_levels,
      .Format = (DXGI_FORMAT)frontend->format,
      .SampleDesc = {
         .Count = frontend->sample_count,
         .Quality = frontend->sample_quality,
      },
      .Layout = (D3D12DDI_TEXTURE_LAYOUT)frontend->layout,
      .Flags = flags,
      .InitialResourceState = initial_state,
      .pRowMajorLayout =
         private_data->flags &
               D12_UMD_RESOURCE_PRIVATE_HAS_ROW_MAJOR_LAYOUT
            ? row_major
            : NULL,
   };
}

static HRESULT APIENTRY
d12_umd_open_heap_resource(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_OPENHEAP_0003 *arguments,
   D3D12DDI_HHEAP heap_handle, D3D12DDI_HRTRESOURCE runtime_resource,
   D3D12DDI_HRESOURCE resource_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_heap *heap = heap_handle.pDrvPrivate;
   struct d12_umd_resource *resource = resource_handle.pDrvPrivate;
   struct d12_umd_resource_private private_data;
   struct agxw_alloc_private allocation_private;
   struct d12_frontend_heap_allocation frontend_allocation;
   struct d12_frontend_resource_desc opened_resource;
   const struct d12_frontend_clear_value *clear_value = NULL;
   bool has_resource;
   HRESULT result;

   if (!device || !arguments || !heap || !runtime_resource.handle ||
       !d12_umd_open_private_data(arguments, &private_data,
                                  &allocation_private))
      return E_INVALIDARG;
   has_resource = !!(private_data.flags &
                     D12_UMD_RESOURCE_PRIVATE_HAS_RESOURCE);
   if ((has_resource && !resource) || (!has_resource && resource))
      return E_INVALIDARG;
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->open_heap)
      return E_NOTIMPL;

   memset(heap, 0, sizeof(*heap));
   if (resource)
      memset(resource, 0, sizeof(*resource));
   result = device->renderer_ops->open_heap(
      device->backend, &private_data.heap, runtime_resource.handle,
      arguments->pOpenAllocationInfo[0].hAllocation,
      arguments->hKMResource.handle, allocation_private.flags,
      &heap->backend, &frontend_allocation);
   if (FAILED(result))
      return result;

   heap->signature = D12_UMD_HEAP_SIGNATURE;
   heap->device = device;
   heap->allocation = (struct d12_umd_allocation){
      .allocation = frontend_allocation.allocation,
      .resource = frontend_allocation.resource,
      .gpu_virtual_address = frontend_allocation.gpu_virtual_address,
      .size = frontend_allocation.size,
   };
   heap->runtime_resource = runtime_resource.handle;
   heap->cpu_page_property =
      (D3D12DDI_CPU_PAGE_PROPERTY)private_data.heap.cpu_page_property;
   InitializeSRWLock(&heap->map_lock);

   if (has_resource) {
      if (!device->renderer_ops->create_placed_resource) {
         result = E_NOTIMPL;
         goto fail;
      }
      if (private_data.flags &
          D12_UMD_RESOURCE_PRIVATE_HAS_CLEAR_VALUE)
         clear_value = &private_data.clear_value;
      opened_resource = private_data.resource;
      opened_resource.initial_state = (uint32_t)arguments->InitialResourceState;
      result = device->renderer_ops->create_placed_resource(
         device->backend, heap->backend, 0, &opened_resource,
         clear_value, &resource->backend);
      if (FAILED(result))
         goto fail;

      resource->signature = D12_UMD_RESOURCE_SIGNATURE;
      resource->device = device;
      resource->allocation = heap->allocation;
      resource->runtime_heap_resource = heap->runtime_resource;
      resource->heap_offset = 0;
      resource->desc = d12_umd_opened_resource_desc(
         &private_data, arguments->InitialResourceState,
         &resource->row_major);
      if (resource->desc.pRowMajorLayout)
         resource->desc.pRowMajorLayout = &resource->row_major;
   }

   AcquireSRWLockExclusive(&device->heap_lock);
   heap->next = device->heaps;
   device->heaps = heap;
   ReleaseSRWLockExclusive(&device->heap_lock);
   return S_OK;

fail:
   if (resource && resource->backend && device->renderer_ops->destroy_resource)
      device->renderer_ops->destroy_resource(resource->backend);
   device->renderer_ops->destroy_heap(heap->backend);
   if (resource)
      SecureZeroMemory(resource, sizeof(*resource));
   SecureZeroMemory(heap, sizeof(*heap));
   return result;
}

static void APIENTRY
d12_umd_destroy_heap_resource(D3D12DDI_HDEVICE handle,
                              D3D12DDI_HHEAP heap_handle,
                              D3D12DDI_HRESOURCE resource_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_heap *heap = (struct d12_umd_heap *)heap_handle.pDrvPrivate;
   struct d12_umd_resource *resource =
      (struct d12_umd_resource *)resource_handle.pDrvPrivate;
   HRESULT result = S_OK;

   if (!device)
      return;
   if (resource && resource->signature == D12_UMD_RESOURCE_SIGNATURE &&
       resource->device == device) {
      if (resource->backend && device->renderer_ops &&
          device->renderer_ops->destroy_resource)
         device->renderer_ops->destroy_resource(resource->backend);
      SecureZeroMemory(resource, sizeof(*resource));
   }
   if (!heap || heap->signature != D12_UMD_HEAP_SIGNATURE ||
       heap->device != device)
      return;

   AcquireSRWLockExclusive(&device->heap_lock);
   if (device->heaps == heap) {
      device->heaps = heap->next;
   } else {
      struct d12_umd_heap *previous = device->heaps;
      while (previous && previous->next != heap)
         previous = previous->next;
      if (previous)
         previous->next = heap->next;
   }
   ReleaseSRWLockExclusive(&device->heap_lock);

   if (heap->mapping)
      (void)d12_umd_kmt_unlock(&device->kmt, heap->allocation.allocation);
   if (heap->backend && device->renderer_ops &&
       device->renderer_ops->destroy_heap) {
      device->renderer_ops->destroy_heap(heap->backend);
   } else {
      result = d12_umd_kmt_deallocate(&device->kmt, heap->runtime_resource,
                                      &heap->allocation);
   }
   if (FAILED(result))
      d12_umd_set_device_error(device, result);
   SecureZeroMemory(heap, sizeof(*heap));
}

static HRESULT APIENTRY
d12_umd_map_heap(D3D12DDI_HDEVICE handle, D3D12DDI_HHEAP heap_handle,
                 void **mapping)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_heap *heap = (struct d12_umd_heap *)heap_handle.pDrvPrivate;
   if (!device || !heap || heap->signature != D12_UMD_HEAP_SIGNATURE ||
       heap->device != device || !mapping ||
       heap->cpu_page_property == D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
      return E_INVALIDARG;
   return d12_umd_heap_retain_mapping(heap, mapping);
}

static void APIENTRY
d12_umd_unmap_heap(D3D12DDI_HDEVICE handle, D3D12DDI_HHEAP heap_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_heap *heap = (struct d12_umd_heap *)heap_handle.pDrvPrivate;
   HRESULT result;

   if (!device || !heap || heap->signature != D12_UMD_HEAP_SIGNATURE ||
       heap->device != device)
      return;
   result = d12_umd_heap_release_mapping(heap);
   if (FAILED(result))
      d12_umd_set_device_error(device, result);
}

static HRESULT
d12_umd_resolve_fence_address(struct d12_umd_device *device,
                              D3DGPU_VIRTUAL_ADDRESS address,
                              volatile uint64_t **cpu_address,
                              struct d12_umd_heap **retained_heap)
{
   struct d12_umd_heap *heap;
   HRESULT result = E_INVALIDARG;

   if (!device || !address || (address & (sizeof(uint64_t) - 1)) ||
       !cpu_address || !retained_heap)
      return E_INVALIDARG;

   AcquireSRWLockShared(&device->heap_lock);
   for (heap = device->heaps; heap; heap = heap->next) {
      const uint64_t base = heap->allocation.gpu_virtual_address;
      const uint64_t offset = address - base;
      void *mapping;

      if (address < base || offset > heap->allocation.size ||
          sizeof(uint64_t) > heap->allocation.size - offset)
         continue;
      result = d12_umd_heap_retain_mapping(heap, &mapping);
      if (SUCCEEDED(result)) {
         *cpu_address = (volatile uint64_t *)((uint8_t *)mapping + offset);
         *retained_heap = heap;
      }
      break;
   }
   ReleaseSRWLockShared(&device->heap_lock);
   return result;
}

static bool
d12_umd_fence_arguments_valid(const D3D12DDIARG_CREATE_FENCE *arguments)
{
   if (!arguments || arguments->FenceCount != 1 || !arguments->Fences)
      return false;

   const D3D12DDI_FENCE *fence = &arguments->Fences[0];
   return fence->FenceValue.BaseAddress &&
          !(fence->FenceValue.BaseAddress & (sizeof(uint64_t) - 1)) &&
          fence->FenceMonitoredValue.BaseAddress &&
          !(fence->FenceMonitoredValue.BaseAddress &
            (sizeof(uint64_t) - 1)) &&
          !((UINT)fence->Flags & ~(UINT)D3D12DDI_FENCE_FLAG_BOTTOM_OF_PIPE);
}

static SIZE_T APIENTRY
d12_umd_calc_private_fence_size(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_FENCE *arguments)
{
   if (!d12_umd_device_from_handle(handle) ||
       !d12_umd_fence_arguments_valid(arguments))
      return 0;
   return offsetof(struct d12_umd_fence, entries) +
          sizeof(struct d12_umd_fence_entry);
}

static HRESULT APIENTRY
d12_umd_create_fence(D3D12DDI_HDEVICE handle, D3D12DDI_HFENCE fence_handle,
                     const D3D12DDIARG_CREATE_FENCE *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_fence *fence = fence_handle.pDrvPrivate;
   HRESULT result;

   if (!device || !fence || !d12_umd_fence_arguments_valid(arguments))
      return E_INVALIDARG;
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->create_fence)
      return E_NOTIMPL;

   memset(fence, 0, offsetof(struct d12_umd_fence, entries) +
                            sizeof(struct d12_umd_fence_entry));
   fence->device = device;
   fence->count = 1;
   fence->entries[0].placement.current_gpu_address =
      arguments->Fences[0].FenceValue.BaseAddress;
   fence->entries[0].placement.monitored_gpu_address =
      arguments->Fences[0].FenceMonitoredValue.BaseAddress;
   fence->entries[0].placement.flags = (uint32_t)arguments->Fences[0].Flags;

   result = d12_umd_resolve_fence_address(
      device, arguments->Fences[0].FenceValue.BaseAddress,
      &fence->entries[0].placement.current_cpu_address,
      &fence->entries[0].current_heap);
   if (FAILED(result))
      goto fail;

   result = device->renderer_ops->create_fence(
      device->backend, fence->count, &fence->entries[0].placement,
      &fence->backend);
   if (FAILED(result)) {
      (void)d12_umd_heap_release_mapping(fence->entries[0].current_heap);
      goto fail;
   }
   fence->signature = D12_UMD_FENCE_SIGNATURE;
   return S_OK;

fail:
   SecureZeroMemory(fence, offsetof(struct d12_umd_fence, entries) +
                              sizeof(struct d12_umd_fence_entry));
   return result;
}

static void APIENTRY
d12_umd_destroy_fence(D3D12DDI_HDEVICE handle,
                      D3D12DDI_HFENCE fence_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_fence *fence = d12_umd_fence_from_handle(fence_handle);
   const SIZE_T size = offsetof(struct d12_umd_fence, entries) +
                       sizeof(struct d12_umd_fence_entry);

   if (!device || !fence || fence->device != device)
      return;
   if (fence->backend && device->renderer_ops &&
       device->renderer_ops->destroy_fence)
      device->renderer_ops->destroy_fence(fence->backend);
   if (fence->entries[0].current_heap) {
      HRESULT result =
         d12_umd_heap_release_mapping(fence->entries[0].current_heap);
      if (FAILED(result))
         d12_umd_set_device_error(device, result);
   }
   SecureZeroMemory(fence, size);
}

static void APIENTRY
d12_umd_signal_fence(D3D12DDI_HCOMMANDQUEUE queue_handle,
                     D3D12DDIARG_FENCE_OPERATION *arguments)
{
   struct d12_umd_command_queue *queue = queue_handle.pDrvPrivate;
   struct d12_umd_fence *fence;
   HRESULT result;

   if (!queue || queue->signature != D12_UMD_COMMAND_QUEUE_SIGNATURE ||
       !arguments)
      return;
   arguments->PhysicalAdapterMask = 0;
   fence = d12_umd_fence_from_handle(arguments->Fence);
   if (!fence || fence->device != queue->device || !fence->backend ||
       !queue->backend || !queue->device->renderer_ops ||
       !queue->device->renderer_ops->signal_fence) {
      d12_umd_set_device_error(queue->device, E_INVALIDARG);
      return;
   }
   result = queue->device->renderer_ops->signal_fence(
      queue->backend, fence->backend, arguments->Value);
   if (SUCCEEDED(result))
      arguments->PhysicalAdapterMask = 1;
   else
      d12_umd_set_device_error(queue->device, result);
}

static void APIENTRY
d12_umd_wait_for_fence(D3D12DDI_HCOMMANDQUEUE queue_handle,
                       D3D12DDIARG_FENCE_OPERATION *arguments)
{
   struct d12_umd_command_queue *queue = queue_handle.pDrvPrivate;
   struct d12_umd_fence *fence;
   HRESULT result;

   if (!queue || queue->signature != D12_UMD_COMMAND_QUEUE_SIGNATURE ||
       !arguments)
      return;
   arguments->PhysicalAdapterMask = 0;
   fence = d12_umd_fence_from_handle(arguments->Fence);
   if (!fence || fence->device != queue->device || !fence->backend ||
       !queue->backend || !queue->device->renderer_ops ||
       !queue->device->renderer_ops->wait_fence) {
      d12_umd_set_device_error(queue->device, E_INVALIDARG);
      return;
   }
   result = queue->device->renderer_ops->wait_fence(
      queue->backend, fence->backend, arguments->Value);
   if (SUCCEEDED(result))
      arguments->PhysicalAdapterMask = 1;
   else
      d12_umd_set_device_error(queue->device, result);
}

static D3D12DDI_GPU_VIRTUAL_ADDRESS APIENTRY
d12_umd_check_resource_virtual_address(D3D12DDI_HDEVICE handle,
                                       D3D12DDI_HRESOURCE resource_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_resource *resource =
      (struct d12_umd_resource *)resource_handle.pDrvPrivate;

   if (!device || !resource ||
       resource->signature != D12_UMD_RESOURCE_SIGNATURE ||
       resource->device != device)
      return 0;
   if (resource->backend && device->renderer_ops &&
       device->renderer_ops->resource_gpu_virtual_address)
      return device->renderer_ops->resource_gpu_virtual_address(
         resource->backend);
   if (!resource->allocation.gpu_virtual_address ||
       resource->allocation.gpu_virtual_address >
          UINT64_MAX - resource->heap_offset)
      return 0;
   return resource->allocation.gpu_virtual_address + resource->heap_offset;
}

static void APIENTRY
d12_umd_check_resource_allocation_info(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATERESOURCE_0003 *resource_arguments,
   D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS optimization_flags,
   UINT64 alignment_restriction, UINT visible_node_mask,
   D3D12DDI_RESOURCE_ALLOCATION_INFO *info)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_frontend_resource_desc frontend;
   struct d12_frontend_resource_allocation_info allocation_info;
   HRESULT result;

   const UINT supported_optimization_flags =
      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_SHADER_RESOURCE |
      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_UNORDERED_ACCESS |
      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_PRIMARY |
      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_DETERMINISTIC;
   if ((UINT)optimization_flags & ~supported_optimization_flags) {
      d12_umd_set_device_error(device, E_INVALIDARG);
      return;
   }
   (void)alignment_restriction;
   if (!device || !resource_arguments || !info ||
       !d12_umd_resource_arguments_valid(resource_arguments) ||
       (visible_node_mask != 0 && visible_node_mask != 1)) {
      d12_umd_set_device_error(device, E_INVALIDARG);
      return;
   }

   if (device->renderer_ops && device->backend &&
       device->renderer_ops->get_resource_allocation_info) {
      frontend = d12_umd_frontend_resource_desc(resource_arguments);
      frontend.alignment = alignment_restriction;
      result = device->renderer_ops->get_resource_allocation_info(
         device->backend, &frontend, visible_node_mask, &allocation_info);
      if (FAILED(result)) {
         d12_umd_set_device_error(device, result);
         return;
      }
      *info = (D3D12DDI_RESOURCE_ALLOCATION_INFO){
         .ResourceDataSize = allocation_info.size,
         .ResourceDataAlignment = allocation_info.alignment,
         .AdditionalDataHeaderAlignment = 1,
         .AdditionalDataAlignment = 1,
         .Layout = (D3D12DDI_TEXTURE_LAYOUT)allocation_info.layout,
      };
      return;
   }

   *info = (D3D12DDI_RESOURCE_ALLOCATION_INFO){
      .ResourceDataSize = resource_arguments->Width,
      .ResourceDataAlignment = D12_UMD_DEFAULT_PLACEMENT_ALIGNMENT,
      .AdditionalDataHeaderAlignment = 1,
      .AdditionalDataAlignment = 1,
      .Layout = D3D12DDI_TL_ROW_MAJOR,
   };
}

static void APIENTRY
d12_umd_check_subresource_info(D3D12DDI_HDEVICE handle,
                               D3D12DDI_HRESOURCE resource_handle,
                               UINT subresource,
                               D3D12DDI_SUBRESOURCE_INFO *info)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_resource *resource = resource_handle.pDrvPrivate;
   struct d12_frontend_subresource_info frontend;
   HRESULT result = E_INVALIDARG;

   if (info)
      memset(info, 0, sizeof(*info));
   if (!device || !resource || !info ||
       resource->signature != D12_UMD_RESOURCE_SIGNATURE ||
       resource->device != device)
      goto fail;

   if (resource->backend && device->renderer_ops &&
       device->renderer_ops->resource_subresource_info) {
      result = device->renderer_ops->resource_subresource_info(
         resource->backend, subresource, &frontend);
      if (FAILED(result))
         goto fail;
      *info = (D3D12DDI_SUBRESOURCE_INFO){
         .Offset = frontend.offset,
         .RowStride = frontend.row_stride,
         .DepthStride = frontend.depth_stride,
         .RowBytePreSwizzleOffset = frontend.row_byte_pre_swizzle_offset,
         .ColumnPreSwizzleOffset = frontend.column_pre_swizzle_offset,
         .DepthPreSwizzleOffset = frontend.depth_pre_swizzle_offset,
      };
      return;
   }

   if (subresource || resource->desc.ResourceType != D3D12DDI_RT_BUFFER) {
      result = E_NOTIMPL;
      goto fail;
   }
   info->RowStride = resource->desc.Width;
   info->DepthStride = resource->desc.Width;
   return;

fail:
   d12_umd_set_device_error(device, result);
}

static void APIENTRY
d12_umd_check_existing_resource_allocation_info(
   D3D12DDI_HDEVICE handle, D3D12DDI_HRESOURCE resource_handle,
   D3D12DDI_RESOURCE_ALLOCATION_INFO *info, D3DKMT_HANDLE *allocation)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_resource *resource = resource_handle.pDrvPrivate;

   if (info)
      memset(info, 0, sizeof(*info));
   if (allocation)
      *allocation = 0;
   if (!device || !resource || !info || !allocation ||
       resource->signature != D12_UMD_RESOURCE_SIGNATURE ||
       resource->device != device || !resource->allocation.allocation) {
      d12_umd_set_device_error(device, E_INVALIDARG);
      return;
   }

   d12_umd_check_resource_allocation_info(
      handle, &resource->desc, D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_NONE, 0,
      1, info);
   *allocation = resource->allocation.allocation;
}

static D3DKMT_HANDLE APIENTRY
d12_umd_check_resource_allocation_handle(
   D3D12DDI_HDEVICE handle, D3D10DDI_HRESOURCE resource_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_resource *resource = resource_handle.pDrvPrivate;

   if (!device || !resource ||
       resource->signature != D12_UMD_RESOURCE_SIGNATURE ||
       resource->device != device || !resource->allocation.allocation)
      return 0;
   return resource->allocation.allocation;
}

static bool
d12_umd_residency_object_allocation(
   struct d12_umd_device *device,
   const D3D12DDI_HANDLE_AND_TYPE *object,
   D3DKMT_HANDLE *allocation)
{
   struct d12_umd_heap *heap;
   struct d12_umd_resource *resource;

   if (!device || !object || !object->Handle || !allocation)
      return false;
   *allocation = 0;

   switch (object->Type) {
   case D3D12DDI_HT_HEAP:
      heap = (struct d12_umd_heap *)object->Handle;
      if (heap->signature != D12_UMD_HEAP_SIGNATURE ||
          heap->device != device || !heap->allocation.allocation)
         return false;
      *allocation = heap->allocation.allocation;
      return true;

   case D3D12DDI_HT_0012_RESOURCE:
      resource = (struct d12_umd_resource *)object->Handle;
      if (resource->signature != D12_UMD_RESOURCE_SIGNATURE ||
          resource->device != device || !resource->allocation.allocation)
         return false;
      *allocation = resource->allocation.allocation;
      return true;

   /* These public objects are ID3D12Pageable, but have no runtime-owned
    * D3DKMT allocation in the AGX frontend.  Their CPU metadata is not
    * pageable and renderer-internal BOs (shader code, descriptor metadata,
    * query bookkeeping) are created through the winsys' internal-allocation
    * path, which keeps them resident independently of application objects.
    * Validate ownership, then deliberately contribute no allocation to the
    * runtime residency callback. */
   case D3D12DDI_HT_COMMAND_QUEUE: {
      const struct d12_umd_command_queue *queue = object->Handle;
      return queue->signature == D12_UMD_COMMAND_QUEUE_SIGNATURE &&
             queue->device == device;
   }
   case D3D12DDI_HT_PIPELINE_STATE: {
      const struct d12_umd_pipeline_state *pipeline = object->Handle;
      return pipeline->signature == D12_UMD_PIPELINE_STATE_SIGNATURE &&
             pipeline->device == device;
   }
   case D3D12DDI_HT_FENCE: {
      const struct d12_umd_fence *fence = object->Handle;
      return fence->signature == D12_UMD_FENCE_SIGNATURE &&
             fence->device == device;
   }
   case D3D12DDI_HT_DESCRIPTOR_HEAP: {
      const struct d12_umd_descriptor_heap *descriptor_heap = object->Handle;
      return descriptor_heap->signature ==
                D12_UMD_DESCRIPTOR_HEAP_SIGNATURE &&
             descriptor_heap->device == device;
   }
   case D3D12DDI_HT_QUERY_HEAP: {
      const struct d12_umd_query_heap *query_heap = object->Handle;
      return query_heap->signature == D12_UMD_QUERY_HEAP_SIGNATURE &&
             query_heap->device == device;
   }
   case D3D12DDI_HT_COMMAND_SIGNATURE: {
      const struct d12_umd_command_signature *signature = object->Handle;
      return signature->signature == D12_UMD_COMMAND_SIGNATURE_SIGNATURE &&
             signature->device == device;
   }
   case D3D12DDI_HT_0010_PIPELINE_LIBRARY: {
      const struct d12_umd_pipeline_library *library = object->Handle;
      return library->signature == D12_UMD_PIPELINE_LIBRARY_SIGNATURE &&
             library->device == device;
   }
   default:
      return false;
   }
}

struct d12_umd_residency_translation {
   D3DKMT_HANDLE *allocations;
   UINT *object_indices;
   UINT allocation_count;
   D3DKMT_HANDLE stack_allocations[16];
   UINT stack_object_indices[16];
};

#if D12_UMD_TARGETS_WDDM21
static D3DKMT_HANDLE
d12_umd_residency_paging_queue(
   struct d12_umd_device *device,
   D3D12DDI_HRTPAGINGQUEUE runtime_paging_queue,
   bool *queue_lock_held)
{
   struct d12_umd_command_queue *queue;
   D3DKMT_HANDLE paging_queue;

   if (!device || !queue_lock_held)
      return 0;
   *queue_lock_held = false;
   if (device->backend && device->renderer_ops &&
       device->renderer_ops->paging_queue) {
      return device->renderer_ops->paging_queue(device->backend);
   }

   AcquireSRWLockShared(&device->queue_lock);
   for (queue = device->queues; queue; queue = queue->next) {
      if (queue->runtime.runtime_queue.handle ==
             runtime_paging_queue.handle &&
          queue->runtime.paging_queue) {
         paging_queue = queue->runtime.paging_queue;
         *queue_lock_held = true;
         return paging_queue;
      }
   }
   ReleaseSRWLockShared(&device->queue_lock);
   return 0;
}

static void
d12_umd_residency_release_queue_lock(struct d12_umd_device *device,
                                     bool queue_lock_held)
{
   if (device && queue_lock_held)
      ReleaseSRWLockShared(&device->queue_lock);
}
#endif

static void
d12_umd_residency_translation_cleanup(
   struct d12_umd_residency_translation *translation)
{
   if (!translation)
      return;
   if (translation->object_indices &&
       translation->object_indices != translation->stack_object_indices)
      HeapFree(GetProcessHeap(), 0, translation->object_indices);
   if (translation->allocations &&
       translation->allocations != translation->stack_allocations)
      HeapFree(GetProcessHeap(), 0, translation->allocations);
   translation->allocations = NULL;
   translation->object_indices = NULL;
   translation->allocation_count = 0;
}

static HRESULT
d12_umd_translate_residency_objects(
   struct d12_umd_device *device, UINT count,
   const D3D12DDI_HANDLE_AND_TYPE *objects,
   struct d12_umd_residency_translation *translation)
{
   if (!device || !translation || (count && !objects))
      return E_INVALIDARG;
   *translation = (struct d12_umd_residency_translation){0};
   translation->allocations = translation->stack_allocations;
   translation->object_indices = translation->stack_object_indices;
   if (!count)
      return S_OK;

   if (count > ARRAYSIZE(translation->stack_allocations)) {
      translation->allocations = HeapAlloc(
         GetProcessHeap(), 0,
         (size_t)count * sizeof(*translation->allocations));
      translation->object_indices = HeapAlloc(
         GetProcessHeap(), 0,
         (size_t)count * sizeof(*translation->object_indices));
      if (!translation->allocations || !translation->object_indices) {
         d12_umd_residency_translation_cleanup(translation);
         return E_OUTOFMEMORY;
      }
   }

   for (UINT index = 0; index < count; ++index) {
      D3DKMT_HANDLE allocation;
      if (!d12_umd_residency_object_allocation(device, &objects[index],
                                               &allocation)) {
         d12_umd_residency_translation_cleanup(translation);
         return E_INVALIDARG;
      }
      if (allocation) {
         const UINT output = translation->allocation_count++;
         translation->allocations[output] = allocation;
         translation->object_indices[output] = index;
      }
   }
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_make_resident(D3D12DDI_HDEVICE handle,
                      D3D12DDIARG_MAKERESIDENT_0001 *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_residency_translation translation;
   D3DDDI_MAKERESIDENT make_resident;
   HRESULT result;

   if (!device || !arguments || arguments->NumAdapters != 1 ||
       !arguments->pRTPagingQueue ||
       !arguments->pRTPagingQueue[0].handle || !arguments->pPagingFenceValue)
      return E_INVALIDARG;

   arguments->WaitMask = 0;
   arguments->pPagingFenceValue[0] = 0;
   result = d12_umd_translate_residency_objects(
      device, arguments->NumObjects, arguments->pObjects, &translation);
   if (FAILED(result))
      return result;
   if (!translation.allocation_count) {
      d12_umd_residency_translation_cleanup(&translation);
      return S_OK;
   }
   if (!device->core_callbacks.pfnMakeResidentCb) {
      d12_umd_residency_translation_cleanup(&translation);
      return E_INVALIDARG;
   }

   make_resident = (D3DDDI_MAKERESIDENT){
      .NumAllocations = translation.allocation_count,
      .AllocationList = translation.allocations,
      .Flags = arguments->Flags,
   };
   result = device->core_callbacks.pfnMakeResidentCb(
      device->runtime_device, arguments->pRTPagingQueue[0], &make_resident);
   if (result == E_PENDING) {
      arguments->pPagingFenceValue[0] = make_resident.PagingFenceValue;
      arguments->WaitMask = 1;
   }
   d12_umd_residency_translation_cleanup(&translation);
   return result;
}

static HRESULT APIENTRY
d12_umd_evict(D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_EVICT *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_residency_translation translation;
   D3DDDICB_EVICT evict;
   HRESULT result;

   if (!device || !arguments)
      return E_INVALIDARG;
   result = d12_umd_translate_residency_objects(
      device, arguments->NumObjects, arguments->pObjects, &translation);
   if (FAILED(result))
      return result;
   if (!translation.allocation_count) {
      d12_umd_residency_translation_cleanup(&translation);
      return S_OK;
   }
   if (!device->core_callbacks.pfnEvictCb) {
      d12_umd_residency_translation_cleanup(&translation);
      return E_INVALIDARG;
   }

   evict = (D3DDDICB_EVICT){
      .NumAllocations = translation.allocation_count,
      .AllocationList = translation.allocations,
      .Flags = arguments->Flags,
   };
   result = device->core_callbacks.pfnEvictCb(device->runtime_device, &evict);
   d12_umd_residency_translation_cleanup(&translation);
   return result;
}

static HRESULT APIENTRY
d12_umd_offer_resources(D3D12DDI_HDEVICE handle,
                        const D3D12DDIARG_OFFERRESOURCES *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_residency_translation translation;
   HRESULT result;

   if (!device || !arguments ||
       (UINT)arguments->Priority > (UINT)D3DDDI_OFFER_PRIORITY_AUTO)
      return E_INVALIDARG;
   result = d12_umd_translate_residency_objects(
      device, arguments->NumObjects, arguments->pObjects, &translation);
   if (FAILED(result))
      return result;
   if (!translation.allocation_count) {
      d12_umd_residency_translation_cleanup(&translation);
      return S_OK;
   }
#if D12_UMD_TARGETS_WDDM21
   if (!device->kernel_callbacks.pfnOfferAllocations2Cb) {
      d12_umd_residency_translation_cleanup(&translation);
      return E_INVALIDARG;
   }
   {
      const D3DDDICB_OFFERALLOCATIONS2 offer = {
         .HandleList = translation.allocations,
         .NumAllocations = translation.allocation_count,
         .Priority = arguments->Priority,
         .Flags = {.Value = 0},
      };
      result = device->kernel_callbacks.pfnOfferAllocations2Cb(
         device->runtime_device.handle, &offer);
   }
#else
   if (!device->core_callbacks.pfnOfferAllocationsCb) {
      d12_umd_residency_translation_cleanup(&translation);
      return E_INVALIDARG;
   }
   {
      const D3D12DDICB_OFFERALLOCATIONS offer = {
      .NumAllocations = translation.allocation_count,
      .HandleList = translation.allocations,
      .Priority = arguments->Priority,
      };
      result = device->core_callbacks.pfnOfferAllocationsCb(
         device->runtime_device, &offer);
   }
#endif
   d12_umd_residency_translation_cleanup(&translation);
   return result;
}

static HRESULT APIENTRY
d12_umd_reclaim_resources(D3D12DDI_HDEVICE handle,
                          D3D12DDIARG_RECLAIMRESOURCES_0001 *arguments)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_residency_translation translation;
#if D12_UMD_TARGETS_WDDM21
   D3DDDICB_RECLAIMALLOCATIONS3 reclaim;
   D3DDDI_RECLAIM_RESULT stack_results[16];
   D3DDDI_RECLAIM_RESULT *allocation_results = stack_results;
   D3DKMT_HANDLE paging_queue;
   bool queue_lock_held;
#else
   D3D12DDICB_RECLAIMALLOCATIONS2 reclaim;
   BOOL stack_discarded[16];
   BOOL *allocation_discarded = stack_discarded;
#endif
   HRESULT result;

   if (!device || !arguments || arguments->NumAdapters != 1 ||
       !arguments->pRTPagingQueue ||
       !arguments->pRTPagingQueue[0].handle ||
       !arguments->pPagingFenceValue ||
       (arguments->NumObjects && !arguments->pDiscarded))
      return E_INVALIDARG;

   arguments->WaitMask = 0;
   arguments->pPagingFenceValue[0] = 0;
   if (arguments->NumObjects)
      memset(arguments->pDiscarded, 0,
             (size_t)arguments->NumObjects * sizeof(*arguments->pDiscarded));
   result = d12_umd_translate_residency_objects(
      device, arguments->NumObjects, arguments->pObjects, &translation);
   if (FAILED(result))
      return result;
   if (!translation.allocation_count) {
      d12_umd_residency_translation_cleanup(&translation);
      return S_OK;
   }
#if D12_UMD_TARGETS_WDDM21
   if (!device->kernel_callbacks.pfnReclaimAllocations3Cb) {
      d12_umd_residency_translation_cleanup(&translation);
      return E_INVALIDARG;
   }
   paging_queue = d12_umd_residency_paging_queue(
      device, arguments->pRTPagingQueue[0], &queue_lock_held);
   if (!paging_queue) {
      d12_umd_residency_translation_cleanup(&translation);
      return E_INVALIDARG;
   }
   memset(stack_results, 0, sizeof(stack_results));
   if (translation.allocation_count > ARRAYSIZE(stack_results)) {
      allocation_results = HeapAlloc(
         GetProcessHeap(), HEAP_ZERO_MEMORY,
         (size_t)translation.allocation_count * sizeof(*allocation_results));
      if (!allocation_results) {
         d12_umd_residency_release_queue_lock(device, queue_lock_held);
         d12_umd_residency_translation_cleanup(&translation);
         return E_OUTOFMEMORY;
      }
   }
   reclaim = (D3DDDICB_RECLAIMALLOCATIONS3){
      .PagingQueue = paging_queue,
      .NumAllocations = translation.allocation_count,
      .HandleList = translation.allocations,
      .pResults = allocation_results,
   };
   result = device->kernel_callbacks.pfnReclaimAllocations3Cb(
      device->runtime_device.handle, &reclaim);
   d12_umd_residency_release_queue_lock(device, queue_lock_held);
   if (SUCCEEDED(result) && reclaim.PagingFenceValue) {
      arguments->pPagingFenceValue[0] = reclaim.PagingFenceValue;
      arguments->WaitMask = 1;
   }
   for (UINT index = 0; index < translation.allocation_count; ++index) {
      arguments->pDiscarded[translation.object_indices[index]] =
         allocation_results[index] != D3DDDI_RECLAIM_RESULT_OK;
   }
   if (allocation_results != stack_results)
      HeapFree(GetProcessHeap(), 0, allocation_results);
#else
   if (!device->core_callbacks.pfnReclaimAllocations2Cb) {
      d12_umd_residency_translation_cleanup(&translation);
      return E_INVALIDARG;
   }
   memset(stack_discarded, 0, sizeof(stack_discarded));
   if (translation.allocation_count > ARRAYSIZE(stack_discarded)) {
      allocation_discarded = HeapAlloc(
         GetProcessHeap(), HEAP_ZERO_MEMORY,
         (size_t)translation.allocation_count *
            sizeof(*allocation_discarded));
      if (!allocation_discarded) {
         d12_umd_residency_translation_cleanup(&translation);
         return E_OUTOFMEMORY;
      }
   }

   reclaim = (D3D12DDICB_RECLAIMALLOCATIONS2){
      .NumAllocations = translation.allocation_count,
      .HandleList = translation.allocations,
      .pDiscarded = allocation_discarded,
   };
   result = device->core_callbacks.pfnReclaimAllocations2Cb(
      device->runtime_device, arguments->pRTPagingQueue[0], &reclaim);
   if (result == E_PENDING) {
      arguments->pPagingFenceValue[0] = reclaim.PagingFenceValue;
      arguments->WaitMask = 1;
   }
   for (UINT index = 0; index < translation.allocation_count; ++index)
      arguments->pDiscarded[translation.object_indices[index]] =
         allocation_discarded[index];
   if (allocation_discarded != stack_discarded)
      HeapFree(GetProcessHeap(), 0, allocation_discarded);
#endif
   d12_umd_residency_translation_cleanup(&translation);
   return result;
}

void
d12_umd_install_device_core_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table)
{
   table->pfnCheckFormatSupport = d12_umd_check_format_support;
   table->pfnCheckMultisampleQualityLevels =
      d12_umd_check_multisample_quality_levels;
   table->pfnGetMipPacking = d12_umd_get_mip_packing;
   table->pfnCalcPrivateCommandQueueSize =
      d12_umd_calc_private_command_queue_size;
   table->pfnCreateCommandQueue = d12_umd_create_command_queue;
   table->pfnDestroyCommandQueue = d12_umd_destroy_command_queue;
   table->pfnMapHeap = d12_umd_map_heap;
   table->pfnUnmapHeap = d12_umd_unmap_heap;
   table->pfnCalcPrivateHeapAndResourceSizes =
      d12_umd_calc_private_heap_resource_sizes;
   table->pfnCreateHeapAndResource = d12_umd_create_heap_resource;
   table->pfnDestroyHeapAndResource = d12_umd_destroy_heap_resource;
   table->pfnCalcPrivateOpenedHeapAndResourceSizes =
      d12_umd_calc_private_opened_heap_resource_sizes;
   table->pfnOpenHeapAndResource = d12_umd_open_heap_resource;
   table->pfnCheckResourceVirtualAddress =
      d12_umd_check_resource_virtual_address;
   table->pfnCheckResourceAllocationInfo =
      d12_umd_check_resource_allocation_info;
   table->pfnCheckSubresourceInfo = d12_umd_check_subresource_info;
   table->pfnCheckExistingResourceAllocationInfo =
      d12_umd_check_existing_resource_allocation_info;
   table->pfnCheckResourceAllocationHandle =
      d12_umd_check_resource_allocation_handle;
   table->pfnCalcPrivateFenceSize = d12_umd_calc_private_fence_size;
   table->pfnCreateFence = d12_umd_create_fence;
   table->pfnDestroyFence = d12_umd_destroy_fence;
   table->pfnMakeResident = d12_umd_make_resident;
   table->pfnEvict = d12_umd_evict;
   table->pfnOfferResources = d12_umd_offer_resources;
   table->pfnReclaimResources = d12_umd_reclaim_resources;
   table->pfnGetImplicitPhysicalAdapterMask =
      d12_umd_get_implicit_physical_adapter_mask;
   table->pfnQueryNodeMap = d12_umd_query_node_map;
}

void
d12_umd_install_command_queue_functions(
   D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *table)
{
   table->pfnExecuteCommandLists = d12_umd_execute_command_lists;
   table->pfnUpdateTileMappings = d12_umd_update_tile_mappings;
   table->pfnCopyTileMappings = d12_umd_copy_tile_mappings;
   table->pfnSignalFence = d12_umd_signal_fence;
   table->pfnWaitForFence = d12_umd_wait_for_fence;
}
