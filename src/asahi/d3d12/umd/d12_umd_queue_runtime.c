/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_queue_runtime.h"

#include <stdbool.h>
#include <string.h>

static bool
d12_umd_queue_priority_valid(enum agxw_queue_priority priority)
{
   return priority >= AGXW_PRIORITY_LOW && priority <= AGXW_PRIORITY_REALTIME;
}

HRESULT
d12_umd_queue_runtime_create(
   const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *callbacks,
   struct d12_umd_kmt *kmt, D3D12DDI_HRTCOMMANDQUEUE runtime_queue,
   enum agxw_queue_priority priority, uint64_t usc_exec_base,
   uint64_t usc_size, bool reserve_usc, struct d12_umd_queue_runtime *queue)
{
   struct agxw_context_private private_data;
   D3DDDICB_CREATECONTEXTVIRTUAL create_context;
   D3DDDICB_CREATEPAGINGQUEUE create_paging;
   HRESULT result;

   if (!callbacks || !kmt || !runtime_queue.handle || !queue ||
       !callbacks->pfnCreateContextVirtualCb ||
       !callbacks->pfnDestroyContextCb || !callbacks->pfnCreatePagingQueueCb ||
       !callbacks->pfnDestroyPagingQueueCb ||
       !d12_umd_queue_priority_valid(priority) || !usc_exec_base ||
       !usc_size || (usc_exec_base & (AGXW_PAGE_SIZE - 1)) ||
       (usc_size & (AGXW_PAGE_SIZE - 1)))
      return E_INVALIDARG;

   memset(queue, 0, sizeof(*queue));
   create_paging = (D3DDDICB_CREATEPAGINGQUEUE){
      .Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL,
      .PhysicalAdapterIndex = 0,
   };
   result = callbacks->pfnCreatePagingQueueCb(runtime_queue, &create_paging);
   if (FAILED(result) || !create_paging.hPagingQueue ||
       !create_paging.hSyncObject || !create_paging.FenceValueCPUVirtualAddress) {
      if (SUCCEEDED(result) && create_paging.hPagingQueue) {
         const D3DDDI_DESTROYPAGINGQUEUE destroy_paging = {
            .hPagingQueue = create_paging.hPagingQueue,
         };
         (void)callbacks->pfnDestroyPagingQueueCb(runtime_queue,
                                                  &destroy_paging);
      }
      return FAILED(result) ? result : E_FAIL;
   }

   if (reserve_usc) {
      result = d12_umd_kmt_reserve_gpu_va(
         kmt, create_paging.hPagingQueue, create_paging.hSyncObject,
         (const volatile uint64_t *)create_paging.FenceValueCPUVirtualAddress,
         usc_exec_base, usc_size);
      if (FAILED(result)) {
         const D3DDDI_DESTROYPAGINGQUEUE destroy_paging = {
            .hPagingQueue = create_paging.hPagingQueue,
         };
         (void)callbacks->pfnDestroyPagingQueueCb(runtime_queue,
                                                  &destroy_paging);
         return result;
      }
   }

   private_data = (struct agxw_context_private){
      .version = AGXW_CONTEXT_PRIVATE_VERSION,
      .struct_size = sizeof(private_data),
      .priority = (uint32_t)priority,
      .usc_exec_base = usc_exec_base,
   };
   create_context = (D3DDDICB_CREATECONTEXTVIRTUAL){
      .NodeOrdinal = AGXW_NODE_3D,
      .EngineAffinity = 1,
      .pPrivateDriverData = &private_data,
      .PrivateDriverDataSize = sizeof(private_data),
   };
   result = callbacks->pfnCreateContextVirtualCb(runtime_queue,
                                                  &create_context);
   if (FAILED(result) || !create_context.hContext) {
      const D3DDDI_DESTROYPAGINGQUEUE destroy_paging = {
         .hPagingQueue = create_paging.hPagingQueue,
      };
      if (reserve_usc)
         (void)d12_umd_kmt_free_gpu_va(kmt, usc_exec_base, usc_size);
      (void)callbacks->pfnDestroyPagingQueueCb(runtime_queue,
                                               &destroy_paging);
      return FAILED(result) ? result : E_FAIL;
   }

   *queue = (struct d12_umd_queue_runtime){
      .runtime_queue = runtime_queue,
      .context = create_context.hContext,
      .paging_queue = create_paging.hPagingQueue,
      .paging_fence = create_paging.hSyncObject,
      .paging_fence_cpu =
         (const volatile uint64_t *)create_paging.FenceValueCPUVirtualAddress,
      .usc_base = usc_exec_base,
      .usc_size = usc_size,
      .owns_usc_reservation = reserve_usc,
   };
   return S_OK;
}

HRESULT
d12_umd_queue_runtime_destroy(
   const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *callbacks,
   struct d12_umd_kmt *kmt, struct d12_umd_queue_runtime *queue)
{
   D3DDDI_DESTROYPAGINGQUEUE destroy_paging;
   D3DDDICB_DESTROYCONTEXT destroy_context;
   HRESULT paging_result = S_OK;
   HRESULT context_result = S_OK;

   HRESULT reservation_result = S_OK;

   if (!callbacks || !kmt || !queue || !queue->runtime_queue.handle ||
       !callbacks->pfnDestroyContextCb || !callbacks->pfnDestroyPagingQueueCb)
      return E_INVALIDARG;

   if (queue->context) {
      destroy_context = (D3DDDICB_DESTROYCONTEXT){
         .hContext = queue->context,
      };
      context_result = callbacks->pfnDestroyContextCb(queue->runtime_queue,
                                                       &destroy_context);
      if (SUCCEEDED(context_result))
         queue->context = NULL;
   }
   if (queue->owns_usc_reservation && !queue->context) {
      reservation_result =
         d12_umd_kmt_free_gpu_va(kmt, queue->usc_base, queue->usc_size);
      if (SUCCEEDED(reservation_result))
         queue->owns_usc_reservation = false;
   }
   if (queue->paging_queue && !queue->context &&
       !queue->owns_usc_reservation) {
      destroy_paging = (D3DDDI_DESTROYPAGINGQUEUE){
         .hPagingQueue = queue->paging_queue,
      };
      paging_result = callbacks->pfnDestroyPagingQueueCb(
         queue->runtime_queue, &destroy_paging);
      if (SUCCEEDED(paging_result)) {
         queue->paging_queue = 0;
         queue->paging_fence = 0;
         queue->paging_fence_cpu = NULL;
      }
   }
   if (!queue->paging_queue && !queue->context)
      memset(queue, 0, sizeof(*queue));
   if (FAILED(context_result))
      return context_result;
   if (FAILED(reservation_result))
      return reservation_result;
   return paging_result;
}
