/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Native ARM64 loader/adapter probe.  This uses a deterministic callback to
 * exercise the exact DLL ABI without needing the D3D12 runtime.  The hardware
 * probe uses the same DLL after INF registration.
 */

#include "d12_umd_wddm.h"
#include <windows.h>
#include <d3d12umddi.h>

#include "../d12_umd_abi.h"
#include "../d12_umd_internal.h"
#include "../d12_umd_pipeline_serialization.h"
#include "../d12_umd_present_funcs.h"
#include "../d12_umd_resource_private.h"
#include "../../d12_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_RT_QUEUE ((HANDLE)(uintptr_t)0x7000)
#define TEST_CONTEXT ((HANDLE)(uintptr_t)0x7100)
#define TEST_PAGING_QUEUE ((D3DKMT_HANDLE)0x7200)
#define TEST_PAGING_FENCE ((D3DKMT_HANDLE)0x7300)
#define TEST_USC_BASE UINT64_C(0x1100000000)
#define TEST_USC_SIZE UINT64_C(0x100000000)
#define TEST_RT_RESOURCE ((HANDLE)(uintptr_t)0x8000)
#define TEST_ALLOCATION ((D3DKMT_HANDLE)0x8100)
#define TEST_KM_RESOURCE ((D3DKMT_HANDLE)0x8200)
#define TEST_RESOURCE_GPU_VA UINT64_C(0x2000000000)
#define TEST_RT_PAGING_QUEUE ((HANDLE)(uintptr_t)0x8300)
#define TEST_RT_COMMAND_LIST_TABLE ((HANDLE)(uintptr_t)0x8400)
#define TEST_DXGI_CONTEXT ((void *)(uintptr_t)0x8500)

static volatile uint64_t paging_fence_value;
static unsigned context_create_calls;
static unsigned context_destroy_calls;
static unsigned paging_create_calls;
static unsigned paging_destroy_calls;
static unsigned reserve_calls;
static unsigned free_calls;
static unsigned allocation_calls;
static unsigned deallocation_calls;
static unsigned lock_calls;
static unsigned unlock_calls;
static unsigned make_resident_calls;
static unsigned evict_calls;
static unsigned offer_calls;
static unsigned reclaim_calls;
static unsigned set_error_calls;
static unsigned priority_calls;
static unsigned residency_query_calls;
static unsigned dxgi_offer_calls;
static unsigned dxgi_reclaim_calls;
#if D12_UMD_TARGETS_WDDM21
static uint64_t dxgi_paging_wait_value;
#endif
static unsigned dxgi_present_calls;
static unsigned blt_allocator_create_calls;
static unsigned blt_allocator_reset_calls;
static unsigned blt_allocator_destroy_calls;
static unsigned blt_list_create_calls;
static unsigned blt_list_reset_calls;
static unsigned blt_list_close_calls;
static unsigned blt_list_destroy_calls;
static unsigned blt_record_calls;
static unsigned blt_execute_calls;
static unsigned signature_create_calls;
static unsigned signature_destroy_calls;
static uint32_t signature_vertex_slot;
static HRESULT blt_execute_result = S_OK;
static D3DDDI_RESIDENCYSTATUS query_residency_status =
   D3DDDI_RESIDENCYSTATUS_RESIDENTINSHAREDMEMORY;
static BOOL set_error_runtime_device_valid;
static HRESULT last_set_error;
static unsigned char mapped_heap[UINT32_C(0x20000)];
static struct agxw_alloc_private saved_allocation_private;
static struct d12_umd_resource_private saved_resource_private;

#define TEST_BLT_DEVICE ((void *)(uintptr_t)0xa100)
#define TEST_BLT_QUEUE ((void *)(uintptr_t)0xa200)
#define TEST_BLT_ALLOCATOR ((void *)(uintptr_t)0xa300)
#define TEST_BLT_LIST ((void *)(uintptr_t)0xa400)
#define TEST_BLT_DESTINATION ((void *)(uintptr_t)0xa500)
#define TEST_BLT_SOURCE ((void *)(uintptr_t)0xa600)
#define TEST_SIGNATURE ((void *)(uintptr_t)0xa700)
#define TEST_TILED_RESOURCE ((void *)(uintptr_t)0xa800)
#define TEST_TILE_HEAP ((void *)(uintptr_t)0xa900)
#define TEST_TILE_QUEUE ((void *)(uintptr_t)0xaa00)
#define TEST_TILE_LIST ((void *)(uintptr_t)0xab00)
#define TEST_TILE_BUFFER ((void *)(uintptr_t)0xac00)
#define TEST_TILED_GPU_VA UINT64_C(0x3300000000)

static unsigned sparse_create_calls;
static unsigned sparse_destroy_calls;
static unsigned sparse_update_calls;
static unsigned sparse_copy_mapping_calls;
static unsigned sparse_copy_tiles_calls;

static HRESULT
test_sparse_create_reserved(
   void *device, const struct d12_frontend_resource_desc *desc,
   const struct d12_frontend_clear_value *clear_value, void **out_resource)
{
   if (device != TEST_BLT_DEVICE || !desc || clear_value || !out_resource ||
       desc->dimension != D3D12DDI_RT_TEXTURE2D || desc->width != 1024 ||
       desc->height != 1024 || desc->mip_levels != 5 ||
       desc->format != DXGI_FORMAT_R8G8B8A8_UNORM ||
       desc->layout != D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE)
      return E_INVALIDARG;
   ++sparse_create_calls;
   *out_resource = TEST_TILED_RESOURCE;
   return S_OK;
}

static void
test_sparse_destroy_resource(void *resource)
{
   if (resource == TEST_TILED_RESOURCE)
      ++sparse_destroy_calls;
}

static uint64_t
test_sparse_resource_gpu_va(void *resource)
{
   return resource == TEST_TILED_RESOURCE ? TEST_TILED_GPU_VA : 0;
}

static HRESULT
test_sparse_mip_packing(void *resource, uint32_t *packed_mips,
                        uint32_t *packed_tiles)
{
   if (resource != TEST_TILED_RESOURCE || !packed_mips || !packed_tiles)
      return E_INVALIDARG;
   *packed_mips = 3;
   *packed_tiles = 2;
   return S_OK;
}

static HRESULT
test_sparse_update_tile_mappings(
   void *queue, void *resource, uint32_t region_count,
   const struct d12_frontend_tiled_resource_coordinate *region_starts,
   const struct d12_frontend_tile_region_size *region_sizes, void *heap,
   uint32_t range_count, const uint32_t *range_flags,
   const uint32_t *heap_start_offsets, const uint32_t *range_tile_counts,
   uint32_t flags)
{
   if (queue != TEST_TILE_QUEUE || resource != TEST_TILED_RESOURCE ||
       heap != TEST_TILE_HEAP || region_count != 1 || !region_starts ||
       !region_sizes || region_starts[0].x != 1 ||
       region_starts[0].y != 2 || region_starts[0].z != 0 ||
       region_starts[0].subresource != 0 ||
       region_sizes[0].num_tiles != 2 || region_sizes[0].use_box != 1 ||
       region_sizes[0].width != 2 || region_sizes[0].height != 1 ||
       region_sizes[0].depth != 1 || range_count != 1 || !range_flags ||
       range_flags[0] != D12_FRONTEND_TILE_RANGE_REUSE_SINGLE_TILE ||
       !heap_start_offsets || heap_start_offsets[0] != 4 ||
       !range_tile_counts || range_tile_counts[0] != 2 ||
       flags != D12_FRONTEND_TILE_MAPPING_NO_HAZARD)
      return E_INVALIDARG;
   ++sparse_update_calls;
   return S_OK;
}

static HRESULT
test_sparse_copy_tile_mappings(
   void *queue, void *destination_resource,
   const struct d12_frontend_tiled_resource_coordinate *destination_start,
   void *source_resource,
   const struct d12_frontend_tiled_resource_coordinate *source_start,
   const struct d12_frontend_tile_region_size *region_size, uint32_t flags)
{
   if (queue != TEST_TILE_QUEUE ||
       destination_resource != TEST_TILED_RESOURCE ||
       source_resource != TEST_TILED_RESOURCE || !destination_start ||
       !source_start || !region_size || destination_start->x != 3 ||
       destination_start->subresource != 1 || source_start->x != 4 ||
       source_start->subresource != 2 || region_size->num_tiles != 2 ||
       region_size->use_box || flags != D12_FRONTEND_TILE_MAPPING_NONE)
      return E_INVALIDARG;
   ++sparse_copy_mapping_calls;
   return S_OK;
}

static HRESULT
test_sparse_copy_tiles(
   void *list, void *resource,
   const struct d12_frontend_tiled_resource_coordinate *region_start,
   const struct d12_frontend_tile_region_size *region_size, void *buffer,
   uint64_t buffer_offset, uint32_t flags)
{
   if (list != TEST_TILE_LIST || resource != TEST_TILED_RESOURCE ||
       buffer != TEST_TILE_BUFFER || !region_start || !region_size ||
       region_start->x != 5 || region_start->subresource != 3 ||
       region_size->num_tiles != 1 || region_size->use_box ||
       buffer_offset != UINT64_C(0x10000) ||
       flags != D12_FRONTEND_TILE_COPY_LINEAR_TO_TILED)
      return E_INVALIDARG;
   ++sparse_copy_tiles_calls;
   return S_OK;
}

static const struct d12_umd_renderer_ops test_sparse_renderer_ops = {
   .update_tile_mappings = test_sparse_update_tile_mappings,
   .copy_tile_mappings = test_sparse_copy_tile_mappings,
   .copy_tiles = test_sparse_copy_tiles,
   .create_reserved_resource = test_sparse_create_reserved,
   .destroy_resource = test_sparse_destroy_resource,
   .resource_gpu_virtual_address = test_sparse_resource_gpu_va,
   .resource_mip_packing = test_sparse_mip_packing,
};

#if D12_UMD_TARGETS_WDDM21
static uint32_t
test_residency_paging_queue(void *device)
{
   return device == TEST_BLT_DEVICE ? TEST_PAGING_QUEUE : 0;
}

static HRESULT
test_residency_wait_paging(void *device, uint64_t value)
{
   if (device != TEST_BLT_DEVICE || !value)
      return E_INVALIDARG;
   dxgi_paging_wait_value = value;
   return S_OK;
}

static const struct d12_umd_renderer_ops test_residency_renderer_ops = {
   .paging_queue = test_residency_paging_queue,
   .wait_paging = test_residency_wait_paging,
};
#endif

static HRESULT
test_create_command_signature(
   void *device, const struct d12_frontend_command_signature_desc *desc,
   void **out_signature)
{
   if (device != TEST_BLT_DEVICE || !desc || !out_signature ||
       desc->argument_count != 2 || !desc->arguments ||
       desc->arguments[0].type !=
          D3D12DDI_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW ||
       desc->arguments[1].type != D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW)
      return E_INVALIDARG;

   ++signature_create_calls;
   signature_vertex_slot = desc->arguments[0].root_parameter_index;
   *out_signature = TEST_SIGNATURE;
   return S_OK;
}

static void
test_destroy_command_signature(void *signature)
{
   if (signature == TEST_SIGNATURE)
      ++signature_destroy_calls;
}

static const struct d12_umd_renderer_ops test_signature_renderer_ops = {
   .create_command_signature = test_create_command_signature,
   .destroy_command_signature = test_destroy_command_signature,
};

static HRESULT
test_blt_create_allocator(void *device,
                          enum d12_umd_renderer_command_type type,
                          void **out_allocator)
{
   if (device != TEST_BLT_DEVICE ||
       type != D12_UMD_RENDERER_COMMAND_DIRECT || !out_allocator)
      return E_INVALIDARG;
   ++blt_allocator_create_calls;
   *out_allocator = TEST_BLT_ALLOCATOR;
   return S_OK;
}

static HRESULT
test_blt_reset_allocator(void *allocator)
{
   if (allocator != TEST_BLT_ALLOCATOR)
      return E_INVALIDARG;
   ++blt_allocator_reset_calls;
   return S_OK;
}

static void
test_blt_destroy_allocator(void *allocator)
{
   if (allocator == TEST_BLT_ALLOCATOR)
      ++blt_allocator_destroy_calls;
}

static HRESULT
test_blt_create_list(void *device,
                     enum d12_umd_renderer_command_type type,
                     void *allocator, void **out_list)
{
   if (device != TEST_BLT_DEVICE ||
       type != D12_UMD_RENDERER_COMMAND_DIRECT ||
       allocator != TEST_BLT_ALLOCATOR || !out_list)
      return E_INVALIDARG;
   ++blt_list_create_calls;
   *out_list = TEST_BLT_LIST;
   return S_OK;
}

static HRESULT
test_blt_reset_list(void *list, void *allocator)
{
   if (list != TEST_BLT_LIST || allocator != TEST_BLT_ALLOCATOR)
      return E_INVALIDARG;
   ++blt_list_reset_calls;
   return S_OK;
}

static HRESULT
test_blt_close_list(void *list)
{
   if (list != TEST_BLT_LIST)
      return E_INVALIDARG;
   ++blt_list_close_calls;
   return S_OK;
}

static void
test_blt_destroy_list(void *list)
{
   if (list == TEST_BLT_LIST)
      ++blt_list_destroy_calls;
}

static HRESULT
test_blt_record(void *list, void *destination, void *source,
                const struct d12_frontend_present_blt_desc *desc)
{
   if (list != TEST_BLT_LIST || destination != TEST_BLT_DESTINATION ||
       source != TEST_BLT_SOURCE || !desc ||
       desc->destination_bottom != 16 || desc->source_bottom != 32)
      return E_INVALIDARG;
   ++blt_record_calls;
   return S_OK;
}

static HRESULT
test_blt_execute(void *queue, uint32_t count, void *const *lists)
{
   if (queue != TEST_BLT_QUEUE || count != 1 || !lists ||
       lists[0] != TEST_BLT_LIST)
      return E_INVALIDARG;
   ++blt_execute_calls;
   return blt_execute_result;
}

static const struct d12_umd_renderer_ops test_blt_renderer_ops = {
   .execute_command_lists = test_blt_execute,
   .create_command_allocator = test_blt_create_allocator,
   .reset_command_allocator = test_blt_reset_allocator,
   .destroy_command_allocator = test_blt_destroy_allocator,
   .create_command_list = test_blt_create_list,
   .close_command_list = test_blt_close_list,
   .reset_command_list = test_blt_reset_list,
   .destroy_command_list = test_blt_destroy_list,
   .present_blt = test_blt_record,
};

static HRESULT APIENTRY
query_adapter_info(HANDLE runtime_adapter,
                   const D3DDDICB_QUERYADAPTERINFO *query)
{
   const struct agxw_adapter_identity *identity =
      (const struct agxw_adapter_identity *)runtime_adapter;

   if (!identity || !query || !query->pPrivateDriverData ||
       query->PrivateDriverDataSize != sizeof(*identity))
      return E_INVALIDARG;

   memcpy(query->pPrivateDriverData, identity, sizeof(*identity));
   return S_OK;
}

static HRESULT APIENTRY
unused_allocate(HANDLE runtime_device, D3DDDICB_ALLOCATE *arguments)
{
   const struct agxw_alloc_private *private_data;

   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->hResource != TEST_RT_RESOURCE ||
       arguments->NumAllocations != 1 || !arguments->pAllocationInfo2 ||
       !arguments->pPrivateDriverData ||
       arguments->PrivateDriverDataSize != sizeof(saved_resource_private) ||
       arguments->pAllocationInfo2->PrivateDriverDataSize !=
          sizeof(struct agxw_alloc_private))
      return E_INVALIDARG;
   if (!d12_umd_resource_private_parse(arguments->pPrivateDriverData,
                                       arguments->PrivateDriverDataSize,
                                       &saved_resource_private))
      return E_INVALIDARG;
   private_data = arguments->pAllocationInfo2->pPrivateDriverData;
   if (!private_data || private_data->version != AGXW_ALLOC_PRIVATE_VERSION ||
       private_data->size != sizeof(mapped_heap) ||
       private_data->flags != (AGXW_ALLOC_VM_PRIVATE | AGXW_ALLOC_WRITEBACK) ||
       private_data->align_log2 != 16)
      return E_INVALIDARG;
   saved_allocation_private = *private_data;
   arguments->hKMResource = TEST_KM_RESOURCE;
   arguments->pAllocationInfo2->hAllocation = TEST_ALLOCATION;
   arguments->pAllocationInfo2->GpuVirtualAddress = TEST_RESOURCE_GPU_VA;
   ++allocation_calls;
   return S_OK;
}

static HRESULT APIENTRY
unused_deallocate(HANDLE runtime_device,
                  const D3DDDICB_DEALLOCATE2 *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->hResource != TEST_RT_RESOURCE ||
       arguments->NumAllocations != 0 || arguments->HandleList != NULL)
      return E_INVALIDARG;
   ++deallocation_calls;
   return S_OK;
}

static HRESULT APIENTRY
lock_allocation(HANDLE runtime_device, D3DDDICB_LOCK2 *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->hAllocation != TEST_ALLOCATION || arguments->Flags.Value != 0)
      return E_INVALIDARG;
   arguments->pData = mapped_heap;
   ++lock_calls;
   return S_OK;
}

static HRESULT APIENTRY
unlock_allocation(HANDLE runtime_device, const D3DDDICB_UNLOCK2 *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->hAllocation != TEST_ALLOCATION)
      return E_INVALIDARG;
   ++unlock_calls;
   return S_OK;
}

static HRESULT APIENTRY
invalidate_allocation(HANDLE runtime_device,
                      const D3DDDICB_INVALIDATECACHE *arguments)
{
   return runtime_device == (HANDLE)(uintptr_t)0x6000 && arguments &&
                 arguments->hAllocation == TEST_ALLOCATION &&
                 arguments->Length
             ? S_OK
             : E_INVALIDARG;
}

static void APIENTRY
set_error(D3D10DDI_HRTDEVICE runtime_device, HRESULT error)
{
   ++set_error_calls;
   set_error_runtime_device_valid =
      runtime_device.handle == (void *)(uintptr_t)0x6000;
   last_set_error = error;
}

static void APIENTRY
set_command_list_ddi_table(D3D12DDI_HRTCOMMANDLIST runtime_list,
                           D3D12DDI_HRTTABLE runtime_table)
{
   (void)runtime_list;
   (void)runtime_table;
}

static HRESULT APIENTRY
dxgi_present(HANDLE runtime_device, DXGIDDICB_PRESENT *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->hSrcAllocation != TEST_ALLOCATION ||
       arguments->hDstAllocation ||
       arguments->pDXGIContext != TEST_DXGI_CONTEXT ||
       arguments->hContext != TEST_CONTEXT ||
       arguments->BroadcastContextCount ||
       arguments->PrivateDriverDataSize || arguments->pPrivateDriverData)
      return E_INVALIDARG;
   ++dxgi_present_calls;
   return S_OK;
}

static HRESULT APIENTRY
set_priority(HANDLE runtime_device, D3DDDICB_SETPRIORITY *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->hResource || arguments->NumAllocations != 1 ||
       !arguments->HandleList ||
       arguments->HandleList[0] != TEST_ALLOCATION ||
       !arguments->pPriorities || arguments->pPriorities[0] != 7)
      return E_INVALIDARG;
   ++priority_calls;
   return S_OK;
}

static HRESULT APIENTRY
query_residency(HANDLE runtime_device,
                const D3DDDICB_QUERYRESIDENCY *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->hResource || arguments->NumAllocations != 1 ||
       !arguments->HandleList ||
       arguments->HandleList[0] != TEST_ALLOCATION ||
       !arguments->pResidencyStatus)
      return E_INVALIDARG;
   arguments->pResidencyStatus[0] = query_residency_status;
   ++residency_query_calls;
   return S_OK;
}

static HRESULT APIENTRY
dxgi_offer_allocations(HANDLE runtime_device,
                       const D3DDDICB_OFFERALLOCATIONS *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->pResources || !arguments->HandleList ||
       !arguments->NumAllocations ||
       arguments->Priority != D3DDDI_OFFER_PRIORITY_NORMAL)
      return E_INVALIDARG;
   for (UINT index = 0; index < arguments->NumAllocations; ++index) {
      if (arguments->HandleList[index] != TEST_ALLOCATION)
         return E_INVALIDARG;
   }
   ++dxgi_offer_calls;
   return S_OK;
}

#if D12_UMD_TARGETS_WDDM21
static HRESULT APIENTRY
dxgi_offer_allocations2(HANDLE runtime_device,
                        const D3DDDICB_OFFERALLOCATIONS2 *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->pResources || !arguments->HandleList ||
       !arguments->NumAllocations ||
       arguments->Priority != D3DDDI_OFFER_PRIORITY_NORMAL ||
       (arguments->Flags.Value & ~1u) != 0u)
      return E_INVALIDARG;
   for (UINT index = 0; index < arguments->NumAllocations; ++index) {
      if (arguments->HandleList[index] != TEST_ALLOCATION)
         return E_INVALIDARG;
   }
   ++dxgi_offer_calls;
   return S_OK;
}

static HRESULT APIENTRY
dxgi_reclaim_allocations3(HANDLE runtime_device,
                          D3DDDICB_RECLAIMALLOCATIONS3 *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->PagingQueue != TEST_PAGING_QUEUE ||
       arguments->pResources || !arguments->HandleList ||
       !arguments->pResults || !arguments->NumAllocations)
      return E_INVALIDARG;
   for (UINT index = 0; index < arguments->NumAllocations; ++index) {
      if (arguments->HandleList[index] != TEST_ALLOCATION)
         return E_INVALIDARG;
      arguments->pResults[index] = (index & 1u)
                                       ? D3DDDI_RECLAIM_RESULT_DISCARDED
                                       : D3DDDI_RECLAIM_RESULT_OK;
   }
   arguments->PagingFenceValue = UINT64_C(13);
   ++dxgi_reclaim_calls;
   return S_OK;
}
#endif

static HRESULT APIENTRY
dxgi_reclaim_allocations(
   HANDLE runtime_device, const D3DDDICB_RECLAIMALLOCATIONS *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->pResources || !arguments->HandleList ||
       !arguments->NumAllocations)
      return E_INVALIDARG;
   for (UINT index = 0; index < arguments->NumAllocations; ++index) {
      if (arguments->HandleList[index] != TEST_ALLOCATION)
         return E_INVALIDARG;
      if (arguments->pDiscarded)
         arguments->pDiscarded[index] = (index & 1u) != 0;
   }
   ++dxgi_reclaim_calls;
   return S_OK;
}

static HRESULT APIENTRY
create_context(D3D12DDI_HRTCOMMANDQUEUE runtime_queue,
               D3DDDICB_CREATECONTEXTVIRTUAL *arguments)
{
   const struct agxw_context_private *private_data;

   if (runtime_queue.handle != TEST_RT_QUEUE || !arguments ||
       arguments->NodeOrdinal != AGXW_NODE_3D ||
       arguments->EngineAffinity != 1 ||
       arguments->PrivateDriverDataSize != sizeof(struct agxw_context_private) ||
       !arguments->pPrivateDriverData)
      return E_INVALIDARG;
   private_data = arguments->pPrivateDriverData;
   if (private_data->version != AGXW_CONTEXT_PRIVATE_VERSION ||
       private_data->priority != AGXW_PRIORITY_MEDIUM ||
       private_data->usc_exec_base != TEST_USC_BASE)
      return E_INVALIDARG;
   arguments->hContext = TEST_CONTEXT;
   ++context_create_calls;
   return S_OK;
}

static HRESULT APIENTRY
destroy_context(D3D12DDI_HRTCOMMANDQUEUE runtime_queue,
                const D3DDDICB_DESTROYCONTEXT *arguments)
{
   if (runtime_queue.handle != TEST_RT_QUEUE || !arguments ||
       arguments->hContext != TEST_CONTEXT)
      return E_INVALIDARG;
   ++context_destroy_calls;
   return S_OK;
}

static HRESULT APIENTRY
create_paging(D3D12DDI_HRTCOMMANDQUEUE runtime_queue,
              D3DDDICB_CREATEPAGINGQUEUE *arguments)
{
   if (runtime_queue.handle != TEST_RT_QUEUE || !arguments ||
       arguments->Priority != D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL)
      return E_INVALIDARG;
   arguments->hPagingQueue = TEST_PAGING_QUEUE;
   arguments->hSyncObject = TEST_PAGING_FENCE;
   arguments->FenceValueCPUVirtualAddress = (void *)&paging_fence_value;
   ++paging_create_calls;
   return S_OK;
}

static HRESULT APIENTRY
destroy_paging(D3D12DDI_HRTCOMMANDQUEUE runtime_queue,
               const D3DDDI_DESTROYPAGINGQUEUE *arguments)
{
   if (runtime_queue.handle != TEST_RT_QUEUE || !arguments ||
       arguments->hPagingQueue != TEST_PAGING_QUEUE)
      return E_INVALIDARG;
   ++paging_destroy_calls;
   return S_OK;
}

static HRESULT APIENTRY
reserve_gpu_va(HANDLE runtime_device,
               D3DDDI_RESERVEGPUVIRTUALADDRESS *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->hPagingQueue != TEST_PAGING_QUEUE ||
       arguments->BaseAddress != TEST_USC_BASE ||
       arguments->Size != TEST_USC_SIZE)
      return E_INVALIDARG;
   arguments->VirtualAddress = TEST_USC_BASE;
   arguments->PagingFenceValue = UINT64_C(3);
   ++reserve_calls;
   return S_OK;
}

static HRESULT APIENTRY
free_gpu_va(HANDLE runtime_device,
            const D3DDDICB_FREEGPUVIRTUALADDRESS *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->BaseAddress != TEST_USC_BASE ||
       arguments->Size != TEST_USC_SIZE)
      return E_INVALIDARG;
   ++free_calls;
   return S_OK;
}

static HRESULT APIENTRY
wait_paging(HANDLE runtime_device,
            const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->ObjectCount != 1 ||
       arguments->ObjectHandleArray[0] != TEST_PAGING_FENCE ||
       arguments->FenceValueArray[0] != UINT64_C(3))
      return E_INVALIDARG;
   paging_fence_value = UINT64_C(3);
   return S_OK;
}

static HRESULT APIENTRY
make_resident(D3D12DDI_HRTDEVICE runtime_device,
              D3D12DDI_HRTPAGINGQUEUE runtime_paging_queue,
              D3DDDI_MAKERESIDENT *arguments)
{
   if (runtime_device.handle != (HANDLE)(uintptr_t)0x6000 ||
       runtime_paging_queue.handle != TEST_RT_PAGING_QUEUE || !arguments ||
       arguments->hPagingQueue != 0 || !arguments->NumAllocations ||
       !arguments->AllocationList ||
       arguments->PriorityList != NULL || arguments->Flags.Value != 0)
      return E_INVALIDARG;
   for (UINT index = 0; index < arguments->NumAllocations; ++index) {
      if (arguments->AllocationList[index] != TEST_ALLOCATION)
         return E_INVALIDARG;
   }
   arguments->PagingFenceValue = UINT64_C(11);
   ++make_resident_calls;
   return E_PENDING;
}

static HRESULT APIENTRY
evict(D3D12DDI_HRTDEVICE runtime_device,
      const D3DDDICB_EVICT *arguments)
{
   if (runtime_device.handle != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       !arguments->NumAllocations || !arguments->AllocationList ||
       arguments->Flags.Value != 0)
      return E_INVALIDARG;
   for (UINT index = 0; index < arguments->NumAllocations; ++index) {
      if (arguments->AllocationList[index] != TEST_ALLOCATION)
         return E_INVALIDARG;
   }
   ++evict_calls;
   return S_OK;
}

static HRESULT APIENTRY
offer_allocations(D3D12DDI_HRTDEVICE runtime_device,
                  const D3D12DDICB_OFFERALLOCATIONS *arguments)
{
   if (runtime_device.handle != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       !arguments->NumAllocations || !arguments->HandleList ||
       arguments->Priority != D3DDDI_OFFER_PRIORITY_NORMAL)
      return E_INVALIDARG;
   for (UINT index = 0; index < arguments->NumAllocations; ++index) {
      if (arguments->HandleList[index] != TEST_ALLOCATION)
         return E_INVALIDARG;
   }
   ++offer_calls;
   return S_OK;
}

#if D12_UMD_TARGETS_WDDM21
static HRESULT APIENTRY
offer_allocations2(HANDLE runtime_device,
                   const D3DDDICB_OFFERALLOCATIONS2 *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->pResources || !arguments->NumAllocations ||
       !arguments->HandleList ||
       arguments->Priority != D3DDDI_OFFER_PRIORITY_NORMAL ||
       arguments->Flags.Value != 0)
      return E_INVALIDARG;
   for (UINT index = 0; index < arguments->NumAllocations; ++index) {
      if (arguments->HandleList[index] != TEST_ALLOCATION)
         return E_INVALIDARG;
   }
   ++offer_calls;
   return S_OK;
}

static HRESULT APIENTRY
reclaim_allocations3(HANDLE runtime_device,
                     D3DDDICB_RECLAIMALLOCATIONS3 *arguments)
{
   if (runtime_device != (HANDLE)(uintptr_t)0x6000 || !arguments ||
       arguments->PagingQueue != TEST_PAGING_QUEUE ||
       !arguments->NumAllocations || !arguments->HandleList ||
       !arguments->pResults)
      return E_INVALIDARG;
   for (UINT index = 0; index < arguments->NumAllocations; ++index) {
      if (arguments->HandleList[index] != TEST_ALLOCATION)
         return E_INVALIDARG;
      arguments->pResults[index] = D3DDDI_RECLAIM_RESULT_DISCARDED;
   }
   arguments->PagingFenceValue = UINT64_C(13);
   ++reclaim_calls;
   return S_OK;
}
#endif

static HRESULT APIENTRY
reclaim_allocations(D3D12DDI_HRTDEVICE runtime_device,
                    D3D12DDI_HRTPAGINGQUEUE runtime_paging_queue,
                    D3D12DDICB_RECLAIMALLOCATIONS2 *arguments)
{
   if (runtime_device.handle != (HANDLE)(uintptr_t)0x6000 ||
       runtime_paging_queue.handle != TEST_RT_PAGING_QUEUE || !arguments ||
       !arguments->NumAllocations || !arguments->HandleList ||
       !arguments->pDiscarded)
      return E_INVALIDARG;
   for (UINT index = 0; index < arguments->NumAllocations; ++index) {
      if (arguments->HandleList[index] != TEST_ALLOCATION)
         return E_INVALIDARG;
      arguments->pDiscarded[index] = TRUE;
   }
   arguments->PagingFenceValue = UINT64_C(13);
   ++reclaim_calls;
   return E_PENDING;
}

static void
require_pointer_slots(const void *table, size_t count, size_t unused_a,
                      size_t unused_b, const char *message)
{
   const void *const *slots = table;
   size_t index;

   for (index = 0; index < count; ++index) {
      const int unused = index == unused_a || index == unused_b;
      if ((!unused && slots[index] == NULL) || (unused && slots[index] != NULL)) {
         fprintf(stderr, "FAIL: %s slot %zu\n", message, index);
         ExitProcess(1);
      }
   }
}

static void
require_result(int condition, const char *message)
{
   if (!condition) {
      fprintf(stderr, "FAIL: %s\n", message);
      ExitProcess(1);
   }
}

static void
fail_load_library(const wchar_t *path, DWORD error)
{
   wchar_t detail[512] = {0};
   const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
      error, 0, detail, ARRAYSIZE(detail), NULL);

   if (length) {
      fwprintf(stderr,
               L"FAIL: LoadLibraryW(\"%ls\") error %lu (0x%08lx): %ls\n",
               path, (unsigned long)error, (unsigned long)error, detail);
   } else {
      fwprintf(stderr,
               L"FAIL: LoadLibraryW(\"%ls\") error %lu (0x%08lx); "
               L"FormatMessageW failed with %lu\n",
               path, (unsigned long)error, (unsigned long)error,
               (unsigned long)GetLastError());
   }
   ExitProcess(1);
}

int
wmain(int argc, wchar_t **argv)
{
   const int load_only = argc > 1 && wcscmp(argv[1], L"--load-only") == 0;
   const wchar_t *dll_path =
      load_only ? (argc > 2 ? argv[2] : NULL)
                : (argc > 1 ? argv[1] : L"d3d12agx_umd.dll");
   const struct agxw_adapter_identity identity = {
      .magic = AGXW_ADAPTER_IDENTITY_MAGIC,
      .version = AGXW_ADAPTER_IDENTITY_VERSION,
      .struct_size = sizeof(struct agxw_adapter_identity),
      .chip_id = UINT32_C(0x6020),
      .gpu_generation = 14,
      .gpu_variant = 'S',
      .gpu_revision = UINT32_C(0x10),
      .contract_version = AGXW_CONTRACT_VERSION,
      .params_blob_size = AGXW_PARAMS_GLOBAL_SIZE,
   };
   const D3DDDI_ADAPTERCALLBACKS callbacks = {
      .pfnQueryAdapterInfoCb = query_adapter_info,
   };
   D3D12DDI_ADAPTERFUNCS functions;
   D3D12DDIARG_OPENADAPTER open_arguments;
   PFND3D12DDI_OPENADAPTER open_adapter;
   HMODULE module;
   FARPROC symbol;
   HRESULT result;

   if (!dll_path) {
      fputs("usage: d12_umd_ddiprobe.exe [--load-only] [umd.dll]\n",
            stderr);
      return 2;
   }
   module = LoadLibraryW(dll_path);
   if (!module)
      fail_load_library(dll_path, GetLastError());
   if (load_only)
      fputws(L"d12_umd_ddiprobe: LoadLibraryW PASS\n", stderr);
   symbol = GetProcAddress(module, "OpenAdapter12");
   require_result(symbol != NULL, "GetProcAddress(OpenAdapter12)");
   if (load_only) {
      fputs("d12_umd_ddiprobe: OpenAdapter12 export PASS\n", stderr);
      FreeLibrary(module);
      fputs("d12_umd_ddiprobe: FreeLibrary PASS\n", stderr);
      puts("d12_umd_ddiprobe: CLEAN LOAD PASS");
      return 0;
   }
   memcpy(&open_adapter, &symbol, sizeof(open_adapter));

   memset(&functions, 0, sizeof(functions));
   open_arguments = (D3D12DDIARG_OPENADAPTER){
      .hRTAdapter = {.handle = (void *)&identity},
      .pAdapterCallbacks = &callbacks,
      .pAdapterFuncs = &functions,
   };
   result = open_adapter(&open_arguments);
   require_result(result == S_OK, "OpenAdapter12 result");
   require_result(open_arguments.hAdapter.pDrvPrivate != NULL,
                  "OpenAdapter12 driver handle");

   require_result(functions.pfnCalcPrivateDeviceSize != NULL,
                  "CalcPrivateDeviceSize slot");
   require_result(functions.pfnCreateDevice != NULL, "CreateDevice slot");
   require_result(functions.pfnCloseAdapter != NULL, "CloseAdapter slot");
   require_result(functions.pfnGetSupportedVersions != NULL,
                  "GetSupportedVersions slot");
   require_result(functions.pfnGetCaps != NULL, "GetCaps slot");
   require_result(functions.pfnGetOptionalDDITables != NULL,
                  "GetOptionalDDITables slot");
   require_result(functions.pfnFillDDITable != NULL, "FillDDITable slot");
   require_result(functions.pfnDestroyDevice != NULL, "DestroyDevice slot");

   {
      D3D12DDI_TEXTURE_LAYOUT_CAPS_0001 data;
      D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_TEXTURE_LAYOUT,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == S_OK &&
                        data.DeviceDependentLayoutCount == 0 &&
                        data.DeviceDependentSwizzleCount == 0 &&
                        !data.Supports64KStandardSwizzle &&
                        !data.SupportsRowMajorTexture,
                     "conservative texture-layout caps");
      query.pInfo = &data.DeviceDependentLayoutCount;
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == E_INVALIDARG,
                     "reject device-layout query with zero layouts");
      query.pInfo = NULL;
      --query.DataSize;
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == E_INVALIDARG,
                     "reject wrong texture-layout cap width");

      {
         D3D12DDI_TEXTURE_LAYOUT_CAPS legacy_data;
         const D3D12DDIARG_GETCAPS legacy_query = {
            .Type = D3D12DDICAPS_TYPE_TEXTURE_LAYOUT,
            .pData = &legacy_data,
            .DataSize = sizeof(legacy_data),
         };
         result = functions.pfnGetCaps(open_arguments.hAdapter,
                                       &legacy_query);
         require_result(result == S_OK &&
                           legacy_data.DeviceDependentLayoutCount == 0 &&
                           legacy_data.DeviceDependentSwizzleCount == 0 &&
                           !legacy_data.Supports64KStandardSwizzle,
                        "legacy texture-layout cap width");
      }
   }

   {
      UINT32 count = 0;
      UINT64 versions[3] = {0};
      const UINT32 expected_count = D12_UMD_TARGETS_WDDM21 ? 3u : 1u;
      result = functions.pfnGetSupportedVersions(open_arguments.hAdapter,
                                                  &count, NULL);
      require_result(result == S_OK && count == expected_count,
                     "supported-version count query");

      count = 0;
      result = functions.pfnGetSupportedVersions(open_arguments.hAdapter,
                                                  &count, versions);
      require_result(result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER),
                     "supported-version short buffer");

      count = ARRAYSIZE(versions);
      result = functions.pfnGetSupportedVersions(open_arguments.hAdapter,
                                                  &count, versions);
      require_result(result == S_OK && count == expected_count &&
#if D12_UMD_TARGETS_WDDM21
                        versions[0] == D3D12DDI_SUPPORTED_0011 &&
                        versions[1] == D3D12DDI_SUPPORTED_0010 &&
                        versions[2] == D3D12DDI_SUPPORTED_0003,
#else
                        versions[0] == D3D12DDI_SUPPORTED_0003,
#endif
                     "supported-version payload");
   }

   {
      UINT node = 0;
      D3D12DDI_MEMORY_ARCHITECTURE_CAPS data;
      const D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE,
         .pInfo = &node,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == S_OK && data.UMA && data.IOCoherent &&
                        data.CacheCoherent,
                     "memory architecture caps");
   }

   {
      D3D12DDI_ARCHITECTURE_INFO_DATA data;
      const D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_ARCHITECTURE_INFO,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == S_OK && data.TileBasedDeferredRenderer,
                     "tile architecture cap");
   }

   {
      D3D12DDI_D3D12_OPTIONS_DATA_0003 data;
      const D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_D3D12_OPTIONS,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == S_OK && data.ResourceBindingTier == 1 &&
                        data.TiledResourcesTier == 1 &&
                        data.ResourceHeapTier == 1,
                     "D3D12 option caps");
   }

   {
      D3D12DDI_3DPIPELINELEVEL data = 0;
      const D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_3DPIPELINESUPPORT,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == S_OK &&
                        data == D3D12DDI_3DPIPELINELEVEL_11_0,
                     "3D pipeline cap");
   }

   {
      UINT count = 0;
#if D12_UMD_TARGETS_WDDM21
      D3D12DDI_SHADER_MODEL models[2] = {0};
#endif
      D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 data = {
         .pNumShaderModelsSupported = &count,
      };
      D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_0011_SHADER_MODELS,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
#if D12_UMD_TARGETS_WDDM21
      require_result(result == S_OK && count == 2,
                     "shader-model count query");
      data.pShaderModelsSupported = models;
      count = ARRAYSIZE(models);
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(
         result == S_OK && count == 2 &&
            models[0] == D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011 &&
            models[1] == D3D12DDI_SHADER_MODEL_5_1_RELEASE_0011,
         "shader-model payload");
#else
      require_result(result == E_NOTIMPL,
                     "WDDM 2.0 does not expose DXIL shader-model caps");
#endif
   }

   {
      D3D12DDIARG_CALCPRIVATEDEVICESIZE size_arguments = {
         .Interface = D3D12DDI_INTERFACE_VERSION,
         .Version = D3D12DDI_BUILD_VERSION,
         .Flags = D3D12DDI_CREATE_DEVICE_FLAG_NONE,
      };
      D3D12DDI_DEVICE_FUNCS_CORE_0003 device_table;
      D3D12DDI_DEVICE_FUNCS_CORE_0010 release1_table;
      D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 command_list_table;
      D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 command_queue_table;
#if D12_UMD_TARGETS_WDDM21
      DXGI1_5_DDI_BASE_FUNCTIONS dxgi_table;
#else
      DXGI1_4_DDI_BASE_FUNCTIONS dxgi_table;
#endif
      DXGI_DDI_BASE_CALLBACKS dxgi_callbacks = {
         .pfnPresentCb = dxgi_present,
      };
      DXGI_DDI_BASE_ARGS dxgi_arguments = {
         .pDXGIBaseCallbacks = &dxgi_callbacks,
#if D12_UMD_TARGETS_WDDM21
         .pDXGIDDIBaseFunctions6 = &dxgi_table,
#else
         .pDXGIDDIBaseFunctions5 = &dxgi_table,
#endif
      };
      D3DDDI_DEVICECALLBACKS kernel_callbacks;
      D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 core_callbacks;
      D3D12DDIARG_CREATEDEVICE_0003 create_arguments;
      D3D12DDI_HDEVICE driver_device;
      SIZE_T device_size = functions.pfnCalcPrivateDeviceSize(
         open_arguments.hAdapter, &size_arguments);

      require_result(device_size > 0, "private device size");
      result = functions.pfnFillDDITable(
         open_arguments.hAdapter, D3D12DDI_TABLE_TYPE_DEVICE_CORE,
         &device_table, sizeof(device_table), 0, (D3D12DDI_HRTTABLE){0});
      require_result(result == S_OK, "fill device core table");
      require_pointer_slots(&device_table, 89, SIZE_MAX, SIZE_MAX,
                            "device core table");
      result = functions.pfnFillDDITable(
         open_arguments.hAdapter, D3D12DDI_TABLE_TYPE_DEVICE_CORE,
         &release1_table, sizeof(release1_table), 0,
         (D3D12DDI_HRTTABLE){0});
#if D12_UMD_TARGETS_WDDM21
      require_result(result == S_OK, "fill Release 1 device core table");
      require_pointer_slots(&release1_table, 91, SIZE_MAX, SIZE_MAX,
                            "Release 1 device core table");
#else
      require_result(result == E_INVALIDARG,
                     "WDDM 2.0 rejects the DXIL Release 1 table");
#endif

      result = functions.pfnFillDDITable(
         open_arguments.hAdapter, D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D,
         &command_list_table, sizeof(command_list_table), 0,
         (D3D12DDI_HRTTABLE){.handle = TEST_RT_COMMAND_LIST_TABLE});
      require_result(result == S_OK, "fill command-list table");
      require_pointer_slots(&command_list_table, 51, SIZE_MAX, SIZE_MAX,
                            "command-list table");
      result = functions.pfnFillDDITable(
         open_arguments.hAdapter, D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D,
         &command_list_table, sizeof(command_list_table), 0,
         (D3D12DDI_HRTTABLE){0});
      require_result(result == E_INVALIDARG,
                     "reject command-list table without runtime handle");

      result = functions.pfnFillDDITable(
         open_arguments.hAdapter, D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D,
         &command_queue_table, sizeof(command_queue_table), 0,
         (D3D12DDI_HRTTABLE){0});
      require_result(result == S_OK, "fill command-queue table");
      require_pointer_slots(&command_queue_table, 7, 1, 2,
                            "command-queue table");
      result = functions.pfnFillDDITable(
         open_arguments.hAdapter, D3D12DDI_TABLE_TYPE_DXGI,
         &dxgi_arguments, sizeof(dxgi_arguments), 0,
         (D3D12DDI_HRTTABLE){0});
#if D12_UMD_TARGETS_WDDM21
      require_result(result == S_OK, "fill WDDM 2.1 DXGI table");
      require_pointer_slots(&dxgi_table, 22, 13, 15,
                            "WDDM 2.1 DXGI table");
#else
      require_result(result == S_OK, "fill WDDM 2.0 DXGI table");
      require_pointer_slots(&dxgi_table, 21, 13, 15,
                            "WDDM 2.0 DXGI table");
#endif
      result = functions.pfnFillDDITable(
         open_arguments.hAdapter, D3D12DDI_TABLE_TYPE_DXGI,
         &dxgi_arguments, sizeof(dxgi_arguments) - 1, 0,
         (D3D12DDI_HRTTABLE){0});
      require_result(result == E_INVALIDARG,
                     "reject wrong DXGI argument width");
      result = functions.pfnFillDDITable(
         open_arguments.hAdapter, D3D12DDI_TABLE_TYPE_DEVICE_CORE,
         &device_table, sizeof(device_table) - 1, 0,
         (D3D12DDI_HRTTABLE){0});
      require_result(result == E_INVALIDARG, "reject wrong table width");

      memset(&kernel_callbacks, 0, sizeof(kernel_callbacks));
      kernel_callbacks.pfnAllocateCb = unused_allocate;
      kernel_callbacks.pfnDeallocate2Cb = unused_deallocate;
      kernel_callbacks.pfnSetPriorityCb = set_priority;
      kernel_callbacks.pfnQueryResidencyCb = query_residency;
      kernel_callbacks.pfnOfferAllocationsCb = dxgi_offer_allocations;
      kernel_callbacks.pfnReclaimAllocationsCb = dxgi_reclaim_allocations;
#if D12_UMD_TARGETS_WDDM21
      kernel_callbacks.pfnOfferAllocations2Cb = dxgi_offer_allocations2;
      kernel_callbacks.pfnReclaimAllocations3Cb =
         dxgi_reclaim_allocations3;
#endif
      kernel_callbacks.pfnReserveGpuVirtualAddressCb = reserve_gpu_va;
      kernel_callbacks.pfnFreeGpuVirtualAddressCb = free_gpu_va;
      kernel_callbacks.pfnWaitForSynchronizationObjectFromCpuCb = wait_paging;
      kernel_callbacks.pfnLock2Cb = lock_allocation;
      kernel_callbacks.pfnUnlock2Cb = unlock_allocation;
      kernel_callbacks.pfnInvalidateCacheCb = invalidate_allocation;
      memset(&core_callbacks, 0, sizeof(core_callbacks));
      core_callbacks.pfnSetErrorCb = set_error;
      core_callbacks.pfnSetCommandListDDITableCb =
         set_command_list_ddi_table;
      core_callbacks.pfnCreateContextVirtualCb = create_context;
      core_callbacks.pfnDestroyContextCb = destroy_context;
      core_callbacks.pfnCreatePagingQueueCb = create_paging;
      core_callbacks.pfnDestroyPagingQueueCb = destroy_paging;
      core_callbacks.pfnMakeResidentCb = make_resident;
      core_callbacks.pfnEvictCb = evict;
      core_callbacks.pfnOfferAllocationsCb = offer_allocations;
      core_callbacks.pfnReclaimAllocations2Cb = reclaim_allocations;
      driver_device.pDrvPrivate =
         HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, device_size);
      require_result(driver_device.pDrvPrivate != NULL,
                     "allocate private device storage");
      create_arguments = (D3D12DDIARG_CREATEDEVICE_0003){
         .hRTDevice = {.handle = (void *)(uintptr_t)0x6000},
         .Interface = D3D12DDI_INTERFACE_VERSION,
         .Version = D3D12DDI_BUILD_VERSION,
         .pKTCallbacks = &kernel_callbacks,
         .hDrvDevice = driver_device,
         .p12UMCallbacks = &core_callbacks,
      };
      result = functions.pfnCreateDevice(open_arguments.hAdapter,
                                         &create_arguments);
      require_result(result == DXGI_STATUS_NO_REDIRECTION,
                     "CreateDevice explicit-present result");

#if D12_UMD_TARGETS_WDDM21
      {
         const D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 library_arguments = {
            0
         };
         D3D12DDI_HPIPELINELIBRARY library_handle;
         SIZE_T library_size =
            release1_table.pfnCalcPrivatePipelineLibrarySize(
               driver_device, &library_arguments);
         void *serialized;
         SIZE_T serialized_size;

         require_result(library_size > 0,
                        "Release 1 private pipeline-library size");
         library_handle.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, library_size);
         require_result(library_handle.pDrvPrivate != NULL,
                        "allocate pipeline-library private storage");
         result = release1_table.pfnCreatePipelineLibrary(
            driver_device, &library_arguments, library_handle);
         require_result(result == S_OK, "create empty pipeline library");
         serialized_size = release1_table.pfnCalcSerializedLibrarySize(
            driver_device, library_handle);
         require_result(serialized_size == 40,
                        "empty pipeline-library serialized size");
         serialized = HeapAlloc(GetProcessHeap(), 0, serialized_size);
         require_result(serialized != NULL,
                        "allocate serialized pipeline library");
         result = release1_table.pfnSerializeLibrary(
            driver_device, library_handle, serialized);
         require_result(result == S_OK, "serialize empty pipeline library");
         HeapFree(GetProcessHeap(), 0, serialized);
         release1_table.pfnDestroyPipelineLibrary(driver_device,
                                                   library_handle);
         HeapFree(GetProcessHeap(), 0, library_handle.pDrvPrivate);
      }
#endif

      {
         UINT node_map[2] = {UINT32_MAX, UINT32_MAX};
         SIZE_T shader_comment_size = 0;

         require_result(device_table.pfnGetImplicitPhysicalAdapterMask(
                           driver_device) == 1,
                        "single implicit physical-adapter mask");
         device_table.pfnQueryNodeMap(driver_device, ARRAYSIZE(node_map),
                                      node_map);
         require_result(node_map[0] == 0 &&
                           node_map[1] == D3D12DDI_NODE_MAP_HIDE_NODE,
                        "single-node physical-adapter map");
         require_result(
            device_table.pfnRetrieveShaderComment(
               driver_device, (D3D12DDI_HPIPELINESTATE){0}, NULL,
               &shader_comment_size) == E_INVALIDARG,
            "RetrieveShaderComment rejects a null pipeline state");
         require_result(
            device_table.pfnRetrieveShaderComment(
               driver_device, (D3D12DDI_HPIPELINESTATE){0}, NULL, NULL) ==
               E_INVALIDARG,
            "RetrieveShaderComment requires the size pointer");
      }

      {
         UINT format_support = 0;
         UINT quality_levels = 0;

         device_table.pfnCheckFormatSupport(
            driver_device, DXGI_FORMAT_R8G8B8A8_UNORM, &format_support);
         require_result(
            (format_support & D3D12DDI_FORMAT_SUPPORT_SHADER_SAMPLE) &&
               (format_support & D3D12DDI_FORMAT_SUPPORT_RENDERTARGET) &&
               (format_support & D3D12DDI_FORMAT_SUPPORT_BLENDABLE) &&
               (format_support & D3D12DDI_FORMAT_SUPPORT_TILED) &&
               (format_support & D3D12DDI_FORMAT_SUPPORT_DISPLAY),
            "native RGBA8 DDI format support");
         format_support = UINT32_MAX;
         device_table.pfnCheckFormatSupport(
            driver_device, (DXGI_FORMAT)UINT32_MAX, &format_support);
         require_result(format_support == 0,
                        "unknown DDI format fails closed");

         device_table.pfnCheckMultisampleQualityLevels(
            driver_device, DXGI_FORMAT_R8G8B8A8_UNORM, 4,
            D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAG_NONE,
            &quality_levels);
         require_result(quality_levels == 1, "native RGBA8 4x MSAA");
         device_table.pfnCheckMultisampleQualityLevels(
            driver_device, DXGI_FORMAT_R8G8B8A8_UNORM, 4,
            D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAG_TILED_RESOURCE,
            &quality_levels);
         require_result(quality_levels == 0,
                        "sparse MSAA is not advertised");
         device_table.pfnCheckMultisampleQualityLevels(
            driver_device, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
            D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAG_TILED_RESOURCE,
            &quality_levels);
         require_result(quality_levels == 1,
                        "single-sample sparse RGBA8 is advertised");
      }

      {
         const D3D12DDI_ROOT_SIGNATURE root_desc = {0};
         D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 root_arguments = {
            .pRootSignature = &root_desc,
            .NodeMask = 0,
         };
         D3D12DDI_HROOTSIGNATURE driver_root_signature;
         const SIZE_T root_signature_size =
            device_table.pfnCalcPrivateRootSignatureSize(
               driver_device, &root_arguments);

         require_result(root_signature_size > 0,
                        "private root-signature size");
         driver_root_signature.pDrvPrivate =
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                      root_signature_size);
         require_result(driver_root_signature.pDrvPrivate != NULL,
                        "allocate root-signature private storage");
         result = device_table.pfnCreateRootSignature(
            driver_device, &root_arguments, driver_root_signature);
         require_result(result == E_NOTIMPL,
                        "standalone root signature has no renderer");
         HeapFree(GetProcessHeap(), 0,
                  driver_root_signature.pDrvPrivate);

         {
            const D3D12DDI_ROOT_SIGNATURE invalid_desc = {
               .NumParameters = 1,
            };
            root_arguments.pRootSignature = &invalid_desc;
            require_result(device_table.pfnCalcPrivateRootSignatureSize(
                              driver_device, &root_arguments) == 0,
                           "reject missing root parameters");
         }
      }

      {
         const UINT compute_tokens[] = {
            (UINT32_C(5) << 16) | (UINT32_C(5) << 4),
            3,
            UINT32_C(1) << 24,
         };
         D3D12DDI_HSHADER driver_shader;
         const SIZE_T shader_size = device_table.pfnCalcPrivateShaderSize(
            driver_device, compute_tokens, (D3D12DDI_HROOTSIGNATURE){0},
            NULL);

         require_result(shader_size > 0, "private shader size");
         driver_shader.pDrvPrivate =
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, shader_size);
         require_result(driver_shader.pDrvPrivate != NULL,
                        "allocate shader private storage");
         device_table.pfnCreateComputeShader(
            driver_device, compute_tokens, (D3D12DDI_HROOTSIGNATURE){0},
            driver_shader, D3D12DDI_CREATE_SHADER_FLAG_NONE);
         device_table.pfnDestroyShader(driver_device, driver_shader);
         HeapFree(GetProcessHeap(), 0, driver_shader.pDrvPrivate);

         {
            const UINT geometry_tokens[] = {
               (UINT32_C(2) << 16) | (UINT32_C(5) << 4),
               3,
               UINT32_C(1) << 24,
            };
            D3D12DDIARG_SIGNATURE_ENTRY output_signature = {
               .SystemValue = D3D10_SB_NAME_UNDEFINED,
               .Register = 0,
               .Mask = 0xf,
               .RegisterComponentType =
                  D3D10_SB_REGISTER_COMPONENT_FLOAT32,
            };
            const D3D12DDIARG_STAGE_IO_SIGNATURES signatures = {
               .pOutputSignatureDeprecated = &output_signature,
               .NumOutputSignatureEntries = 1,
            };
            const D3D12DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY declarations[] = {
               {
                  .Stream = 0,
                  .OutputSlot = 0,
                  .RegisterIndex = UINT32_MAX,
                  .RegisterMask = 0xf,
               },
               {
                  .Stream = 0,
                  .OutputSlot = 0,
                  .RegisterIndex = 0,
                  .RegisterMask = 0x3,
               },
            };
            const UINT stride = 24;
            D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT
               stream_output_arguments = {
                  .pShaderCode = geometry_tokens,
                  .pOutputStreamDecl = declarations,
                  .NumEntries = ARRAYSIZE(declarations),
                  .BufferStridesInBytes = &stride,
                  .NumStrides = 1,
                  .RasterizedStream = 0,
               };
            D3D12DDI_HSHADER stream_output_shader;
            const SIZE_T stream_output_shader_size =
               device_table.pfnCalcPrivateGeometryShaderWithStreamOutput(
                  driver_device, &stream_output_arguments, &signatures);

            require_result(stream_output_shader_size > 0,
                           "private geometry stream-output shader size");
            stream_output_shader.pDrvPrivate = HeapAlloc(
               GetProcessHeap(), HEAP_ZERO_MEMORY,
               stream_output_shader_size);
            require_result(stream_output_shader.pDrvPrivate != NULL,
                           "allocate stream-output shader storage");
            device_table.pfnCreateGeometryShaderWithStreamOutput(
               driver_device, &stream_output_arguments,
               stream_output_shader, &signatures,
               D3D12DDI_CREATE_SHADER_FLAG_NONE);
            device_table.pfnDestroyShader(driver_device,
                                          stream_output_shader);
            HeapFree(GetProcessHeap(), 0,
                     stream_output_shader.pDrvPrivate);

            stream_output_arguments.pShaderCode = NULL;
            require_result(
               device_table.pfnCalcPrivateGeometryShaderWithStreamOutput(
                  driver_device, &stream_output_arguments,
                  &signatures) > 0,
               "stream-output passthrough shader size");
            stream_output_arguments.NumStrides = 0;
            require_result(
               device_table.pfnCalcPrivateGeometryShaderWithStreamOutput(
                  driver_device, &stream_output_arguments,
                  &signatures) == 0,
               "reject stream-output declaration without stride");
         }

         {
            const UINT invalid_tokens[] = {compute_tokens[0], 1};
            require_result(device_table.pfnCalcPrivateShaderSize(
                              driver_device, invalid_tokens,
                              (D3D12DDI_HROOTSIGNATURE){0}, NULL) == 0,
                           "reject truncated tokenized shader");
         }
      }

      {
         D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 descriptor_arguments = {
            .Type = D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            .NumDescriptors = 64,
            .Flags = D3D12DDI_DESCRIPTOR_HEAP_FLAG_CPU_VISIBLE |
                     D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
            .NodeMask = 0,
         };
         D3D12DDI_HDESCRIPTORHEAP descriptor_heap;
         const SIZE_T descriptor_heap_size =
            device_table.pfnCalcPrivateDescriptorHeapSize(
               driver_device, &descriptor_arguments);

         require_result(descriptor_heap_size > 0,
                        "private descriptor-heap size");
         require_result(device_table.pfnGetDescriptorSizeInBytes(
                           driver_device,
                           D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) ==
                           D12_FRONTEND_DESCRIPTOR_SIZE,
                        "native descriptor stride");
         descriptor_heap.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, descriptor_heap_size);
         require_result(descriptor_heap.pDrvPrivate != NULL,
                        "allocate descriptor-heap private storage");
         result = device_table.pfnCreateDescriptorHeap(
            driver_device, &descriptor_arguments, descriptor_heap);
         require_result(result == E_NOTIMPL,
                        "standalone descriptor heap has no renderer");
         HeapFree(GetProcessHeap(), 0, descriptor_heap.pDrvPrivate);

         descriptor_arguments.Type = D3D12DDI_DESCRIPTOR_HEAP_TYPE_RTV;
         descriptor_arguments.Flags =
            D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
         require_result(device_table.pfnCalcPrivateDescriptorHeapSize(
                           driver_device, &descriptor_arguments) == 0,
                        "reject shader-visible RTV heap");
      }

      {
         D3D12DDIARG_CREATE_QUERY_HEAP_0001 query_arguments = {
            .Type = D3D12DDI_QUERY_HEAP_TYPE_OCCLUSION,
            .Count = 64,
            .NodeMask = 1,
         };
         D3D12DDI_HQUERYHEAP query_heap;
         const SIZE_T query_heap_size =
            device_table.pfnCalcPrivateQueryHeapSize(
               driver_device, &query_arguments);

         require_result(query_heap_size > 0, "private query-heap size");
         query_heap.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, query_heap_size);
         require_result(query_heap.pDrvPrivate != NULL,
                        "allocate query-heap private storage");
         result = device_table.pfnCreateQueryHeap(
            driver_device, &query_arguments, query_heap);
         require_result(result == E_NOTIMPL,
                        "standalone query heap has no renderer");
         HeapFree(GetProcessHeap(), 0, query_heap.pDrvPrivate);

         query_arguments.Type =
            D3D12DDI_QUERY_HEAP_TYPE_0020_VIDEO_DECODE_STATISTICS;
         require_result(device_table.pfnCalcPrivateQueryHeapSize(
                           driver_device, &query_arguments) == 0,
                        "reject unsupported query-heap type");
      }

      {
         const D3D12DDI_INDIRECT_ARGUMENT_DESC indirect_argument = {
            .Type = D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW,
         };
         D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 signature_arguments = {
            .ByteStride = sizeof(D3D12DDI_DRAW_ARGUMENTS),
            .NumArgumentDescs = 1,
            .pArgumentDescs = &indirect_argument,
            .NodeMask = 1,
         };
         D3D12DDI_HCOMMANDSIGNATURE command_signature;
         const SIZE_T command_signature_size =
            device_table.pfnCalcPrivateCommandSignatureSize(
               driver_device, &signature_arguments);

         require_result(command_signature_size > 0,
                        "private command-signature size");
         command_signature.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, command_signature_size);
         require_result(command_signature.pDrvPrivate != NULL,
                        "allocate command-signature private storage");
         result = device_table.pfnCreateCommandSignature(
            driver_device, &signature_arguments, command_signature);
         require_result(result == E_NOTIMPL,
                        "standalone command signature has no renderer");
         HeapFree(GetProcessHeap(), 0, command_signature.pDrvPrivate);

         signature_arguments.ByteStride = 1;
         require_result(device_table.pfnCalcPrivateCommandSignatureSize(
                           driver_device, &signature_arguments) == 0,
                        "reject undersized indirect stride");

         {
            const struct d12_frontend_root_parameter parameters[] = {
               {
                  .parameter_type =
                     D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
                  .constants = {
                     .num_32bit_values = 4,
                  },
               },
               {.parameter_type = D3D12DDI_ROOT_PARAMETER_TYPE_CBV},
               {.parameter_type = D3D12DDI_ROOT_PARAMETER_TYPE_SRV},
               {.parameter_type = D3D12DDI_ROOT_PARAMETER_TYPE_UAV},
            };
            struct d12_umd_root_signature root_signature = {
               .signature = D12_UMD_ROOT_SIGNATURE_SIGNATURE,
               .device = driver_device.pDrvPrivate,
               .backend = (void *)(uintptr_t)1,
               .desc = {
                  .num_parameters = ARRAYSIZE(parameters),
                  .parameters = parameters,
               },
            };
            D3D12DDI_INDIRECT_ARGUMENT_DESC mutable_arguments[] = {
               {
                  .Type = D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT,
                  .Constant = {
                     .RootParameterIndex = 0,
                     .DestOffsetIn32BitValues = 1,
                     .Num32BitValuesToSet = 2,
                  },
               },
               {
                  .Type =
                     D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW,
                  .ConstantBufferView = {.RootParameterIndex = 1},
               },
               {
                  .Type =
                     D3D12DDI_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW,
                  .ShaderResourceView = {.RootParameterIndex = 2},
               },
               {
                  .Type =
                     D3D12DDI_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW,
                  .UnorderedAccessView = {.RootParameterIndex = 3},
               },
               {.Type = D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW},
            };
            D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 mutable_signature = {
               .ByteStride = 48,
               .NumArgumentDescs = ARRAYSIZE(mutable_arguments),
               .pArgumentDescs = mutable_arguments,
               .hRootSignature = {.pDrvPrivate = &root_signature},
               .NodeMask = 1,
            };

            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) > 0,
               "root constant and CBV/SRV/UAV indirect signature");
            mutable_arguments[0].Constant.Num32BitValuesToSet = 4;
            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) == 0,
               "reject indirect constants outside root parameter");
            mutable_arguments[0].Constant.Num32BitValuesToSet = 2;
            mutable_signature.hRootSignature.pDrvPrivate = NULL;
            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) == 0,
               "require root signature for indirect root mutation");
            mutable_signature.hRootSignature.pDrvPrivate = &root_signature;
            mutable_arguments[1].ConstantBufferView.RootParameterIndex = 2;
            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) == 0,
               "reject indirect root descriptor type mismatch");
            mutable_arguments[1].ConstantBufferView.RootParameterIndex = 1;

            {
               D3D12DDI_INDIRECT_ARGUMENT_DESC first =
                  mutable_arguments[0];
               D3D12DDI_INDIRECT_ARGUMENT_DESC second =
                  mutable_arguments[1];
               mutable_arguments[0] = second;
               mutable_arguments[1] = first;
               require_result(
                  device_table.pfnCalcPrivateCommandSignatureSize(
                     driver_device, &mutable_signature) == 0,
                  "require sorted indirect root parameters");
               mutable_arguments[0] = first;
               mutable_arguments[1] = second;
            }

            mutable_arguments[1].Type =
               D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            mutable_arguments[1].Constant.RootParameterIndex = 0;
            mutable_arguments[1].Constant.DestOffsetIn32BitValues = 2;
            mutable_arguments[1].Constant.Num32BitValuesToSet = 1;
            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) == 0,
               "reject overlapping indirect root constants");
            mutable_arguments[1].Constant.DestOffsetIn32BitValues = 3;
            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) > 0,
               "accept sorted non-overlapping indirect root constants");

            mutable_arguments[1].Type =
               D3D12DDI_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
            mutable_arguments[1].VertexBuffer.Slot = 7;
            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) > 0,
               "indirect vertex-buffer view signature");
            mutable_arguments[2].Type =
               D3D12DDI_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
            mutable_arguments[2].VertexBuffer.Slot = 7;
            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) == 0,
               "reject duplicate indirect vertex-buffer slot");
            mutable_arguments[2].Type =
               D3D12DDI_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
            mutable_arguments[2].ShaderResourceView.RootParameterIndex = 2;
            mutable_arguments[1].VertexBuffer.Slot = 16;
            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) == 0,
               "reject out-of-range indirect vertex-buffer slot");
            mutable_arguments[1].Type =
               D3D12DDI_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) == 0,
               "require indexed draw for indirect index-buffer view");
            mutable_arguments[4].Type =
               D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) > 0,
               "indirect index-buffer view signature");
            mutable_arguments[4].Type =
               D3D12DDI_INDIRECT_ARGUMENT_TYPE_DISPATCH;
            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) == 0,
               "reject IA state in indirect dispatch signature");
         }

         {
            const D3D12DDI_INDIRECT_ARGUMENT_DESC arguments[] = {
               {
                  .Type =
                     D3D12DDI_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW,
                  .VertexBuffer = {.Slot = 7},
               },
               {.Type = D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW},
            };
            const D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 arguments_desc = {
               .ByteStride = sizeof(D3D12DDI_VERTEX_BUFFER_VIEW) +
                             sizeof(D3D12DDI_DRAW_ARGUMENTS),
               .NumArgumentDescs = ARRAYSIZE(arguments),
               .pArgumentDescs = arguments,
               .NodeMask = 1,
            };
            struct d12_umd_device *private_device =
               driver_device.pDrvPrivate;
            struct d12_device *saved_backend = private_device->backend;
            const struct d12_umd_renderer_ops *saved_renderer_ops =
               private_device->renderer_ops;
            D3D12DDI_HCOMMANDSIGNATURE translated_signature;
            SIZE_T translated_size =
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &arguments_desc);

            translated_signature.pDrvPrivate = HeapAlloc(
               GetProcessHeap(), HEAP_ZERO_MEMORY, translated_size);
            require_result(translated_signature.pDrvPrivate != NULL,
                           "allocate translated command signature");
            signature_create_calls = 0;
            signature_destroy_calls = 0;
            signature_vertex_slot = UINT32_MAX;
            private_device->backend =
               (struct d12_device *)TEST_BLT_DEVICE;
            private_device->renderer_ops = &test_signature_renderer_ops;
            result = device_table.pfnCreateCommandSignature(
               driver_device, &arguments_desc, translated_signature);
            require_result(result == S_OK && signature_create_calls == 1 &&
                              signature_vertex_slot == 7,
                           "preserve DDI indirect vertex-buffer slot");
            device_table.pfnDestroyCommandSignature(
               driver_device, translated_signature);
            require_result(signature_destroy_calls == 1,
                           "destroy translated command signature");
            private_device->renderer_ops = saved_renderer_ops;
            private_device->backend = saved_backend;
            HeapFree(GetProcessHeap(), 0,
                     translated_signature.pDrvPrivate);
         }
      }

      {
         static const uint8_t shader[] = {
            0x44, 0x58, 0x42, 0x43, 1, 2, 3, 4,
         };
         const struct d12_frontend_compute_pipeline_desc pipeline_desc = {
            .shader = {.data = shader, .size = sizeof(shader)},
         };
         void *serialized = NULL;
         size_t serialized_size = 0;
         SIZE_T private_size;
         void *pipeline_storage;
         SIZE_T returned_size = 99;
         const UINT *returned_blob = (const UINT *)(uintptr_t)1;

         require_result(d12_umd_pipeline_serialize_compute(
                           &pipeline_desc, NULL, &serialized,
                           &serialized_size),
                        "create pipeline serialization fixture");
         private_size =
            device_table.pfnCalcPrivateDeserializedObjectSize(
               driver_device, D3D12DDI_HT_PIPELINE_STATE, serialized_size,
               (const UINT *)serialized);
         require_result(private_size > 0,
                        "private deserialized pipeline-state size");
         require_result(
            device_table.pfnCalcPrivateDeserializedObjectSize(
               driver_device, D3D12DDI_HT_COMMAND_LIST, serialized_size,
               (const UINT *)serialized) == 0,
            "reject unsupported serialized-object type");

         pipeline_storage = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                      private_size);
         require_result(pipeline_storage != NULL,
                        "allocate deserialized pipeline-state storage");
         result = device_table.pfnCreateDeserializedObject(
            driver_device, D3D12DDI_HT_PIPELINE_STATE, serialized_size,
            (const UINT *)serialized, pipeline_storage,
            (void *)(uintptr_t)0x9000);
         require_result(result == E_NOTIMPL,
                        "standalone deserialization has no renderer");
         HeapFree(GetProcessHeap(), 0, pipeline_storage);

         result = device_table.pfnSerializeObject(
            driver_device, D3D12DDI_HT_PIPELINE_STATE, NULL,
            &returned_size, &returned_blob);
         require_result(result == E_INVALIDARG && returned_size == 0 &&
                           returned_blob == NULL,
                        "reject invalid pipeline serialization handle");
         device_table.pfnDestroyObjectSerialization(driver_device, 0, NULL);

         ((uint8_t *)serialized)[serialized_size - 1] ^= 0x80;
         require_result(
            device_table.pfnCalcPrivateDeserializedObjectSize(
               driver_device, D3D12DDI_HT_PIPELINE_STATE, serialized_size,
               (const UINT *)serialized) == 0,
            "reject corrupted pipeline serialization");
         free(serialized);
      }

      {
         D3D12DDIARG_CREATECOMMANDALLOCATOR allocator_arguments = {
            .Type = D3D12DDI_COMMAND_LIST_TYPE_DIRECT,
            .QueueFlags = D3D12DDI_COMMAND_QUEUE_FLAG_3D,
         };
         D3D12DDI_HCOMMANDALLOCATOR driver_allocator;
         const SIZE_T allocator_size =
            device_table.pfnCalcPrivateCommandAllocatorSize(
               driver_device, &allocator_arguments);

         require_result(allocator_size > 0,
                        "private command-allocator size");
         driver_allocator.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, allocator_size);
         require_result(driver_allocator.pDrvPrivate != NULL,
                        "allocate command-allocator private storage");
         allocator_arguments.hDrvCommandAllocator = driver_allocator;
         result = device_table.pfnCreateCommandAllocator(
            driver_device, &allocator_arguments);
         require_result(result == E_NOTIMPL,
                        "standalone command allocator has no renderer");
         HeapFree(GetProcessHeap(), 0, driver_allocator.pDrvPrivate);

         allocator_arguments.QueueFlags =
            D3D12DDI_COMMAND_QUEUE_FLAG_PAGING;
         require_result(device_table.pfnCalcPrivateCommandAllocatorSize(
                           driver_device, &allocator_arguments) == 0,
                        "reject paging command allocator");
      }

      {
         D3D12DDIARG_CREATECOMMANDQUEUE_0001 queue_arguments = {
            .hRTCommandQueue = {.handle = TEST_RT_QUEUE},
            .QueueFlags = D3D12DDI_COMMAND_QUEUE_FLAG_3D,
            .NodeMask = 0,
         };
         D3D12DDI_HCOMMANDQUEUE driver_queue;
         const SIZE_T queue_size =
            device_table.pfnCalcPrivateCommandQueueSize(driver_device,
                                                        &queue_arguments);
         require_result(queue_size > 0, "private command-queue size");
         driver_queue.pDrvPrivate =
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, queue_size);
         require_result(driver_queue.pDrvPrivate != NULL,
                        "allocate private command-queue storage");
         queue_arguments.hDrvCommandQueue = driver_queue;
         result = device_table.pfnCreateCommandQueue(driver_device,
                                                     &queue_arguments);
         require_result(result == S_OK && context_create_calls == 1 &&
                           paging_create_calls == 1 && reserve_calls == 1,
                        "CreateCommandQueue callback translation");
         set_error_calls = 0;
         set_error_runtime_device_valid = FALSE;
         last_set_error = S_OK;
         command_queue_table.pfnUpdateTileMappings(
            driver_queue, (D3D12DDI_HRESOURCE){0}, 0, NULL, NULL,
            (D3D12DDI_HHEAP){0}, 0, NULL, NULL, NULL,
            D3D12DDI_TILE_MAPPING_FLAG_NONE);
         require_result(set_error_calls == 1 &&
                           set_error_runtime_device_valid &&
                           last_set_error == E_INVALIDARG,
                        "invalid Tier-1 UpdateTileMappings fails closed");
         command_queue_table.pfnCopyTileMappings(
            driver_queue, (D3D12DDI_HRESOURCE){0}, NULL,
            (D3D12DDI_HRESOURCE){0}, NULL, NULL,
            D3D12DDI_TILE_MAPPING_FLAG_NONE);
         require_result(set_error_calls == 2 &&
                           set_error_runtime_device_valid &&
                           last_set_error == E_INVALIDARG,
                        "invalid Tier-1 CopyTileMappings fails closed");
         device_table.pfnDestroyCommandQueue(driver_device, driver_queue);
         require_result(context_destroy_calls == 1 &&
                           paging_destroy_calls == 1 && free_calls == 1,
                        "DestroyCommandQueue callback translation");
         HeapFree(GetProcessHeap(), 0, driver_queue.pDrvPrivate);
      }

      {
         struct d12_umd_device *private_device = driver_device.pDrvPrivate;
         struct d12_device *saved_backend = private_device->backend;
         const struct d12_umd_renderer_ops *saved_renderer_ops =
            private_device->renderer_ops;
         const D3D12DDIARG_CREATERESOURCE_0003 resource_arguments = {
            .ResourceType = D3D12DDI_RT_TEXTURE2D,
            .Width = 1024,
            .Height = 1024,
            .DepthOrArraySize = 1,
            .MipLevels = 5,
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .SampleDesc = {.Count = 1},
            .Layout = D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE,
            .Flags = D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE,
            .InitialResourceState = D3D12DDI_RESOURCE_STATE_COMMON,
         };
         D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes;
         D3D12DDI_HRESOURCE resource_handle;
         struct d12_umd_command_queue tile_queue = {
            .signature = D12_UMD_COMMAND_QUEUE_SIGNATURE,
            .device = private_device,
            .type = D12_UMD_RENDERER_COMMAND_DIRECT,
            .backend = TEST_TILE_QUEUE,
         };
         struct d12_umd_heap tile_heap = {
            .signature = D12_UMD_HEAP_SIGNATURE,
            .device = private_device,
            .backend = TEST_TILE_HEAP,
         };
         D3D12DDI_HCOMMANDQUEUE queue_handle = {
            .pDrvPrivate = &tile_queue,
         };
         D3D12DDI_HHEAP heap_handle = {.pDrvPrivate = &tile_heap};
         const D3D12DDI_TILED_RESOURCE_COORDINATE update_start = {
            .X = 1, .Y = 2,
         };
         const D3D12DDI_TILE_REGION_SIZE update_size = {
            .NumTiles = 2, .UseBox = TRUE, .Width = 2,
            .Height = 1, .Depth = 1,
         };
         const D3D12DDI_TILE_RANGE_FLAGS range_flag =
            D3D12DDI_TILE_RANGE_FLAG_REUSE_SINGLE_TILE;
         const UINT heap_start = 4;
         const UINT range_count = 2;
         const D3D12DDI_TILED_RESOURCE_COORDINATE destination_start = {
            .X = 3, .Subresource = 1,
         };
         const D3D12DDI_TILED_RESOURCE_COORDINATE source_start = {
            .X = 4, .Subresource = 2,
         };
         const D3D12DDI_TILE_REGION_SIZE copy_mapping_size = {
            .NumTiles = 2,
         };
         struct d12_umd_resource buffer_resource = {
            .signature = D12_UMD_RESOURCE_SIGNATURE,
            .device = private_device,
            .backend = TEST_TILE_BUFFER,
            .desc = {.ResourceType = D3D12DDI_RT_BUFFER},
         };
         struct d12_umd_command_list tile_list = {
            .signature = D12_UMD_COMMAND_LIST_SIGNATURE,
            .device = private_device,
            .type = D12_UMD_RENDERER_COMMAND_DIRECT,
            .backend = TEST_TILE_LIST,
         };
         const D3D12DDI_TILED_RESOURCE_COORDINATE copy_start = {
            .X = 5, .Subresource = 3,
         };
         const D3D12DDI_TILE_REGION_SIZE copy_size = {.NumTiles = 1};
         D3D12DDI_HCOMMANDLIST list_handle = {.pDrvPrivate = &tile_list};
         D3D12DDI_HRESOURCE buffer_handle = {
            .pDrvPrivate = &buffer_resource,
         };
         UINT packed_mips = 0;
         UINT packed_tiles = 0;

         private_device->backend = (struct d12_device *)TEST_BLT_DEVICE;
         private_device->renderer_ops = &test_sparse_renderer_ops;
         sparse_create_calls = 0;
         sparse_destroy_calls = 0;
         sparse_update_calls = 0;
         sparse_copy_mapping_calls = 0;
         sparse_copy_tiles_calls = 0;
         sizes = device_table.pfnCalcPrivateHeapAndResourceSizes(
            driver_device, NULL, &resource_arguments);
         require_result(sizes.Heap == 0 && sizes.Resource > 0,
                        "reserved resource has no private heap");
         resource_handle.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Resource);
         require_result(resource_handle.pDrvPrivate != NULL,
                        "allocate reserved resource private storage");
         result = device_table.pfnCreateHeapAndResource(
            driver_device, NULL, (D3D12DDI_HHEAP){0},
            (D3D12DDI_HRTRESOURCE){0}, &resource_arguments, NULL,
            resource_handle);
         require_result(
            result == S_OK && sparse_create_calls == 1 &&
               ((struct d12_umd_resource *)resource_handle.pDrvPrivate)
                  ->reserved &&
               device_table.pfnCheckResourceVirtualAddress(
                  driver_device, resource_handle) == TEST_TILED_GPU_VA,
            "create reserved resource without KMD allocation");
         device_table.pfnGetMipPacking(driver_device, resource_handle,
                                       &packed_mips, &packed_tiles);
         require_result(packed_mips == 3 && packed_tiles == 2,
                        "reserved resource mip packing translation");

         set_error_calls = 0;
         command_queue_table.pfnUpdateTileMappings(
            queue_handle, resource_handle, 1, &update_start, &update_size,
            heap_handle, 1, &range_flag, &heap_start, &range_count,
            D3D12DDI_TILE_MAPPING_FLAG_NO_HAZARD);
         command_queue_table.pfnCopyTileMappings(
            queue_handle, resource_handle, &destination_start,
            resource_handle, &source_start, &copy_mapping_size,
            D3D12DDI_TILE_MAPPING_FLAG_NONE);
         command_list_table.pfnCopyTiles(
            list_handle, resource_handle, &copy_start, &copy_size,
            buffer_handle, UINT64_C(0x10000),
            D3D12DDI_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE);
         require_result(set_error_calls == 0 && sparse_update_calls == 1 &&
                           sparse_copy_mapping_calls == 1 &&
                           sparse_copy_tiles_calls == 1,
                        "translate Tier-1 sparse resource operations");

         command_list_table.pfnCopyTiles(
            list_handle, resource_handle, &copy_start, &copy_size,
            buffer_handle, UINT64_C(0x10000),
            D3D12DDI_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE |
               D3D12DDI_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
         require_result(set_error_calls == 1 &&
                           last_set_error == E_INVALIDARG &&
                           sparse_copy_tiles_calls == 1,
                        "reject conflicting tile-copy directions");

         device_table.pfnDestroyHeapAndResource(
            driver_device, (D3D12DDI_HHEAP){0}, resource_handle);
         require_result(sparse_destroy_calls == 1,
                        "destroy reserved resource without heap teardown");
         HeapFree(GetProcessHeap(), 0, resource_handle.pDrvPrivate);
         private_device->renderer_ops = saved_renderer_ops;
         private_device->backend = saved_backend;
      }

      {
         const D3D12DDIARG_CREATEHEAP_0001 heap_arguments = {
            .ByteSize = sizeof(mapped_heap),
            .Alignment = UINT64_C(0x10000),
            .MemoryPool = D3D12DDI_MEMORY_POOL_L0,
            .CPUPageProperty = D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK,
            .Flags = D3D12DDI_HEAP_FLAG_BUFFERS,
            .CreationNodeMask = 1,
            .VisibleNodeMask = 1,
         };
         const D3D12DDIARG_CREATERESOURCE_0003 resource_arguments = {
            .ResourceType = D3D12DDI_RT_BUFFER,
            .Width = UINT64_C(0x10000),
            .Height = 1,
            .DepthOrArraySize = 1,
            .MipLevels = 1,
            .Format = DXGI_FORMAT_UNKNOWN,
            .SampleDesc = {.Count = 1},
            .Layout = D3D12DDI_TL_ROW_MAJOR,
            .InitialResourceState = D3D12DDI_RESOURCE_STATE_COMMON,
         };
         const D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes =
            device_table.pfnCalcPrivateHeapAndResourceSizes(
               driver_device, &heap_arguments, &resource_arguments);
         D3D12DDI_HHEAP heap_handle;
         D3D12DDI_HRESOURCE resource_handle;
         D3D12DDI_HRTRESOURCE runtime_resource = {
            .handle = TEST_RT_RESOURCE,
         };
         void *mapping = NULL;

         require_result(sizes.Heap > 0 && sizes.Resource > 0,
                        "private heap/resource sizes");
         heap_handle.pDrvPrivate =
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Heap);
         resource_handle.pDrvPrivate =
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Resource);
         require_result(heap_handle.pDrvPrivate && resource_handle.pDrvPrivate,
                        "allocate heap/resource private storage");
         result = device_table.pfnCreateHeapAndResource(
            driver_device, &heap_arguments, heap_handle, runtime_resource,
            &resource_arguments, NULL, resource_handle);
         require_result(result == S_OK && allocation_calls == 1,
                        "CreateHeapAndResource allocation callback");
         require_result(device_table.pfnCheckResourceVirtualAddress(
                           driver_device, resource_handle) ==
                           TEST_RESOURCE_GPU_VA,
                        "resource GPU virtual address");

         {
            DXGI_DDI_ARG_SETRESOURCEPRIORITY priority_arguments = {
               .hDevice = (DXGI_DDI_HDEVICE)(uintptr_t)
                  driver_device.pDrvPrivate,
               .hResource = (DXGI_DDI_HRESOURCE)(uintptr_t)
                  resource_handle.pDrvPrivate,
               .Priority = 7,
            };
            const DXGI_DDI_HRESOURCE resources[] = {
               (DXGI_DDI_HRESOURCE)(uintptr_t)
                  resource_handle.pDrvPrivate,
            };
            DXGI_DDI_RESIDENCY residency = 0;
            DXGI_DDI_ARG_QUERYRESOURCERESIDENCY residency_arguments = {
               .hDevice = (DXGI_DDI_HDEVICE)(uintptr_t)
                  driver_device.pDrvPrivate,
               .pResources = resources,
               .pStatus = &residency,
               .Resources = ARRAYSIZE(resources),
            };

            result = dxgi_table.pfnSetResourcePriority(
               &priority_arguments);
            require_result(result == S_OK && priority_calls == 1,
                           "DXGI allocation-priority callback");
            result = dxgi_table.pfnQueryResourceResidency(
               &residency_arguments);
            require_result(
               result == D12_UMD_S_RESIDENT_IN_SHARED_MEMORY &&
                  residency_query_calls == 1 &&
                  residency ==
                     DXGI_DDI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY,
               "DXGI batched residency callback");
            query_residency_status = D3DDDI_RESIDENCYSTATUS_NOTRESIDENT;
            result = dxgi_table.pfnQueryResourceResidency(
               &residency_arguments);
            require_result(result == D12_UMD_S_NOT_RESIDENT &&
                              residency_query_calls == 2 &&
                              residency == DXGI_DDI_RESIDENCY_EVICTED_TO_DISK,
                           "DXGI nonresident HRESULT and status");
            query_residency_status =
               D3DDDI_RESIDENCYSTATUS_RESIDENTINSHAREDMEMORY;

            {
               DXGI_DDI_HRESOURCE offered_resources[17];
               BOOL discarded[ARRAYSIZE(offered_resources)] = {0};
#if D12_UMD_TARGETS_WDDM21
               DXGI_DDI_ARG_OFFERRESOURCES1 offer_arguments = {
#else
               DXGI_DDI_ARG_OFFERRESOURCES offer_arguments = {
#endif
                  .hDevice = priority_arguments.hDevice,
                  .pResources = offered_resources,
                  .Resources = ARRAYSIZE(offered_resources),
                  .Priority = D3DDDI_OFFER_PRIORITY_NORMAL,
#if D12_UMD_TARGETS_WDDM21
                  .Flags = {.AllowDecommit = 1},
#endif
               };
               DXGI_DDI_ARG_RECLAIMRESOURCES reclaim_arguments = {
                  .hDevice = priority_arguments.hDevice,
                  .pResources = offered_resources,
                  .pDiscarded = discarded,
                  .Resources = ARRAYSIZE(offered_resources),
               };

               for (UINT index = 0; index < ARRAYSIZE(offered_resources);
                    ++index)
                  offered_resources[index] = resources[0];
#if D12_UMD_TARGETS_WDDM21
               result = dxgi_table.pfnOfferResources1(&offer_arguments);
#else
               result = dxgi_table.pfnOfferResources(&offer_arguments);
#endif
               require_result(result == S_OK && dxgi_offer_calls == 1,
                              "DXGI OfferResources allocation batching");
               result = dxgi_table.pfnReclaimResources(&reclaim_arguments);
               require_result(result == S_OK && dxgi_reclaim_calls == 1,
                              "DXGI ReclaimResources allocation batching");
               for (UINT index = 0; index < ARRAYSIZE(discarded); ++index) {
                  require_result(discarded[index] == ((index & 1u) != 0),
                                 "DXGI ReclaimResources discarded mapping");
               }

               reclaim_arguments.Resources = 1;
               reclaim_arguments.pDiscarded = NULL;
               result = dxgi_table.pfnReclaimResources(&reclaim_arguments);
               require_result(result == S_OK && dxgi_reclaim_calls == 2,
                              "DXGI ReclaimResources optional results");

#if D12_UMD_TARGETS_WDDM21
               {
                  struct d12_umd_device *private_device =
                     driver_device.pDrvPrivate;
                  D3DDDI_RECLAIM_RESULT reclaim_results[
                     ARRAYSIZE(offered_resources)] = {0};
                  DXGI_DDI_ARG_RECLAIMRESOURCES1 reclaim1_arguments = {
                     .hDevice = priority_arguments.hDevice,
                     .pResources = offered_resources,
                     .pResults = reclaim_results,
                     .Resources = ARRAYSIZE(offered_resources),
                  };
                  void *saved_backend = private_device->backend;
                  const struct d12_umd_renderer_ops *saved_renderer_ops =
                     private_device->renderer_ops;

                  private_device->backend = TEST_BLT_DEVICE;
                  private_device->renderer_ops =
                     &test_residency_renderer_ops;
                  result = dxgi_table.pfnReclaimResources1(
                     &reclaim1_arguments);
                  private_device->backend = saved_backend;
                  private_device->renderer_ops = saved_renderer_ops;
                  require_result(result == S_OK && dxgi_reclaim_calls == 3,
                                 "WDDM 2.1 DXGI ReclaimResources1 callback");
                  require_result(dxgi_paging_wait_value == UINT64_C(13),
                                 "WDDM 2.1 DXGI paging-fence wait");
                  for (UINT index = 0; index < ARRAYSIZE(reclaim_results);
                       ++index) {
                     require_result(
                        reclaim_results[index] ==
                           ((index & 1u) ? D3DDDI_RECLAIM_RESULT_DISCARDED
                                         : D3DDDI_RECLAIM_RESULT_OK),
                        "WDDM 2.1 DXGI reclaim-result mapping");
                  }

                  reclaim1_arguments.Resources = 1;
                  reclaim1_arguments.pResults = NULL;
                  dxgi_paging_wait_value = 0;
                  private_device->backend = TEST_BLT_DEVICE;
                  private_device->renderer_ops =
                     &test_residency_renderer_ops;
                  result = dxgi_table.pfnReclaimResources1(
                     &reclaim1_arguments);
                  private_device->backend = saved_backend;
                  private_device->renderer_ops = saved_renderer_ops;
                  require_result(result == S_OK && dxgi_reclaim_calls == 4 &&
                                    dxgi_paging_wait_value == UINT64_C(13),
                                 "WDDM 2.1 DXGI reclaim optional results");
               }
#endif

               offer_arguments.Resources = 0;
               offer_arguments.pResources = NULL;
               reclaim_arguments.Resources = 0;
               reclaim_arguments.pResources = NULL;
#if D12_UMD_TARGETS_WDDM21
               result = dxgi_table.pfnOfferResources1(&offer_arguments);
#else
               result = dxgi_table.pfnOfferResources(&offer_arguments);
#endif
               require_result(result == S_OK && dxgi_offer_calls == 1,
                              "DXGI empty OfferResources no-op");
               result = dxgi_table.pfnReclaimResources(&reclaim_arguments);
               require_result(result == S_OK && dxgi_reclaim_calls ==
#if D12_UMD_TARGETS_WDDM21
                                 4,
#else
                                 2,
#endif
                              "DXGI empty ReclaimResources no-op");

               {
                  DXGI_DDI_ARG_RESOLVESHAREDRESOURCE resolve_arguments = {
                     .hDevice = priority_arguments.hDevice,
                     .hResource = resources[0],
                  };
                  result = dxgi_table.pfnResolveSharedResource(
                     &resolve_arguments);
                  require_result(result == S_OK,
                                 "DXGI ResolveSharedResource validation");
                  resolve_arguments.hResource = 0;
                  result = dxgi_table.pfnResolveSharedResource(
                     &resolve_arguments);
                  require_result(result == E_INVALIDARG,
                                 "reject invalid DXGI shared resource");
               }

               offer_arguments.Resources = 1;
               offer_arguments.pResources = offered_resources;
               offered_resources[0] = 0;
#if D12_UMD_TARGETS_WDDM21
               result = dxgi_table.pfnOfferResources1(&offer_arguments);
#else
               result = dxgi_table.pfnOfferResources(&offer_arguments);
#endif
               require_result(result == E_INVALIDARG && dxgi_offer_calls == 1,
                              "reject invalid DXGI offered resource");
               offered_resources[0] = resources[0];
            }

            {
               DXGI_DDI_ARG_TRIMRESIDENCYSET trim_arguments = {
                  .hDevice = priority_arguments.hDevice,
                  .TrimFlags = {.RestartPeriodicTrim = 1},
               };

               result = dxgi_table.pfnTrimResidencySet(&trim_arguments);
               require_result(result == S_OK,
                              "DXGI restart-periodic trim");
               trim_arguments.TrimFlags =
                  (D3DDDI_TRIMRESIDENCYSET_FLAGS){.PeriodicTrim = 1};
               result = dxgi_table.pfnTrimResidencySet(&trim_arguments);
               require_result(result == S_OK, "DXGI periodic trim");
               trim_arguments.TrimFlags =
                  (D3DDDI_TRIMRESIDENCYSET_FLAGS){.TrimToBudget = 1};
               trim_arguments.NumBytesToTrim = UINT64_C(0x200000);
               result = dxgi_table.pfnTrimResidencySet(&trim_arguments);
               require_result(result == S_OK, "DXGI budget trim");
               trim_arguments.TrimFlags =
                  (D3DDDI_TRIMRESIDENCYSET_FLAGS){
                     .PeriodicTrim = 1,
                     .RestartPeriodicTrim = 1,
                  };
               result = dxgi_table.pfnTrimResidencySet(&trim_arguments);
               require_result(result == E_INVALIDARG,
                              "reject conflicting DXGI trim modes");
               trim_arguments.TrimFlags.Value = UINT32_C(0x80000000);
               result = dxgi_table.pfnTrimResidencySet(&trim_arguments);
               require_result(result == E_INVALIDARG,
                              "reject reserved DXGI trim flags");
            }

            {
               struct d12_umd_device *private_device =
                  driver_device.pDrvPrivate;
               struct d12_umd_heap rotate_heaps[3] = {0};
               struct d12_umd_resource rotate_resources[3] = {0};
               DXGI_DDI_HRESOURCE rotate_handles[3];
               HANDLE runtime_handles[3];
               const D3D12DDIARG_CREATERESOURCE_0003 rotate_resource_desc = {
                  .ResourceType = D3D12DDI_RT_TEXTURE2D,
                  .Width = 64,
                  .Height = 64,
                  .DepthOrArraySize = 1,
                  .MipLevels = 1,
                  .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                  .SampleDesc = {.Count = 1},
                  .Layout = D3D12DDI_TL_UNDEFINED,
                  .Flags = D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET |
                           D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE,
                  .InitialResourceState = D3D12DDI_RESOURCE_STATE_COMMON,
               };
               DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES rotate_arguments = {
                  .hDevice = priority_arguments.hDevice,
                  .pResources = rotate_handles,
                  .Resources = ARRAYSIZE(rotate_handles),
               };

               require_result(private_device->backend == NULL,
                              "callback-only probe has no renderer backend");
               for (UINT index = 0; index < ARRAYSIZE(rotate_heaps); ++index) {
                  runtime_handles[index] =
                     (HANDLE)(uintptr_t)(UINT64_C(0x91000000) + index);
                  rotate_heaps[index].signature = D12_UMD_HEAP_SIGNATURE;
                  rotate_heaps[index].device = private_device;
                  rotate_heaps[index].allocation =
                     (struct d12_umd_allocation){
                        .allocation = 0x92000000u + index,
                        .resource = 0x93000000u + index,
                        .gpu_virtual_address =
                           UINT64_C(0x2400000000) +
                           (uint64_t)index * UINT64_C(0x10000),
                        .size = UINT64_C(0x10000),
                     };
                  rotate_heaps[index].runtime_resource =
                     runtime_handles[index];
                  rotate_heaps[index].cpu_page_property =
                     D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK;
                  InitializeSRWLock(&rotate_heaps[index].map_lock);

                  rotate_resources[index].signature =
                     D12_UMD_RESOURCE_SIGNATURE;
                  rotate_resources[index].device = private_device;
                  rotate_resources[index].heap = &rotate_heaps[index];
                  rotate_resources[index].desc = rotate_resource_desc;
                  rotate_handles[index] =
                     (DXGI_DDI_HRESOURCE)(uintptr_t)&rotate_resources[index];
               }

               result = dxgi_table.pfnRotateResourceIdentities(
                  &rotate_arguments);
               require_result(result == S_OK,
                              "DXGI rotate-resource callback");
               for (UINT index = 0; index < ARRAYSIZE(rotate_heaps); ++index) {
                  const UINT source = (index + 1) % ARRAYSIZE(rotate_heaps);
                  require_result(
                     rotate_heaps[index].allocation.allocation ==
                        0x92000000u + source &&
                        rotate_heaps[index].allocation.resource ==
                           0x93000000u + source &&
                        rotate_heaps[index].allocation.gpu_virtual_address ==
                           UINT64_C(0x2400000000) +
                              (uint64_t)source * UINT64_C(0x10000) &&
                        rotate_heaps[index].runtime_resource ==
                           runtime_handles[index],
                     "DXGI kernel identity rotates, runtime handle stays");
               }

               const struct d12_umd_allocation before =
                  rotate_heaps[0].allocation;
               rotate_heaps[0].map_count = 1;
               result = dxgi_table.pfnRotateResourceIdentities(
                  &rotate_arguments);
               require_result(
                  result == E_INVALIDARG &&
                     memcmp(&before, &rotate_heaps[0].allocation,
                            sizeof(before)) == 0,
                  "reject rotation of CPU-mapped resource transactionally");
               rotate_heaps[0].map_count = 0;

               {
                  DXGI_DDI_ARG_BLT blt = {
                     .hDevice = priority_arguments.hDevice,
                     .hDstResource = rotate_handles[0],
                     .DstRight = 64,
                     .DstBottom = 64,
                     .hSrcResource = rotate_handles[1],
                     .Rotate = DXGI_DDI_MODE_ROTATION_IDENTITY,
                  };
                  DXGI_DDI_ARG_BLT1 blt1 = {
                     .hDevice = priority_arguments.hDevice,
                     .hDstResource = rotate_handles[0],
                     .DstRight = 32,
                     .DstBottom = 32,
                     .hSrcResource = rotate_handles[1],
                     .SrcRight = 32,
                     .SrcBottom = 32,
                     .Rotate = DXGI_DDI_MODE_ROTATION_IDENTITY,
                  };

                  result = dxgi_table.pfnBlt(&blt);
                  require_result(
                     result == DXGI_DDI_ERR_UNSUPPORTED,
                     "DXGI Blt validates before renderer availability");
                  result = dxgi_table.pfnBlt1(&blt1);
                  require_result(
                     result == DXGI_DDI_ERR_UNSUPPORTED,
                     "DXGI Blt1 validates before renderer availability");
                  blt1.SrcRight = 0;
                  result = dxgi_table.pfnBlt1(&blt1);
                  require_result(result == E_INVALIDARG,
                                 "reject empty DXGI Blt1 source rectangle");
                  blt1.SrcRight = 32;
                  blt1.DstBottom = 16;
                  result = dxgi_table.pfnBlt1(&blt1);
                  require_result(result == E_INVALIDARG,
                                 "require DXGI stretch flag for scaling");
                  blt1.Flags.Stretch = 1;
                  result = dxgi_table.pfnBlt1(&blt1);
                  require_result(result == DXGI_DDI_ERR_UNSUPPORTED,
                                 "DXGI stretch reaches renderer gate");

                  blt1.DstBottom = 32;
                  blt1.Flags.Value = 0;
                  blt1.Rotate = DXGI_DDI_MODE_ROTATION_ROTATE90;
                  result = dxgi_table.pfnBlt1(&blt1);
                  require_result(result == DXGI_DDI_ERR_UNSUPPORTED,
                                 "DXGI rotation reaches renderer gate");

                  blt1.Rotate = DXGI_DDI_MODE_ROTATION_IDENTITY;
                  rotate_resources[1].desc.Format =
                     DXGI_FORMAT_B8G8R8A8_UNORM;
                  result = dxgi_table.pfnBlt1(&blt1);
                  require_result(result == E_INVALIDARG,
                                 "require DXGI convert flag for format change");
                  blt1.Flags.Convert = 1;
                  result = dxgi_table.pfnBlt1(&blt1);
                  require_result(result == DXGI_DDI_ERR_UNSUPPORTED,
                                 "DXGI conversion reaches renderer gate");

                  blt1.Flags.Value = UINT32_C(0x80000000);
                  result = dxgi_table.pfnBlt1(&blt1);
                  require_result(result == E_INVALIDARG,
                                 "reject reserved DXGI blit flags");
                  rotate_resources[1].desc.Format =
                     DXGI_FORMAT_R8G8B8A8_UNORM;

                  {
                     struct d12_umd_command_queue blt_queue = {
                        .signature = D12_UMD_COMMAND_QUEUE_SIGNATURE,
                        .device = private_device,
                        .type = D12_UMD_RENDERER_COMMAND_DIRECT,
                        .backend = TEST_BLT_QUEUE,
                        .next = private_device->queues,
                     };
                     struct d12_umd_command_queue *saved_queues =
                        private_device->queues;
                     struct d12_device *saved_backend =
                        private_device->backend;
                     const struct d12_umd_renderer_ops *saved_renderer_ops =
                        private_device->renderer_ops;

                     InitializeSRWLock(&blt_queue.dxgi_blt_lock);
                     blt_allocator_create_calls = 0;
                     blt_allocator_reset_calls = 0;
                     blt_allocator_destroy_calls = 0;
                     blt_list_create_calls = 0;
                     blt_list_reset_calls = 0;
                     blt_list_close_calls = 0;
                     blt_list_destroy_calls = 0;
                     blt_record_calls = 0;
                     blt_execute_calls = 0;
                     blt_execute_result = S_OK;
                     private_device->backend = TEST_BLT_DEVICE;
                     private_device->renderer_ops = &test_blt_renderer_ops;
                     private_device->queues = &blt_queue;
                     rotate_resources[0].backend = TEST_BLT_DESTINATION;
                     rotate_resources[1].backend = TEST_BLT_SOURCE;
                     blt1.Flags.Value = 0;
                     blt1.Flags.Stretch = 1;
                     blt1.DstBottom = 16;

                     result = dxgi_table.pfnBlt1(&blt1);
                     require_result(result == S_OK,
                                    "create DXGI blit recorder cache");
                     result = dxgi_table.pfnBlt1(&blt1);
                     require_result(result == S_OK,
                                    "reuse DXGI blit recorder cache");
                     require_result(
                        blt_allocator_create_calls == 1 &&
                           blt_list_create_calls == 1 &&
                           blt_allocator_reset_calls == 1 &&
                           blt_list_reset_calls == 1 &&
                           blt_record_calls == 2 &&
                           blt_list_close_calls == 2 &&
                           blt_execute_calls == 2 &&
                           blt_allocator_destroy_calls == 0 &&
                           blt_list_destroy_calls == 0,
                        "DXGI blit steady state avoids object churn");

                     blt_execute_result = E_FAIL;
                     result = dxgi_table.pfnBlt1(&blt1);
                     require_result(
                        result == E_FAIL &&
                           blt_allocator_reset_calls == 2 &&
                           blt_list_reset_calls == 2 &&
                           blt_record_calls == 3 &&
                           blt_list_close_calls == 3 &&
                           blt_execute_calls == 3 &&
                           blt_allocator_destroy_calls == 1 &&
                           blt_list_destroy_calls == 1 &&
                           !blt_queue.dxgi_blt_allocator &&
                           !blt_queue.dxgi_blt_list,
                        "DXGI blit failure discards recorder cache");

                     blt_execute_result = S_OK;
                     rotate_resources[0].backend = NULL;
                     rotate_resources[1].backend = NULL;
                     private_device->queues = saved_queues;
                     private_device->renderer_ops = saved_renderer_ops;
                     private_device->backend = saved_backend;
                  }
               }

               rotate_handles[1] = rotate_handles[0];
               result = dxgi_table.pfnRotateResourceIdentities(
                  &rotate_arguments);
               require_result(result == E_INVALIDARG,
                              "reject duplicate rotated resource");
               rotate_arguments.pResources = NULL;
               rotate_arguments.Resources = 0;
               result = dxgi_table.pfnRotateResourceIdentities(
                  &rotate_arguments);
               require_result(result == S_OK,
                              "empty DXGI rotation is a no-op");
            }

            {
               D3D12DDIARG_CREATECOMMANDQUEUE_0001 queue_arguments = {
                  .hRTCommandQueue = {.handle = TEST_RT_QUEUE},
                  .QueueFlags = D3D12DDI_COMMAND_QUEUE_FLAG_3D,
                  .NodeMask = 0,
               };
               D3D12DDI_HCOMMANDQUEUE driver_queue;
               const SIZE_T queue_size =
                  device_table.pfnCalcPrivateCommandQueueSize(
                     driver_device, &queue_arguments);
               DXGI_DDI_ARG_PRESENT present_arguments = {
                  .hDevice = (DXGI_DDI_HDEVICE)(uintptr_t)
                     driver_device.pDrvPrivate,
                  .hSurfaceToPresent = (DXGI_DDI_HRESOURCE)(uintptr_t)
                     resource_handle.pDrvPrivate,
                  .pDXGIContext = TEST_DXGI_CONTEXT,
                  .Flags = {.Flip = 1},
                  .FlipInterval = DXGI_DDI_FLIP_INTERVAL_ONE,
               };
               const DXGI_DDI_ARG_PRESENTSURFACE surface = {
                  .hSurface = present_arguments.hSurfaceToPresent,
               };
               const DXGI_DDI_ARG_PRESENTSURFACE stereo_surfaces[2] = {
                  {.hSurface = present_arguments.hSurfaceToPresent},
                  {.hSurface = present_arguments.hSurfaceToPresent},
               };
               const RECT dirty_rect = {0, 0, 1, 1};
               DXGI_DDI_ARG_PRESENT1 present1_arguments = {
                  .hDevice = present_arguments.hDevice,
                  .phSurfacesToPresent = &surface,
                  .SurfacesToPresent = 1,
                  .pDXGIContext = TEST_DXGI_CONTEXT,
                  .Flags = {.Flip = 1},
                  .FlipInterval = DXGI_DDI_FLIP_INTERVAL_ONE,
                  .pDirtyRects = &dirty_rect,
                  .DirtyRects = 1,
               };

               require_result(queue_size > 0,
                              "DXGI present queue private size");
               driver_queue.pDrvPrivate = HeapAlloc(
                  GetProcessHeap(), HEAP_ZERO_MEMORY, queue_size);
               require_result(driver_queue.pDrvPrivate != NULL,
                              "allocate DXGI present queue");
               queue_arguments.hDrvCommandQueue = driver_queue;
               result = device_table.pfnCreateCommandQueue(
                  driver_device, &queue_arguments);
               require_result(result == S_OK,
                              "create DXGI present queue");
               result = dxgi_table.pfnPresent(&present_arguments);
               require_result(result == S_OK && dxgi_present_calls == 1,
                              "DXGI flip present callback");
               result = dxgi_table.pfnPresent1(&present1_arguments);
               require_result(result == S_OK && dxgi_present_calls == 2,
                              "DXGI Present1 dirty-rect callback");
               present1_arguments.phSurfacesToPresent = stereo_surfaces;
               present1_arguments.SurfacesToPresent =
                  ARRAYSIZE(stereo_surfaces);
               result = dxgi_table.pfnPresent1(&present1_arguments);
               require_result(
                  result == S_OK && dxgi_present_calls == 3 &&
                     present1_arguments.BackBufferMultiplicity == 1,
                  "DXGI Present1 multi-surface last-source callback");
               present1_arguments.Reserved = 1;
               result = dxgi_table.pfnPresent1(&present1_arguments);
               require_result(result == E_INVALIDARG &&
                                 dxgi_present_calls == 3,
                              "reject reserved DXGI Present1 fields");
               present1_arguments.Reserved = 0;
               present1_arguments.Flags.Value = UINT32_C(0x80000000);
               result = dxgi_table.pfnPresent1(&present1_arguments);
               require_result(result == E_INVALIDARG &&
                                 dxgi_present_calls == 3,
                              "reject reserved DXGI present flags");
               present1_arguments.Flags.Value = 0;
               present1_arguments.Flags.Blt = 1;
               present1_arguments.Flags.Flip = 1;
               result = dxgi_table.pfnPresent1(&present1_arguments);
               require_result(result == E_INVALIDARG &&
                                 dxgi_present_calls == 3,
                              "reject conflicting DXGI present modes");
               present1_arguments.Flags.Value = 0;
               present1_arguments.Flags.Flip = 1;
               present1_arguments.FlipInterval =
                  (DXGI_DDI_FLIP_INTERVAL_TYPE)UINT32_C(0x7fffffff);
               result = dxgi_table.pfnPresent1(&present1_arguments);
               require_result(result == E_INVALIDARG &&
                                 dxgi_present_calls == 3,
                              "reject invalid DXGI flip interval");
               device_table.pfnDestroyCommandQueue(driver_device,
                                                    driver_queue);
               HeapFree(GetProcessHeap(), 0, driver_queue.pDrvPrivate);
            }
         }

         {
            D3D12DDI_SUBRESOURCE_INFO subresource_info;
            D3D12DDI_RESOURCE_ALLOCATION_INFO existing_info;
            D3DKMT_HANDLE existing_allocation = 0;
            D3D10DDI_HRESOURCE legacy_resource;
            UINT packed_mips = UINT32_MAX;
            UINT packed_tiles = UINT32_MAX;

            device_table.pfnCheckSubresourceInfo(
               driver_device, resource_handle, 0, &subresource_info);
            require_result(subresource_info.Offset == 0 &&
                              subresource_info.RowStride ==
                                 resource_arguments.Width &&
                              subresource_info.DepthStride ==
                                 resource_arguments.Width,
                           "buffer subresource layout");
            device_table.pfnCheckExistingResourceAllocationInfo(
               driver_device, resource_handle, &existing_info,
               &existing_allocation);
            require_result(
               existing_info.ResourceDataSize == resource_arguments.Width &&
                  existing_info.ResourceDataAlignment == UINT64_C(0x10000) &&
                  existing_allocation == TEST_ALLOCATION,
               "existing resource allocation ABI");
            legacy_resource.pDrvPrivate = resource_handle.pDrvPrivate;
            require_result(device_table.pfnCheckResourceAllocationHandle(
                              driver_device, legacy_resource) ==
                              TEST_ALLOCATION,
                           "resource allocation handle");
            set_error_calls = 0;
            last_set_error = S_OK;
            device_table.pfnGetMipPacking(driver_device, resource_handle,
                                          &packed_mips, &packed_tiles);
            require_result(set_error_calls == 1 && packed_mips == 0 &&
                              packed_tiles == 0 &&
                              last_set_error == E_INVALIDARG,
                           "ordinary resource rejects sparse mip packing");
         }

         {
            D3DDDI_OPENALLOCATIONINFO opened_allocation = {
               .hAllocation = TEST_ALLOCATION,
               .pPrivateDriverData = &saved_allocation_private,
               .PrivateDriverDataSize = sizeof(saved_allocation_private),
            };
            const D3D12DDIARG_OPENHEAP_0003 opened_arguments = {
               .NumAllocations = 1,
               .pOpenAllocationInfo = &opened_allocation,
               .hKMResource = {.handle = TEST_KM_RESOURCE},
               .pPrivateDriverData = &saved_resource_private,
               .PrivateDriverDataSize = sizeof(saved_resource_private),
               .InitialResourceState = D3D12DDI_RESOURCE_STATE_COMMON,
            };
            const D3D12DDI_HEAP_AND_RESOURCE_SIZES open_sizes =
               device_table.pfnCalcPrivateOpenedHeapAndResourceSizes(
                  driver_device, &opened_arguments);
            D3D12DDI_HHEAP opened_heap;
            D3D12DDI_HRESOURCE opened_resource;

            require_result(open_sizes.Heap > 0 && open_sizes.Resource > 0,
                           "opened heap/resource private sizes");
            opened_heap.pDrvPrivate = HeapAlloc(
               GetProcessHeap(), HEAP_ZERO_MEMORY, open_sizes.Heap);
            opened_resource.pDrvPrivate = HeapAlloc(
               GetProcessHeap(), HEAP_ZERO_MEMORY, open_sizes.Resource);
            require_result(opened_heap.pDrvPrivate &&
                              opened_resource.pDrvPrivate,
                           "allocate opened heap/resource private storage");
            result = device_table.pfnOpenHeapAndResource(
               driver_device, &opened_arguments, opened_heap,
               runtime_resource, opened_resource);
            require_result(result == E_NOTIMPL,
                           "standalone opened resource has no renderer");
            HeapFree(GetProcessHeap(), 0, opened_resource.pDrvPrivate);
            HeapFree(GetProcessHeap(), 0, opened_heap.pDrvPrivate);
         }
         {
            struct agxw_resource_private foreign_resource_private = {
               .version = AGXW_RESOURCE_PRIVATE_VERSION,
               .struct_size = sizeof(foreign_resource_private),
               .allocation_count = AGXW_MAX_ALLOCATIONS_PER_RESOURCE,
            };
            struct agxw_alloc_private foreign_allocation_private = {
               .version = AGXW_ALLOC_PRIVATE_VERSION,
               .struct_size = sizeof(foreign_allocation_private),
               .size = sizeof(mapped_heap),
               .flags = AGXW_ALLOC_SHAREABLE | AGXW_ALLOC_WRITEBACK,
               .align_log2 = 16,
            };
            D3DDDI_OPENALLOCATIONINFO foreign_allocation = {
               .hAllocation = TEST_ALLOCATION,
               .pPrivateDriverData = &foreign_allocation_private,
               .PrivateDriverDataSize = sizeof(foreign_allocation_private),
            };
            D3D12DDIARG_OPENHEAP_0003 foreign_arguments = {
               .NumAllocations = 1,
               .pOpenAllocationInfo = &foreign_allocation,
               .hKMResource = {.handle = TEST_KM_RESOURCE},
               .pPrivateDriverData = &foreign_resource_private,
               .PrivateDriverDataSize = sizeof(foreign_resource_private),
               .InitialResourceState = D3D12DDI_RESOURCE_STATE_COMMON,
            };
            D3D12DDI_HEAP_AND_RESOURCE_SIZES foreign_sizes;
            D3D12DDI_HHEAP foreign_heap;
            D3D12DDI_HRESOURCE foreign_resource;

            require_result(agxw_standard_desc_pack(
                              128, 128, (uint32_t)D3DDDIFMT_A8B8G8R8,
                              &foreign_allocation_private.reserved) == 0,
                           "foreign D3D11 standard descriptor fixture");
            foreign_sizes =
               device_table.pfnCalcPrivateOpenedHeapAndResourceSizes(
                  driver_device, &foreign_arguments);
            require_result(foreign_sizes.Heap > 0 &&
                              foreign_sizes.Resource > 0,
                           "foreign D3D11 opened resource sizes");
            foreign_heap.pDrvPrivate = HeapAlloc(
               GetProcessHeap(), HEAP_ZERO_MEMORY, foreign_sizes.Heap);
            foreign_resource.pDrvPrivate = HeapAlloc(
               GetProcessHeap(), HEAP_ZERO_MEMORY, foreign_sizes.Resource);
            require_result(foreign_heap.pDrvPrivate &&
                              foreign_resource.pDrvPrivate,
                           "allocate foreign opened resource storage");
            result = device_table.pfnOpenHeapAndResource(
               driver_device, &foreign_arguments, foreign_heap,
               runtime_resource, foreign_resource);
            require_result(result == E_NOTIMPL,
                           "standalone foreign open reaches renderer seam");
            HeapFree(GetProcessHeap(), 0, foreign_resource.pDrvPrivate);
            HeapFree(GetProcessHeap(), 0, foreign_heap.pDrvPrivate);
         }
         result = device_table.pfnMapHeap(driver_device, heap_handle, &mapping);
         require_result(result == S_OK && mapping == mapped_heap &&
                           lock_calls == 1,
                        "MapHeap Lock2 callback");
         device_table.pfnUnmapHeap(driver_device, heap_handle);
         require_result(unlock_calls == 1, "UnmapHeap Unlock2 callback");

         {
            D3D12DDI_FENCE placement = {
               .FenceValue = {
                  .BaseAddress = TEST_RESOURCE_GPU_VA,
               },
               .FenceMonitoredValue = {
                  .BaseAddress = TEST_RESOURCE_GPU_VA + sizeof(uint64_t),
               },
               .Flags = D3D12DDI_FENCE_FLAG_BOTTOM_OF_PIPE,
            };
            D3D12DDIARG_CREATE_FENCE fence_arguments = {
               .FenceCount = 1,
               .Fences = &placement,
            };
            D3D12DDI_HFENCE driver_fence;
            const SIZE_T fence_size = device_table.pfnCalcPrivateFenceSize(
               driver_device, &fence_arguments);

            require_result(fence_size > 0, "private fence size");
            driver_fence.pDrvPrivate = HeapAlloc(
               GetProcessHeap(), HEAP_ZERO_MEMORY, fence_size);
            require_result(driver_fence.pDrvPrivate != NULL,
                           "allocate fence private storage");
            result = device_table.pfnCreateFence(
               driver_device, driver_fence, &fence_arguments);
            require_result(result == E_NOTIMPL,
                           "standalone fence has no renderer");
            HeapFree(GetProcessHeap(), 0, driver_fence.pDrvPrivate);

            fence_arguments.FenceCount = 2;
            require_result(device_table.pfnCalcPrivateFenceSize(
                              driver_device, &fence_arguments) == 0,
                           "reject unsupported multi-adapter fence");
         }

         {
            struct d12_umd_device *private_device =
               driver_device.pDrvPrivate;
            const D3D12DDI_HRTPAGINGQUEUE runtime_paging_queue = {
               .handle = TEST_RT_PAGING_QUEUE,
            };
            const D3D12DDI_HANDLE_AND_TYPE object = {
               .Handle = resource_handle.pDrvPrivate,
               .Type = D3D12DDI_HT_0012_RESOURCE,
            };
            UINT64 paging_fence = 0;
            D3D12DDIARG_MAKERESIDENT_0001 make_arguments = {
               .NumAdapters = 1,
               .pRTPagingQueue = &runtime_paging_queue,
               .NumObjects = 1,
               .pObjects = &object,
               .pPagingFenceValue = &paging_fence,
            };
            const D3D12DDIARG_EVICT evict_arguments = {
               .NumObjects = 1,
               .pObjects = &object,
            };
            const D3D12DDIARG_OFFERRESOURCES offer_arguments = {
               .NumObjects = 1,
               .pObjects = &object,
               .Priority = D3DDDI_OFFER_PRIORITY_NORMAL,
            };
            BOOL discarded = FALSE;
            D3D12DDIARG_RECLAIMRESOURCES_0001 reclaim_arguments = {
               .NumAdapters = 1,
               .pRTPagingQueue = &runtime_paging_queue,
               .NumObjects = 1,
               .pObjects = &object,
               .pDiscarded = &discarded,
               .pPagingFenceValue = &paging_fence,
            };

#if D12_UMD_TARGETS_WDDM21
            private_device->kernel_callbacks.pfnOfferAllocations2Cb =
               offer_allocations2;
            private_device->kernel_callbacks.pfnReclaimAllocations3Cb =
               reclaim_allocations3;
#else
            (void)private_device;
#endif

            result = device_table.pfnMakeResident(driver_device,
                                                   &make_arguments);
            require_result(result == E_PENDING && make_resident_calls == 1 &&
                              make_arguments.WaitMask == 1 &&
                              paging_fence == UINT64_C(11),
                           "MakeResident callback and paging fence");
            result = device_table.pfnEvict(driver_device, &evict_arguments);
            require_result(result == S_OK && evict_calls == 1,
                           "Evict callback translation");
            result = device_table.pfnOfferResources(driver_device,
                                                     &offer_arguments);
            require_result(result == S_OK && offer_calls == 1,
                           "OfferResources callback translation");
            paging_fence = 0;
            result = device_table.pfnReclaimResources(driver_device,
                                                       &reclaim_arguments);
            require_result(
#if D12_UMD_TARGETS_WDDM21
                              result == S_OK && reclaim_calls == 1 &&
#else
                              result == E_PENDING && reclaim_calls == 1 &&
#endif
                              discarded && reclaim_arguments.WaitMask == 1 &&
                              paging_fence == UINT64_C(13),
                           "ReclaimResources callback and paging fence");

            {
               const D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010
                  library_arguments = {0};
               const SIZE_T library_size =
                  release1_table.pfnCalcPrivatePipelineLibrarySize(
                     driver_device, &library_arguments);
               D3D12DDI_HPIPELINELIBRARY library_handle = {
                  .pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           library_size),
               };
               D3D12DDI_HANDLE_AND_TYPE pageable_objects[2] = {
                  object,
                  {
                     .Handle = library_handle.pDrvPrivate,
                     .Type = D3D12DDI_HT_0010_PIPELINE_LIBRARY,
                  },
               };
               BOOL mixed_discarded[2] = {FALSE, TRUE};

               require_result(library_size > 0 &&
                                 library_handle.pDrvPrivate != NULL,
                              "allocate pageable pipeline library");
               result = release1_table.pfnCreatePipelineLibrary(
                  driver_device, &library_arguments, library_handle);
               require_result(result == S_OK,
                              "create pageable pipeline library");

               make_arguments.NumObjects = 1;
               make_arguments.pObjects = &pageable_objects[1];
               paging_fence = UINT64_MAX;
               result = release1_table.pfnMakeResident(driver_device,
                                                        &make_arguments);
               require_result(result == S_OK && make_resident_calls == 1 &&
                                 make_arguments.WaitMask == 0 &&
                                 paging_fence == 0,
                              "CPU-only pipeline library residency is a "
                              "validated no-op");

               {
                  const D3D12DDIARG_EVICT library_evict = {
                     .NumObjects = 1,
                     .pObjects = &pageable_objects[1],
                  };
                  const D3D12DDIARG_OFFERRESOURCES library_offer = {
                     .NumObjects = 1,
                     .pObjects = &pageable_objects[1],
                     .Priority = D3DDDI_OFFER_PRIORITY_NORMAL,
                  };

                  result = release1_table.pfnEvict(driver_device,
                                                    &library_evict);
                  require_result(result == S_OK && evict_calls == 1,
                                 "CPU-only pipeline library eviction is a "
                                 "validated no-op");
                  result = release1_table.pfnOfferResources(driver_device,
                                                             &library_offer);
                  require_result(result == S_OK && offer_calls == 1,
                                 "CPU-only pipeline library offer is a "
                                 "validated no-op");
               }

               make_arguments.NumObjects = ARRAYSIZE(pageable_objects);
               make_arguments.pObjects = pageable_objects;
               paging_fence = 0;
               result = release1_table.pfnMakeResident(driver_device,
                                                        &make_arguments);
               require_result(result == E_PENDING &&
                                 make_resident_calls == 2 &&
                                 make_arguments.WaitMask == 1 &&
                                 paging_fence == UINT64_C(11),
                              "mixed pageable objects translate only runtime "
                              "allocations");

               reclaim_arguments.NumObjects = ARRAYSIZE(pageable_objects);
               reclaim_arguments.pObjects = pageable_objects;
               reclaim_arguments.pDiscarded = mixed_discarded;
               paging_fence = 0;
               result = release1_table.pfnReclaimResources(
                  driver_device, &reclaim_arguments);
               require_result(
#if D12_UMD_TARGETS_WDDM21
                                 result == S_OK && reclaim_calls == 2 &&
#else
                                 result == E_PENDING && reclaim_calls == 2 &&
#endif
                                 mixed_discarded[0] && !mixed_discarded[1] &&
                                 reclaim_arguments.WaitMask == 1 &&
                                 paging_fence == UINT64_C(13),
                              "mixed reclaim maps discarded state to original "
                              "objects");

               make_arguments.NumObjects = 0;
               make_arguments.pObjects = NULL;
               paging_fence = UINT64_MAX;
               result = release1_table.pfnMakeResident(driver_device,
                                                        &make_arguments);
               require_result(result == S_OK && make_resident_calls == 2 &&
                                 make_arguments.WaitMask == 0 &&
                                 paging_fence == 0,
                              "empty residency request is valid");

               release1_table.pfnDestroyPipelineLibrary(driver_device,
                                                          library_handle);
               HeapFree(GetProcessHeap(), 0, library_handle.pDrvPrivate);
            }

            {
               D3D12DDI_HANDLE_AND_TYPE objects[17];
               BOOL discarded_objects[ARRAYSIZE(objects)];
               D3D12DDIARG_EVICT large_evict = {
                  .NumObjects = ARRAYSIZE(objects),
                  .pObjects = objects,
               };
               D3D12DDIARG_OFFERRESOURCES large_offer = {
                  .NumObjects = ARRAYSIZE(objects),
                  .pObjects = objects,
                  .Priority = D3DDDI_OFFER_PRIORITY_NORMAL,
               };

               for (UINT index = 0; index < ARRAYSIZE(objects); ++index) {
                  objects[index] = object;
                  discarded_objects[index] = FALSE;
               }
               make_arguments.NumObjects = ARRAYSIZE(objects);
               make_arguments.pObjects = objects;
               paging_fence = 0;
               result = release1_table.pfnMakeResident(driver_device,
                                                        &make_arguments);
               require_result(result == E_PENDING &&
                                 make_resident_calls == 3 &&
                                 make_arguments.WaitMask == 1 &&
                                 paging_fence == UINT64_C(11),
                              "large residency batch callback translation");
               result = release1_table.pfnEvict(driver_device, &large_evict);
               require_result(result == S_OK && evict_calls == 2,
                              "large eviction batch callback translation");
               result = release1_table.pfnOfferResources(driver_device,
                                                          &large_offer);
               require_result(result == S_OK && offer_calls == 2,
                              "large offer batch callback translation");

               reclaim_arguments.NumObjects = ARRAYSIZE(objects);
               reclaim_arguments.pObjects = objects;
               reclaim_arguments.pDiscarded = discarded_objects;
               paging_fence = 0;
               result = release1_table.pfnReclaimResources(
                  driver_device, &reclaim_arguments);
               require_result(
#if D12_UMD_TARGETS_WDDM21
                                 result == S_OK && reclaim_calls == 3 &&
#else
                                 result == E_PENDING && reclaim_calls == 3 &&
#endif
                                 reclaim_arguments.WaitMask == 1 &&
                                 paging_fence == UINT64_C(13),
                              "large reclaim batch callback translation");
               for (UINT index = 0; index < ARRAYSIZE(objects); ++index) {
                  require_result(discarded_objects[index],
                                 "large reclaim maps every discarded result");
               }
            }
         }

         device_table.pfnDestroyHeapAndResource(driver_device, heap_handle,
                                                resource_handle);
         require_result(deallocation_calls == 1,
                        "DestroyHeapAndResource deallocation callback");
         HeapFree(GetProcessHeap(), 0, resource_handle.pDrvPrivate);
         HeapFree(GetProcessHeap(), 0, heap_handle.pDrvPrivate);
      }

      {
         const D3D12DDIARG_CREATEHEAP_0001 heap_arguments = {
            .ByteSize = UINT64_C(0x400000),
            .Alignment = UINT64_C(0x10000),
            .MemoryPool = D3D12DDI_MEMORY_POOL_L0,
            .CPUPageProperty = D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE,
            .Flags = D3D12DDI_HEAP_FLAG_NON_RT_DS_TEXTURES,
            .CreationNodeMask = 1,
            .VisibleNodeMask = 1,
         };
         const D3D12DDIARG_CREATERESOURCE_0003 texture_arguments = {
            .ResourceType = D3D12DDI_RT_TEXTURE2D,
            .Width = 1024,
            .Height = 1024,
            .DepthOrArraySize = 1,
            .MipLevels = 1,
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .SampleDesc = {.Count = 1},
            .Layout = D3D12DDI_TL_UNDEFINED,
            .Flags = D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE,
            .InitialResourceState = D3D12DDI_RESOURCE_STATE_COMMON,
         };
         const D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes =
            device_table.pfnCalcPrivateHeapAndResourceSizes(
               driver_device, &heap_arguments, &texture_arguments);

         require_result(sizes.Heap > 0 && sizes.Resource > 0,
                        "texture heap/resource private sizes");
      }

      functions.pfnDestroyDevice(driver_device);
      HeapFree(GetProcessHeap(), 0, driver_device.pDrvPrivate);

      {
         const D3D12DDIARG_CALCPRIVATEDEVICESIZE release1_size_arguments = {
            .Interface = D3D12DDI_INTERFACE_VERSION_R1,
            .Version = D3D12DDI_BUILD_VERSION_0011,
            .Flags = D3D12DDI_CREATE_DEVICE_FLAG_NONE,
         };
         const SIZE_T release1_device_size =
            functions.pfnCalcPrivateDeviceSize(
               open_arguments.hAdapter, &release1_size_arguments);

#if D12_UMD_TARGETS_WDDM21
         require_result(release1_device_size == device_size,
                        "Release 1 private device size");
         driver_device.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, release1_device_size);
         require_result(driver_device.pDrvPrivate != NULL,
                        "allocate Release 1 private device storage");
         create_arguments.Interface = D3D12DDI_INTERFACE_VERSION_R1;
         create_arguments.Version = D3D12DDI_BUILD_VERSION_0011;
         create_arguments.hDrvDevice = driver_device;
         result = functions.pfnCreateDevice(open_arguments.hAdapter,
                                             &create_arguments);
         require_result(result == DXGI_STATUS_NO_REDIRECTION,
                        "CreateDevice Release 1 explicit-present result");
         functions.pfnDestroyDevice(driver_device);
         HeapFree(GetProcessHeap(), 0, driver_device.pDrvPrivate);
#else
         require_result(release1_device_size == 0,
                        "WDDM 2.0 rejects Release 1 device creation");
#endif
      }
   }

   result = functions.pfnCloseAdapter(open_arguments.hAdapter);
   require_result(result == S_OK, "CloseAdapter result");
   FreeLibrary(module);
   puts("d12_umd_ddiprobe: PASS");
   return 0;
}
