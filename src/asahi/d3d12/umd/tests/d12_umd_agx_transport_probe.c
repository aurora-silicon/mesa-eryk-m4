/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Strict Windows ABI probe for the runtime-owned AGX winsys transport.  It
 * drives the frontend-neutral operations table and verifies that every hot
 * allocation/VA/residency/context/submission field reaches the corresponding
 * D3DDDI callback without pointer casting or context-handle truncation.
 */

#include "../d12_umd_agx_transport.h"
#include "agxw_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ADAPTER ((HANDLE)(uintptr_t)0x1000)
#define TEST_DEVICE ((HANDLE)(uintptr_t)0x2000)
#define TEST_CONTEXT ((HANDLE)(uintptr_t)UINT64_C(0x1234567887654321))
#define TEST_RUNTIME_RESOURCE                                                 \
   ((HANDLE)(uintptr_t)UINT64_C(0x2345678998765432))
#define TEST_ALLOCATION ((D3DKMT_HANDLE)0x3100)
#define TEST_RESOURCE ((D3DKMT_HANDLE)0x3200)
#define TEST_PAGING_QUEUE ((D3DKMT_HANDLE)0x3300)
#define TEST_PAGING_FENCE ((D3DKMT_HANDLE)0x3400)
#define TEST_SYNC ((D3DKMT_HANDLE)0x3500)
#define TEST_GPU_VA UINT64_C(0x4200000000)

static volatile uint64_t paging_value;
static unsigned callback_mask;

enum callback_bit {
   CALLBACK_ALLOCATE = 1u << 0,
   CALLBACK_RESIDENT = 1u << 1,
   CALLBACK_PAGING = 1u << 2,
   CALLBACK_MAP = 1u << 3,
   CALLBACK_CONTEXT = 1u << 4,
   CALLBACK_SUBMIT = 1u << 5,
   CALLBACK_SYNC = 1u << 6,
   CALLBACK_ESCAPE = 1u << 7,
   CALLBACK_DEALLOCATE = 1u << 8,
   CALLBACK_INVALIDATE = 1u << 9,
};

static void
require_result(int condition, const char *message)
{
   if (!condition) {
      fprintf(stderr, "FAIL: %s\n", message);
      ExitProcess(1);
   }
}

static HRESULT APIENTRY
allocate_cb(HANDLE device, D3DDDICB_ALLOCATE *arguments)
{
   if (device != TEST_DEVICE || !arguments || arguments->NumAllocations != 1 ||
       !arguments->pAllocationInfo2 ||
       (arguments->hResource != 0 &&
        arguments->hResource != TEST_RUNTIME_RESOURCE) ||
       arguments->pAllocationInfo2->Flags.Value != UINT32_C(0x55aa))
      return E_INVALIDARG;
   arguments->hKMResource = TEST_RESOURCE;
   arguments->pAllocationInfo2->hAllocation = TEST_ALLOCATION;
   arguments->pAllocationInfo2->GpuVirtualAddress = TEST_GPU_VA;
   callback_mask |= CALLBACK_ALLOCATE;
   return S_OK;
}

static HRESULT APIENTRY
deallocate_cb(HANDLE device, const D3DDDICB_DEALLOCATE2 *arguments)
{
   if (device != TEST_DEVICE || !arguments ||
       arguments->hResource != TEST_RUNTIME_RESOURCE ||
       arguments->NumAllocations != 0 || arguments->HandleList)
      return E_INVALIDARG;
   callback_mask |= CALLBACK_DEALLOCATE;
   return S_OK;
}

static HRESULT APIENTRY
lock_cb(HANDLE device, D3DDDICB_LOCK2 *arguments)
{
   static uint8_t mapping[64];
   if (device != TEST_DEVICE || !arguments)
      return E_INVALIDARG;
   arguments->pData = mapping;
   return S_OK;
}

static HRESULT APIENTRY
unlock_cb(HANDLE device, const D3DDDICB_UNLOCK2 *arguments)
{
   return device == TEST_DEVICE && arguments ? S_OK : E_INVALIDARG;
}

static HRESULT APIENTRY
invalidate_cache_cb(HANDLE device,
                    const D3DDDICB_INVALIDATECACHE *arguments)
{
   if (device != TEST_DEVICE || !arguments ||
       arguments->hAllocation != TEST_ALLOCATION ||
       arguments->Offset != 64 || arguments->Length != 128)
      return E_INVALIDARG;
   callback_mask |= CALLBACK_INVALIDATE;
   return S_OK;
}

static HRESULT APIENTRY
resident_cb(HANDLE device, D3DDDI_MAKERESIDENT *arguments)
{
   if (device != TEST_DEVICE || !arguments || arguments->NumAllocations != 1 ||
       !arguments->AllocationList ||
       arguments->AllocationList[0] != TEST_ALLOCATION)
      return E_INVALIDARG;
   arguments->PagingFenceValue = 9;
   callback_mask |= CALLBACK_RESIDENT;
   return E_PENDING;
}

static HRESULT APIENTRY
evict_cb(HANDLE device, D3DDDICB_EVICT *arguments)
{
   return device == TEST_DEVICE && arguments ? S_OK : E_INVALIDARG;
}

static HRESULT APIENTRY
create_paging_cb(HANDLE device, D3DDDICB_CREATEPAGINGQUEUE *arguments)
{
   if (device != TEST_DEVICE || !arguments)
      return E_INVALIDARG;
   arguments->hPagingQueue = TEST_PAGING_QUEUE;
   arguments->hSyncObject = TEST_PAGING_FENCE;
   arguments->FenceValueCPUVirtualAddress = (void *)&paging_value;
   callback_mask |= CALLBACK_PAGING;
   return S_OK;
}

static HRESULT APIENTRY
destroy_paging_cb(HANDLE device, const D3DDDI_DESTROYPAGINGQUEUE *arguments)
{
   return device == TEST_DEVICE && arguments ? S_OK : E_INVALIDARG;
}

static HRESULT APIENTRY
reserve_cb(HANDLE device, D3DDDI_RESERVEGPUVIRTUALADDRESS *arguments)
{
   if (device != TEST_DEVICE || !arguments)
      return E_INVALIDARG;
   arguments->VirtualAddress = TEST_GPU_VA;
   arguments->PagingFenceValue = 4;
   return S_OK;
}

static HRESULT APIENTRY
map_cb(HANDLE device, D3DDDI_MAPGPUVIRTUALADDRESS *arguments)
{
   if (device != TEST_DEVICE || !arguments ||
       arguments->hAllocation != TEST_ALLOCATION ||
       arguments->Protection.Value != AGX_KMT_GPUVA_PROT_WRITE)
      return E_INVALIDARG;
   arguments->VirtualAddress = TEST_GPU_VA;
   arguments->PagingFenceValue = 5;
   callback_mask |= CALLBACK_MAP;
   return S_OK;
}

static HRESULT APIENTRY
free_va_cb(HANDLE device,
           const D3DDDICB_FREEGPUVIRTUALADDRESS *arguments)
{
   return device == TEST_DEVICE && arguments ? S_OK : E_INVALIDARG;
}

static HRESULT APIENTRY
update_va_cb(HANDLE device,
             const D3DDDICB_UPDATEGPUVIRTUALADDRESS *arguments)
{
   if (device != TEST_DEVICE || !arguments ||
       arguments->hContext != TEST_CONTEXT || arguments->NumOperations != 1 ||
       arguments->Operations[0].OperationType !=
          D3DDDI_UPDATEGPUVIRTUALADDRESS_MAP ||
       arguments->Operations[0].Map.hAllocation != TEST_ALLOCATION)
      return E_INVALIDARG;
   return S_OK;
}

static HRESULT APIENTRY
create_context_cb(HANDLE device, D3DDDICB_CREATECONTEXTVIRTUAL *arguments)
{
   if (device != TEST_DEVICE || !arguments || arguments->NodeOrdinal != 2 ||
       arguments->EngineAffinity != 4)
      return E_INVALIDARG;
   arguments->hContext = TEST_CONTEXT;
   callback_mask |= CALLBACK_CONTEXT;
   return S_OK;
}

static HRESULT APIENTRY
destroy_context_cb(HANDLE device, const D3DDDICB_DESTROYCONTEXT *arguments)
{
   return device == TEST_DEVICE && arguments &&
                arguments->hContext == TEST_CONTEXT
             ? S_OK
             : E_INVALIDARG;
}

static HRESULT APIENTRY
submit_cb(HANDLE device, const D3DDDICB_SUBMITCOMMAND *arguments)
{
   if (device != TEST_DEVICE || !arguments ||
       arguments->Commands != TEST_GPU_VA || arguments->CommandLength != 128 ||
       arguments->BroadcastContextCount != 1 ||
       arguments->BroadcastContext[0] != TEST_CONTEXT)
      return E_INVALIDARG;
   callback_mask |= CALLBACK_SUBMIT;
   return S_OK;
}

static HRESULT APIENTRY
create_sync_cb(HANDLE device,
               D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 *arguments)
{
   if (device != TEST_DEVICE || !arguments ||
       arguments->Info.Type != D3DDDI_MONITORED_FENCE)
      return E_INVALIDARG;
   arguments->hSyncObject = TEST_SYNC;
   arguments->Info.MonitoredFence.FenceValueCPUVirtualAddress =
      (void *)&paging_value;
   arguments->Info.MonitoredFence.FenceValueGPUVirtualAddress = TEST_GPU_VA;
   callback_mask |= CALLBACK_SYNC;
   return S_OK;
}

static HRESULT APIENTRY
destroy_sync_cb(HANDLE device,
                const D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT *arguments)
{
   return device == TEST_DEVICE && arguments ? S_OK : E_INVALIDARG;
}

static HRESULT APIENTRY
signal_cpu_cb(
   HANDLE device,
   const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *arguments)
{
   return device == TEST_DEVICE && arguments ? S_OK : E_INVALIDARG;
}

static HRESULT APIENTRY
wait_cpu_cb(HANDLE device,
            const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *arguments)
{
   return device == TEST_DEVICE && arguments ? S_OK : E_INVALIDARG;
}

static HRESULT APIENTRY
signal_gpu_cb(
   HANDLE device,
   const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU *arguments)
{
   return device == TEST_DEVICE && arguments &&
                arguments->hContext == TEST_CONTEXT
             ? S_OK
             : E_INVALIDARG;
}

static HRESULT APIENTRY
wait_gpu_cb(HANDLE device,
            const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU *arguments)
{
   return device == TEST_DEVICE && arguments &&
                arguments->hContext == TEST_CONTEXT
             ? S_OK
             : E_INVALIDARG;
}

static HRESULT APIENTRY
escape_cb(HANDLE adapter, const D3DDDICB_ESCAPE *arguments)
{
   if (adapter != TEST_ADAPTER || !arguments ||
       arguments->hDevice != TEST_DEVICE)
      return E_INVALIDARG;
   callback_mask |= CALLBACK_ESCAPE;
   return S_OK;
}

int
main(void)
{
   const D3DDDI_DEVICECALLBACKS callbacks = {
      .pfnAllocateCb = allocate_cb,
      .pfnEscapeCb = escape_cb,
      .pfnDestroyContextCb = destroy_context_cb,
      .pfnDestroySynchronizationObjectCb = destroy_sync_cb,
      .pfnCreateSynchronizationObject2Cb = create_sync_cb,
      .pfnMakeResidentCb = resident_cb,
      .pfnEvictCb = evict_cb,
      .pfnWaitForSynchronizationObjectFromCpuCb = wait_cpu_cb,
      .pfnSignalSynchronizationObjectFromCpuCb = signal_cpu_cb,
      .pfnWaitForSynchronizationObjectFromGpuCb = wait_gpu_cb,
      .pfnSignalSynchronizationObjectFromGpuCb = signal_gpu_cb,
      .pfnCreatePagingQueueCb = create_paging_cb,
      .pfnDestroyPagingQueueCb = destroy_paging_cb,
      .pfnLock2Cb = lock_cb,
      .pfnUnlock2Cb = unlock_cb,
      .pfnInvalidateCacheCb = invalidate_cache_cb,
      .pfnReserveGpuVirtualAddressCb = reserve_cb,
      .pfnMapGpuVirtualAddressCb = map_cb,
      .pfnFreeGpuVirtualAddressCb = free_va_cb,
      .pfnUpdateGpuVirtualAddressCb = update_va_cb,
      .pfnCreateContextVirtualCb = create_context_cb,
      .pfnSubmitCommandCb = submit_cb,
      .pfnDeallocate2Cb = deallocate_cb,
   };
   struct d12_umd_agx_transport transport;
   const struct agx_kmt_luid key = {
      .LowPart = UINT32_C(0xaabbccdd),
      .HighPart = INT32_C(0x11223344),
   };
   struct agx_d3dkmt_runtime_device runtime;
   const struct agx_d3dkmt_runtime_ops *ops;
   struct agx_kmt_allocationinfo2 info = {.Flags = UINT32_C(0x55aa)};
   struct agx_kmt_createallocation allocation = {
      .NumAllocations = 1,
      .pAllocationInfo2 = &info,
   };
   struct agx_kmt_createpagingqueue paging = {0};
   struct agx_kmt_mapgpuva map = {
      .hPagingQueue = TEST_PAGING_QUEUE,
      .hAllocation = TEST_ALLOCATION,
      .Protection = {.Value = AGX_KMT_GPUVA_PROT_WRITE},
   };
   struct agx_kmt_createcontextvirtual context = {
      .NodeOrdinal = 2,
      .EngineAffinity = 4,
   };

   require_result(
      d12_umd_agx_transport_init(
         &transport, (D3D12DDI_HRTADAPTER){.handle = TEST_ADAPTER},
         (D3D12DDI_HRTDEVICE){.handle = TEST_DEVICE}, &callbacks) == S_OK,
      "transport init");
   runtime = d12_umd_agx_runtime_device(&transport, key,
                                        AGXW_PARAMS_GLOBAL_SIZE);
   ops = runtime.transport.ops;
   require_result(agx_d3dkmt_runtime_ops_complete(ops), "complete ops");
   require_result(runtime.process_key.LowPart == key.LowPart &&
                     runtime.process_key.HighPart == key.HighPart,
                  "stable process key");

   require_result(ops->CreateAllocation2(runtime.transport.userdata,
                                         &allocation) == 0 &&
                     info.hAllocation == TEST_ALLOCATION &&
                     info.GpuVirtualAddress == TEST_GPU_VA,
                  "allocation translation");
   {
      struct agx_kmt_allocationinfo2 resource_info = {
         .Flags = UINT32_C(0x55aa),
      };
      struct agx_kmt_createallocation resource_allocation = {
         .NumAllocations = 1,
         .pAllocationInfo2 = &resource_info,
         .hPrivateRuntimeResourceHandle = TEST_RUNTIME_RESOURCE,
      };
      require_result(ops->CreateAllocation2(runtime.transport.userdata,
                                             &resource_allocation) == 0 &&
                        resource_allocation.hResource == TEST_RESOURCE &&
                        resource_info.hAllocation == TEST_ALLOCATION,
                     "runtime-resource allocation translation");
      const struct agx_kmt_destroyallocation2 destroy = {
         .hResource = resource_allocation.hResource,
         .hPrivateRuntimeResourceHandle = TEST_RUNTIME_RESOURCE,
      };
      require_result(ops->DestroyAllocation2(runtime.transport.userdata,
                                              &destroy) == 0,
                     "runtime-resource destruction translation");
   }
   require_result(ops->CreatePagingQueue(runtime.transport.userdata,
                                         &paging) == 0 &&
                     paging.hPagingQueue == TEST_PAGING_QUEUE &&
                     paging.hSyncObject == TEST_PAGING_FENCE,
                  "paging translation");
   require_result(ops->MapGpuVirtualAddress(runtime.transport.userdata,
                                            &map) == 0 &&
                     map.VirtualAddress == TEST_GPU_VA &&
                     map.PagingFenceValue == 5,
                  "GPU VA map translation");
   {
      const struct agx_kmt_invalidatecache invalidate = {
         .hDevice = 1,
         .hAllocation = TEST_ALLOCATION,
         .Offset = 64,
         .Length = 128,
      };
      require_result(ops->InvalidateCache(runtime.transport.userdata,
                                           &invalidate) == 0,
                     "cache ownership translation");
   }
   {
      const agx_kmt_handle allocation_handle = TEST_ALLOCATION;
      struct agx_kmt_makeresident resident = {
         .NumAllocations = 1,
         .AllocationList = &allocation_handle,
      };
      require_result(ops->MakeResident(runtime.transport.userdata,
                                       &resident) ==
                        (agx_kmt_status)E_PENDING &&
                        resident.PagingFenceValue == 9,
                     "residency translation");
   }
   require_result(ops->CreateContextVirtual(runtime.transport.userdata,
                                            &context) == 0 &&
                     context.hContext == 1,
                  "context token creation");
   {
      struct agx_kmt_updategpuva_op operation = {
         .OperationType = AGX_KMT_UPDATEGPUVA_MAP,
         .Map = {
            .BaseAddress = TEST_GPU_VA,
            .SizeInBytes = UINT64_C(0x10000),
            .hAllocation = TEST_ALLOCATION,
            .AllocationSizeInBytes = UINT64_C(0x10000),
         },
      };
      const struct agx_kmt_updategpuva update = {
         .hContext = context.hContext,
         .NumOperations = 1,
         .Operations = &operation,
      };
      require_result(ops->UpdateGpuVirtualAddress(runtime.transport.userdata,
                                                   &update) == 0,
                     "GPU VA update context translation");
   }
   {
      struct agx_kmt_submitcommand submit = {
         .Commands = TEST_GPU_VA,
         .CommandLength = 128,
         .BroadcastContextCount = 1,
         .BroadcastContext = {context.hContext},
      };
      require_result(ops->SubmitCommand(runtime.transport.userdata,
                                        &submit) == 0,
                     "submit context translation");
   }
   {
      struct agx_kmt_createsyncobj2 sync = {
         .Info = {
            .Type = AGX_KMT_SYNC_MONITORED_FENCE,
            .MonitoredFence = {.InitialFenceValue = 7},
         },
      };
      require_result(ops->CreateSynchronizationObject2(
                        runtime.transport.userdata, &sync) == 0 &&
                        sync.hSyncObject == TEST_SYNC &&
                        sync.Info.MonitoredFence.FenceValueGPUVirtualAddress ==
                           TEST_GPU_VA,
                     "monitored fence translation");
   }
   {
      const struct agx_kmt_escape escape = {0};
      require_result(ops->Escape(runtime.transport.userdata, &escape) == 0,
                     "escape translation");
   }
   {
      const struct agx_kmt_destroycontext destroy = {
         .hContext = context.hContext,
      };
      require_result(ops->DestroyContext(runtime.transport.userdata,
                                         &destroy) == 0,
                     "context token destruction");
   }

   require_result(callback_mask == UINT32_C(0x3ff),
                  "all callback families");
   d12_umd_agx_transport_finish(&transport);
   puts("d12_umd_agx_transport_probe: PASS");
   return 0;
}
