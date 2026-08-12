/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_kmt.h"

#include <stdbool.h>
#include <string.h>

static bool
d12_umd_is_power_of_two(uint64_t value)
{
   return value && !(value & (value - 1));
}

static uint32_t
d12_umd_log2_u64(uint64_t value)
{
   uint32_t result = 0;
   while (value > 1) {
      value >>= 1;
      ++result;
   }
   return result;
}

HRESULT
d12_umd_kmt_init(struct d12_umd_kmt *kmt, HANDLE runtime_device,
                 const D3DDDI_DEVICECALLBACKS *callbacks)
{
   if (!kmt || !runtime_device || !callbacks || !callbacks->pfnAllocateCb ||
       !callbacks->pfnDeallocate2Cb)
      return E_INVALIDARG;

   *kmt = (struct d12_umd_kmt){
      .runtime_device = runtime_device,
      .callbacks = callbacks,
   };
   return S_OK;
}

HRESULT
d12_umd_kmt_allocate(struct d12_umd_kmt *kmt, HANDLE runtime_resource,
                     uint64_t size, uint64_t alignment, uint32_t agxw_flags,
                     const void *resource_private_data,
                     uint32_t resource_private_data_size,
                     struct d12_umd_allocation *allocation)
{
   struct agxw_alloc_private private_data;
   D3DDDI_ALLOCATIONINFO2 allocation_info;
   D3DDDICB_ALLOCATE arguments;
   HRESULT result;

   if (!kmt || !kmt->callbacks || !allocation || !size ||
       (agxw_flags & ~(uint32_t)AGXW_ALLOC_FLAGS_ALL) ||
       (resource_private_data_size && !resource_private_data))
      return E_INVALIDARG;

   if (!alignment)
      alignment = AGXW_PAGE_SIZE;
   if (!d12_umd_is_power_of_two(alignment) || alignment < AGXW_PAGE_SIZE)
      return E_INVALIDARG;

   memset(allocation, 0, sizeof(*allocation));
   private_data = (struct agxw_alloc_private){
      .version = AGXW_ALLOC_PRIVATE_VERSION,
      .struct_size = sizeof(private_data),
      .size = size,
      .flags = agxw_flags,
      .align_log2 = d12_umd_log2_u64(alignment),
   };
   allocation_info = (D3DDDI_ALLOCATIONINFO2){
      .pPrivateDriverData = &private_data,
      .PrivateDriverDataSize = sizeof(private_data),
   };
   arguments = (D3DDDICB_ALLOCATE){
      .pPrivateDriverData = resource_private_data,
      .PrivateDriverDataSize = resource_private_data_size,
      .hResource = runtime_resource,
      .NumAllocations = 1,
      .pAllocationInfo2 = &allocation_info,
   };

   result = kmt->callbacks->pfnAllocateCb(kmt->runtime_device, &arguments);
   if (FAILED(result))
      return result;

   if (!allocation_info.hAllocation) {
      /* A successful callback without an allocation is a runtime/KMD ABI
       * violation. There is no handle to clean up, so fail it loudly here.
       */
      return E_FAIL;
   }

   *allocation = (struct d12_umd_allocation){
      .allocation = allocation_info.hAllocation,
      .resource = arguments.hKMResource,
      .gpu_virtual_address = allocation_info.GpuVirtualAddress,
      .size = size,
   };
   return S_OK;
}

HRESULT
d12_umd_kmt_deallocate(struct d12_umd_kmt *kmt, HANDLE runtime_resource,
                       struct d12_umd_allocation *allocation)
{
   D3DDDICB_DEALLOCATE2 arguments;
   HRESULT result;

   if (!kmt || !kmt->callbacks || !allocation || !allocation->allocation)
      return E_INVALIDARG;

   arguments = (D3DDDICB_DEALLOCATE2){
      .hResource = runtime_resource,
      .NumAllocations = runtime_resource ? 0 : 1,
      .HandleList = runtime_resource ? NULL : &allocation->allocation,
   };
   result = kmt->callbacks->pfnDeallocate2Cb(kmt->runtime_device, &arguments);
   if (SUCCEEDED(result))
      memset(allocation, 0, sizeof(*allocation));
   return result;
}

HRESULT
d12_umd_kmt_submit_dma(struct d12_umd_kmt *kmt, HANDLE context,
                       D3DGPU_VIRTUAL_ADDRESS commands, uint32_t cmdbuf_size,
                       D3DKMT_HANDLE result_allocation)
{
   struct agxw_submit_private private_data;
   D3DDDICB_SUBMITCOMMAND arguments;

   if (!kmt || !kmt->callbacks || !kmt->callbacks->pfnSubmitCommandCb ||
       !context || !commands || !cmdbuf_size ||
       cmdbuf_size > AGXW_MAX_CMDBUF_BYTES)
      return E_INVALIDARG;

   private_data = (struct agxw_submit_private){
      .version = AGXW_SUBMIT_PRIVATE_VERSION,
      .struct_size = sizeof(private_data),
      .in_sync_count = 0,
      .out_sync_count = 0,
      .result_allocation = result_allocation,
   };
   arguments = (D3DDDICB_SUBMITCOMMAND){
      .Commands = commands,
      .CommandLength = (UINT)(sizeof(struct agxw_dma_header) + cmdbuf_size),
      .BroadcastContextCount = 1,
      .BroadcastContext = {context},
      .pPrivateDriverData = &private_data,
      .PrivateDriverDataSize = sizeof(private_data),
   };

   return kmt->callbacks->pfnSubmitCommandCb(kmt->runtime_device, &arguments);
}

HRESULT
d12_umd_kmt_free_gpu_va(struct d12_umd_kmt *kmt,
                        D3DGPU_VIRTUAL_ADDRESS base, uint64_t size)
{
   D3DDDICB_FREEGPUVIRTUALADDRESS arguments;

   if (!kmt || !kmt->callbacks ||
       !kmt->callbacks->pfnFreeGpuVirtualAddressCb || !base || !size)
      return E_INVALIDARG;

   arguments = (D3DDDICB_FREEGPUVIRTUALADDRESS){
      .BaseAddress = base,
      .Size = size,
   };
   return kmt->callbacks->pfnFreeGpuVirtualAddressCb(kmt->runtime_device,
                                                     &arguments);
}

HRESULT
d12_umd_kmt_reserve_gpu_va(
   struct d12_umd_kmt *kmt, D3DKMT_HANDLE paging_queue,
   D3DKMT_HANDLE paging_fence, const volatile uint64_t *paging_fence_cpu,
   D3DGPU_VIRTUAL_ADDRESS base, uint64_t size)
{
   D3DDDI_RESERVEGPUVIRTUALADDRESS reserve;
   D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait;
   D3DKMT_HANDLE wait_handle;
   UINT64 wait_value;
   HRESULT result;

   if (!kmt || !kmt->callbacks ||
       !kmt->callbacks->pfnReserveGpuVirtualAddressCb ||
       !kmt->callbacks->pfnFreeGpuVirtualAddressCb ||
       !kmt->callbacks->pfnWaitForSynchronizationObjectFromCpuCb ||
       !paging_queue || !paging_fence || !paging_fence_cpu || !base || !size ||
       base > UINT64_MAX - size || (base & (AGXW_PAGE_SIZE - 1)) ||
       (size & (AGXW_PAGE_SIZE - 1)))
      return E_INVALIDARG;

   reserve = (D3DDDI_RESERVEGPUVIRTUALADDRESS){
      .hPagingQueue = paging_queue,
      .BaseAddress = base,
      .MinimumAddress = base,
      .MaximumAddress = base + size,
      .Size = size,
      .ReservationType = D3DDDIGPUVIRTUALADDRESS_RESERVE_NO_ACCESS,
   };
   result = kmt->callbacks->pfnReserveGpuVirtualAddressCb(kmt->runtime_device,
                                                          &reserve);
   if (FAILED(result))
      return result;

   if (reserve.VirtualAddress != base) {
      if (reserve.VirtualAddress)
         (void)d12_umd_kmt_free_gpu_va(kmt, reserve.VirtualAddress, size);
      return E_FAIL;
   }

   if (*paging_fence_cpu >= reserve.PagingFenceValue)
      return S_OK;

   wait_handle = paging_fence;
   wait_value = reserve.PagingFenceValue;
   wait = (D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU){
      .ObjectCount = 1,
      .ObjectHandleArray = &wait_handle,
      .FenceValueArray = &wait_value,
   };
   result = kmt->callbacks->pfnWaitForSynchronizationObjectFromCpuCb(
      kmt->runtime_device, &wait);
   if (FAILED(result))
      (void)d12_umd_kmt_free_gpu_va(kmt, base, size);
   return result;
}

HRESULT
d12_umd_kmt_lock(struct d12_umd_kmt *kmt, D3DKMT_HANDLE allocation,
                 void **mapping)
{
   D3DDDICB_LOCK2 arguments;
   HRESULT result;

   if (!kmt || !kmt->callbacks || !kmt->callbacks->pfnLock2Cb || !allocation ||
       !mapping)
      return E_INVALIDARG;

   *mapping = NULL;
   arguments = (D3DDDICB_LOCK2){
      .hAllocation = allocation,
   };
   result = kmt->callbacks->pfnLock2Cb(kmt->runtime_device, &arguments);
   if (FAILED(result))
      return result;
   if (!arguments.pData)
      return E_FAIL;
   *mapping = arguments.pData;
   return S_OK;
}

HRESULT
d12_umd_kmt_unlock(struct d12_umd_kmt *kmt, D3DKMT_HANDLE allocation)
{
   D3DDDICB_UNLOCK2 arguments;

   if (!kmt || !kmt->callbacks || !kmt->callbacks->pfnUnlock2Cb ||
       !allocation)
      return E_INVALIDARG;
   arguments = (D3DDDICB_UNLOCK2){
      .hAllocation = allocation,
   };
   return kmt->callbacks->pfnUnlock2Cb(kmt->runtime_device, &arguments);
}
