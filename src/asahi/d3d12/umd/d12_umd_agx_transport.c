/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Adapter from Mesa's frontend-neutral AGX WDDM winsys operations to the
 * callback ABI supplied to a D3D12 UMD by the Direct3D runtime.
 */

#include "d12_umd_agx_transport.h"

#include "d12_umd_sync.h"

#include <stdlib.h>
#include <string.h>

#define D12_UMD_STATUS_INVALID_PARAMETER ((agx_kmt_status)0xc000000du)
#define D12_UMD_STATUS_NO_MEMORY         ((agx_kmt_status)0xc0000017u)
#define D12_UMD_STATUS_NOT_SUPPORTED     ((agx_kmt_status)0xc00000bbu)
#define D12_UMD_STATUS_PENDING           ((agx_kmt_status)0x00000103u)

_Static_assert(sizeof(SIZE_T) == sizeof(uint64_t),
               "native ARM64 cache ranges are 64-bit");

static agx_kmt_status
d12_umd_status(HRESULT result)
{
   return (agx_kmt_status)result;
}

HANDLE
d12_umd_agx_transport_context_handle(
   struct d12_umd_agx_transport *transport, uint32_t token)
{
   HANDLE context = NULL;
   if (!token || token > D12_UMD_AGX_CONTEXT_CAPACITY)
      return NULL;

   d12_umd_sync_acquire_shared(&transport->context_lock);
   context = transport->contexts[token - 1u].runtime_context;
   ReleaseSRWLockShared(&transport->context_lock);
   return context;
}


#define d12_umd_context_lookup d12_umd_agx_transport_context_handle

static agx_kmt_handle
d12_umd_context_insert(struct d12_umd_agx_transport *transport,
                       HANDLE runtime_context)
{
   agx_kmt_handle token = 0;
   d12_umd_sync_acquire_exclusive(&transport->context_lock);
   for (uint32_t index = 0; index < D12_UMD_AGX_CONTEXT_CAPACITY; ++index) {
      if (!transport->contexts[index].runtime_context) {
         transport->contexts[index].runtime_context = runtime_context;
         token = index + 1u;
         break;
      }
   }
   ReleaseSRWLockExclusive(&transport->context_lock);
   return token;
}

static void
d12_umd_context_remove(struct d12_umd_agx_transport *transport,
                       agx_kmt_handle token, HANDLE expected)
{
   if (!token || token > D12_UMD_AGX_CONTEXT_CAPACITY)
      return;

   d12_umd_sync_acquire_exclusive(&transport->context_lock);
   if (transport->contexts[token - 1u].runtime_context == expected)
      transport->contexts[token - 1u].runtime_context = NULL;
   ReleaseSRWLockExclusive(&transport->context_lock);
}

static agx_kmt_status
d12_umd_allocate(void *userdata,
                 struct agx_kmt_createallocation *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDI_ALLOCATIONINFO2 info = {0};
   D3DDDICB_ALLOCATE allocate = {0};
   HRESULT result;

   if (!arguments || arguments->NumAllocations != 1u ||
       !arguments->pAllocationInfo2 || arguments->hResource ||
       arguments->hGlobalShare || arguments->pPrivateRuntimeData ||
       arguments->PrivateRuntimeDataSize ||
       (arguments->Flags & ~AGX_KMT_CREATEALLOC_ALLOWNOTZEROED))
      return D12_UMD_STATUS_NOT_SUPPORTED;

   info.pSystemMem = arguments->pAllocationInfo2->pSystemMem;
   info.pPrivateDriverData =
      arguments->pAllocationInfo2->pPrivateDriverData;
   info.PrivateDriverDataSize =
      arguments->pAllocationInfo2->PrivateDriverDataSize;
   info.VidPnSourceId = arguments->pAllocationInfo2->VidPnSourceId;
   info.Flags.Value = arguments->pAllocationInfo2->Flags;
   allocate = (D3DDDICB_ALLOCATE){
      .pPrivateDriverData = arguments->pPrivateDriverData,
      .PrivateDriverDataSize = arguments->PrivateDriverDataSize,
      .hResource = arguments->hPrivateRuntimeResourceHandle,
      .NumAllocations = 1,
      .pAllocationInfo2 = &info,
   };
   result = transport->callbacks->pfnAllocateCb(
      transport->runtime_device.handle, &allocate);
   if (FAILED(result))
      return d12_umd_status(result);

   arguments->hResource = allocate.hKMResource;
   arguments->pAllocationInfo2->hAllocation = info.hAllocation;
   arguments->pAllocationInfo2->GpuVirtualAddress = info.GpuVirtualAddress;
   return info.hAllocation ? d12_umd_status(S_OK) :
                             D12_UMD_STATUS_INVALID_PARAMETER;
}

static agx_kmt_status
d12_umd_unsupported_query_resource(
   void *userdata, struct agx_kmt_queryresourceinfo *arguments)
{
   (void)userdata;
   (void)arguments;
   return D12_UMD_STATUS_NOT_SUPPORTED;
}

static agx_kmt_status
d12_umd_unsupported_query_resource_nt(
   void *userdata, struct agx_kmt_queryresourceinfofromnthandle *arguments)
{
   (void)userdata;
   (void)arguments;
   return D12_UMD_STATUS_NOT_SUPPORTED;
}

static agx_kmt_status
d12_umd_unsupported_share(void *userdata, uint32_t count,
                          const agx_kmt_handle *objects,
                          void *object_attributes, uint32_t access,
                          void **handle)
{
   (void)userdata;
   (void)count;
   (void)objects;
   (void)object_attributes;
   (void)access;
   (void)handle;
   return D12_UMD_STATUS_NOT_SUPPORTED;
}

static agx_kmt_status
d12_umd_unsupported_open_resource(void *userdata,
                                  struct agx_kmt_openresource *arguments)
{
   (void)userdata;
   (void)arguments;
   return D12_UMD_STATUS_NOT_SUPPORTED;
}

static agx_kmt_status
d12_umd_unsupported_open_resource_nt(
   void *userdata, struct agx_kmt_openresourcefromnthandle *arguments)
{
   (void)userdata;
   (void)arguments;
   return D12_UMD_STATUS_NOT_SUPPORTED;
}

static agx_kmt_status
d12_umd_deallocate(void *userdata,
                   const struct agx_kmt_destroyallocation2 *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDICB_DEALLOCATE2 deallocate;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   if (arguments->hResource &&
       !arguments->hPrivateRuntimeResourceHandle)
      return D12_UMD_STATUS_NOT_SUPPORTED;

   deallocate = (D3DDDICB_DEALLOCATE2){
      .hResource = arguments->hPrivateRuntimeResourceHandle,
      .NumAllocations = arguments->hPrivateRuntimeResourceHandle
                           ? 0
                           : arguments->AllocationCount,
      .HandleList = arguments->hPrivateRuntimeResourceHandle
                       ? NULL
                       : arguments->phAllocationList,
   };
   deallocate.Flags.Value = arguments->Flags;
   return d12_umd_status(transport->callbacks->pfnDeallocate2Cb(
      transport->runtime_device.handle, &deallocate));
}

static agx_kmt_status
d12_umd_lock(void *userdata, struct agx_kmt_lock2 *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDICB_LOCK2 lock;
   HRESULT result;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;

   lock = (D3DDDICB_LOCK2){.hAllocation = arguments->hAllocation};
   lock.Flags.Value = arguments->Flags;
   result = transport->callbacks->pfnLock2Cb(transport->runtime_device.handle,
                                             &lock);
   if (SUCCEEDED(result))
      arguments->pData = lock.pData;
   return d12_umd_status(result);
}

static agx_kmt_status
d12_umd_unlock(void *userdata, const struct agx_kmt_unlock2 *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   const D3DDDICB_UNLOCK2 unlock = {
      .hAllocation = arguments->hAllocation,
   };
   return d12_umd_status(transport->callbacks->pfnUnlock2Cb(
      transport->runtime_device.handle, &unlock));
}

static agx_kmt_status
d12_umd_invalidate_cache(
   void *userdata, const struct agx_kmt_invalidatecache *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDICB_INVALIDATECACHE invalidate;

   if (!arguments || !arguments->hAllocation || !arguments->Length)
      return D12_UMD_STATUS_INVALID_PARAMETER;

   invalidate = (D3DDDICB_INVALIDATECACHE){
      .hAllocation = arguments->hAllocation,
      .Offset = (SIZE_T)arguments->Offset,
      .Length = (SIZE_T)arguments->Length,
   };
   return d12_umd_status(transport->callbacks->pfnInvalidateCacheCb(
      transport->runtime_device.handle, &invalidate));
}

static agx_kmt_status
d12_umd_make_resident(void *userdata,
                      struct agx_kmt_makeresident *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDI_MAKERESIDENT resident;
   HRESULT result;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   resident = (D3DDDI_MAKERESIDENT){
      .hPagingQueue = arguments->hPagingQueue,
      .NumAllocations = arguments->NumAllocations,
      .AllocationList = arguments->AllocationList,
      .PriorityList = arguments->PriorityList,
   };
   resident.Flags.Value = arguments->Flags;
   result = transport->callbacks->pfnMakeResidentCb(
      transport->runtime_device.handle, &resident);
   arguments->NumAllocations = resident.NumAllocations;
   arguments->PagingFenceValue = resident.PagingFenceValue;
   arguments->NumBytesToTrim = resident.NumBytesToTrim;
   if (result == E_PENDING)
      return D12_UMD_STATUS_PENDING;
   return d12_umd_status(result);
}

static agx_kmt_status
d12_umd_evict(void *userdata, struct agx_kmt_evict *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDICB_EVICT evict;
   HRESULT result;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   evict = (D3DDDICB_EVICT){
      .NumAllocations = arguments->NumAllocations,
      .AllocationList = arguments->AllocationList,
   };
   evict.Flags.Value = arguments->Flags;
   result = transport->callbacks->pfnEvictCb(transport->runtime_device.handle,
                                              &evict);
   arguments->NumBytesToTrim = evict.NumBytesToTrim;
   return d12_umd_status(result);
}

static agx_kmt_status
d12_umd_create_paging_queue(void *userdata,
                            struct agx_kmt_createpagingqueue *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDICB_CREATEPAGINGQUEUE paging;
   HRESULT result;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   paging = (D3DDDICB_CREATEPAGINGQUEUE){
      .Priority = (D3DDDI_PAGINGQUEUE_PRIORITY)arguments->Priority,
      .PhysicalAdapterIndex = arguments->PhysicalAdapterIndex,
   };
   result = transport->callbacks->pfnCreatePagingQueueCb(
      transport->runtime_device.handle, &paging);
   if (SUCCEEDED(result)) {
      arguments->hPagingQueue = paging.hPagingQueue;
      arguments->hSyncObject = paging.hSyncObject;
      arguments->FenceValueCPUVirtualAddress =
         paging.FenceValueCPUVirtualAddress;
   }
   return d12_umd_status(result);
}

static agx_kmt_status
d12_umd_destroy_paging_queue(
   void *userdata, struct agx_kmt_destroypagingqueue *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   const D3DDDI_DESTROYPAGINGQUEUE paging = {
      .hPagingQueue = arguments->hPagingQueue,
   };
   return d12_umd_status(transport->callbacks->pfnDestroyPagingQueueCb(
      transport->runtime_device.handle, &paging));
}

static agx_kmt_status
d12_umd_reserve_gpu_va(void *userdata,
                       struct agx_kmt_reservegpuva *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDI_RESERVEGPUVIRTUALADDRESS reserve;
   HRESULT result;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   reserve = (D3DDDI_RESERVEGPUVIRTUALADDRESS){
      .hPagingQueue = arguments->hPagingQueue,
      .BaseAddress = arguments->BaseAddress,
      .MinimumAddress = arguments->MinimumAddress,
      .MaximumAddress = arguments->MaximumAddress,
      .Size = arguments->Size,
      .ReservationType =
         (D3DDDIGPUVIRTUALADDRESS_RESERVATION_TYPE)arguments->ReservationType,
      .DriverProtection = arguments->DriverProtection,
   };
   result = transport->callbacks->pfnReserveGpuVirtualAddressCb(
      transport->runtime_device.handle, &reserve);
   if (SUCCEEDED(result)) {
      arguments->VirtualAddress = reserve.VirtualAddress;
      arguments->PagingFenceValue = reserve.PagingFenceValue;
   }
   return d12_umd_status(result);
}

static agx_kmt_status
d12_umd_map_gpu_va(void *userdata, struct agx_kmt_mapgpuva *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDI_MAPGPUVIRTUALADDRESS map;
   HRESULT result;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   map = (D3DDDI_MAPGPUVIRTUALADDRESS){
      .hPagingQueue = arguments->hPagingQueue,
      .BaseAddress = arguments->BaseAddress,
      .MinimumAddress = arguments->MinimumAddress,
      .MaximumAddress = arguments->MaximumAddress,
      .hAllocation = arguments->hAllocation,
      .OffsetInPages = arguments->OffsetInPages,
      .SizeInPages = arguments->SizeInPages,
      .DriverProtection = arguments->DriverProtection,
      .Reserved0 = arguments->Reserved0,
      .Reserved1 = arguments->Reserved1,
   };
   map.Protection.Value = arguments->Protection.Value;
   result = transport->callbacks->pfnMapGpuVirtualAddressCb(
      transport->runtime_device.handle, &map);
   if (SUCCEEDED(result)) {
      arguments->VirtualAddress = map.VirtualAddress;
      arguments->PagingFenceValue = map.PagingFenceValue;
   }
   return d12_umd_status(result);
}

static agx_kmt_status
d12_umd_free_gpu_va(void *userdata,
                    const struct agx_kmt_freegpuva *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   const D3DDDICB_FREEGPUVIRTUALADDRESS free_va = {
      .BaseAddress = arguments->BaseAddress,
      .Size = arguments->Size,
   };
   return d12_umd_status(transport->callbacks->pfnFreeGpuVirtualAddressCb(
      transport->runtime_device.handle, &free_va));
}

static void
d12_umd_translate_gpu_va_op(
   D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *destination,
   const struct agx_kmt_updategpuva_op *source)
{
   memset(destination, 0, sizeof(*destination));
   destination->OperationType =
      (D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION_TYPE)source->OperationType;
   switch (source->OperationType) {
   case AGX_KMT_UPDATEGPUVA_MAP:
      destination->Map.BaseAddress = source->Map.BaseAddress;
      destination->Map.SizeInBytes = source->Map.SizeInBytes;
      destination->Map.hAllocation = source->Map.hAllocation;
      destination->Map.AllocationOffsetInBytes =
         source->Map.AllocationOffsetInBytes;
      destination->Map.AllocationSizeInBytes =
         source->Map.AllocationSizeInBytes;
      break;
   case AGX_KMT_UPDATEGPUVA_UNMAP:
      destination->Unmap.BaseAddress = source->Unmap.BaseAddress;
      destination->Unmap.SizeInBytes = source->Unmap.SizeInBytes;
      destination->Unmap.Protection.Value = source->Unmap.Protection.Value;
      break;
   case AGX_KMT_UPDATEGPUVA_COPY:
      destination->Copy.SourceAddress = source->Copy.SourceAddress;
      destination->Copy.SizeInBytes = source->Copy.SizeInBytes;
      destination->Copy.DestAddress = source->Copy.DestAddress;
      break;
   case AGX_KMT_UPDATEGPUVA_MAP_PROTECT:
      destination->MapProtect.BaseAddress = source->MapProtect.BaseAddress;
      destination->MapProtect.SizeInBytes = source->MapProtect.SizeInBytes;
      destination->MapProtect.hAllocation = source->MapProtect.hAllocation;
      destination->MapProtect.AllocationOffsetInBytes =
         source->MapProtect.AllocationOffsetInBytes;
      destination->MapProtect.AllocationSizeInBytes =
         source->MapProtect.AllocationSizeInBytes;
      destination->MapProtect.Protection.Value =
         source->MapProtect.Protection.Value;
      destination->MapProtect.DriverProtection =
         source->MapProtect.DriverProtection;
      break;
   default:
      break;
   }
}

static agx_kmt_status
d12_umd_update_gpu_va(void *userdata,
                      const struct agx_kmt_updategpuva *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *operations;
   D3DDDICB_UPDATEGPUVIRTUALADDRESS update;
   HANDLE context;
   HRESULT result;

   if (!arguments || !arguments->NumOperations || !arguments->Operations)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   context = d12_umd_context_lookup(transport, arguments->hContext);
   if (!context)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   operations = calloc(arguments->NumOperations, sizeof(*operations));
   if (!operations)
      return D12_UMD_STATUS_NO_MEMORY;
   for (uint32_t index = 0; index < arguments->NumOperations; ++index)
      d12_umd_translate_gpu_va_op(&operations[index],
                                  &arguments->Operations[index]);

   update = (D3DDDICB_UPDATEGPUVIRTUALADDRESS){
      .hContext = context,
      .hFenceObject = arguments->hFenceObject,
      .NumOperations = arguments->NumOperations,
      .Operations = operations,
      .Reserved0 = (UINT)arguments->Reserved0,
      .Reserved1 = arguments->Reserved1,
      .FenceValue = arguments->FenceValue,
   };
   update.Flags.Value = arguments->Flags;
   result = transport->callbacks->pfnUpdateGpuVirtualAddressCb(
      transport->runtime_device.handle, &update);
   free(operations);
   return d12_umd_status(result);
}

static agx_kmt_status
d12_umd_create_context(void *userdata,
                       struct agx_kmt_createcontextvirtual *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDICB_CREATECONTEXTVIRTUAL create;
   agx_kmt_handle token;
   HRESULT result;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   create = (D3DDDICB_CREATECONTEXTVIRTUAL){
      .NodeOrdinal = arguments->NodeOrdinal,
      .EngineAffinity = arguments->EngineAffinity,
      .pPrivateDriverData = arguments->pPrivateDriverData,
      .PrivateDriverDataSize = arguments->PrivateDriverDataSize,
   };
   create.Flags.Value = arguments->Flags;
   result = transport->callbacks->pfnCreateContextVirtualCb(
      transport->runtime_device.handle, &create);
   if (FAILED(result) || !create.hContext)
      return FAILED(result) ? d12_umd_status(result) :
                              D12_UMD_STATUS_INVALID_PARAMETER;
   token = d12_umd_context_insert(transport, create.hContext);
   if (!token) {
      const D3DDDICB_DESTROYCONTEXT destroy = {.hContext = create.hContext};
      (void)transport->callbacks->pfnDestroyContextCb(
         transport->runtime_device.handle, &destroy);
      return D12_UMD_STATUS_NO_MEMORY;
   }
   arguments->hContext = token;
   return d12_umd_status(S_OK);
}

static agx_kmt_status
d12_umd_destroy_context(void *userdata,
                        const struct agx_kmt_destroycontext *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   HANDLE context;
   HRESULT result;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   context = d12_umd_context_lookup(transport, arguments->hContext);
   if (!context)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   const D3DDDICB_DESTROYCONTEXT destroy = {.hContext = context};
   result = transport->callbacks->pfnDestroyContextCb(
      transport->runtime_device.handle, &destroy);
   if (SUCCEEDED(result))
      d12_umd_context_remove(transport, arguments->hContext, context);
   return d12_umd_status(result);
}

static agx_kmt_status
d12_umd_submit(void *userdata,
               const struct agx_kmt_submitcommand *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDICB_SUBMITCOMMAND submit = {0};
   if (!arguments ||
       arguments->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   submit.Commands = arguments->Commands;
   submit.CommandLength = arguments->CommandLength;
   submit.Flags.Value = arguments->Flags;
   submit.BroadcastContextCount = arguments->BroadcastContextCount;
   for (uint32_t index = 0; index < arguments->BroadcastContextCount;
        ++index) {
      submit.BroadcastContext[index] = d12_umd_context_lookup(
         transport, arguments->BroadcastContext[index]);
      if (!submit.BroadcastContext[index])
         return D12_UMD_STATUS_INVALID_PARAMETER;
   }
   submit.pPrivateDriverData = arguments->pPrivateDriverData;
   submit.PrivateDriverDataSize = arguments->PrivateDriverDataSize;
   submit.NumPrimaries = arguments->NumPrimaries;
   memcpy(submit.WrittenPrimaries, arguments->WrittenPrimaries,
          sizeof(submit.WrittenPrimaries));
   submit.NumHistoryBuffers = arguments->NumHistoryBuffers;
   submit.HistoryBufferArray = arguments->HistoryBufferArray;
   return d12_umd_status(transport->callbacks->pfnSubmitCommandCb(
      transport->runtime_device.handle, &submit));
}

static agx_kmt_status
d12_umd_create_sync(void *userdata,
                    struct agx_kmt_createsyncobj2 *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 create = {0};
   HRESULT result;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   create.Info.Type =
      (D3DDDI_SYNCHRONIZATIONOBJECT_TYPE)arguments->Info.Type;
   create.Info.Flags.Value = arguments->Info.Flags;
   if (arguments->Info.Type == AGX_KMT_SYNC_MONITORED_FENCE) {
      create.Info.MonitoredFence.InitialFenceValue =
         arguments->Info.MonitoredFence.InitialFenceValue;
      create.Info.MonitoredFence.EngineAffinity =
         arguments->Info.MonitoredFence.EngineAffinity;
   } else {
      return D12_UMD_STATUS_NOT_SUPPORTED;
   }
   result = transport->callbacks->pfnCreateSynchronizationObject2Cb(
      transport->runtime_device.handle, &create);
   if (SUCCEEDED(result)) {
      arguments->hSyncObject = create.hSyncObject;
      arguments->Info.MonitoredFence.FenceValueCPUVirtualAddress =
         create.Info.MonitoredFence.FenceValueCPUVirtualAddress;
      arguments->Info.MonitoredFence.FenceValueGPUVirtualAddress =
         create.Info.MonitoredFence.FenceValueGPUVirtualAddress;
      arguments->Info.SharedHandle = create.Info.SharedHandle;
   }
   return d12_umd_status(result);
}

static agx_kmt_status
d12_umd_destroy_sync(void *userdata,
                     const struct agx_kmt_destroysyncobj *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   const D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT destroy = {
      .hSyncObject = arguments->hSyncObject,
   };
   return d12_umd_status(
      transport->callbacks->pfnDestroySynchronizationObjectCb(
         transport->runtime_device.handle, &destroy));
}

static agx_kmt_status
d12_umd_unsupported_open_sync(
   void *userdata, struct agx_kmt_opensyncobjfromnthandle2 *arguments)
{
   (void)userdata;
   (void)arguments;
   return D12_UMD_STATUS_NOT_SUPPORTED;
}

static agx_kmt_status
d12_umd_unsupported_open_name(void *userdata,
                              struct agx_kmt_openhandlefromname *arguments)
{
   (void)userdata;
   (void)arguments;
   return D12_UMD_STATUS_NOT_SUPPORTED;
}

static agx_kmt_status
d12_umd_signal_cpu(void *userdata,
                   const struct agx_kmt_signalsyncfromcpu *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU signal = {
      .ObjectCount = arguments->ObjectCount,
      .ObjectHandleArray = arguments->ObjectHandleArray,
      .FenceValueArray = arguments->FenceValueArray,
   };
   return d12_umd_status(
      transport->callbacks->pfnSignalSynchronizationObjectFromCpuCb(
         transport->runtime_device.handle, &signal));
}

static agx_kmt_status
d12_umd_wait_cpu(void *userdata,
                 const struct agx_kmt_waitsyncfromcpu *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   wait = (D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU){
      .ObjectCount = arguments->ObjectCount,
      .ObjectHandleArray = arguments->ObjectHandleArray,
      .FenceValueArray = arguments->FenceValueArray,
      .hAsyncEvent = arguments->hAsyncEvent,
   };
   wait.Flags.Value = arguments->Flags;
   return d12_umd_status(
      transport->callbacks->pfnWaitForSynchronizationObjectFromCpuCb(
         transport->runtime_device.handle, &wait));
}

static agx_kmt_status
d12_umd_signal_gpu(void *userdata,
                   const struct agx_kmt_signalsyncfromgpu *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU signal;
   HANDLE context;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   context = d12_umd_context_lookup(transport, arguments->hContext);
   if (!context)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   signal = (D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU){
      .hContext = context,
      .ObjectCount = arguments->ObjectCount,
      .ObjectHandleArray = arguments->ObjectHandleArray,
      .MonitoredFenceValueArray = arguments->MonitoredFenceValueArray,
   };
   return d12_umd_status(
      transport->callbacks->pfnSignalSynchronizationObjectFromGpuCb(
         transport->runtime_device.handle, &signal));
}

static agx_kmt_status
d12_umd_wait_gpu(void *userdata,
                 const struct agx_kmt_waitsyncfromgpu *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU wait;
   HANDLE context;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   context = d12_umd_context_lookup(transport, arguments->hContext);
   if (!context)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   wait = (D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU){
      .hContext = context,
      .ObjectCount = arguments->ObjectCount,
      .ObjectHandleArray = arguments->ObjectHandleArray,
      .MonitoredFenceValueArray = arguments->MonitoredFenceValueArray,
   };
   return d12_umd_status(
      transport->callbacks->pfnWaitForSynchronizationObjectFromGpuCb(
         transport->runtime_device.handle, &wait));
}

static agx_kmt_status
d12_umd_unsupported_clock(
   void *userdata, struct agx_kmt_queryclockcalibration *arguments)
{
   (void)userdata;
   (void)arguments;
   return D12_UMD_STATUS_NOT_SUPPORTED;
}

static agx_kmt_status
d12_umd_escape(void *userdata, const struct agx_kmt_escape *arguments)
{
   struct d12_umd_agx_transport *transport = userdata;
   D3DDDICB_ESCAPE escape;
   if (!arguments)
      return D12_UMD_STATUS_INVALID_PARAMETER;
   escape = (D3DDDICB_ESCAPE){
      .hDevice = transport->runtime_device.handle,
      .pPrivateDriverData = arguments->pPrivateDriverData,
      .PrivateDriverDataSize = arguments->PrivateDriverDataSize,
   };
   escape.Flags.Value = arguments->Flags;
   if (arguments->hContext) {
      escape.hContext =
         d12_umd_context_lookup(transport, arguments->hContext);
      if (!escape.hContext)
         return D12_UMD_STATUS_INVALID_PARAMETER;
   }
   return d12_umd_status(transport->callbacks->pfnEscapeCb(
      transport->runtime_adapter.handle, &escape));
}

static const struct agx_d3dkmt_runtime_ops d12_umd_agx_ops = {
   .CreateAllocation2 = d12_umd_allocate,
   .QueryResourceInfo = d12_umd_unsupported_query_resource,
   .QueryResourceInfoFromNtHandle = d12_umd_unsupported_query_resource_nt,
   .ShareObjects = d12_umd_unsupported_share,
   .OpenResource = d12_umd_unsupported_open_resource,
   .OpenResourceFromNtHandle = d12_umd_unsupported_open_resource_nt,
   .DestroyAllocation2 = d12_umd_deallocate,
   .Lock2 = d12_umd_lock,
   .Unlock2 = d12_umd_unlock,
   .InvalidateCache = d12_umd_invalidate_cache,
   .MakeResident = d12_umd_make_resident,
   .Evict = d12_umd_evict,
   .CreatePagingQueue = d12_umd_create_paging_queue,
   .DestroyPagingQueue = d12_umd_destroy_paging_queue,
   .ReserveGpuVirtualAddress = d12_umd_reserve_gpu_va,
   .MapGpuVirtualAddress = d12_umd_map_gpu_va,
   .UpdateGpuVirtualAddress = d12_umd_update_gpu_va,
   .FreeGpuVirtualAddress = d12_umd_free_gpu_va,
   .CreateContextVirtual = d12_umd_create_context,
   .DestroyContext = d12_umd_destroy_context,
   .SubmitCommand = d12_umd_submit,
   .CreateSynchronizationObject2 = d12_umd_create_sync,
   .DestroySynchronizationObject = d12_umd_destroy_sync,
   .OpenSyncObjectFromNtHandle2 = d12_umd_unsupported_open_sync,
   .OpenNtHandleFromName = d12_umd_unsupported_open_name,
   .OpenSyncObjectNtHandleFromName = d12_umd_unsupported_open_name,
   .SignalSynchronizationObjectFromCpu = d12_umd_signal_cpu,
   .WaitForSynchronizationObjectFromCpu = d12_umd_wait_cpu,
   .SignalSynchronizationObjectFromGpu = d12_umd_signal_gpu,
   .WaitForSynchronizationObjectFromGpu = d12_umd_wait_gpu,
   .QueryClockCalibration = d12_umd_unsupported_clock,
   .Escape = d12_umd_escape,
};

HRESULT
d12_umd_agx_transport_init(struct d12_umd_agx_transport *transport,
                           D3D12DDI_HRTADAPTER runtime_adapter,
                           D3D12DDI_HRTDEVICE runtime_device,
                           const D3DDDI_DEVICECALLBACKS *callbacks)
{
   if (!transport || !runtime_adapter.handle || !runtime_device.handle ||
       !callbacks || !callbacks->pfnAllocateCb ||
       !callbacks->pfnDeallocate2Cb || !callbacks->pfnLock2Cb ||
       !callbacks->pfnUnlock2Cb || !callbacks->pfnInvalidateCacheCb ||
       !callbacks->pfnMakeResidentCb ||
       !callbacks->pfnEvictCb || !callbacks->pfnCreatePagingQueueCb ||
       !callbacks->pfnDestroyPagingQueueCb ||
       !callbacks->pfnReserveGpuVirtualAddressCb ||
       !callbacks->pfnMapGpuVirtualAddressCb ||
       !callbacks->pfnUpdateGpuVirtualAddressCb ||
       !callbacks->pfnFreeGpuVirtualAddressCb ||
       !callbacks->pfnCreateContextVirtualCb ||
       !callbacks->pfnDestroyContextCb || !callbacks->pfnSubmitCommandCb ||
       !callbacks->pfnCreateSynchronizationObject2Cb ||
       !callbacks->pfnDestroySynchronizationObjectCb ||
       !callbacks->pfnSignalSynchronizationObjectFromCpuCb ||
       !callbacks->pfnWaitForSynchronizationObjectFromCpuCb ||
       !callbacks->pfnSignalSynchronizationObjectFromGpuCb ||
       !callbacks->pfnWaitForSynchronizationObjectFromGpuCb ||
       !callbacks->pfnEscapeCb)
      return E_INVALIDARG;

   memset(transport, 0, sizeof(*transport));
   transport->runtime_adapter = runtime_adapter;
   transport->runtime_device = runtime_device;
   transport->callbacks = callbacks;
   InitializeSRWLock(&transport->context_lock);
   return S_OK;
}

void
d12_umd_agx_transport_finish(struct d12_umd_agx_transport *transport)
{
   if (transport)
      SecureZeroMemory(transport, sizeof(*transport));
}

struct agx_d3dkmt_runtime_device
d12_umd_agx_runtime_device(struct d12_umd_agx_transport *transport,
                           struct agx_kmt_luid process_key,
                           uint32_t params_size)
{
   return (struct agx_d3dkmt_runtime_device){
      .transport = {
         .ops = &d12_umd_agx_ops,
         .userdata = transport,
      },
      .process_key = process_key,
      .params_size = params_size,
   };
}
