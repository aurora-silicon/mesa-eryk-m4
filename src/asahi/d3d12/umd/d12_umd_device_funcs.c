/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Implemented baseline device-table entries.  These functions translate the
 * D3D12 DDI queue object into the existing AppleAgxWddm context, paging and VA
 * contracts exclusively through callbacks supplied by the runtime.
 */

#include "d12_umd_device_funcs.h"

#include "d12_umd_sync.h"

#include "d12_umd_internal.h"
#include "../d12_command_validation_core.h"
#include "d12_umd_residency.h"
#include "d12_umd_resource_private.h"
#include "../d12_format_caps_core.h"
#include "../d12_present_format_core.h"
#include "../d12_resolve_core.h"

#include <stdbool.h>
#include <string.h>

#define D12_UMD_DEFAULT_PLACEMENT_ALIGNMENT UINT64_C(0x10000)

#define D12_LOGIC_OP_FORMAT_ABI(name, value)                               \
   _Static_assert((uint32_t)DXGI_FORMAT_##name ==                           \
                     (uint32_t)D12_FORMAT_CORE_##name,                      \
                  "DXGI/core logic-op format ABI drift");
D12_FORMAT_CORE_LOGIC_OP_LIST(D12_LOGIC_OP_FORMAT_ABI)
#undef D12_LOGIC_OP_FORMAT_ABI

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
                          D12_FRONTEND_FORMAT_MULTISAMPLE_RENDER_TARGET |
                          D12_FRONTEND_FORMAT_MULTISAMPLE_LOAD |
                          D12_FRONTEND_FORMAT_TYPED_UAV |
                          D12_FRONTEND_FORMAT_UAV_TYPED_LOAD |
                          D12_FRONTEND_FORMAT_UAV_TYPED_STORE;
   const uint64_t resolve = d12_resolve_core_supports_average(format)
                               ? D12_FRONTEND_FORMAT_MULTISAMPLE_RESOLVE
                               : 0;
   const uint64_t blendable_color =
      color | D12_FRONTEND_FORMAT_BLENDABLE | resolve;
   uint64_t support;

   switch (format) {
   /* Microsoft defines these 15 formats as one all-or-nothing typed-UAV-load
    * group. The renderer format table supports the same set; keep the
    * callback-only ABI probe build in lockstep with it. */
   case DXGI_FORMAT_R32G32B32A32_FLOAT:
   case DXGI_FORMAT_R32G32B32A32_UINT:
   case DXGI_FORMAT_R32G32B32A32_SINT:
   case DXGI_FORMAT_R16G16B16A16_FLOAT:
   case DXGI_FORMAT_R16G16B16A16_UINT:
   case DXGI_FORMAT_R16G16B16A16_SINT:
   case DXGI_FORMAT_R8G8B8A8_UINT:
   case DXGI_FORMAT_R8G8B8A8_SINT:
   case DXGI_FORMAT_R16_FLOAT:
   case DXGI_FORMAT_R16_SINT:
   case DXGI_FORMAT_R8_UNORM:
   case DXGI_FORMAT_R8_UINT:
   case DXGI_FORMAT_R8_SINT:
      support = color;
      break;
   case DXGI_FORMAT_R8G8B8A8_UNORM:
      support = blendable_color;
      break;
   case DXGI_FORMAT_B8G8R8A8_UNORM:
   case DXGI_FORMAT_B8G8R8X8_UNORM:
      support = texture | D12_FRONTEND_FORMAT_RENDER_TARGET |
                D12_FRONTEND_FORMAT_BLENDABLE | resolve |
                D12_FRONTEND_FORMAT_MULTISAMPLE_RENDER_TARGET |
                D12_FRONTEND_FORMAT_MULTISAMPLE_LOAD;
      break;
   case DXGI_FORMAT_R32_FLOAT:
      support = blendable_color;
      break;
   case DXGI_FORMAT_R32_SINT:
      support = color;
      break;
   case DXGI_FORMAT_R32_UINT:
   case DXGI_FORMAT_R16_UINT:
      support = color | resolve | D12_FRONTEND_FORMAT_INDEX_BUFFER;
      break;
   case DXGI_FORMAT_D32_FLOAT:
   case DXGI_FORMAT_D16_UNORM:
      support = D12_FRONTEND_FORMAT_TEXTURE_1D |
                D12_FRONTEND_FORMAT_TEXTURE_2D |
                D12_FRONTEND_FORMAT_TEXTURE_CUBE |
                D12_FRONTEND_FORMAT_SHADER_LOAD |
                D12_FRONTEND_FORMAT_SHADER_SAMPLE |
                D12_FRONTEND_FORMAT_SHADER_SAMPLE_COMPARISON |
                D12_FRONTEND_FORMAT_SHADER_GATHER_COMPARISON |
                D12_FRONTEND_FORMAT_MIP |
                D12_FRONTEND_FORMAT_DEPTH_STENCIL |
                D12_FRONTEND_FORMAT_MULTISAMPLE_LOAD;
      break;
   default:
      support = format == DXGI_FORMAT_UNKNOWN ? D12_FRONTEND_FORMAT_BUFFER
                                               : 0;
      break;
   }

   /* The callback-only DLL has no Mesa format database, but its DDI table
    * must still expose the same display-format ABI as the full renderer so
    * clean-environment probes cannot validate a narrower contract. */
   if (d12_present_format_core_supported((uint32_t)format))
      support |= D12_FRONTEND_FORMAT_DISPLAY;
   return support;
}

static UINT
d12_umd_translate_format_support(DXGI_FORMAT format, uint64_t support)
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
   if (d12_format_core_supports_output_merger_logic_op((uint32_t)format))
      result |= D3D12DDI_FORMAT_SUPPORT_OUTPUT_MERGER_LOGIC_OP;
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
   *out_support = d12_umd_translate_format_support(format, support);
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
   else if (sample_count != 1 && d12_sample_count_supported(sample_count) &&
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
d12_umd_heap_storage_create(struct d12_umd_heap *heap)
{
   struct d12_umd_heap_storage *storage;

   if (!heap || !heap->device || heap->storage ||
       !heap->allocation.allocation ||
       (heap->backend &&
        (!heap->device->renderer_ops ||
         !heap->device->renderer_ops->destroy_heap)))
      return E_INVALIDARG;
   storage = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*storage));
   if (!storage)
      return E_OUTOFMEMORY;
   storage->device = heap->device;
   storage->backend = heap->backend;
   storage->destroy_backend =
      heap->backend ? heap->device->renderer_ops->destroy_heap : NULL;
   storage->allocation = heap->allocation;
   storage->runtime_resource = heap->runtime_resource;
   storage->cpu_page_property = heap->cpu_page_property;
   InitializeSRWLock(&storage->lock);

   d12_umd_sync_acquire_exclusive(&heap->device->heap_storage_lock);
   if (InterlockedCompareExchange(&heap->device->destroying, 0, 0)) {
      ReleaseSRWLockExclusive(&heap->device->heap_storage_lock);
      SecureZeroMemory(storage, sizeof(*storage));
      HeapFree(GetProcessHeap(), 0, storage);
      return DXGI_ERROR_DEVICE_REMOVED;
   }
   storage->next = heap->device->heap_storages;
   heap->device->heap_storages = storage;
   ++heap->device->heap_storage_count;
   ReleaseSRWLockExclusive(&heap->device->heap_storage_lock);
   heap->storage = storage;
   return S_OK;
}

static HRESULT
d12_umd_heap_storage_finalize(struct d12_umd_heap_storage *storage)
{
   struct d12_umd_device *device;
   HRESULT result = S_OK;

   if (!storage || !(device = storage->device))
      return E_INVALIDARG;

   d12_umd_sync_acquire_exclusive(&device->heap_storage_lock);
   {
      struct d12_umd_heap_storage **link = &device->heap_storages;
      while (*link && *link != storage)
         link = &(*link)->next;
      if (*link == storage) {
         *link = storage->next;
         if (device->heap_storage_count)
            --device->heap_storage_count;
      }
   }
   ReleaseSRWLockExclusive(&device->heap_storage_lock);

   if (storage->mapping) {
      result = d12_umd_kmt_unlock(&device->kmt,
                                  storage->allocation.allocation);
      storage->mapping = NULL;
   }
   if (storage->backend && storage->destroy_backend) {
      storage->destroy_backend(storage->backend);
   } else {
      HRESULT deallocate = d12_umd_kmt_deallocate(
         &device->kmt, storage->runtime_resource, &storage->allocation);
      if (SUCCEEDED(result))
         result = deallocate;
   }
   SecureZeroMemory(storage, sizeof(*storage));
   HeapFree(GetProcessHeap(), 0, storage);
   return result;
}

static HRESULT
d12_umd_heap_storage_request_destroy(struct d12_umd_heap_storage *storage)
{
   enum d12_umd_lifetime_result lifetime_result;

   if (!storage)
      return E_INVALIDARG;
   d12_umd_sync_acquire_exclusive(&storage->lock);
   lifetime_result = d12_umd_lifetime_request_destroy(&storage->lifetime);
   ReleaseSRWLockExclusive(&storage->lock);
   if (lifetime_result == D12_UMD_LIFETIME_INVALID)
      return E_INVALIDARG;
   return lifetime_result == D12_UMD_LIFETIME_FINALIZE
             ? d12_umd_heap_storage_finalize(storage)
             : S_OK;
}

static HRESULT
d12_umd_heap_storage_ensure_destroy_requested(
   struct d12_umd_heap_storage *storage)
{
   bool already_requested;

   if (!storage)
      return E_INVALIDARG;
   d12_umd_sync_acquire_shared(&storage->lock);
   already_requested = storage->lifetime.destroy_requested;
   ReleaseSRWLockShared(&storage->lock);
   return already_requested ? S_OK
                            : d12_umd_heap_storage_request_destroy(storage);
}

static HRESULT
d12_umd_heap_storage_retain_mapping(struct d12_umd_heap_storage *storage,
                                    void **mapping, bool fence_retain)
{
   struct d12_umd_device *device;
   enum d12_umd_lifetime_result lifetime_result;
   HRESULT result = S_OK;

   if (mapping)
      *mapping = NULL;
   if (!storage || !mapping ||
       storage->cpu_page_property ==
          D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
      return E_INVALIDARG;
   device = storage->device;
   if (!device)
      return E_INVALIDARG;

   d12_umd_sync_acquire_exclusive(&storage->lock);
   lifetime_result = d12_umd_lifetime_retain(&storage->lifetime);
   if (lifetime_result == D12_UMD_LIFETIME_INVALID) {
      result = E_INVALIDARG;
   } else if (lifetime_result == D12_UMD_LIFETIME_OVERFLOW) {
      result = E_FAIL;
   } else {
      if (!storage->mapping)
         result = d12_umd_kmt_lock(&device->kmt,
                                   storage->allocation.allocation,
                                   &storage->mapping);
      if (SUCCEEDED(result)) {
         if (fence_retain)
            ++storage->fence_retain_count;
         else
            ++storage->map_retain_count;
         *mapping = storage->mapping;
      } else {
         (void)d12_umd_lifetime_release(&storage->lifetime);
      }
   }
   ReleaseSRWLockExclusive(&storage->lock);
   return result;
}

static HRESULT
d12_umd_heap_storage_release_mapping_kind(
   struct d12_umd_heap_storage *storage, bool fence_retain)
{
   struct d12_umd_device *device;
   enum d12_umd_lifetime_result lifetime_result;
   HRESULT result = S_OK;
   bool finalize = false;

   if (!storage || !storage->device)
      return E_INVALIDARG;
   device = storage->device;
   d12_umd_sync_acquire_exclusive(&storage->lock);
   if ((fence_retain && !storage->fence_retain_count) ||
       (!fence_retain && !storage->map_retain_count)) {
      ReleaseSRWLockExclusive(&storage->lock);
      return E_INVALIDARG;
   }
   lifetime_result = d12_umd_lifetime_release(&storage->lifetime);
   if (lifetime_result == D12_UMD_LIFETIME_INVALID) {
      result = E_INVALIDARG;
   } else {
      if (fence_retain)
         --storage->fence_retain_count;
      else
         --storage->map_retain_count;
   }
   if (lifetime_result != D12_UMD_LIFETIME_INVALID &&
       !storage->lifetime.retain_count) {
      result = d12_umd_kmt_unlock(&device->kmt,
                                  storage->allocation.allocation);
      if (SUCCEEDED(result))
         storage->mapping = NULL;
      if (lifetime_result == D12_UMD_LIFETIME_FINALIZE)
         finalize = true;
   }
   ReleaseSRWLockExclusive(&storage->lock);
   if (finalize) {
      HRESULT final_result = d12_umd_heap_storage_finalize(storage);
      if (SUCCEEDED(result))
         result = final_result;
   }
   return result;
}

static HRESULT
d12_umd_heap_storage_release_mapping(struct d12_umd_heap_storage *storage)
{
   return d12_umd_heap_storage_release_mapping_kind(storage, false);
}

static HRESULT
d12_umd_heap_retain_mapping(struct d12_umd_heap *heap, void **mapping)
{
   if (mapping)
      *mapping = NULL;
   if (!heap || heap->signature != D12_UMD_HEAP_SIGNATURE || !mapping ||
       !heap->storage)
      return E_INVALIDARG;
   return d12_umd_heap_storage_retain_mapping(heap->storage, mapping, false);
}

static HRESULT
d12_umd_heap_retain_fence_mapping(struct d12_umd_heap *heap, void **mapping)
{
   if (mapping)
      *mapping = NULL;
   if (!heap || heap->signature != D12_UMD_HEAP_SIGNATURE || !mapping ||
       !heap->storage)
      return E_INVALIDARG;
   return d12_umd_heap_storage_retain_mapping(heap->storage, mapping, true);
}

static HRESULT
d12_umd_heap_release_mapping(struct d12_umd_heap *heap)
{
   if (!heap || heap->signature != D12_UMD_HEAP_SIGNATURE || !heap->storage)
      return E_INVALIDARG;
   return d12_umd_heap_storage_release_mapping(heap->storage);
}

static void
d12_umd_fence_release_heap_storage(void *cookie)
{
   struct d12_umd_heap_storage *storage = cookie;
   struct d12_umd_device *device = storage ? storage->device : NULL;
   HRESULT result =
      d12_umd_heap_storage_release_mapping_kind(storage, true);

   if (FAILED(result) && device)
      d12_umd_set_device_error(device, result);
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
   if (InterlockedCompareExchange(&device->destroying, 0, 0))
      return DXGI_ERROR_DEVICE_REMOVED;
   if (!arguments->hDrvCommandQueue.pDrvPrivate ||
       !arguments->hRTCommandQueue.handle)
      return E_INVALIDARG;

   queue = (struct d12_umd_command_queue *)
              arguments->hDrvCommandQueue.pDrvPrivate;
   memset(queue, 0, sizeof(*queue));
   InitializeSRWLock(&queue->dxgi_blt_lock);
   type = d12_umd_queue_renderer_type(arguments->QueueFlags);

   d12_umd_sync_acquire_exclusive(&device->queue_lock);
   reserve_usc = !device->backend && device->queue_count == 0;
   if (device->backend) {
      if (!device->renderer_ops ||
          !device->renderer_ops->create_command_queue ||
          !device->renderer_ops->destroy_command_queue) {
         result = E_NOTIMPL;
      } else {
         result = device->renderer_ops->create_command_queue(
            device->backend, type, &queue->backend);
         result = d12_umd_renderer_object_result(
            result, &queue->backend,
            device->renderer_ops->destroy_command_queue);
      }
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
   if (FAILED(result))
      SecureZeroMemory(queue, sizeof(*queue));
   return result;
}

#if D12_UMD_TARGETS_WDDM23
static bool
d12_umd_translate_command_queue_0023(
   const D3D12DDIARG_CREATECOMMANDQUEUE_0023 *source,
   D3D12DDI_HCOMMANDQUEUE driver_queue,
   D3D12DDI_HRTCOMMANDQUEUE runtime_queue,
   D3D12DDIARG_CREATECOMMANDQUEUE_0001 *destination)
{
   if (!source || !destination ||
       source->QueueCreationFlags !=
          D3D12DDI_COMMAND_QUEUE_CREATION_FLAG_NONE)
      return false;
   *destination = (D3D12DDIARG_CREATECOMMANDQUEUE_0001){
      .hDrvCommandQueue = driver_queue,
      .hRTCommandQueue = runtime_queue,
      .QueueFlags = source->QueueFlags,
      .NodeMask = source->NodeMask,
   };
   return true;
}

static SIZE_T APIENTRY
d12_umd_calc_private_command_queue_size_0023(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATECOMMANDQUEUE_0023 *arguments)
{
   D3D12DDIARG_CREATECOMMANDQUEUE_0001 baseline;
   return d12_umd_translate_command_queue_0023(
             arguments, (D3D12DDI_HCOMMANDQUEUE){0},
             (D3D12DDI_HRTCOMMANDQUEUE){0}, &baseline)
             ? d12_umd_calc_private_command_queue_size(handle, &baseline)
             : 0;
}

static HRESULT APIENTRY
d12_umd_create_command_queue_0023(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATECOMMANDQUEUE_0023 *arguments,
   D3D12DDI_HCOMMANDQUEUE driver_queue,
   D3D12DDI_HRTCOMMANDQUEUE runtime_queue)
{
   D3D12DDIARG_CREATECOMMANDQUEUE_0001 baseline;
   if (!d12_umd_translate_command_queue_0023(
          arguments, driver_queue, runtime_queue, &baseline))
      return E_INVALIDARG;
   return d12_umd_create_command_queue(handle, &baseline);
}
#endif

#if D12_UMD_TARGETS_WDDM31
static bool
d12_umd_translate_command_queue_0050(
   const D3D12DDIARG_CREATECOMMANDQUEUE_0050 *source,
   D3D12DDIARG_CREATECOMMANDQUEUE_0023 *destination)
{
   if (!source || !destination || source->SchedulingGroup.pDrvPrivate)
      return false;
   *destination = (D3D12DDIARG_CREATECOMMANDQUEUE_0023){
      .QueueFlags = source->QueueFlags,
      .NodeMask = source->NodeMask,
      .QueueCreationFlags = source->QueueCreationFlags,
   };
   return true;
}

static SIZE_T APIENTRY
d12_umd_calc_private_command_queue_size_0050(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATECOMMANDQUEUE_0050 *arguments)
{
   D3D12DDIARG_CREATECOMMANDQUEUE_0023 release2;
   return d12_umd_translate_command_queue_0050(arguments, &release2)
             ? d12_umd_calc_private_command_queue_size_0023(handle,
                                                             &release2)
             : 0;
}

static HRESULT APIENTRY
d12_umd_create_command_queue_0050(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATECOMMANDQUEUE_0050 *arguments,
   D3D12DDI_HCOMMANDQUEUE driver_queue,
   D3D12DDI_HRTCOMMANDQUEUE runtime_queue)
{
   D3D12DDIARG_CREATECOMMANDQUEUE_0023 release2;
   if (!d12_umd_translate_command_queue_0050(arguments, &release2))
      return E_INVALIDARG;
   return d12_umd_create_command_queue_0023(
      handle, &release2, driver_queue, runtime_queue);
}
#endif

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

   d12_umd_sync_acquire_exclusive(&device->queue_lock);
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

HRESULT
d12_umd_prepare_device_destroy(struct d12_umd_device *device)
{
   struct d12_umd_heap_storage *storage;
   HRESULT result = S_OK;

   if (!device || device->signature != D12_UMD_DEVICE_SIGNATURE)
      return E_INVALIDARG;
   if (InterlockedExchange(&device->destroying, 1))
      return S_FALSE;

   /* A renderer queue owns every asynchronous placement-fence reference.
    * Destroying all queues first joins their workers and releases every
    * queued Wait/Signal before heap mappings or the shared renderer device
    * are retired.  The D3D12 runtime normally destroys child objects first;
    * this loop makes that ordering explicit and defensive. */
   for (;;) {
      struct d12_umd_command_queue *queue;

      d12_umd_sync_acquire_shared(&device->queue_lock);
      queue = device->queues;
      ReleaseSRWLockShared(&device->queue_lock);
      if (!queue)
         break;
      d12_umd_destroy_command_queue(
         device->driver_device,
         (D3D12DDI_HCOMMANDQUEUE){.pDrvPrivate = queue});
   }

   /* Runtime heap blocks cease to be addressable after DestroyDevice.  The
    * driver-owned storage registry remains authoritative even for heaps
    * whose DDI private block was destroyed while a fence was queued. */
   d12_umd_sync_acquire_exclusive(&device->heap_lock);
   device->heaps = NULL;
   ReleaseSRWLockExclusive(&device->heap_lock);

   d12_umd_sync_acquire_shared(&device->heap_storage_lock);
   storage = device->heap_storages;
   ReleaseSRWLockShared(&device->heap_storage_lock);
   while (storage) {
      struct d12_umd_heap_storage *next = storage->next;
      HRESULT destroy_result =
         d12_umd_heap_storage_ensure_destroy_requested(storage);
      if (FAILED(destroy_result) && SUCCEEDED(result))
         result = destroy_result;
      storage = next;
   }

   /* Unbalanced application MapHeap calls have no asynchronous renderer
    * owner and can be retired at the device boundary. Fence retains cannot
    * be forced: seeing one after all queues were joined is an internal
    * ownership violation and must fail the teardown audit rather than hide
    * a possible use-after-free. */
   d12_umd_sync_acquire_shared(&device->heap_storage_lock);
   storage = device->heap_storages;
   ReleaseSRWLockShared(&device->heap_storage_lock);
   while (storage) {
      struct d12_umd_heap_storage *next = storage->next;
      bool finalize = false;

      d12_umd_sync_acquire_exclusive(&storage->lock);
      if (storage->fence_retain_count ||
          storage->lifetime.retain_count != storage->map_retain_count) {
         if (SUCCEEDED(result))
            result = E_UNEXPECTED;
      } else {
         enum d12_umd_lifetime_result lifetime_result =
            d12_umd_lifetime_abandon_retains(
               &storage->lifetime, storage->map_retain_count);
         if (lifetime_result != D12_UMD_LIFETIME_FINALIZE) {
            if (SUCCEEDED(result))
               result = E_UNEXPECTED;
         } else {
            storage->map_retain_count = 0;
            finalize = true;
         }
      }
      ReleaseSRWLockExclusive(&storage->lock);

      if (finalize) {
         HRESULT final_result = d12_umd_heap_storage_finalize(storage);
         if (FAILED(final_result) && SUCCEEDED(result))
            result = final_result;
      }
      storage = next;
   }

   d12_umd_sync_acquire_shared(&device->heap_storage_lock);
   if (device->heap_storages || device->heap_storage_count) {
      if (SUCCEEDED(result))
         result = E_UNEXPECTED;
   }
   ReleaseSRWLockShared(&device->heap_storage_lock);
   return result;
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
       !d12_sample_count_supported(arguments->SampleDesc.Count) ||
       !d12_resource_multisample_flags_valid(
          arguments->SampleDesc.Count,
          arguments->Flags & D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET,
          arguments->Flags & D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL) ||
       arguments->ResourceType < D3D12DDI_RT_BUFFER ||
       arguments->ResourceType > D3D12DDI_RT_TEXTURE3D ||
       (arguments->Layout != D3D12DDI_TL_ROW_MAJOR &&
        arguments->Layout != D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE) ||
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

/* CheckResourceAllocationInfo is the one resource path on which the runtime
 * may pass TL_UNDEFINED.  Microsoft requires the UMD to return a concrete
 * layout and guarantees that later creation uses that resolved value.  AGX's
 * opaque page-tiled layout is represented by the standard DDI token for an
 * undefined swizzle within 64 KiB tiles; it does not require publishing a
 * device-dependent swizzle table. */
static bool
d12_umd_resolve_resource_arguments(
   const D3D12DDIARG_CREATERESOURCE_0003 *arguments,
   D3D12DDIARG_CREATERESOURCE_0003 *resolved)
{
   if (!arguments || !resolved)
      return false;

   *resolved = *arguments;
   if (resolved->Layout == D3D12DDI_TL_UNDEFINED) {
      if (resolved->ResourceType == D3D12DDI_RT_BUFFER)
         return false;
      resolved->Layout = D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE;
   }
   return d12_umd_resource_arguments_valid(resolved);
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

static bool
d12_umd_reserved_resource_valid(
   struct d12_umd_device *device,
   const D3D12DDIARG_CREATERESOURCE_0003 *arguments)
{
   struct d12_frontend_resource_desc frontend;

   if (!device || !arguments)
      return false;
   if (arguments->ResourceType == D3D12DDI_RT_BUFFER)
      return arguments->Layout == D3D12DDI_TL_ROW_MAJOR;
   if (arguments->ResourceType != D3D12DDI_RT_TEXTURE2D ||
       arguments->Layout != D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE ||
       arguments->SampleDesc.Count != 1 || arguments->SampleDesc.Quality)
      return false;

   /* Only the renderer knows where its opaque AGX layout begins the packed
    * mip tail. Tier 1 forbids a tail shared by array layers, so production
    * sizing must consult the same validator as creation rather than handing
    * the runtime private storage for an object that will later be rejected. */
   if (device->backend) {
      if (!device->renderer_ops ||
          !device->renderer_ops->validate_reserved_resource)
         return false;
      frontend = d12_umd_frontend_resource_desc(arguments);
      return SUCCEEDED(
         device->renderer_ops->validate_reserved_resource(&frontend));
   }
   return true;
}

static HRESULT
d12_umd_resource_allocation_info(
   struct d12_umd_device *device,
   const D3D12DDIARG_CREATERESOURCE_0003 *resource_arguments,
   uint64_t alignment_restriction, uint32_t visible_node_mask,
   bool resolve_undefined_layout, D3D12DDI_RESOURCE_ALLOCATION_INFO *info)
{
   D3D12DDIARG_CREATERESOURCE_0003 resolved_arguments;
   struct d12_frontend_resource_desc frontend;
   struct d12_frontend_resource_allocation_info allocation_info;
   HRESULT result;

   if (!device || !resource_arguments || !info ||
       (alignment_restriction &&
        !d12_umd_is_power_of_two(alignment_restriction)) ||
       (visible_node_mask != 0 && visible_node_mask != 1))
      return E_INVALIDARG;

   if (resolve_undefined_layout) {
      if (!d12_umd_resolve_resource_arguments(resource_arguments,
                                               &resolved_arguments))
         return E_INVALIDARG;
   } else {
      if (!d12_umd_resource_arguments_valid(resource_arguments))
         return E_INVALIDARG;
      resolved_arguments = *resource_arguments;
   }

   if (device->renderer_ops && device->backend &&
       device->renderer_ops->get_resource_allocation_info) {
      frontend = d12_umd_frontend_resource_desc(&resolved_arguments);
      frontend.alignment = alignment_restriction;
      memset(&allocation_info, 0, sizeof(allocation_info));
      result = device->renderer_ops->get_resource_allocation_info(
         device->backend, &frontend, visible_node_mask, &allocation_info);
      if (FAILED(result))
         return result;
      if (!allocation_info.size || !allocation_info.alignment ||
          !d12_umd_is_power_of_two(allocation_info.alignment) ||
          allocation_info.layout != (uint32_t)resolved_arguments.Layout)
         return E_FAIL;

      *info = (D3D12DDI_RESOURCE_ALLOCATION_INFO){
         .ResourceDataSize = allocation_info.size,
         .ResourceDataAlignment = allocation_info.alignment,
         .AdditionalDataHeaderAlignment = 1,
         .AdditionalDataAlignment = 1,
         .Layout = (D3D12DDI_TEXTURE_LAYOUT)allocation_info.layout,
      };
      return S_OK;
   }

   /* The callback-only build has no renderer and exists to validate the DDI
    * ABI.  Its deterministic fallback is exact for buffers; production Mesa
    * builds always take the shared AGX layout path above for textures. */
   *info = (D3D12DDI_RESOURCE_ALLOCATION_INFO){
      .ResourceDataSize = resolved_arguments.Width,
      .ResourceDataAlignment = D12_UMD_DEFAULT_PLACEMENT_ALIGNMENT,
      .AdditionalDataHeaderAlignment = 1,
      .AdditionalDataAlignment = 1,
      .Layout = resolved_arguments.Layout,
   };
   return S_OK;
}

static bool
d12_umd_heap_resource_compatible(
   struct d12_umd_device *device,
   const D3D12DDIARG_CREATEHEAP_0001 *heap_arguments,
   const D3D12DDIARG_CREATERESOURCE_0003 *resource_arguments)
{
   D3D12DDI_RESOURCE_ALLOCATION_INFO info;
   const uint64_t heap_alignment =
      heap_arguments->Alignment ? heap_arguments->Alignment
                                : D12_UMD_DEFAULT_PLACEMENT_ALIGNMENT;
   D3D12DDI_HEAP_FLAGS required_category;

   if (resource_arguments->ResourceType == D3D12DDI_RT_BUFFER) {
      required_category = D3D12DDI_HEAP_FLAG_BUFFERS;
   } else if (resource_arguments->Flags &
              (D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET |
               D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL)) {
      required_category = D3D12DDI_HEAP_FLAG_RT_DS_TEXTURES;
   } else {
      required_category = D3D12DDI_HEAP_FLAG_NON_RT_DS_TEXTURES;
   }

   if (!(heap_arguments->Flags & required_category) ||
       FAILED(d12_umd_resource_allocation_info(
          device, resource_arguments, 0, heap_arguments->VisibleNodeMask,
          false, &info)))
      return false;

   return info.ResourceDataSize <= heap_arguments->ByteSize &&
          info.ResourceDataAlignment <= heap_alignment;
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
       (resource_arguments && !heap_arguments && !reuses_gpu_va &&
        !d12_umd_reserved_resource_valid(device, resource_arguments)) ||
       (heap_arguments && resource_arguments &&
        !d12_umd_heap_resource_compatible(
           device, heap_arguments, resource_arguments)))
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
          (!resource_arguments->ReuseBufferGPUVA.BaseAddress.UMD.hResource
              .pDrvPrivate &&
           !d12_umd_reserved_resource_valid(device, resource_arguments)) ||
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
         if (!device->renderer_ops->create_reused_resource ||
             !device->renderer_ops->destroy_resource ||
             !device->renderer_ops->resource_gpu_virtual_address)
            return E_NOTIMPL;
         result = d12_umd_reused_resource_info(
            device, resource_arguments, &reuse_info);
         if (FAILED(result))
            return result;
         result = device->renderer_ops->create_reused_resource(
            device->backend, reuse_info.source->backend,
            reuse_info.source_offset, &frontend_resource, translated_clear,
            &resource->backend);
         result = d12_umd_renderer_object_result(
            result, &resource->backend,
            device->renderer_ops->destroy_resource);
         if (SUCCEEDED(result) &&
             device->renderer_ops->resource_gpu_virtual_address(
                resource->backend) != reuse_info.gpu_virtual_address) {
            device->renderer_ops->destroy_resource(resource->backend);
            resource->backend = NULL;
            result = E_FAIL;
         }
      } else {
         if (!device->renderer_ops->create_reserved_resource ||
             !device->renderer_ops->destroy_resource ||
             !device->renderer_ops->resource_gpu_virtual_address)
            return E_NOTIMPL;
         result = device->renderer_ops->create_reserved_resource(
            device->backend, &frontend_resource, translated_clear,
            &resource->backend);
         result = d12_umd_renderer_object_result(
            result, &resource->backend,
            device->renderer_ops->destroy_resource);
      }
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
         !d12_umd_heap_resource_compatible(
            device, heap_arguments, resource_arguments))))
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

   if (device->backend) {
      if (!device->renderer_ops ||
          !device->renderer_ops->create_heap ||
          !device->renderer_ops->destroy_heap ||
          (resource_arguments &&
           (!device->renderer_ops->create_placed_resource ||
            !device->renderer_ops->destroy_resource)))
         return E_NOTIMPL;
      result = device->renderer_ops->create_heap(
         device->backend, &frontend_heap, runtime_resource.handle,
         &private_data, sizeof(private_data), standard_descriptor,
         &heap->backend,
         &frontend_allocation);
      result = d12_umd_renderer_object_result(
         result, &heap->backend, device->renderer_ops->destroy_heap);
      if (FAILED(result)) {
         SecureZeroMemory(heap, sizeof(*heap));
         return result;
      }
      if (!frontend_allocation.allocation || !frontend_allocation.resource ||
          !frontend_allocation.gpu_virtual_address ||
          frontend_allocation.size < heap_arguments->ByteSize) {
         if (heap->backend)
            device->renderer_ops->destroy_heap(heap->backend);
         SecureZeroMemory(heap, sizeof(*heap));
         return E_FAIL;
      }
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
   result = d12_umd_heap_storage_create(heap);
   if (FAILED(result)) {
      if (heap->backend && device->renderer_ops &&
          device->renderer_ops->destroy_heap) {
         device->renderer_ops->destroy_heap(heap->backend);
      } else {
         (void)d12_umd_kmt_deallocate(&device->kmt,
                                      heap->runtime_resource,
                                      &heap->allocation);
      }
      SecureZeroMemory(heap, sizeof(*heap));
      return result;
   }

   d12_umd_sync_acquire_exclusive(&device->heap_lock);
   heap->next = device->heaps;
   device->heaps = heap;
   ReleaseSRWLockExclusive(&device->heap_lock);

   if (resource_arguments) {
      if (heap->backend && device->renderer_ops &&
          device->renderer_ops->create_placed_resource) {
         result = device->renderer_ops->create_placed_resource(
            device->backend, heap->backend, 0, &frontend_resource,
            translated_clear, &resource->backend);
         result = d12_umd_renderer_object_result(
            result, &resource->backend,
            device->renderer_ops->destroy_resource);
         if (FAILED(result)) {
            d12_umd_sync_acquire_exclusive(&device->heap_lock);
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
            if (resource->backend)
               device->renderer_ops->destroy_resource(resource->backend);
            (void)d12_umd_heap_storage_request_destroy(heap->storage);
            SecureZeroMemory(resource, sizeof(*resource));
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
      uint64_t allocation_size;

      if (private_data->heap.size >
          UINT64_MAX - (AGXW_PAGE_SIZE - UINT64_C(1)))
         return false;
      allocation_size =
         (private_data->heap.size + (AGXW_PAGE_SIZE - UINT64_C(1))) &
         ~(AGXW_PAGE_SIZE - UINT64_C(1));
      if (allocation_private->size != allocation_size ||
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
       !device->renderer_ops->open_heap ||
       !device->renderer_ops->destroy_heap ||
       (has_resource &&
        (!device->renderer_ops->create_placed_resource ||
         !device->renderer_ops->destroy_resource)))
      return E_NOTIMPL;

   memset(heap, 0, sizeof(*heap));
   if (resource)
      memset(resource, 0, sizeof(*resource));
   result = device->renderer_ops->open_heap(
      device->backend, &private_data.heap, runtime_resource.handle,
      arguments->pOpenAllocationInfo[0].hAllocation,
      arguments->hKMResource.handle, allocation_private.flags,
      &heap->backend, &frontend_allocation);
   result = d12_umd_renderer_object_result(
      result, &heap->backend, device->renderer_ops->destroy_heap);
   if (FAILED(result)) {
      SecureZeroMemory(heap, sizeof(*heap));
      return result;
   }
   if (!frontend_allocation.allocation || !frontend_allocation.resource ||
       !frontend_allocation.gpu_virtual_address ||
       frontend_allocation.size < private_data.heap.size) {
      result = E_FAIL;
      goto fail;
   }

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
   result = d12_umd_heap_storage_create(heap);
   if (FAILED(result))
      goto fail;

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
      result = d12_umd_renderer_object_result(
         result, &resource->backend,
         device->renderer_ops->destroy_resource);
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

   d12_umd_sync_acquire_exclusive(&device->heap_lock);
   heap->next = device->heaps;
   device->heaps = heap;
   ReleaseSRWLockExclusive(&device->heap_lock);
   return S_OK;

fail:
   if (resource && resource->backend && device->renderer_ops->destroy_resource)
      device->renderer_ops->destroy_resource(resource->backend);
   if (heap->storage) {
      (void)d12_umd_heap_storage_request_destroy(heap->storage);
   } else if (heap->backend) {
      device->renderer_ops->destroy_heap(heap->backend);
   }
   if (resource)
      SecureZeroMemory(resource, sizeof(*resource));
   SecureZeroMemory(heap, sizeof(*heap));
   return result;
}

#if D12_UMD_TARGETS_WDDM23
static D3D12DDI_HEAP_AND_RESOURCE_SIZES APIENTRY
d12_umd_calc_private_heap_resource_sizes_0030(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATEHEAP_0001 *heap_arguments,
   const D3D12DDIARG_CREATERESOURCE_0003 *resource_arguments,
   D3D12DDI_HPROTECTEDRESOURCESESSION_0030 protected_session)
{
   if (protected_session.pDrvPrivate)
      return (D3D12DDI_HEAP_AND_RESOURCE_SIZES){0};
   return d12_umd_calc_private_heap_resource_sizes(
      handle, heap_arguments, resource_arguments);
}

static HRESULT APIENTRY
d12_umd_create_heap_resource_0030(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATEHEAP_0001 *heap_arguments,
   D3D12DDI_HHEAP heap, D3D12DDI_HRTRESOURCE runtime_resource,
   const D3D12DDIARG_CREATERESOURCE_0003 *resource_arguments,
   const D3D12DDI_CLEAR_VALUES *clear_value,
   D3D12DDI_HPROTECTEDRESOURCESESSION_0030 protected_session,
   D3D12DDI_HRESOURCE resource)
{
   if (protected_session.pDrvPrivate)
      return E_NOTIMPL;
   return d12_umd_create_heap_resource(
      handle, heap_arguments, heap, runtime_resource, resource_arguments,
      clear_value, resource);
}
#endif

#if D12_UMD_TARGETS_WDDM31
static bool
d12_umd_legacy_state_from_barrier_layout_0088(
   D3D12DDI_BARRIER_LAYOUT layout, D3D12DDI_RESOURCE_STATES *state)
{
   const D3D12DDI_RESOURCE_STATES generic_read =
      (D3D12DDI_RESOURCE_STATES)(
         D3D12DDI_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
         D3D12DDI_RESOURCE_STATE_INDEX_BUFFER |
         D3D12DDI_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
         D3D12DDI_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
         D3D12DDI_RESOURCE_STATE_INDIRECT_ARGUMENT |
         D3D12DDI_RESOURCE_STATE_COPY_SOURCE);
   const D3D12DDI_RESOURCE_STATES shader_resource =
      (D3D12DDI_RESOURCE_STATES)(
         D3D12DDI_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
         D3D12DDI_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

   if (!state)
      return false;

   /* DDI 0088 replaces InitialResourceState with InitialBarrierLayout even
    * when enhanced barriers are not advertised. Preserve every layout with
    * an exact legacy-state representation. Video, VRS and UNDEFINED layouts
    * carry semantics this backend does not expose and must fail closed. */
   switch (layout) {
   case D3D12DDI_BARRIER_LAYOUT_COMMON:
   case D3D12DDI_BARRIER_LAYOUT_DIRECT_QUEUE_COMMON:
   case D3D12DDI_BARRIER_LAYOUT_COMPUTE_QUEUE_COMMON:
      *state = D3D12DDI_RESOURCE_STATE_COMMON;
      return true;
   case D3D12DDI_BARRIER_LAYOUT_GENERIC_READ:
   case D3D12DDI_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ:
   case D3D12DDI_BARRIER_LAYOUT_COMPUTE_QUEUE_GENERIC_READ:
   case D3D12DDI_BARRIER_LAYOUT_LEGACY_DIRECT_QUEUE_GENERIC_READ_COMPUTE_QUEUE_ACCESSIBLE:
      *state = generic_read;
      return true;
   case D3D12DDI_BARRIER_LAYOUT_RENDER_TARGET:
      *state = D3D12DDI_RESOURCE_STATE_RENDER_TARGET;
      return true;
   case D3D12DDI_BARRIER_LAYOUT_UNORDERED_ACCESS:
   case D3D12DDI_BARRIER_LAYOUT_DIRECT_QUEUE_UNORDERED_ACCESS:
   case D3D12DDI_BARRIER_LAYOUT_COMPUTE_QUEUE_UNORDERED_ACCESS:
      *state = D3D12DDI_RESOURCE_STATE_UNORDERED_ACCESS;
      return true;
   case D3D12DDI_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
      *state = D3D12DDI_RESOURCE_STATE_DEPTH_WRITE;
      return true;
   case D3D12DDI_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
      *state = D3D12DDI_RESOURCE_STATE_DEPTH_READ;
      return true;
   case D3D12DDI_BARRIER_LAYOUT_SHADER_RESOURCE:
   case D3D12DDI_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE:
   case D3D12DDI_BARRIER_LAYOUT_LEGACY_SHADER_RESOURCE:
      *state = shader_resource;
      return true;
   case D3D12DDI_BARRIER_LAYOUT_COMPUTE_QUEUE_SHADER_RESOURCE:
      *state = D3D12DDI_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      return true;
   case D3D12DDI_BARRIER_LAYOUT_LEGACY_PIXEL_SHADER_RESOURCE:
      *state = D3D12DDI_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      return true;
   case D3D12DDI_BARRIER_LAYOUT_COPY_SOURCE:
   case D3D12DDI_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_SOURCE:
   case D3D12DDI_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_SOURCE:
   case D3D12DDI_BARRIER_LAYOUT_LEGACY_COPY_SOURCE:
      *state = D3D12DDI_RESOURCE_STATE_COPY_SOURCE;
      return true;
   case D3D12DDI_BARRIER_LAYOUT_COPY_DEST:
   case D3D12DDI_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_DEST:
   case D3D12DDI_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_DEST:
   case D3D12DDI_BARRIER_LAYOUT_LEGACY_COPY_DEST:
      *state = D3D12DDI_RESOURCE_STATE_COPY_DEST;
      return true;
   case D3D12DDI_BARRIER_LAYOUT_RESOLVE_SOURCE:
      *state = D3D12DDI_RESOURCE_STATE_RESOLVE_SOURCE;
      return true;
   case D3D12DDI_BARRIER_LAYOUT_RESOLVE_DEST:
      *state = D3D12DDI_RESOURCE_STATE_RESOLVE_DEST;
      return true;
   default:
      *state = D3D12DDI_RESOURCE_STATE_COMMON;
      return false;
   }
}

static bool
d12_umd_translate_create_resource_0088(
   const D3D12DDIARG_CREATERESOURCE_0088 *source,
   D3D12DDIARG_CREATERESOURCE_0003 *destination)
{
   D3D12DDI_RESOURCE_STATES initial_state;

   if (!source || !destination ||
       source->SamplerFeedbackMipRegion.Width ||
       source->SamplerFeedbackMipRegion.Height ||
       source->SamplerFeedbackMipRegion.Depth ||
       source->NumCastableFormats)
      return false;

   /* Enhanced-barrier resource creation deliberately gives buffers no
    * layout: Microsoft's DDI contract requires UNDEFINED and specifies that
    * ordinary buffers are treated as legacy COMMON.  Texture UNDEFINED is
    * not an initialized layout and remains invalid. */
   if (source->ResourceType == D3D12DDI_RT_BUFFER) {
      if (source->InitialBarrierLayout != D3D12DDI_BARRIER_LAYOUT_UNDEFINED)
         return false;
      initial_state = D3D12DDI_RESOURCE_STATE_COMMON;
   } else if (!d12_umd_legacy_state_from_barrier_layout_0088(
                 source->InitialBarrierLayout, &initial_state)) {
      return false;
   }

   *destination = (D3D12DDIARG_CREATERESOURCE_0003){
      .ReuseBufferGPUVA = source->ReuseBufferGPUVA,
      .ResourceType = source->ResourceType,
      .Width = source->Width,
      .Height = source->Height,
      .DepthOrArraySize = source->DepthOrArraySize,
      .MipLevels = source->MipLevels,
      .Format = source->Format,
      .SampleDesc = source->SampleDesc,
      .Layout = source->Layout,
      .Flags = source->Flags,
      .InitialResourceState = initial_state,
      .pRowMajorLayout = source->pRowMajorLayout,
   };
   return true;
}

static D3D12DDI_HEAP_AND_RESOURCE_SIZES APIENTRY
d12_umd_calc_private_heap_resource_sizes_0088(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATEHEAP_0001 *heap_arguments,
   const D3D12DDIARG_CREATERESOURCE_0088 *resource_arguments,
   D3D12DDI_HPROTECTEDRESOURCESESSION_0030 protected_session)
{
   D3D12DDIARG_CREATERESOURCE_0003 translated_resource;

   if (protected_session.pDrvPrivate ||
       (resource_arguments &&
        !d12_umd_translate_create_resource_0088(
           resource_arguments, &translated_resource)))
      return (D3D12DDI_HEAP_AND_RESOURCE_SIZES){0};

   return d12_umd_calc_private_heap_resource_sizes(
      handle, heap_arguments,
      resource_arguments ? &translated_resource : NULL);
}

static HRESULT APIENTRY
d12_umd_create_heap_resource_0088(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATEHEAP_0001 *heap_arguments,
   D3D12DDI_HHEAP heap, D3D12DDI_HRTRESOURCE runtime_resource,
   const D3D12DDIARG_CREATERESOURCE_0088 *resource_arguments,
   const D3D12DDI_CLEAR_VALUES *clear_value,
   D3D12DDI_HPROTECTEDRESOURCESESSION_0030 protected_session,
   D3D12DDI_HRESOURCE resource)
{
   D3D12DDIARG_CREATERESOURCE_0003 translated_resource;

   if (protected_session.pDrvPrivate)
      return E_NOTIMPL;
   if (resource_arguments &&
       !d12_umd_translate_create_resource_0088(
          resource_arguments, &translated_resource))
      return E_NOTIMPL;

   return d12_umd_create_heap_resource(
      handle, heap_arguments, heap, runtime_resource,
      resource_arguments ? &translated_resource : NULL, clear_value,
      resource);
}

static D3D12DDI_HEAP_AND_RESOURCE_SIZES APIENTRY
d12_umd_calc_private_opened_heap_resource_sizes_0043(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_OPENHEAP_0003 *arguments,
   D3D12DDI_HPROTECTEDRESOURCESESSION_0030 protected_session)
{
   if (protected_session.pDrvPrivate)
      return (D3D12DDI_HEAP_AND_RESOURCE_SIZES){0};
   return d12_umd_calc_private_opened_heap_resource_sizes(handle, arguments);
}

static HRESULT APIENTRY
d12_umd_open_heap_resource_0043(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_OPENHEAP_0003 *arguments,
   D3D12DDI_HHEAP heap, D3D12DDI_HRTRESOURCE runtime_resource,
   D3D12DDI_HPROTECTEDRESOURCESESSION_0030 protected_session,
   D3D12DDI_HRESOURCE resource)
{
   if (protected_session.pDrvPrivate)
      return E_NOTIMPL;
   return d12_umd_open_heap_resource(
      handle, arguments, heap, runtime_resource, resource);
}
#endif

static void APIENTRY
d12_umd_destroy_heap_resource(D3D12DDI_HDEVICE handle,
                              D3D12DDI_HHEAP heap_handle,
                              D3D12DDI_HRESOURCE resource_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_heap *heap = (struct d12_umd_heap *)heap_handle.pDrvPrivate;
   struct d12_umd_resource *resource =
      (struct d12_umd_resource *)resource_handle.pDrvPrivate;
   struct d12_umd_heap_storage *storage;
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
       heap->device != device || !heap->storage)
      return;
   storage = heap->storage;

   d12_umd_sync_acquire_exclusive(&device->heap_lock);
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

   result = d12_umd_heap_storage_request_destroy(storage);
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

   if (mapping)
      *mapping = NULL;
   if (!device || !heap || heap->signature != D12_UMD_HEAP_SIGNATURE ||
       heap->device != device || !mapping ||
       !heap->storage ||
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
                              struct d12_umd_heap_storage **retained_storage)
{
   struct d12_umd_heap *heap;
   HRESULT result = E_INVALIDARG;

   if (!device || !address || (address & (sizeof(uint64_t) - 1)) ||
       !cpu_address || !retained_storage)
      return E_INVALIDARG;

   d12_umd_sync_acquire_shared(&device->heap_lock);
   for (heap = device->heaps; heap; heap = heap->next) {
      const uint64_t base = heap->allocation.gpu_virtual_address;
      const uint64_t offset = address - base;
      void *mapping;

      if (address < base || offset > heap->allocation.size ||
          sizeof(uint64_t) > heap->allocation.size - offset)
         continue;
      result = d12_umd_heap_retain_fence_mapping(heap, &mapping);
      if (SUCCEEDED(result)) {
         *cpu_address = (volatile uint64_t *)((uint8_t *)mapping + offset);
         *retained_storage = heap->storage;
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
   struct d12_umd_heap_storage *retained_storage = NULL;
   bool lifetime_transferred;
   HRESULT result;

   if (!device || !fence || !d12_umd_fence_arguments_valid(arguments))
      return E_INVALIDARG;
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->create_fence ||
       !device->renderer_ops->destroy_fence)
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
      &retained_storage);
   if (FAILED(result))
      goto fail;
   fence->entries[0].placement.lifetime_cookie = retained_storage;
   fence->entries[0].placement.release_lifetime =
      d12_umd_fence_release_heap_storage;

   result = device->renderer_ops->create_fence(
      device->backend, fence->count, &fence->entries[0].placement,
      &fence->backend);
   lifetime_transferred = fence->backend != NULL;
   result = d12_umd_renderer_object_result(
      result, &fence->backend, device->renderer_ops->destroy_fence);
   if (FAILED(result)) {
      if (!lifetime_transferred)
         d12_umd_fence_release_heap_storage(
            fence->entries[0].placement.lifetime_cookie);
      goto fail;
   }
   /* The renderer owns the retained mapping until its final asynchronous
    * fence reference is released.  Keep no actionable duplicate in the
    * runtime-owned DDI block. */
   fence->entries[0].placement.lifetime_cookie = NULL;
   fence->entries[0].placement.release_lifetime = NULL;
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

static HRESULT
d12_umd_query_resource_allocation_info(
   struct d12_umd_device *device,
   const D3D12DDIARG_CREATERESOURCE_0003 *resource_arguments,
   D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS optimization_flags,
   UINT64 alignment_restriction, UINT visible_node_mask,
   D3D12DDI_RESOURCE_ALLOCATION_INFO *info)
{
   const UINT supported_optimization_flags =
      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_SHADER_RESOURCE |
      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_UNORDERED_ACCESS |
      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_PRIMARY |
      D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_DETERMINISTIC;
   if (info)
      memset(info, 0, sizeof(*info));
   if (!device || !resource_arguments || !info ||
       ((UINT)optimization_flags & ~supported_optimization_flags))
      return E_INVALIDARG;
   return d12_umd_resource_allocation_info(
      device, resource_arguments, alignment_restriction, visible_node_mask,
      true, info);
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
   HRESULT result = d12_umd_query_resource_allocation_info(
      device, resource_arguments, optimization_flags, alignment_restriction,
      visible_node_mask, info);
   if (FAILED(result))
      d12_umd_set_device_error(device, result);
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

static HRESULT
d12_umd_query_existing_resource_allocation_info(
   struct d12_umd_device *device, D3D12DDI_HRESOURCE resource_handle,
   D3D12DDI_RESOURCE_ALLOCATION_INFO *info, D3DKMT_HANDLE *allocation)
{
   struct d12_umd_resource *resource = resource_handle.pDrvPrivate;
   HRESULT result;

   if (info)
      memset(info, 0, sizeof(*info));
   if (allocation)
      *allocation = 0;
   if (!device || !resource || !info || !allocation ||
       resource->signature != D12_UMD_RESOURCE_SIGNATURE ||
       resource->device != device || !resource->allocation.allocation)
      return E_INVALIDARG;

   result = d12_umd_resource_allocation_info(
      device, &resource->desc, 0, 1, false, info);
   if (FAILED(result) || resource->heap_offset > resource->allocation.size ||
       info->ResourceDataSize >
          resource->allocation.size - resource->heap_offset) {
      memset(info, 0, sizeof(*info));
      return FAILED(result) ? result : E_FAIL;
   }
   *allocation = resource->allocation.allocation;
   return S_OK;
}

static void APIENTRY
d12_umd_check_existing_resource_allocation_info(
   D3D12DDI_HDEVICE handle, D3D12DDI_HRESOURCE resource_handle,
   D3D12DDI_RESOURCE_ALLOCATION_INFO *info, D3DKMT_HANDLE *allocation)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   const HRESULT result = d12_umd_query_existing_resource_allocation_info(
      device, resource_handle, info, allocation);
   if (FAILED(result))
      d12_umd_set_device_error(device, result);
}

#if D12_UMD_TARGETS_WDDM23
static bool
d12_umd_translate_allocation_info_0022(
   const D3D12DDI_RESOURCE_ALLOCATION_INFO *source,
   D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *destination)
{
   if (!source || !destination ||
       source->ResourceDataAlignment > UINT32_MAX ||
       source->AdditionalDataHeaderAlignment1 > UINT32_MAX ||
       source->AdditionalDataAlignment > UINT32_MAX)
      return false;
   memset(destination, 0, sizeof(*destination));
   destination->ResourceDataSize = source->ResourceDataSize;
   destination->AdditionalDataHeaderSize = source->AdditionalDataHeaderSize;
   destination->AdditionalDataSize = source->AdditionalDataSize;
   destination->ResourceDataAlignment =
      (UINT32)source->ResourceDataAlignment;
   destination->AdditionalDataHeaderAlignment =
      source->AdditionalDataHeaderAlignment1;
   destination->AdditionalDataAlignment =
      (UINT32)source->AdditionalDataAlignment;
   destination->Layout = source->Layout;
   destination->MipLevelSwizzleTransition[0] =
      source->MipLevelSwizzleTransition;
   memcpy(&destination->MipLevelSwizzleTransition[1],
          source->AdditionalMipLevelSwizzleTransitionsArray,
          sizeof(source->AdditionalMipLevelSwizzleTransitionsArray));
   return true;
}

static void APIENTRY
d12_umd_check_resource_allocation_info_0022(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATERESOURCE_0003 *resource_arguments,
   D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS optimization_flags,
   UINT32 alignment_restriction, UINT visible_node_mask,
   D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *info)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDI_RESOURCE_ALLOCATION_INFO baseline;
   HRESULT result;

   if (!info)
      return;
   memset(&baseline, 0, sizeof(baseline));
   memset(info, 0, sizeof(*info));
   result = d12_umd_query_resource_allocation_info(
      device, resource_arguments, optimization_flags,
      alignment_restriction, visible_node_mask, &baseline);
   if (SUCCEEDED(result) &&
       !d12_umd_translate_allocation_info_0022(&baseline, info))
      result = E_FAIL;
   if (FAILED(result)) {
      memset(info, 0, sizeof(*info));
      d12_umd_set_device_error(device, result);
   }
}

static void APIENTRY
d12_umd_check_existing_resource_allocation_info_0022(
   D3D12DDI_HDEVICE handle, D3D12DDI_HRESOURCE resource,
   D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *info)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDI_RESOURCE_ALLOCATION_INFO baseline;
   D3DKMT_HANDLE allocation = 0;
   HRESULT result;

   if (!info)
      return;
   memset(&baseline, 0, sizeof(baseline));
   memset(info, 0, sizeof(*info));
   result = d12_umd_query_existing_resource_allocation_info(
      device, resource, &baseline, &allocation);
   if (SUCCEEDED(result) &&
       !d12_umd_translate_allocation_info_0022(&baseline, info))
      result = E_FAIL;
   if (FAILED(result)) {
      memset(info, 0, sizeof(*info));
      d12_umd_set_device_error(device, result);
   }
}
#endif

#if D12_UMD_TARGETS_WDDM31
static void APIENTRY
d12_umd_check_resource_allocation_info_0088(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATERESOURCE_0088 *resource_arguments,
   D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS optimization_flags,
   UINT32 alignment_restriction, UINT visible_node_mask,
   D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *info)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   D3D12DDIARG_CREATERESOURCE_0003 translated_resource;

   if (!info)
      return;
   memset(info, 0, sizeof(*info));
   if (!resource_arguments) {
      d12_umd_set_device_error(device, E_INVALIDARG);
      return;
   }
   if (!d12_umd_translate_create_resource_0088(
          resource_arguments, &translated_resource)) {
      d12_umd_set_device_error(device, E_NOTIMPL);
      return;
   }
   d12_umd_check_resource_allocation_info_0022(
      handle, &translated_resource, optimization_flags,
      alignment_restriction, visible_node_mask, info);
}
#endif

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
#if D12_UMD_TARGETS_WDDM24
   case D3D12DDI_HT_0040_COMMAND_POOL: {
      const struct d12_umd_command_pool *pool = object->Handle;
      return pool->signature == D12_UMD_COMMAND_POOL_SIGNATURE &&
             pool->device == device;
   }
   case D3D12DDI_HT_0040_COMMAND_RECORDER: {
      const struct d12_umd_command_recorder *recorder = object->Handle;
      return recorder->signature == D12_UMD_COMMAND_RECORDER_SIGNATURE &&
             recorder->device == device;
   }
#endif
   default:
      return false;
   }
}

#if D12_UMD_TARGETS_WDDM21
static bool
d12_umd_debug_object_info(struct d12_umd_device *device,
                          const D3D12DDI_HANDLE_AND_TYPE *object,
                          D3DKMT_HANDLE *allocation, uint64_t *start,
                          uint64_t *size)
{
   if (!device || !object || !allocation || !start || !size)
      return false;
   *allocation = 0;
   *start = 0;
   *size = 0;

   switch (object->Type) {
   case D3D12DDI_HT_HEAP: {
      const struct d12_umd_heap *heap = object->Handle;
      if (!heap || heap->signature != D12_UMD_HEAP_SIGNATURE ||
          heap->device != device)
         return false;
      *allocation = heap->allocation.allocation;
      *start = heap->allocation.gpu_virtual_address;
      *size = heap->allocation.size;
      return true;
   }
   case D3D12DDI_HT_0012_RESOURCE: {
      const struct d12_umd_resource *resource = object->Handle;
      D3D12DDI_RESOURCE_ALLOCATION_INFO info;
      HRESULT result;

      if (!resource || resource->signature != D12_UMD_RESOURCE_SIGNATURE ||
          resource->device != device)
         return false;
      *allocation = resource->allocation.allocation;
      *start = d12_umd_check_resource_virtual_address(
         (D3D12DDI_HDEVICE){.pDrvPrivate = device},
         (D3D12DDI_HRESOURCE){.pDrvPrivate = (void *)resource});
      memset(&info, 0, sizeof(info));
      result = d12_umd_resource_allocation_info(
         device, &resource->desc, 0, 1, false, &info);
      if (FAILED(result))
         return false;
      *size = info.ResourceDataSize;
      return true;
   }
   default:
      /* Valid CPU-only pageable objects deliberately have no allocation or
       * GPU VA range. */
      return d12_umd_residency_object_allocation(device, object, allocation);
   }
}

static void APIENTRY
d12_umd_get_debug_allocation_info_0012(
   D3D12DDI_HDEVICE handle, D3D12DDI_HANDLE_AND_TYPE object,
   UINT *virtual_address_count,
   D3D12DDI_DEBUG_VIRTUAL_ADDRESS_ALLOCATION_INFO_0012 *virtual_addresses,
   UINT *kmt_count,
   D3D12DDI_DEBUG_KMT_ALLOCATION_INFO_0012 *kmt_allocations)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   const UINT virtual_address_capacity =
      virtual_address_count ? *virtual_address_count : 0;
   const UINT kmt_capacity = kmt_count ? *kmt_count : 0;
   D3DKMT_HANDLE allocation = 0;
   uint64_t start = 0;
   uint64_t size = 0;
   bool valid;

   if (!virtual_address_count || !kmt_count)
      return;
   *virtual_address_count = 0;
   *kmt_count = 0;
   valid = d12_umd_debug_object_info(device, &object, &allocation, &start,
                                     &size);
   if (!valid) {
      d12_umd_set_device_error(device, E_INVALIDARG);
      return;
   }
   if (start && size && start > UINT64_MAX - size) {
      d12_umd_set_device_error(device, E_FAIL);
      return;
   }

   if (allocation) {
      *kmt_count = 1;
      if (kmt_allocations && kmt_capacity) {
         kmt_allocations[0] =
            (D3D12DDI_DEBUG_KMT_ALLOCATION_INFO_0012){
               .PhysicalAdapterIndex = 0,
               .hAllocation = allocation,
            };
      }
   }
   if (start && size) {
      *virtual_address_count = 1;
      if (virtual_addresses && virtual_address_capacity) {
         virtual_addresses[0] =
            (D3D12DDI_DEBUG_VIRTUAL_ADDRESS_ALLOCATION_INFO_0012){
               .PhysicalAdapterIndex = 0,
               .StartAddress = start,
               .EndAddress = start + size,
            };
      }
   }
}

#if D12_UMD_TARGETS_WDDM23
static void APIENTRY
d12_umd_get_debug_allocation_info_0014(
   D3D12DDI_HDEVICE handle, D3D12DDI_HANDLE_AND_TYPE object,
   UINT *virtual_address_count,
   D3D12DDI_DEBUG_VIRTUAL_ADDRESS_ALLOCATION_INFO_0012 *virtual_addresses,
   UINT *kmt_count,
   D3D12DDI_DEBUG_KMT_ALLOCATION_INFO_0014 *kmt_allocations)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   const UINT virtual_address_capacity =
      virtual_address_count ? *virtual_address_count : 0;
   const UINT kmt_capacity = kmt_count ? *kmt_count : 0;
   D3DKMT_HANDLE allocation = 0;
   uint64_t start = 0;
   uint64_t size = 0;
   uint64_t allocation_offset = 0;

   if (!virtual_address_count || !kmt_count)
      return;
   *virtual_address_count = 0;
   *kmt_count = 0;
   if (!d12_umd_debug_object_info(device, &object, &allocation, &start,
                                  &size)) {
      d12_umd_set_device_error(device, E_INVALIDARG);
      return;
   }
   if (object.Type == D3D12DDI_HT_0012_RESOURCE) {
      const struct d12_umd_resource *resource = object.Handle;
      allocation_offset = resource->heap_offset;
   }
   if ((start && size && start > UINT64_MAX - size) ||
       (allocation_offset && size > UINT64_MAX - allocation_offset)) {
      d12_umd_set_device_error(device, E_FAIL);
      return;
   }
   if (allocation) {
      *kmt_count = 1;
      if (kmt_allocations && kmt_capacity) {
         kmt_allocations[0] = (D3D12DDI_DEBUG_KMT_ALLOCATION_INFO_0014){
            .PhysicalAdapterIndex = 0,
            .hAllocation = allocation,
            .Offset = allocation_offset,
            .Size = size,
         };
      }
   }
   if (start && size) {
      *virtual_address_count = 1;
      if (virtual_addresses && virtual_address_capacity) {
         virtual_addresses[0] =
            (D3D12DDI_DEBUG_VIRTUAL_ADDRESS_ALLOCATION_INFO_0012){
               .PhysicalAdapterIndex = 0,
               .StartAddress = start,
               .EndAddress = start + size,
            };
      }
   }
}
#endif
#endif

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

   d12_umd_sync_acquire_shared(&device->queue_lock);
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

struct d12_umd_residency_object_context {
   struct d12_umd_device *device;
   const D3D12DDI_HANDLE_AND_TYPE *objects;
};

static bool
d12_umd_resolve_residency_object(void *raw_context, UINT index,
                                 D3DKMT_HANDLE *allocation)
{
   const struct d12_umd_residency_object_context *context = raw_context;

   return context && d12_umd_residency_object_allocation(
                        context->device, &context->objects[index], allocation);
}

static HRESULT
d12_umd_translate_residency_objects(
   struct d12_umd_device *device, UINT count,
   const D3D12DDI_HANDLE_AND_TYPE *objects,
   struct d12_umd_residency_translation *translation)
{
   if (!device || !translation || (count && !objects))
      return E_INVALIDARG;
   struct d12_umd_residency_object_context context = {
      .device = device,
      .objects = objects,
   };
   return d12_umd_residency_translate(
      count, d12_umd_resolve_residency_object, &context, translation);
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
   if (!translation.set.allocation_count) {
      d12_umd_residency_translation_cleanup(&translation);
      return S_OK;
   }
   if (!device->core_callbacks.pfnMakeResidentCb) {
      d12_umd_residency_translation_cleanup(&translation);
      return E_INVALIDARG;
   }

   make_resident = (D3DDDI_MAKERESIDENT){
      .NumAllocations = translation.set.allocation_count,
      .AllocationList = translation.set.allocations,
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
   if (!translation.set.allocation_count) {
      d12_umd_residency_translation_cleanup(&translation);
      return S_OK;
   }
   if (!device->core_callbacks.pfnEvictCb) {
      d12_umd_residency_translation_cleanup(&translation);
      return E_INVALIDARG;
   }

   evict = (D3DDDICB_EVICT){
      .NumAllocations = translation.set.allocation_count,
      .AllocationList = translation.set.allocations,
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
   if (!translation.set.allocation_count) {
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
         .HandleList = translation.set.allocations,
         .NumAllocations = translation.set.allocation_count,
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
      .NumAllocations = translation.set.allocation_count,
      .HandleList = translation.set.allocations,
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
   if (!translation.set.allocation_count) {
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
   if (translation.set.allocation_count > ARRAYSIZE(stack_results)) {
      allocation_results = HeapAlloc(
         GetProcessHeap(), HEAP_ZERO_MEMORY,
         (size_t)translation.set.allocation_count *
            sizeof(*allocation_results));
      if (!allocation_results) {
         d12_umd_residency_release_queue_lock(device, queue_lock_held);
         d12_umd_residency_translation_cleanup(&translation);
         return E_OUTOFMEMORY;
      }
   }
   reclaim = (D3DDDICB_RECLAIMALLOCATIONS3){
      .PagingQueue = paging_queue,
      .NumAllocations = translation.set.allocation_count,
      .HandleList = translation.set.allocations,
      .pResults = allocation_results,
   };
   result = device->kernel_callbacks.pfnReclaimAllocations3Cb(
      device->runtime_device.handle, &reclaim);
   d12_umd_residency_release_queue_lock(device, queue_lock_held);
   if (SUCCEEDED(result) && reclaim.PagingFenceValue) {
      arguments->pPagingFenceValue[0] = reclaim.PagingFenceValue;
      arguments->WaitMask = 1;
   }
   for (UINT index = 0; index < translation.set.object_count; ++index) {
      const UINT allocation_index =
         translation.set.object_allocation_indices[index];
      if (allocation_index != D12_UMD_RESIDENCY_NO_ALLOCATION) {
         arguments->pDiscarded[index] =
            allocation_results[allocation_index] !=
            D3DDDI_RECLAIM_RESULT_OK;
      }
   }
   if (allocation_results != stack_results)
      HeapFree(GetProcessHeap(), 0, allocation_results);
#else
   if (!device->core_callbacks.pfnReclaimAllocations2Cb) {
      d12_umd_residency_translation_cleanup(&translation);
      return E_INVALIDARG;
   }
   memset(stack_discarded, 0, sizeof(stack_discarded));
   if (translation.set.allocation_count > ARRAYSIZE(stack_discarded)) {
      allocation_discarded = HeapAlloc(
         GetProcessHeap(), HEAP_ZERO_MEMORY,
         (size_t)translation.set.allocation_count *
            sizeof(*allocation_discarded));
      if (!allocation_discarded) {
         d12_umd_residency_translation_cleanup(&translation);
         return E_OUTOFMEMORY;
      }
   }

   reclaim = (D3D12DDICB_RECLAIMALLOCATIONS2){
      .NumAllocations = translation.set.allocation_count,
      .HandleList = translation.set.allocations,
      .pDiscarded = allocation_discarded,
   };
   result = device->core_callbacks.pfnReclaimAllocations2Cb(
      device->runtime_device, arguments->pRTPagingQueue[0], &reclaim);
   if (result == E_PENDING) {
      arguments->pPagingFenceValue[0] = reclaim.PagingFenceValue;
      arguments->WaitMask = 1;
   }
   for (UINT index = 0; index < translation.set.object_count; ++index) {
      const UINT allocation_index =
         translation.set.object_allocation_indices[index];
      if (allocation_index != D12_UMD_RESIDENCY_NO_ALLOCATION)
         arguments->pDiscarded[index] =
            allocation_discarded[allocation_index];
   }
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

#if D12_UMD_TARGETS_WDDM21
void
d12_umd_install_device_core_functions_0013(
   D3D12DDI_DEVICE_FUNCS_CORE_0013 *table)
{
   table->pfnGetDebugAllocationInfo =
      d12_umd_get_debug_allocation_info_0012;
}
#endif

#if D12_UMD_TARGETS_WDDM23
void
d12_umd_install_device_core_functions_0030(
   D3D12DDI_DEVICE_FUNCS_CORE_0030 *table)
{
   table->pfnCalcPrivateCommandQueueSize =
      d12_umd_calc_private_command_queue_size_0023;
   table->pfnCreateCommandQueue = d12_umd_create_command_queue_0023;
   table->pfnCalcPrivateHeapAndResourceSizes =
      d12_umd_calc_private_heap_resource_sizes_0030;
   table->pfnCreateHeapAndResource = d12_umd_create_heap_resource_0030;
   table->pfnCheckResourceAllocationInfo =
      d12_umd_check_resource_allocation_info_0022;
   table->pfnCheckExistingResourceAllocationInfo =
      d12_umd_check_existing_resource_allocation_info_0022;
   table->pfnGetDebugAllocationInfo =
      d12_umd_get_debug_allocation_info_0014;
}
#endif

#if D12_UMD_TARGETS_WDDM31
void
d12_umd_install_device_core_functions_0099(
   D3D12DDI_DEVICE_FUNCS_CORE_0099 *table)
{
   table->pfnCalcPrivateCommandQueueSize =
      d12_umd_calc_private_command_queue_size_0050;
   table->pfnCreateCommandQueue = d12_umd_create_command_queue_0050;
   table->pfnCalcPrivateHeapAndResourceSizes =
      d12_umd_calc_private_heap_resource_sizes_0088;
   table->pfnCreateHeapAndResource = d12_umd_create_heap_resource_0088;
   table->pfnCalcPrivateOpenedHeapAndResourceSizes =
      d12_umd_calc_private_opened_heap_resource_sizes_0043;
   table->pfnOpenHeapAndResource = d12_umd_open_heap_resource_0043;
   table->pfnCheckResourceAllocationInfo =
      d12_umd_check_resource_allocation_info_0088;
}
#endif

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
