/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * DXGI presentation handoff for the baseline D3D12 DDI.  The runtime owns
 * the actual Present call and scheduling policy; the UMD returns the WDDM
 * allocation and context handles which dxgkrnl already assigned.  A
 * composition destination is copied with the shared native renderer before
 * the handoff, while a flip-model source is passed through without extra GPU
 * work.
 */

#include "d12_umd_present_funcs.h"

#include "d12_umd_internal.h"
#include "d12_umd_residency.h"
#include "d12_umd_resource_command_funcs.h"

#include <string.h>

_Static_assert(sizeof(DXGI_DDI_BASE_ARGS) == 2u * sizeof(void *),
               "DXGI base arguments changed width");
#if D12_UMD_TARGETS_WDDM21
_Static_assert(sizeof(DXGI1_5_DDI_BASE_FUNCTIONS) == 22u * sizeof(void *),
               "WDDM 2.1 DXGI table changed width");
_Static_assert(offsetof(DXGI1_5_DDI_BASE_FUNCTIONS, pfnOfferResources1) ==
                  9u * sizeof(void *),
               "WDDM 2.1 DXGI offer slot moved");
_Static_assert(offsetof(DXGI1_5_DDI_BASE_FUNCTIONS, pfnReclaimResources1) ==
                  21u * sizeof(void *),
               "WDDM 2.1 DXGI reclaim slot moved");
#else
_Static_assert(sizeof(DXGI1_4_DDI_BASE_FUNCTIONS) == 21u * sizeof(void *),
               "WDDM 2.0 DXGI table changed width");
_Static_assert(offsetof(DXGI1_4_DDI_BASE_FUNCTIONS, pfnReserved1) ==
                  13u * sizeof(void *),
               "DXGI reserved slot 1 moved");
_Static_assert(offsetof(DXGI1_4_DDI_BASE_FUNCTIONS, pfnReserved2) ==
                  15u * sizeof(void *),
               "DXGI reserved slot 2 moved");
#endif
_Static_assert(sizeof(DXGI_DDI_ARG_OFFERRESOURCES) ==
                  2u * sizeof(void *) + 2u * sizeof(UINT),
               "DXGI offer-resources arguments changed width");
_Static_assert(sizeof(DXGI_DDI_ARG_RECLAIMRESOURCES) ==
                  4u * sizeof(void *),
               "DXGI reclaim-resources arguments changed width");
_Static_assert(sizeof(DXGI_DDI_ARG_TRIMRESIDENCYSET) == 24,
               "DXGI trim-residency arguments changed width");
_Static_assert(offsetof(DXGI_DDI_ARG_TRIMRESIDENCYSET, NumBytesToTrim) == 16,
               "DXGI trim-residency byte target moved");
_Static_assert(sizeof(DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES) == 24,
               "DXGI rotate-resource arguments changed width");
_Static_assert(offsetof(DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES,
                        pResources) == 8,
               "DXGI rotate-resource array moved");
_Static_assert(offsetof(DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES,
                        Resources) == 16,
               "DXGI rotate-resource count moved");
_Static_assert(sizeof(DXGI_DDI_ARG_BLT) == 64,
               "DXGI blit arguments changed width");
_Static_assert(offsetof(DXGI_DDI_ARG_BLT, hSrcResource) == 40 &&
                  offsetof(DXGI_DDI_ARG_BLT, Flags) == 52,
               "DXGI blit source/flags moved");
_Static_assert(sizeof(DXGI_DDI_ARG_BLT1) == 80,
               "DXGI blit1 arguments changed width");
_Static_assert(offsetof(DXGI_DDI_ARG_BLT1, hSrcResource) == 40 &&
                  offsetof(DXGI_DDI_ARG_BLT1, Flags) == 68,
               "DXGI blit1 source/flags moved");
_Static_assert(sizeof(DXGI_DDI_ARG_PRESENT) == 56,
               "DXGI present arguments changed width");
_Static_assert(offsetof(DXGI_DDI_ARG_PRESENT, hSurfaceToPresent) == 8 &&
                  offsetof(DXGI_DDI_ARG_PRESENT, hDstResource) == 24 &&
                  offsetof(DXGI_DDI_ARG_PRESENT, pDXGIContext) == 40 &&
                  offsetof(DXGI_DDI_ARG_PRESENT, Flags) == 48 &&
                  offsetof(DXGI_DDI_ARG_PRESENT, FlipInterval) == 52,
               "DXGI present fields moved");
_Static_assert(sizeof(DXGI_DDI_ARG_PRESENT1) == 80,
               "DXGI present1 arguments changed width");
_Static_assert(offsetof(DXGI_DDI_ARG_PRESENT1, phSurfacesToPresent) == 8 &&
                  offsetof(DXGI_DDI_ARG_PRESENT1, pDXGIContext) == 40 &&
                  offsetof(DXGI_DDI_ARG_PRESENT1,
                           BackBufferMultiplicity) == 76,
               "DXGI present1 fields moved");
_Static_assert(D12_UMD_S_NOT_RESIDENT == MAKE_HRESULT(0, 0x876, 2165),
               "Direct3D not-resident status changed value");
_Static_assert(D12_UMD_S_RESIDENT_IN_SHARED_MEMORY ==
                  MAKE_HRESULT(0, 0x876, 2166),
               "Direct3D shared-memory status changed value");

static struct d12_umd_resource *
d12_umd_present_resource(struct d12_umd_device *device,
                         D3D12DDI_HRESOURCE handle)
{
   struct d12_umd_resource *resource = handle.pDrvPrivate;
   return resource && resource->signature == D12_UMD_RESOURCE_SIGNATURE &&
                 resource->device == device &&
                 resource->allocation.allocation
             ? resource
             : NULL;
}

static HANDLE
d12_umd_present_context(struct d12_umd_command_queue *queue)
{
   struct d12_umd_device *device = queue->device;

   if (!queue->backend)
      return queue->runtime.context;
   if (!device->renderer_ops ||
       !device->renderer_ops->command_queue_context_token)
      return NULL;
   const uint32_t token =
      device->renderer_ops->command_queue_context_token(queue->backend);
   return token ? d12_umd_agx_transport_context_handle(
                     &device->agx_transport, token)
                : NULL;
}

static struct d12_umd_device *
d12_umd_dxgi_device(DXGI_DDI_HDEVICE handle)
{
   struct d12_umd_device *device =
      (struct d12_umd_device *)(uintptr_t)handle;
   return device && device->signature == D12_UMD_DEVICE_SIGNATURE ? device
                                                                  : NULL;
}

static struct d12_umd_resource *
d12_umd_dxgi_resource(struct d12_umd_device *device,
                      DXGI_DDI_HRESOURCE handle)
{
   D3D12DDI_HRESOURCE resource = {
      .pDrvPrivate = (void *)(uintptr_t)handle,
   };
   return d12_umd_present_resource(device, resource);
}

struct d12_umd_dxgi_residency_context {
   struct d12_umd_device *device;
   const DXGI_DDI_HRESOURCE *resources;
   bool reject_reserved;
};

static bool
d12_umd_dxgi_resolve_residency_resource(void *raw_context, UINT index,
                                        D3DKMT_HANDLE *allocation)
{
   const struct d12_umd_dxgi_residency_context *context = raw_context;
   struct d12_umd_resource *resource;

   if (!context || !allocation ||
       !(resource = d12_umd_dxgi_resource(
            context->device, context->resources[index])) ||
       (context->reject_reserved && resource->reserved))
      return false;
   *allocation = resource->allocation.allocation;
   return true;
}

static HRESULT
d12_umd_dxgi_translate_residency_resources(
   struct d12_umd_device *device, const DXGI_DDI_HRESOURCE *resources,
   UINT count, bool reject_reserved,
   struct d12_umd_residency_translation *translation)
{
   struct d12_umd_dxgi_residency_context context = {
      .device = device,
      .resources = resources,
      .reject_reserved = reject_reserved,
   };

   if (!device || (count && !resources))
      return E_INVALIDARG;
   return d12_umd_residency_translate(
      count, d12_umd_dxgi_resolve_residency_resource, &context, translation);
}

/* Called only while device->heap_lock is held. The runtime token survives
 * identity rotation but does not keep or expose runtime-owned heap storage. */
static struct d12_umd_heap *
d12_umd_live_resource_heap(struct d12_umd_device *device,
                           const struct d12_umd_resource *resource)
{
   if (!resource || !resource->runtime_heap_resource)
      return NULL;

   for (struct d12_umd_heap *heap = device->heaps; heap; heap = heap->next) {
      if (heap->runtime_resource == resource->runtime_heap_resource &&
          heap->signature == D12_UMD_HEAP_SIGNATURE &&
          heap->device == device)
         return heap;
   }
   return NULL;
}

static DXGI_DDI_BASE_CALLBACKS *
d12_umd_dxgi_callback_table(struct d12_umd_device *device)
{
   return device && device->dxgi_callbacks
             ? InterlockedCompareExchangePointer(
                  (PVOID volatile *)device->dxgi_callbacks, NULL, NULL)
             : NULL;
}

static HRESULT
d12_umd_dxgi_validate_present_mode(DXGI_DDI_PRESENT_FLAGS flags,
                                   DXGI_DDI_FLIP_INTERVAL_TYPE interval)
{
   /* The pinned dxgiddi.h defines bits 0..6. All remaining bits are the
    * Reserved field and must be zero. Blt and Flip describe mutually
    * exclusive presentation paths. The final enum value is the modern
    * immediate-plus-tearing spelling; accepting it is harmless on WDDM 2.0
    * because DXGI owns policy and will only supply it when negotiated. */
   if ((flags.Value & ~UINT32_C(0x7f)) || (flags.Blt && flags.Flip) ||
       (UINT)interval >
          (UINT)DXGI_DDI_FLIP_INTERVAL_IMMEDIATE_ALLOW_TEARING)
      return E_INVALIDARG;
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_dxgi_present(DXGI_DDI_ARG_PRESENT *arguments)
{
   struct d12_umd_device *device;
   struct d12_umd_resource *source;
   struct d12_umd_resource *destination = NULL;
   DXGI_DDI_BASE_CALLBACKS *callbacks;
   struct d12_umd_command_queue *queue;
   DXGIDDICB_PRESENT present;
   HRESULT result = E_FAIL;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       FAILED(d12_umd_dxgi_validate_present_mode(arguments->Flags,
                                                 arguments->FlipInterval)) ||
       !(source = d12_umd_dxgi_resource(
            device, arguments->hSurfaceToPresent)) ||
       arguments->SrcSubResourceIndex != 0)
      return E_INVALIDARG;
   if (arguments->hDstResource) {
      destination = d12_umd_dxgi_resource(device, arguments->hDstResource);
      if (!destination || arguments->DstSubResourceIndex != 0)
         return E_INVALIDARG;
   }
   callbacks = d12_umd_dxgi_callback_table(device);
   if (!callbacks || !callbacks->pfnPresentCb)
      return E_NOTIMPL;

   AcquireSRWLockShared(&device->queue_lock);
   for (queue = device->queues; queue; queue = queue->next) {
      if (queue->type == D12_UMD_RENDERER_COMMAND_DIRECT)
         break;
   }
   if (queue) {
      const HANDLE context = d12_umd_present_context(queue);
      if (context) {
         present = (DXGIDDICB_PRESENT){
            .hSrcAllocation = source->allocation.allocation,
            .hDstAllocation = destination
                                 ? destination->allocation.allocation
                                 : 0,
            .pDXGIContext = arguments->pDXGIContext,
            .hContext = context,
            .BroadcastContextCount = 0,
            .PrivateDriverDataSize = 0,
            .pPrivateDriverData = NULL,
            .bOptimizeForComposition = FALSE,
         };
         result = callbacks->pfnPresentCb(device->runtime_device.handle,
                                          &present);
      }
   }
   ReleaseSRWLockShared(&device->queue_lock);
   return result;
}

static HRESULT APIENTRY
d12_umd_dxgi_get_gamma_caps(DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS *arguments)
{
   if (!arguments || !d12_umd_dxgi_device(arguments->hDevice) ||
       !arguments->pGammaCapabilities)
      return E_INVALIDARG;
   memset(arguments->pGammaCapabilities, 0,
          sizeof(*arguments->pGammaCapabilities));
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_dxgi_set_display_mode(DXGI_DDI_ARG_SETDISPLAYMODE *arguments)
{
   return arguments && d12_umd_dxgi_device(arguments->hDevice)
             ? DXGI_DDI_ERR_UNSUPPORTED
             : E_INVALIDARG;
}

static HRESULT APIENTRY
d12_umd_dxgi_set_resource_priority(
   DXGI_DDI_ARG_SETRESOURCEPRIORITY *arguments)
{
   struct d12_umd_device *device;
   struct d12_umd_resource *resource;
   D3DKMT_HANDLE allocation;
   D3DDDICB_SETPRIORITY priority;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       !(resource = d12_umd_dxgi_resource(device, arguments->hResource)))
      return E_INVALIDARG;
   if (!device->kernel_callbacks.pfnSetPriorityCb)
      return E_NOTIMPL;
   allocation = resource->allocation.allocation;
   priority = (D3DDDICB_SETPRIORITY){
      .NumAllocations = 1,
      .HandleList = &allocation,
      .pPriorities = &arguments->Priority,
   };
   return device->kernel_callbacks.pfnSetPriorityCb(
      device->runtime_device.handle, &priority);
}

static HRESULT APIENTRY
d12_umd_dxgi_query_resource_residency(
   DXGI_DDI_ARG_QUERYRESOURCERESIDENCY *arguments)
{
   struct d12_umd_device *device;
   struct d12_umd_residency_translation translation;
   D3DDDI_RESIDENCYSTATUS stack_status[16];
   D3DDDI_RESIDENCYSTATUS *status = stack_status;
   D3DDDICB_QUERYRESIDENCY query;
   HRESULT result;
   BOOL any_not_resident = FALSE;
   BOOL any_shared_memory = FALSE;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       !arguments->Resources || arguments->Resources > UINT32_MAX ||
       !arguments->pResources || !arguments->pStatus)
      return E_INVALIDARG;
   if (!device->kernel_callbacks.pfnQueryResidencyCb)
      return E_NOTIMPL;
   result = d12_umd_dxgi_translate_residency_resources(
      device, arguments->pResources, (UINT)arguments->Resources,
      false, &translation);
   if (FAILED(result))
      return result;
   if (translation.set.allocation_count > ARRAYSIZE(stack_status)) {
      status = HeapAlloc(GetProcessHeap(), 0,
                         (size_t)translation.set.allocation_count *
                            sizeof(*status));
      if (!status) {
         result = E_OUTOFMEMORY;
         goto cleanup_translation;
      }
   }
   query = (D3DDDICB_QUERYRESIDENCY){
      .NumAllocations = translation.set.allocation_count,
      .HandleList = translation.set.allocations,
      .pResidencyStatus = status,
   };
   result = device->kernel_callbacks.pfnQueryResidencyCb(
      device->runtime_device.handle, &query);
   if (FAILED(result))
      goto cleanup;
   for (UINT index = 0; index < translation.set.allocation_count; ++index) {
      switch (status[index]) {
      case D3DDDI_RESIDENCYSTATUS_RESIDENTINSHAREDMEMORY:
         any_shared_memory = TRUE;
         break;
      case D3DDDI_RESIDENCYSTATUS_NOTRESIDENT:
         any_not_resident = TRUE;
         break;
      case D3DDDI_RESIDENCYSTATUS_RESIDENTINGPUMEMORY:
         break;
      default:
         result = E_FAIL;
         goto cleanup;
      }
   }
   for (SIZE_T index = 0; index < arguments->Resources; ++index) {
      switch (status[translation.set.object_allocation_indices[index]]) {
      case D3DDDI_RESIDENCYSTATUS_RESIDENTINGPUMEMORY:
         arguments->pStatus[index] = DXGI_DDI_RESIDENCY_FULLY_RESIDENT;
         break;
      case D3DDDI_RESIDENCYSTATUS_RESIDENTINSHAREDMEMORY:
         arguments->pStatus[index] =
            DXGI_DDI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY;
         break;
      default:
         arguments->pStatus[index] = DXGI_DDI_RESIDENCY_EVICTED_TO_DISK;
         break;
      }
   }
   if (any_not_resident)
      result = D12_UMD_S_NOT_RESIDENT;
   else if (any_shared_memory)
      result = D12_UMD_S_RESIDENT_IN_SHARED_MEMORY;

cleanup:
   if (status != stack_status)
      HeapFree(GetProcessHeap(), 0, status);
cleanup_translation:
   d12_umd_residency_translation_cleanup(&translation);
   return result;
}

static HRESULT APIENTRY
d12_umd_dxgi_rotate_resource_identities(
   DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *arguments)
{
   struct d12_umd_device *device;
   struct d12_umd_resource *stack_resources[8];
   struct d12_umd_heap *stack_heaps[8];
   struct d12_umd_allocation stack_allocations[8];
   void *stack_backends[8];
   struct d12_umd_resource **resources = stack_resources;
   struct d12_umd_heap **heaps = stack_heaps;
   struct d12_umd_allocation *allocations = stack_allocations;
   void **backends = stack_backends;
   HRESULT result = E_INVALIDARG;
   UINT locks_held = 0;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       (arguments->Resources && !arguments->pResources))
      return E_INVALIDARG;
   if (!arguments->Resources)
      return S_OK;
   if (arguments->Resources > ARRAYSIZE(stack_resources)) {
      if ((size_t)arguments->Resources > SIZE_MAX / sizeof(*resources) ||
          (size_t)arguments->Resources > SIZE_MAX / sizeof(*heaps) ||
          (size_t)arguments->Resources > SIZE_MAX / sizeof(*allocations) ||
          (size_t)arguments->Resources > SIZE_MAX / sizeof(*backends))
         return E_OUTOFMEMORY;
      resources = HeapAlloc(GetProcessHeap(), 0,
                            (size_t)arguments->Resources *
                               sizeof(*resources));
      heaps = HeapAlloc(GetProcessHeap(), 0,
                        (size_t)arguments->Resources * sizeof(*heaps));
      allocations = HeapAlloc(GetProcessHeap(), 0,
                              (size_t)arguments->Resources *
                                 sizeof(*allocations));
      backends = HeapAlloc(GetProcessHeap(), 0,
                           (size_t)arguments->Resources *
                              sizeof(*backends));
      if (!resources || !heaps || !allocations || !backends) {
         result = E_OUTOFMEMORY;
         goto cleanup;
      }
   }

   AcquireSRWLockExclusive(&device->rotation_lock);
   AcquireSRWLockShared(&device->heap_lock);
   for (UINT i = 0; i < arguments->Resources; ++i) {
      resources[i] = d12_umd_dxgi_resource(device,
                                           arguments->pResources[i]);
      heaps[i] = d12_umd_live_resource_heap(device, resources[i]);
      if (!heaps[i])
         goto unlock_rotation;
      allocations[i] = resources[i]->allocation;
      backends[i] = resources[i]->backend;
      for (UINT j = 0; j < i; ++j) {
         if (resources[j] == resources[i] || heaps[j] == heaps[i])
            goto unlock_rotation;
      }

      const D3D12DDIARG_CREATERESOURCE_0003 *a = &resources[0]->desc;
      const D3D12DDIARG_CREATERESOURCE_0003 *b = &resources[i]->desc;
      if (i && (a->ResourceType != b->ResourceType || a->Width != b->Width ||
                a->Height != b->Height ||
                a->DepthOrArraySize != b->DepthOrArraySize ||
                a->MipLevels != b->MipLevels || a->Format != b->Format ||
                a->SampleDesc.Count != b->SampleDesc.Count ||
                a->SampleDesc.Quality != b->SampleDesc.Quality ||
                a->Layout != b->Layout || a->Flags != b->Flags ||
                (!!a->pRowMajorLayout != !!b->pRowMajorLayout) ||
                (a->pRowMajorLayout &&
                 (resources[0]->row_major.RowPitch !=
                     resources[i]->row_major.RowPitch ||
                  resources[0]->row_major.SlicePitch !=
                     resources[i]->row_major.SlicePitch)) ||
                heaps[0]->allocation.size != heaps[i]->allocation.size ||
                heaps[0]->cpu_page_property != heaps[i]->cpu_page_property))
         goto unlock_rotation;
   }

   if (arguments->Resources == 1) {
      result = S_OK;
      goto unlock_rotation;
   }

   /* A CPU pointer cannot change allocation identity underneath its caller.
    * Serialize against every heap map and retain the locks until the AGX BO,
    * descriptors and frontend allocation handles all agree. */
   for (UINT i = 0; i < arguments->Resources; ++i) {
      AcquireSRWLockExclusive(&heaps[i]->map_lock);
      ++locks_held;
      if (heaps[i]->map_count || heaps[i]->mapping)
         goto unlock_maps;
   }

   if (device->backend) {
      if (!device->renderer_ops ||
          !device->renderer_ops->rotate_resource_identities) {
         result = E_NOTIMPL;
         goto unlock_maps;
      }
      for (UINT i = 0; i < arguments->Resources; ++i) {
         if (!heaps[i]->backend || !backends[i]) {
            result = E_FAIL;
            goto unlock_maps;
         }
      }
      result = device->renderer_ops->rotate_resource_identities(
         device->backend, backends, arguments->Resources);
      if (FAILED(result))
         goto unlock_maps;
   }

   /* The renderer keeps object pointers and runtime lifetime tokens fixed.
    * Rotate only the WDDM allocation/KMD resource/GPU-VA identity cached by
    * the DDI wrappers. */
   for (UINT i = 0; i + 1 < arguments->Resources; ++i) {
      heaps[i]->allocation = allocations[i + 1];
      resources[i]->allocation = allocations[i + 1];
   }
   heaps[arguments->Resources - 1]->allocation = allocations[0];
   resources[arguments->Resources - 1]->allocation = allocations[0];
   result = S_OK;

unlock_maps:
   while (locks_held)
      ReleaseSRWLockExclusive(&heaps[--locks_held]->map_lock);
unlock_rotation:
   ReleaseSRWLockShared(&device->heap_lock);
   ReleaseSRWLockExclusive(&device->rotation_lock);
cleanup:
   if (resources != stack_resources) {
      HeapFree(GetProcessHeap(), 0, resources);
      HeapFree(GetProcessHeap(), 0, heaps);
      HeapFree(GetProcessHeap(), 0, allocations);
      HeapFree(GetProcessHeap(), 0, backends);
   }
   return result;
}

static HRESULT
d12_umd_dxgi_execute_blt(struct d12_umd_device *device,
                         struct d12_umd_resource *destination,
                         struct d12_umd_resource *source,
                         const struct d12_umd_blt_desc *desc)
{
   struct d12_umd_command_queue *queue;
   HRESULT result = E_NOTIMPL;

   result = d12_umd_validate_blt(device, destination, source, desc);
   if (FAILED(result))
      return result == E_NOTIMPL ? DXGI_DDI_ERR_UNSUPPORTED : result;
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->create_command_allocator ||
       !device->renderer_ops->reset_command_allocator ||
       !device->renderer_ops->destroy_command_allocator ||
       !device->renderer_ops->create_command_list ||
       !device->renderer_ops->reset_command_list ||
       !device->renderer_ops->close_command_list ||
       !device->renderer_ops->destroy_command_list ||
       !device->renderer_ops->execute_command_lists)
      return DXGI_DDI_ERR_UNSUPPORTED;

   /* The DXGI base DDI does not pass an application command-list handle.
    * Record into a queue-local native list and submit it to the device's live
    * direct queue. Holding queue_lock keeps the context alive through Execute;
    * the operation is ordered with the subsequent Present callback on that
    * same WDDM context. The per-queue lock serializes the internal recorder,
    * allowing the steady-state path to reset it without allocator/list churn. */
   AcquireSRWLockShared(&device->queue_lock);
   for (queue = device->queues; queue; queue = queue->next) {
      if (queue->type == D12_UMD_RENDERER_COMMAND_DIRECT && queue->backend)
         break;
   }
   if (!queue)
      goto cleanup;

   AcquireSRWLockExclusive(&queue->dxgi_blt_lock);
   if (!queue->dxgi_blt_allocator) {
      result = device->renderer_ops->create_command_allocator(
         device->backend, D12_UMD_RENDERER_COMMAND_DIRECT,
         &queue->dxgi_blt_allocator);
      result = d12_umd_renderer_object_result(
         result, &queue->dxgi_blt_allocator,
         device->renderer_ops->destroy_command_allocator);
      if (FAILED(result))
         goto discard_queue;
      result = device->renderer_ops->create_command_list(
         device->backend, D12_UMD_RENDERER_COMMAND_DIRECT,
         queue->dxgi_blt_allocator, &queue->dxgi_blt_list);
      result = d12_umd_renderer_object_result(
         result, &queue->dxgi_blt_list,
         device->renderer_ops->destroy_command_list);
   } else {
      result = device->renderer_ops->reset_command_allocator(
         queue->dxgi_blt_allocator);
      if (SUCCEEDED(result))
         result = device->renderer_ops->reset_command_list(
            queue->dxgi_blt_list, queue->dxgi_blt_allocator);
   }
   if (FAILED(result))
      goto discard_queue;

   result = d12_umd_record_blt(device, queue->dxgi_blt_list, destination,
                               source, desc);
   if (FAILED(result))
      goto discard_queue;
   result = device->renderer_ops->close_command_list(queue->dxgi_blt_list);
   if (FAILED(result))
      goto discard_queue;
   result = device->renderer_ops->execute_command_lists(
      queue->backend, 1, &queue->dxgi_blt_list);
   if (FAILED(result))
      goto discard_queue;
   goto cleanup_queue;

discard_queue:
   if (queue->dxgi_blt_list)
      device->renderer_ops->destroy_command_list(queue->dxgi_blt_list);
   if (queue->dxgi_blt_allocator)
      device->renderer_ops->destroy_command_allocator(
         queue->dxgi_blt_allocator);
   queue->dxgi_blt_list = NULL;
   queue->dxgi_blt_allocator = NULL;

cleanup_queue:
   ReleaseSRWLockExclusive(&queue->dxgi_blt_lock);
cleanup:
   ReleaseSRWLockShared(&device->queue_lock);
   return result == E_NOTIMPL ? DXGI_DDI_ERR_UNSUPPORTED : result;
}

static HRESULT APIENTRY
d12_umd_dxgi_blt(DXGI_DDI_ARG_BLT *arguments)
{
   struct d12_umd_device *device;
   struct d12_umd_resource *destination;
   struct d12_umd_resource *source;
   UINT source_width;
   UINT source_height;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       !(destination = d12_umd_dxgi_resource(
            device, arguments->hDstResource)) ||
       !(source = d12_umd_dxgi_resource(device,
                                        arguments->hSrcResource)) ||
       !d12_umd_resource_subresource_extent(
          source, arguments->SrcSubresource, &source_width, &source_height))
      return E_INVALIDARG;

   const struct d12_umd_blt_desc desc = {
      .destination_subresource = arguments->DstSubresource,
      .destination_left = arguments->DstLeft,
      .destination_top = arguments->DstTop,
      .destination_right = arguments->DstRight,
      .destination_bottom = arguments->DstBottom,
      .source_subresource = arguments->SrcSubresource,
      .source_left = 0,
      .source_top = 0,
      .source_right = source_width,
      .source_bottom = source_height,
      .flags = arguments->Flags.Value,
      .rotation = arguments->Rotate,
   };
   return d12_umd_dxgi_execute_blt(device, destination, source, &desc);
}

static HRESULT APIENTRY
d12_umd_dxgi_resolve_shared_resource(
   DXGI_DDI_ARG_RESOLVESHAREDRESOURCE *arguments)
{
   struct d12_umd_device *device;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       !d12_umd_dxgi_resource(device, arguments->hResource))
      return E_INVALIDARG;

   /* ExecuteCommandLists hands closed native command buffers to dxgkrnl
    * immediately and the WDDM 2.0 target exposes one physical adapter, so
    * there is no split-GPU backing store or deferred UMD batch to merge here.
    * The no-redirection CreateDevice status keeps foreign-UMD ownership out
    * of this baseline until that metadata contract is implemented. */
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_dxgi_blt1(DXGI_DDI_ARG_BLT1 *arguments)
{
   struct d12_umd_device *device;
   struct d12_umd_resource *destination;
   struct d12_umd_resource *source;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       !(destination = d12_umd_dxgi_resource(
            device, arguments->hDstResource)) ||
       !(source = d12_umd_dxgi_resource(device,
                                        arguments->hSrcResource)))
      return E_INVALIDARG;

   const struct d12_umd_blt_desc desc = {
      .destination_subresource = arguments->DstSubresource,
      .destination_left = arguments->DstLeft,
      .destination_top = arguments->DstTop,
      .destination_right = arguments->DstRight,
      .destination_bottom = arguments->DstBottom,
      .source_subresource = arguments->SrcSubresource,
      .source_left = arguments->SrcLeft,
      .source_top = arguments->SrcTop,
      .source_right = arguments->SrcRight,
      .source_bottom = arguments->SrcBottom,
      .flags = arguments->Flags.Value,
      .rotation = arguments->Rotate,
   };
   return d12_umd_dxgi_execute_blt(device, destination, source, &desc);
}

#if !D12_UMD_TARGETS_WDDM21
static HRESULT APIENTRY
d12_umd_dxgi_offer_resources(DXGI_DDI_ARG_OFFERRESOURCES *arguments)
{
   struct d12_umd_device *device;
   struct d12_umd_residency_translation translation;
   D3DDDICB_OFFERALLOCATIONS offer;
   HRESULT result;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       (arguments->Resources && !arguments->pResources) ||
       (UINT)arguments->Priority > (UINT)D3DDDI_OFFER_PRIORITY_AUTO)
      return E_INVALIDARG;
   if (!arguments->Resources)
      return S_OK;
   if (!device->kernel_callbacks.pfnOfferAllocationsCb)
      return E_NOTIMPL;
   result = d12_umd_dxgi_translate_residency_resources(
      device, arguments->pResources, arguments->Resources, false,
      &translation);
   if (FAILED(result))
      return result;
   offer = (D3DDDICB_OFFERALLOCATIONS){
      .HandleList = translation.set.allocations,
      .NumAllocations = translation.set.allocation_count,
      .Priority = arguments->Priority,
   };
   result = device->kernel_callbacks.pfnOfferAllocationsCb(
      device->runtime_device.handle, &offer);
   d12_umd_residency_translation_cleanup(&translation);
   return result;
}
#endif

#if D12_UMD_TARGETS_WDDM21
static HRESULT APIENTRY
d12_umd_dxgi_offer_resources1(DXGI_DDI_ARG_OFFERRESOURCES1 *arguments)
{
   struct d12_umd_device *device;
   struct d12_umd_residency_translation translation;
   HRESULT result;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       (arguments->Resources && !arguments->pResources) ||
       (UINT)arguments->Priority > (UINT)D3DDDI_OFFER_PRIORITY_AUTO ||
       (arguments->Flags.Value & ~1u) != 0u)
      return E_INVALIDARG;
   if (!arguments->Resources)
      return S_OK;
   if (!device->kernel_callbacks.pfnOfferAllocations2Cb)
      return E_NOTIMPL;
   result = d12_umd_dxgi_translate_residency_resources(
      device, arguments->pResources, arguments->Resources, true,
      &translation);
   if (FAILED(result))
      return result;
   {
      const D3DDDICB_OFFERALLOCATIONS2 offer = {
         .HandleList = translation.set.allocations,
         .NumAllocations = translation.set.allocation_count,
         .Priority = arguments->Priority,
         .Flags = arguments->Flags,
      };
      result = device->kernel_callbacks.pfnOfferAllocations2Cb(
         device->runtime_device.handle, &offer);
   }
   d12_umd_residency_translation_cleanup(&translation);
   return result;
}
#endif

static HRESULT APIENTRY
d12_umd_dxgi_reclaim_resources(DXGI_DDI_ARG_RECLAIMRESOURCES *arguments)
{
   struct d12_umd_device *device;
   struct d12_umd_residency_translation translation;
   BOOL stack_discarded[16] = {0};
   BOOL *allocation_discarded = stack_discarded;
   D3DDDICB_RECLAIMALLOCATIONS reclaim;
   HRESULT result;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       (arguments->Resources && !arguments->pResources))
      return E_INVALIDARG;
   if (!arguments->Resources)
      return S_OK;
   if (!device->kernel_callbacks.pfnReclaimAllocationsCb)
      return E_NOTIMPL;
   result = d12_umd_dxgi_translate_residency_resources(
      device, arguments->pResources, arguments->Resources, false,
      &translation);
   if (FAILED(result))
      return result;
   if (translation.set.allocation_count > ARRAYSIZE(stack_discarded)) {
      allocation_discarded = HeapAlloc(
         GetProcessHeap(), HEAP_ZERO_MEMORY,
         (size_t)translation.set.allocation_count *
            sizeof(*allocation_discarded));
      if (!allocation_discarded) {
         result = E_OUTOFMEMORY;
         goto cleanup;
      }
   }
   reclaim = (D3DDDICB_RECLAIMALLOCATIONS){
      .HandleList = translation.set.allocations,
      .pDiscarded = allocation_discarded,
      .NumAllocations = translation.set.allocation_count,
   };
   result = device->kernel_callbacks.pfnReclaimAllocationsCb(
      device->runtime_device.handle, &reclaim);
   if (SUCCEEDED(result) && arguments->pDiscarded) {
      for (UINT index = 0; index < arguments->Resources; ++index) {
         arguments->pDiscarded[index] = allocation_discarded[
            translation.set.object_allocation_indices[index]];
      }
   }

cleanup:
   if (allocation_discarded != stack_discarded)
      HeapFree(GetProcessHeap(), 0, allocation_discarded);
   d12_umd_residency_translation_cleanup(&translation);
   return result;
}

#if D12_UMD_TARGETS_WDDM21
static HRESULT APIENTRY
d12_umd_dxgi_reclaim_resources1(DXGI_DDI_ARG_RECLAIMRESOURCES1 *arguments)
{
   struct d12_umd_device *device;
   struct d12_umd_residency_translation translation;
   D3DDDI_RECLAIM_RESULT stack_results[16];
   D3DDDI_RECLAIM_RESULT *allocation_results = stack_results;
   D3DKMT_HANDLE paging_queue;
   HRESULT result;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       (arguments->Resources && !arguments->pResources))
      return E_INVALIDARG;
   if (!arguments->Resources)
      return S_OK;
   if (!device->kernel_callbacks.pfnReclaimAllocations3Cb ||
       !device->backend || !device->renderer_ops ||
       !device->renderer_ops->paging_queue ||
       !device->renderer_ops->wait_paging ||
       !(paging_queue = device->renderer_ops->paging_queue(device->backend)))
      return E_NOTIMPL;
   result = d12_umd_dxgi_translate_residency_resources(
      device, arguments->pResources, arguments->Resources, true,
      &translation);
   if (FAILED(result))
      return result;
   if (translation.set.allocation_count > ARRAYSIZE(stack_results)) {
      allocation_results = HeapAlloc(
         GetProcessHeap(), 0,
         (size_t)translation.set.allocation_count *
            sizeof(*allocation_results));
      if (!allocation_results) {
         result = E_OUTOFMEMORY;
         goto cleanup;
      }
   }
   {
      D3DDDICB_RECLAIMALLOCATIONS3 reclaim = {
         .PagingQueue = paging_queue,
         .NumAllocations = translation.set.allocation_count,
         .HandleList = translation.set.allocations,
         .pResults = allocation_results,
      };
      result = device->kernel_callbacks.pfnReclaimAllocations3Cb(
         device->runtime_device.handle, &reclaim);
      if (SUCCEEDED(result) && reclaim.PagingFenceValue)
         result = device->renderer_ops->wait_paging(
            device->backend, reclaim.PagingFenceValue);
   }
   if (SUCCEEDED(result) && arguments->pResults) {
      for (UINT index = 0; index < arguments->Resources; ++index) {
         arguments->pResults[index] = allocation_results[
            translation.set.object_allocation_indices[index]];
      }
   }

cleanup:
   if (allocation_results != stack_results)
      HeapFree(GetProcessHeap(), 0, allocation_results);
   d12_umd_residency_translation_cleanup(&translation);
   return result;
}
#endif

static HRESULT APIENTRY
d12_umd_dxgi_get_mpo_caps(DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS *arguments)
{
   if (!arguments || !d12_umd_dxgi_device(arguments->hDevice))
      return E_INVALIDARG;
   memset(&arguments->MultiplaneOverlayCaps, 0,
          sizeof(arguments->MultiplaneOverlayCaps));
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_dxgi_get_mpo_group_caps(
   DXGI_DDI_ARG_GETMULTIPLANEOVERLAYGROUPCAPS *arguments)
{
   if (!arguments || !d12_umd_dxgi_device(arguments->hDevice))
      return E_INVALIDARG;
   memset(&arguments->MultiplaneOverlayGroupCaps, 0,
          sizeof(arguments->MultiplaneOverlayGroupCaps));
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_dxgi_present_mpo(DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY *arguments)
{
   return arguments && d12_umd_dxgi_device(arguments->hDevice)
             ? DXGI_DDI_ERR_UNSUPPORTED
             : E_INVALIDARG;
}

static HRESULT APIENTRY
d12_umd_dxgi_present1(DXGI_DDI_ARG_PRESENT1 *arguments)
{
   DXGI_DDI_ARG_PRESENT present;
   struct d12_umd_device *device;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       !arguments->SurfacesToPresent || !arguments->phSurfacesToPresent ||
       (arguments->DirtyRects && !arguments->pDirtyRects) ||
       arguments->Reserved ||
       FAILED(d12_umd_dxgi_validate_present_mode(arguments->Flags,
                                                 arguments->FlipInterval)) ||
       (arguments->SurfacesToPresent > 1 && arguments->hDstResource))
      return E_INVALIDARG;

   /* Microsoft requires one Present callback even for a stereo/multi-surface
    * array and specifies that only the final source handle is translated.
    * Validate every earlier handle so a malformed list cannot smuggle a
    * foreign driver's private pointer through the array. */
   for (UINT i = 0; i < arguments->SurfacesToPresent; ++i) {
      struct d12_umd_resource *resource = d12_umd_dxgi_resource(
         device, arguments->phSurfacesToPresent[i].hSurface);
      if (!resource || arguments->phSurfacesToPresent[i].SubResourceIndex)
         return E_INVALIDARG;
   }

   arguments->BackBufferMultiplicity = 1;
   const UINT last = arguments->SurfacesToPresent - 1;
   present = (DXGI_DDI_ARG_PRESENT){
      .hDevice = arguments->hDevice,
      .hSurfaceToPresent = arguments->phSurfacesToPresent[last].hSurface,
      .SrcSubResourceIndex =
         arguments->phSurfacesToPresent[last].SubResourceIndex,
      .hDstResource = arguments->hDstResource,
      .DstSubResourceIndex = arguments->DstSubResourceIndex,
      .pDXGIContext = arguments->pDXGIContext,
      .Flags = arguments->Flags,
      .FlipInterval = arguments->FlipInterval,
   };
   return d12_umd_dxgi_present(&present);
}

static HRESULT APIENTRY
d12_umd_dxgi_check_present_duration(
   DXGI_DDI_ARG_CHECKPRESENTDURATIONSUPPORT *arguments)
{
   if (!arguments || !d12_umd_dxgi_device(arguments->hDevice))
      return E_INVALIDARG;
   arguments->ClosestSmallerDuration = 0;
   arguments->ClosestLargerDuration = 0;
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_dxgi_trim_residency(DXGI_DDI_ARG_TRIMRESIDENCYSET *arguments)
{
   struct d12_umd_device *device;
   const UINT flags = arguments ? arguments->TrimFlags.Value : 0;

   if (!arguments || !(device = d12_umd_dxgi_device(arguments->hDevice)) ||
       (flags & ~UINT32_C(0x7)) ||
       (arguments->TrimFlags.PeriodicTrim &&
        arguments->TrimFlags.RestartPeriodicTrim))
      return E_INVALIDARG;

   /* Application heaps live on VidMm's explicit device residency list and
    * must not be guessed or evicted here. Trim only renderer-internal BOs
    * which have reached the shared AGX cache (refcount zero). Generation
    * tracking retains BOs referenced since the prior periodic request, while
    * TrimToBudget evicts the oldest cache entries only up to its byte target. */
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->trim_memory)
      return S_OK;
   return device->renderer_ops->trim_memory(
      device->backend, arguments->TrimFlags.PeriodicTrim != 0,
      arguments->TrimFlags.RestartPeriodicTrim != 0,
      arguments->TrimFlags.TrimToBudget ? arguments->NumBytesToTrim : 0);
}

static HRESULT APIENTRY
d12_umd_dxgi_check_mpo_color_space(
   DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT *arguments)
{
   if (!arguments || !d12_umd_dxgi_device(arguments->hDevice))
      return E_INVALIDARG;
   arguments->Supported = FALSE;
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_dxgi_present_mpo1(DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY1 *arguments)
{
   return arguments && d12_umd_dxgi_device(arguments->hDevice)
             ? DXGI_DDI_ERR_UNSUPPORTED
             : E_INVALIDARG;
}

HRESULT
d12_umd_fill_dxgi_table(void *table, SIZE_T table_size, UINT table_number)
{
   DXGI_DDI_BASE_ARGS *arguments = table;
#if D12_UMD_TARGETS_WDDM21
   DXGI1_5_DDI_BASE_FUNCTIONS *functions;
#else
   DXGI1_4_DDI_BASE_FUNCTIONS *functions;
#endif

   if (!arguments || table_size != sizeof(*arguments) || table_number != 0 ||
       !arguments->pDXGIBaseCallbacks ||
#if D12_UMD_TARGETS_WDDM21
       !(functions = arguments->pDXGIDDIBaseFunctions6))
#else
       !(functions = arguments->pDXGIDDIBaseFunctions5))
#endif
      return E_INVALIDARG;

   memset(functions, 0, sizeof(*functions));
   functions->pfnPresent = d12_umd_dxgi_present;
   functions->pfnGetGammaCaps = d12_umd_dxgi_get_gamma_caps;
   functions->pfnSetDisplayMode = d12_umd_dxgi_set_display_mode;
   functions->pfnSetResourcePriority = d12_umd_dxgi_set_resource_priority;
   functions->pfnQueryResourceResidency =
      d12_umd_dxgi_query_resource_residency;
   functions->pfnRotateResourceIdentities =
      d12_umd_dxgi_rotate_resource_identities;
   functions->pfnBlt = d12_umd_dxgi_blt;
   functions->pfnResolveSharedResource =
      d12_umd_dxgi_resolve_shared_resource;
   functions->pfnBlt1 = d12_umd_dxgi_blt1;
#if D12_UMD_TARGETS_WDDM21
   functions->pfnOfferResources1 = d12_umd_dxgi_offer_resources1;
#else
   functions->pfnOfferResources = d12_umd_dxgi_offer_resources;
#endif
   functions->pfnReclaimResources = d12_umd_dxgi_reclaim_resources;
   functions->pfnGetMultiplaneOverlayCaps = d12_umd_dxgi_get_mpo_caps;
   functions->pfnGetMultiplaneOverlayGroupCaps =
      d12_umd_dxgi_get_mpo_group_caps;
   functions->pfnPresentMultiplaneOverlay = d12_umd_dxgi_present_mpo;
   functions->pfnPresent1 = d12_umd_dxgi_present1;
   functions->pfnCheckPresentDurationSupport =
      d12_umd_dxgi_check_present_duration;
   functions->pfnTrimResidencySet = d12_umd_dxgi_trim_residency;
   functions->pfnCheckMultiplaneOverlayColorSpaceSupport =
      d12_umd_dxgi_check_mpo_color_space;
   functions->pfnPresentMultiplaneOverlay1 = d12_umd_dxgi_present_mpo1;
#if D12_UMD_TARGETS_WDDM21
   functions->pfnReclaimResources1 = d12_umd_dxgi_reclaim_resources1;
#endif
   return S_OK;
}

static UINT APIENTRY
d12_umd_get_present_private_driver_data_size(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_PRESENT_0001 *arguments)
{
   (void)handle;
   (void)arguments;
   return 0;
}

static void APIENTRY
d12_umd_present(D3D12DDI_HCOMMANDLIST list_handle,
                D3D12DDI_HCOMMANDQUEUE queue_handle,
                const D3D12DDIARG_PRESENT_0001 *arguments,
                D3D12DDI_PRESENT_0003 *present)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_command_queue *queue = queue_handle.pDrvPrivate;
   struct d12_umd_device *device;
   struct d12_umd_resource *source;
   struct d12_umd_resource *destination = NULL;
   HANDLE context;

   if (!queue || queue->signature != D12_UMD_COMMAND_QUEUE_SIGNATURE ||
       !queue->device || !present)
      return;
   device = queue->device;
   memset(present, 0, sizeof(*present));

   if (!arguments || !list || list->device != device ||
       list->type != D12_UMD_RENDERER_COMMAND_DIRECT ||
       queue->type != D12_UMD_RENDERER_COMMAND_DIRECT ||
       arguments->SurfacesToPresent != 1 ||
       !arguments->phSurfacesToPresent ||
       arguments->PrivateDriverDataSize) {
      d12_umd_report_command_list_error(list, E_INVALIDARG);
      return;
   }
   source = d12_umd_present_resource(
      device, arguments->phSurfacesToPresent[0].hSurface);
   if (!source) {
      d12_umd_report_command_list_error(list, E_INVALIDARG);
      return;
   }
   if (arguments->phSurfacesToPresent[0].SubResourceIndex != 0) {
      d12_umd_report_command_list_error(list, E_INVALIDARG);
      return;
   }
   if (arguments->hDstResource.pDrvPrivate) {
      destination =
         d12_umd_present_resource(device, arguments->hDstResource);
      if (!destination || arguments->DstSubResourceIndex != 0 ||
          !list->backend || !source->backend || !destination->backend ||
          !device->renderer_ops || !device->renderer_ops->copy_resource) {
         d12_umd_report_command_list_error(list, E_INVALIDARG);
         return;
      }
      device->renderer_ops->copy_resource(
         list->backend, destination->backend, source->backend);
      present->AddedGpuWork = TRUE;
   }

   context = d12_umd_present_context(queue);
   if (!context) {
      d12_umd_report_command_list_error(list, E_FAIL);
      return;
   }
   present->hSrcAllocation = source->allocation.allocation;
   present->hDstAllocation =
      destination ? destination->allocation.allocation : 0;
   present->hContext = context;
   present->BroadcastContextCount = 0;
   present->BackBufferMultiplicity = 1;
}

void
d12_umd_install_present_device_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table)
{
   table->pfnGetPresentPrivateDriverDataSize =
      d12_umd_get_present_private_driver_data_size;
}

void
d12_umd_install_present_command_list_functions(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *table)
{
   table->pfnPresent = d12_umd_present;
}
