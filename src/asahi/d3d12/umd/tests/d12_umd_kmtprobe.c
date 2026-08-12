/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Native Windows callback-transport unit probe.  The mocks assert the exact
 * bytes handed to dxgkrnl/KMD without needing a real allocation.
 */

#include "../d12_umd_kmt.h"

#include <stdio.h>
#include <string.h>

#define TEST_DEVICE ((HANDLE)(uintptr_t)0x1000)
#define TEST_RESOURCE ((HANDLE)(uintptr_t)0x2000)
#define TEST_ALLOCATION ((D3DKMT_HANDLE)0x3456)
#define TEST_KM_RESOURCE ((D3DKMT_HANDLE)0x4567)
#define TEST_GPU_VA UINT64_C(0x123400000)
#define TEST_CONTEXT ((HANDLE)(uintptr_t)0x3000)
#define TEST_COMMANDS UINT64_C(0x567800000)
#define TEST_RESULT_ALLOCATION ((D3DKMT_HANDLE)0x5678)
#define TEST_PAGING_QUEUE ((D3DKMT_HANDLE)0x6789)
#define TEST_PAGING_FENCE ((D3DKMT_HANDLE)0x789a)
#define TEST_USC_BASE UINT64_C(0x1100000000)
#define TEST_USC_SIZE UINT64_C(0x100000000)

static unsigned allocate_calls;
static unsigned deallocate_calls;
static unsigned submit_calls;
static unsigned reserve_calls;
static unsigned free_va_calls;
static unsigned wait_cpu_calls;
static volatile uint64_t paging_fence_value;

static HRESULT APIENTRY
allocate_callback(HANDLE device, D3DDDICB_ALLOCATE *arguments)
{
   struct agxw_alloc_private *private_data;

   if (device != TEST_DEVICE || !arguments ||
       arguments->hResource != TEST_RESOURCE || arguments->NumAllocations != 1 ||
       !arguments->pAllocationInfo2)
      return E_INVALIDARG;

   private_data = arguments->pAllocationInfo2->pPrivateDriverData;
   if (!private_data ||
       arguments->pAllocationInfo2->PrivateDriverDataSize !=
          AGXW_ALLOC_PRIVATE_SIZE ||
       private_data->version != AGXW_ALLOC_PRIVATE_VERSION ||
       private_data->struct_size != AGXW_ALLOC_PRIVATE_SIZE ||
       private_data->size != UINT64_C(0x5000) ||
       private_data->flags != (AGXW_ALLOC_WRITEBACK | AGXW_ALLOC_SHAREABLE) ||
       private_data->align_log2 != 16 || private_data->reserved != 0)
      return E_INVALIDARG;

   arguments->hKMResource = TEST_KM_RESOURCE;
   arguments->pAllocationInfo2->hAllocation = TEST_ALLOCATION;
   arguments->pAllocationInfo2->GpuVirtualAddress = TEST_GPU_VA;
   ++allocate_calls;
   return S_OK;
}

static HRESULT APIENTRY
deallocate_callback(HANDLE device, const D3DDDICB_DEALLOCATE2 *arguments)
{
   if (device != TEST_DEVICE || !arguments ||
       arguments->hResource != TEST_RESOURCE ||
       arguments->NumAllocations != 0 || arguments->HandleList != NULL)
      return E_INVALIDARG;

   ++deallocate_calls;
   return S_OK;
}

static HRESULT APIENTRY
submit_callback(HANDLE device, const D3DDDICB_SUBMITCOMMAND *arguments)
{
   const struct agxw_submit_private *private_data;

   if (device != TEST_DEVICE || !arguments ||
       arguments->Commands != TEST_COMMANDS ||
       arguments->CommandLength !=
          sizeof(struct agxw_dma_header) + UINT32_C(0x4000) ||
       arguments->Flags.Value != 0 || arguments->BroadcastContextCount != 1 ||
       arguments->BroadcastContext[0] != TEST_CONTEXT ||
       arguments->NumPrimaries != 0 || arguments->MarkerLogType != 0 ||
       arguments->pPrivateDriverData == NULL ||
       arguments->PrivateDriverDataSize != sizeof(struct agxw_submit_private))
      return E_INVALIDARG;

   private_data = arguments->pPrivateDriverData;
   if (private_data->version != AGXW_SUBMIT_PRIVATE_VERSION ||
       private_data->struct_size != sizeof(struct agxw_submit_private) ||
       private_data->in_sync_count != 0 || private_data->out_sync_count != 0 ||
       private_data->result_allocation != TEST_RESULT_ALLOCATION ||
       private_data->flags != 0)
      return E_INVALIDARG;

   ++submit_calls;
   return S_OK;
}

static HRESULT APIENTRY
reserve_gpu_va_callback(HANDLE device,
                        D3DDDI_RESERVEGPUVIRTUALADDRESS *arguments)
{
   if (device != TEST_DEVICE || !arguments ||
       arguments->hPagingQueue != TEST_PAGING_QUEUE ||
       arguments->BaseAddress != TEST_USC_BASE ||
       arguments->MinimumAddress != TEST_USC_BASE ||
       arguments->MaximumAddress != TEST_USC_BASE + TEST_USC_SIZE ||
       arguments->Size != TEST_USC_SIZE ||
       arguments->ReservationType !=
          D3DDDIGPUVIRTUALADDRESS_RESERVE_NO_ACCESS ||
       arguments->DriverProtection != 0)
      return E_INVALIDARG;

   arguments->VirtualAddress = TEST_USC_BASE;
   arguments->PagingFenceValue = UINT64_C(7);
   ++reserve_calls;
   return S_OK;
}

static HRESULT APIENTRY
free_gpu_va_callback(HANDLE device,
                     const D3DDDICB_FREEGPUVIRTUALADDRESS *arguments)
{
   if (device != TEST_DEVICE || !arguments ||
       arguments->BaseAddress != TEST_USC_BASE ||
       arguments->Size != TEST_USC_SIZE)
      return E_INVALIDARG;
   ++free_va_calls;
   return S_OK;
}

static HRESULT APIENTRY
wait_cpu_callback(
   HANDLE device,
   const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *arguments)
{
   if (device != TEST_DEVICE || !arguments || arguments->ObjectCount != 1 ||
       !arguments->ObjectHandleArray || !arguments->FenceValueArray ||
       arguments->ObjectHandleArray[0] != TEST_PAGING_FENCE ||
       arguments->FenceValueArray[0] != UINT64_C(7) ||
       arguments->hAsyncEvent != NULL || arguments->Flags.Value != 0)
      return E_INVALIDARG;
   paging_fence_value = UINT64_C(7);
   ++wait_cpu_calls;
   return S_OK;
}

static void
require_result(int condition, const char *message)
{
   if (!condition) {
      fprintf(stderr, "FAIL: %s\n", message);
      ExitProcess(1);
   }
}

int
main(void)
{
   D3DDDI_DEVICECALLBACKS callbacks;
   struct d12_umd_kmt kmt;
   struct d12_umd_allocation allocation;
   HRESULT result;

   memset(&callbacks, 0, sizeof(callbacks));
   callbacks.pfnAllocateCb = allocate_callback;
   callbacks.pfnDeallocate2Cb = deallocate_callback;
   callbacks.pfnSubmitCommandCb = submit_callback;
   callbacks.pfnReserveGpuVirtualAddressCb = reserve_gpu_va_callback;
   callbacks.pfnFreeGpuVirtualAddressCb = free_gpu_va_callback;
   callbacks.pfnWaitForSynchronizationObjectFromCpuCb = wait_cpu_callback;

   result = d12_umd_kmt_init(&kmt, TEST_DEVICE, &callbacks);
   require_result(result == S_OK, "kmt init");
   result = d12_umd_kmt_allocate(
      &kmt, TEST_RESOURCE, UINT64_C(0x5000), UINT64_C(0x10000),
      AGXW_ALLOC_WRITEBACK | AGXW_ALLOC_SHAREABLE, NULL, 0, &allocation);
   require_result(result == S_OK && allocate_calls == 1,
                  "allocation callback result");
   require_result(allocation.allocation == TEST_ALLOCATION &&
                     allocation.resource == TEST_KM_RESOURCE &&
                     allocation.gpu_virtual_address == TEST_GPU_VA &&
                     allocation.size == UINT64_C(0x5000),
                  "allocation callback outputs");

   result = d12_umd_kmt_deallocate(&kmt, TEST_RESOURCE, &allocation);
   require_result(result == S_OK && deallocate_calls == 1,
                  "deallocation callback result");
   require_result(allocation.allocation == 0 && allocation.resource == 0 &&
                     allocation.gpu_virtual_address == 0 && allocation.size == 0,
                  "deallocation clears state");

   result = d12_umd_kmt_allocate(&kmt, TEST_RESOURCE, 1, 3, 0, NULL, 0,
                                 &allocation);
   require_result(result == E_INVALIDARG && allocate_calls == 1,
                  "reject non-power-of-two alignment before callback");
   result = d12_umd_kmt_allocate(&kmt, TEST_RESOURCE, 1, AGXW_PAGE_SIZE,
                                 AGXW_ALLOC_FLAG_RETIRED_TIMESTAMP,
                                 NULL, 0, &allocation);
   require_result(result == E_INVALIDARG && allocate_calls == 1,
                  "reject retired allocation flag before callback");

   result = d12_umd_kmt_submit_dma(&kmt, TEST_CONTEXT, TEST_COMMANDS,
                                   UINT32_C(0x4000), TEST_RESULT_ALLOCATION);
   require_result(result == S_OK && submit_calls == 1,
                  "submit callback bytes");
   result = d12_umd_kmt_submit_dma(&kmt, TEST_CONTEXT, TEST_COMMANDS,
                                   AGXW_MAX_CMDBUF_BYTES + UINT32_C(1), 0);
   require_result(result == E_INVALIDARG && submit_calls == 1,
                  "reject oversized command buffer before callback");

   result = d12_umd_kmt_reserve_gpu_va(
      &kmt, TEST_PAGING_QUEUE, TEST_PAGING_FENCE, &paging_fence_value,
      TEST_USC_BASE, TEST_USC_SIZE);
   require_result(result == S_OK && reserve_calls == 1 && wait_cpu_calls == 1 &&
                     paging_fence_value == UINT64_C(7),
                  "reserve and wait paging callbacks");
   result = d12_umd_kmt_free_gpu_va(&kmt, TEST_USC_BASE, TEST_USC_SIZE);
   require_result(result == S_OK && free_va_calls == 1,
                  "free GPU VA callback");

   puts("d12_umd_kmtprobe: PASS");
   return 0;
}
