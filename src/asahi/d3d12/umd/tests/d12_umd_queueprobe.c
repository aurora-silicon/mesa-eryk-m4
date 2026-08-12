/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Native Windows unit probe for D3D12 core callback queue lifetime.
 */

#include "../d12_umd_queue_runtime.h"

#include <stdio.h>
#include <string.h>

#define TEST_RT_QUEUE ((HANDLE)(uintptr_t)0x1000)
#define TEST_DEVICE ((HANDLE)(uintptr_t)0x1800)
#define TEST_CONTEXT ((HANDLE)(uintptr_t)0x2000)
#define TEST_PAGING_QUEUE ((D3DKMT_HANDLE)0x3000)
#define TEST_PAGING_FENCE ((D3DKMT_HANDLE)0x4000)
#define TEST_USC_BASE UINT64_C(0x50000000)
#define TEST_USC_SIZE UINT64_C(0x100000000)

static volatile uint64_t paging_fence_value;
static unsigned create_context_calls;
static unsigned create_paging_calls;
static unsigned destroy_context_calls;
static unsigned destroy_paging_calls;
static HRESULT paging_create_result = S_OK;
static unsigned reserve_calls;
static unsigned free_calls;
static unsigned wait_calls;

static HRESULT APIENTRY
create_context_callback(D3D12DDI_HRTCOMMANDQUEUE runtime_queue,
                        D3DDDICB_CREATECONTEXTVIRTUAL *arguments)
{
   const struct agxw_context_private *private_data;

   if (runtime_queue.handle != TEST_RT_QUEUE || !arguments ||
       arguments->NodeOrdinal != AGXW_NODE_3D ||
       arguments->EngineAffinity != 1 || arguments->Flags.Value != 0 ||
       arguments->PrivateDriverDataSize != sizeof(struct agxw_context_private) ||
       !arguments->pPrivateDriverData)
      return E_INVALIDARG;

   private_data = arguments->pPrivateDriverData;
   if (private_data->version != AGXW_CONTEXT_PRIVATE_VERSION ||
       private_data->struct_size != sizeof(struct agxw_context_private) ||
       private_data->priority != AGXW_PRIORITY_HIGH ||
       private_data->flags != 0 || private_data->usc_exec_base != TEST_USC_BASE)
      return E_INVALIDARG;

   arguments->hContext = TEST_CONTEXT;
   ++create_context_calls;
   return S_OK;
}

static HRESULT APIENTRY
create_paging_callback(D3D12DDI_HRTCOMMANDQUEUE runtime_queue,
                       D3DDDICB_CREATEPAGINGQUEUE *arguments)
{
   if (runtime_queue.handle != TEST_RT_QUEUE || !arguments ||
       arguments->Priority != D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL ||
       arguments->PhysicalAdapterIndex != 0)
      return E_INVALIDARG;

   ++create_paging_calls;
   if (FAILED(paging_create_result))
      return paging_create_result;

   arguments->hPagingQueue = TEST_PAGING_QUEUE;
   arguments->hSyncObject = TEST_PAGING_FENCE;
   arguments->FenceValueCPUVirtualAddress = (void *)&paging_fence_value;
   return S_OK;
}

static HRESULT APIENTRY
destroy_context_callback(D3D12DDI_HRTCOMMANDQUEUE runtime_queue,
                         const D3DDDICB_DESTROYCONTEXT *arguments)
{
   if (runtime_queue.handle != TEST_RT_QUEUE || !arguments ||
       arguments->hContext != TEST_CONTEXT)
      return E_INVALIDARG;
   ++destroy_context_calls;
   return S_OK;
}

static HRESULT APIENTRY
destroy_paging_callback(D3D12DDI_HRTCOMMANDQUEUE runtime_queue,
                        const D3DDDI_DESTROYPAGINGQUEUE *arguments)
{
   if (runtime_queue.handle != TEST_RT_QUEUE || !arguments ||
       arguments->hPagingQueue != TEST_PAGING_QUEUE)
      return E_INVALIDARG;
   ++destroy_paging_calls;
   return S_OK;
}

static HRESULT APIENTRY
unused_allocate(HANDLE runtime_device, D3DDDICB_ALLOCATE *arguments)
{
   (void)runtime_device;
   (void)arguments;
   return E_NOTIMPL;
}

static HRESULT APIENTRY
unused_deallocate(HANDLE runtime_device,
                  const D3DDDICB_DEALLOCATE2 *arguments)
{
   (void)runtime_device;
   (void)arguments;
   return E_NOTIMPL;
}

static HRESULT APIENTRY
reserve_callback(HANDLE runtime_device,
                 D3DDDI_RESERVEGPUVIRTUALADDRESS *arguments)
{
   if (runtime_device != TEST_DEVICE || !arguments ||
       arguments->hPagingQueue != TEST_PAGING_QUEUE ||
       arguments->BaseAddress != TEST_USC_BASE ||
       arguments->MinimumAddress != TEST_USC_BASE ||
       arguments->MaximumAddress != TEST_USC_BASE + TEST_USC_SIZE ||
       arguments->Size != TEST_USC_SIZE)
      return E_INVALIDARG;
   arguments->VirtualAddress = TEST_USC_BASE;
   arguments->PagingFenceValue = UINT64_C(9);
   ++reserve_calls;
   return S_OK;
}

static HRESULT APIENTRY
free_callback(HANDLE runtime_device,
              const D3DDDICB_FREEGPUVIRTUALADDRESS *arguments)
{
   if (runtime_device != TEST_DEVICE || !arguments ||
       arguments->BaseAddress != TEST_USC_BASE ||
       arguments->Size != TEST_USC_SIZE)
      return E_INVALIDARG;
   ++free_calls;
   return S_OK;
}

static HRESULT APIENTRY
wait_callback(HANDLE runtime_device,
              const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *arguments)
{
   if (runtime_device != TEST_DEVICE || !arguments ||
       arguments->ObjectCount != 1 ||
       arguments->ObjectHandleArray[0] != TEST_PAGING_FENCE ||
       arguments->FenceValueArray[0] != UINT64_C(9))
      return E_INVALIDARG;
   paging_fence_value = UINT64_C(9);
   ++wait_calls;
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
   D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 callbacks;
   D3DDDI_DEVICECALLBACKS kernel_callbacks;
   D3D12DDI_HRTCOMMANDQUEUE runtime_queue;
   struct d12_umd_kmt kmt;
   struct d12_umd_queue_runtime queue;
   HRESULT result;

   memset(&callbacks, 0, sizeof(callbacks));
   callbacks.pfnCreateContextVirtualCb = create_context_callback;
   callbacks.pfnDestroyContextCb = destroy_context_callback;
   callbacks.pfnCreatePagingQueueCb = create_paging_callback;
   callbacks.pfnDestroyPagingQueueCb = destroy_paging_callback;
   memset(&kernel_callbacks, 0, sizeof(kernel_callbacks));
   kernel_callbacks.pfnAllocateCb = unused_allocate;
   kernel_callbacks.pfnDeallocate2Cb = unused_deallocate;
   kernel_callbacks.pfnReserveGpuVirtualAddressCb = reserve_callback;
   kernel_callbacks.pfnFreeGpuVirtualAddressCb = free_callback;
   kernel_callbacks.pfnWaitForSynchronizationObjectFromCpuCb = wait_callback;
   result = d12_umd_kmt_init(&kmt, TEST_DEVICE, &kernel_callbacks);
   require_result(result == S_OK, "initialize KMT callbacks");
   runtime_queue.handle = TEST_RT_QUEUE;

   result = d12_umd_queue_runtime_create(
      &callbacks, &kmt, runtime_queue, AGXW_PRIORITY_HIGH, TEST_USC_BASE,
      TEST_USC_SIZE, true, &queue);
   require_result(result == S_OK && create_context_calls == 1 &&
                     create_paging_calls == 1 && reserve_calls == 1 &&
                     wait_calls == 1,
                  "create queue callbacks");
   require_result(queue.runtime_queue.handle == TEST_RT_QUEUE &&
                     queue.context == TEST_CONTEXT &&
                     queue.paging_queue == TEST_PAGING_QUEUE &&
                     queue.paging_fence == TEST_PAGING_FENCE &&
                     queue.paging_fence_cpu == &paging_fence_value &&
                     queue.usc_base == TEST_USC_BASE &&
                     queue.usc_size == TEST_USC_SIZE &&
                     queue.owns_usc_reservation,
                  "create queue outputs");

   result = d12_umd_queue_runtime_destroy(&callbacks, &kmt, &queue);
   require_result(result == S_OK && destroy_paging_calls == 1 &&
                     destroy_context_calls == 1 && free_calls == 1,
                  "destroy queue callbacks");
   require_result(queue.runtime_queue.handle == NULL && queue.context == NULL &&
                     queue.paging_queue == 0 && queue.paging_fence == 0 &&
                     queue.paging_fence_cpu == NULL,
                  "destroy queue clears state");

   paging_create_result = E_OUTOFMEMORY;
   result = d12_umd_queue_runtime_create(
      &callbacks, &kmt, runtime_queue, AGXW_PRIORITY_HIGH, TEST_USC_BASE,
      TEST_USC_SIZE, true, &queue);
   require_result(result == E_OUTOFMEMORY && create_context_calls == 1 &&
                     create_paging_calls == 2 && destroy_context_calls == 1 &&
                     destroy_paging_calls == 1,
                  "paging failure rolls context back");
   require_result(queue.context == NULL && queue.paging_queue == 0,
                  "rollback leaves empty queue");

   result = d12_umd_queue_runtime_create(
      &callbacks, &kmt, runtime_queue, AGXW_PRIORITY_HIGH,
      TEST_USC_BASE + UINT64_C(1), TEST_USC_SIZE, true, &queue);
   require_result(result == E_INVALIDARG && create_context_calls == 1,
                  "reject unaligned USC base before callback");

   puts("d12_umd_queueprobe: PASS");
   return 0;
}
