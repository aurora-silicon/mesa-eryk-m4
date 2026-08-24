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
#include "../../d12_dxbc_wrap.h"
#include "../../d12_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if D12_UMD_TARGETS_WDDM31
typedef D3D12DDI_DEVICE_FUNCS_CORE_0099 d12_test_extended_device_table;
typedef D3D12DDI_DEVICE_FUNCS_CORE_0099 d12_test_root_device_table;
typedef D3D12DDI_COMMAND_LIST_FUNCS_3D_0099 d12_test_command_list_table;
typedef D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 d12_test_core_callbacks;
typedef D3D12DDI_DEBUG_KMT_ALLOCATION_INFO_0014
   d12_test_debug_kmt_allocation_info;
#define D12_TEST_PRIMARY_INTERFACE D3D12DDI_INTERFACE_VERSION_R8
#define D12_TEST_PRIMARY_BUILD D3D12DDI_BUILD_VERSION_0099
#elif D12_UMD_TARGETS_WDDM24
typedef D3D12DDI_DEVICE_FUNCS_CORE_0040 d12_test_extended_device_table;
typedef D3D12DDI_DEVICE_FUNCS_CORE_0040 d12_test_root_device_table;
typedef D3D12DDI_COMMAND_LIST_FUNCS_3D_0040 d12_test_command_list_table;
typedef D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 d12_test_core_callbacks;
typedef D3D12DDI_DEBUG_KMT_ALLOCATION_INFO_0014
   d12_test_debug_kmt_allocation_info;
#define D12_TEST_PRIMARY_INTERFACE D3D12DDI_INTERFACE_VERSION_R4
#define D12_TEST_PRIMARY_BUILD D3D12DDI_BUILD_VERSION_0042
#elif D12_UMD_TARGETS_WDDM23_EXACT
typedef D3D12DDI_DEVICE_FUNCS_CORE_0030 d12_test_extended_device_table;
typedef D3D12DDI_DEVICE_FUNCS_CORE_0030 d12_test_root_device_table;
typedef D3D12DDI_COMMAND_LIST_FUNCS_3D_0032 d12_test_command_list_table;
typedef D3D12DDI_CORELAYER_DEVICECALLBACKS_0022 d12_test_core_callbacks;
typedef D3D12DDI_DEBUG_KMT_ALLOCATION_INFO_0014
   d12_test_debug_kmt_allocation_info;
#define D12_TEST_PRIMARY_INTERFACE D3D12DDI_INTERFACE_VERSION_R3
#define D12_TEST_PRIMARY_BUILD D3D12DDI_BUILD_VERSION_0032
#else
typedef D3D12DDI_DEVICE_FUNCS_CORE_0010 d12_test_extended_device_table;
typedef D3D12DDI_DEVICE_FUNCS_CORE_0013 d12_test_root_device_table;
typedef D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 d12_test_command_list_table;
typedef D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 d12_test_core_callbacks;
typedef D3D12DDI_DEBUG_KMT_ALLOCATION_INFO_0012
   d12_test_debug_kmt_allocation_info;
#define D12_TEST_PRIMARY_INTERFACE D3D12DDI_INTERFACE_VERSION
#define D12_TEST_PRIMARY_BUILD D3D12DDI_BUILD_VERSION
#endif

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
#define TEST_RT_COMMAND_LIST ((HANDLE)(uintptr_t)0x8600)

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
static UINT make_resident_allocation_count;
static UINT evict_allocation_count;
static UINT offer_allocation_count;
static UINT reclaim_allocation_count;
static unsigned set_error_calls;
static unsigned set_command_list_error_calls;
static unsigned priority_calls;
static unsigned residency_query_calls;
static unsigned dxgi_offer_calls;
static unsigned dxgi_reclaim_calls;
static UINT dxgi_offer_allocation_count;
static UINT dxgi_reclaim_allocation_count;
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
static unsigned root11_create_calls;
static unsigned root11_destroy_calls;
static uint32_t expected_root_range_flags;
static uint32_t expected_root_descriptor_flags;
static unsigned rtv_create_calls;
static void *rtv_resource;
static uint64_t rtv_destination;
#if D12_UMD_TARGETS_WDDM23
static unsigned revision_immediate_write_calls;
static unsigned revision_ranged_barrier_calls;
static unsigned revision_resolve_region_calls;
#endif
static struct d12_frontend_rtv_desc rtv_desc;
static unsigned dsv_create_calls;
static void *dsv_resource;
static uint64_t dsv_destination;
static struct d12_frontend_dsv_desc dsv_desc;
static unsigned uav_create_calls;
static void *uav_resource;
static uint64_t uav_destination;
static struct d12_frontend_uav_desc uav_desc;
static HRESULT blt_execute_result = S_OK;
static D3DDDI_RESIDENCYSTATUS query_residency_status =
   D3DDDI_RESIDENCYSTATUS_RESIDENTINSHAREDMEMORY;
static BOOL set_error_runtime_device_valid;
static HRESULT last_set_error;
static BOOL set_error_runtime_command_list_valid;
static HRESULT last_set_command_list_error;
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
#define TEST_ROOT_SIGNATURE ((void *)(uintptr_t)0xa710)
#define TEST_TILED_RESOURCE ((void *)(uintptr_t)0xa800)
#define TEST_TILE_HEAP ((void *)(uintptr_t)0xa900)
#define TEST_TILE_QUEUE ((void *)(uintptr_t)0xaa00)
#define TEST_TILE_LIST ((void *)(uintptr_t)0xab00)
#define TEST_TILE_BUFFER ((void *)(uintptr_t)0xac00)
#define TEST_TILED_GPU_VA UINT64_C(0x3300000000)
#define TEST_REUSE_SOURCE ((void *)(uintptr_t)0xad00)
#define TEST_REUSED_RESOURCE ((void *)(uintptr_t)0xae00)
#define TEST_REUSE_GPU_VA UINT64_C(0x4400010000)
#define TEST_REUSED_GPU_VA UINT64_C(0x4400020000)
#define TEST_REUSE_ALLOCATION ((D3DKMT_HANDLE)0xaf00)
#define TEST_TRANSACTION_QUEUE ((void *)(uintptr_t)0xb100)
#define TEST_TRANSACTION_FENCE ((void *)(uintptr_t)0xb200)

static HRESULT blt_allocator_create_result = S_OK;
static HRESULT blt_list_create_result = S_OK;
static void *blt_allocator_output = TEST_BLT_ALLOCATOR;
static void *blt_list_output = TEST_BLT_LIST;

static unsigned sparse_create_calls;
static unsigned sparse_destroy_calls;
static unsigned sparse_update_calls;
static unsigned sparse_copy_mapping_calls;
static unsigned sparse_copy_tiles_calls;
static uint32_t sparse_expected_initial_state;
static unsigned reused_create_calls;
static unsigned reused_destroy_calls;
static bool reused_create_null;
static bool reused_expect_row_major;
static bool reused_allocation_info_fail;
static HRESULT reused_create_result = S_OK;
static uint64_t reused_allocation_info_size = UINT64_C(0x10000);
static unsigned transaction_queue_create_calls;
static unsigned transaction_queue_destroy_calls;
static unsigned transaction_fence_create_calls;
static unsigned transaction_fence_destroy_calls;
static HRESULT transaction_queue_result;
static HRESULT transaction_fence_result;
static void *transaction_queue_output;
static void *transaction_fence_output;
static struct d12_frontend_fence_placement transaction_fence_placement;
static bool transaction_fence_owns_lifetime;
static bool transaction_fence_defer_lifetime_release;

static HRESULT
test_transaction_create_queue(void *device,
                              enum d12_umd_renderer_command_type type,
                              void **out_queue)
{
   if (device != TEST_BLT_DEVICE ||
       type != D12_UMD_RENDERER_COMMAND_DIRECT || !out_queue)
      return E_INVALIDARG;
   ++transaction_queue_create_calls;
   *out_queue = transaction_queue_output;
   return transaction_queue_result;
}

static void
test_transaction_destroy_queue(void *queue)
{
   if (queue == TEST_TRANSACTION_QUEUE)
      ++transaction_queue_destroy_calls;
}

static HRESULT
test_transaction_create_fence(
   void *device, uint32_t count,
   const struct d12_frontend_fence_placement *placements, void **out_fence)
{
   if (device != TEST_BLT_DEVICE || count != 1 || !placements ||
       !out_fence ||
       placements[0].current_gpu_address != TEST_RESOURCE_GPU_VA ||
       placements[0].monitored_gpu_address !=
          TEST_RESOURCE_GPU_VA + sizeof(uint64_t) ||
       placements[0].current_cpu_address !=
          (volatile uint64_t *)(void *)mapped_heap ||
       !placements[0].lifetime_cookie ||
       !placements[0].release_lifetime ||
       placements[0].flags != D3D12DDI_FENCE_FLAG_BOTTOM_OF_PIPE)
      return E_INVALIDARG;
   ++transaction_fence_create_calls;
   *out_fence = transaction_fence_output;
   if (*out_fence) {
      transaction_fence_placement = placements[0];
      transaction_fence_owns_lifetime = true;
   }
   return transaction_fence_result;
}

static void
test_transaction_release_fence_lifetime(void)
{
   if (!transaction_fence_owns_lifetime)
      return;
   transaction_fence_owns_lifetime = false;
   transaction_fence_placement.release_lifetime(
      transaction_fence_placement.lifetime_cookie);
   memset(&transaction_fence_placement, 0,
          sizeof(transaction_fence_placement));
}

static void
test_transaction_destroy_fence(void *fence)
{
   if (fence == TEST_TRANSACTION_FENCE) {
      ++transaction_fence_destroy_calls;
      if (!transaction_fence_defer_lifetime_release)
         test_transaction_release_fence_lifetime();
   }
}

static const struct d12_umd_renderer_ops test_transaction_renderer_ops = {
   .create_command_queue = test_transaction_create_queue,
   .destroy_command_queue = test_transaction_destroy_queue,
   .create_fence = test_transaction_create_fence,
   .destroy_fence = test_transaction_destroy_fence,
};

static const struct d12_umd_renderer_ops
   test_incomplete_transaction_renderer_ops = {
      .create_command_queue = test_transaction_create_queue,
      .create_fence = test_transaction_create_fence,
   };

static HRESULT
test_sparse_validate_reserved(
   const struct d12_frontend_resource_desc *desc)
{
   if (!desc || desc->dimension != D3D12DDI_RT_TEXTURE2D ||
       desc->layout != D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE ||
       desc->initial_state != sparse_expected_initial_state ||
       desc->sample_count != 1 || desc->sample_quality ||
       (desc->depth_or_array_size > 1 && desc->mip_levels > 1))
      return E_INVALIDARG;
   return S_OK;
}

static HRESULT
test_sparse_create_reserved(
   void *device, const struct d12_frontend_resource_desc *desc,
   const struct d12_frontend_clear_value *clear_value, void **out_resource)
{
   if (device != TEST_BLT_DEVICE || !desc || clear_value || !out_resource ||
       desc->dimension != D3D12DDI_RT_TEXTURE2D || desc->width != 1024 ||
       desc->height != 1024 || desc->mip_levels != 5 ||
       desc->format != DXGI_FORMAT_R8G8B8A8_UNORM ||
       desc->layout != D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE ||
       desc->initial_state != sparse_expected_initial_state)
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
test_sparse_resource_allocation_info(
   void *device, const struct d12_frontend_resource_desc *desc,
   uint32_t visible_node_mask,
   struct d12_frontend_resource_allocation_info *info)
{
   if (device != TEST_BLT_DEVICE || !desc || !info ||
       visible_node_mask != 1 ||
       desc->initial_state != sparse_expected_initial_state)
      return E_INVALIDARG;

   *info = (struct d12_frontend_resource_allocation_info){
      .size = desc->dimension == D3D12DDI_RT_BUFFER
                 ? desc->width
                 : UINT64_C(0x500000),
      .alignment = UINT64_C(0x10000),
      .layout = desc->layout,
   };
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
   .validate_reserved_resource = test_sparse_validate_reserved,
   .create_reserved_resource = test_sparse_create_reserved,
   .destroy_resource = test_sparse_destroy_resource,
   .resource_gpu_virtual_address = test_sparse_resource_gpu_va,
   .get_resource_allocation_info = test_sparse_resource_allocation_info,
   .resource_mip_packing = test_sparse_mip_packing,
};

static HRESULT
test_reused_resource_allocation_info(
   void *device, const struct d12_frontend_resource_desc *desc,
   uint32_t visible_node_mask,
   struct d12_frontend_resource_allocation_info *info)
{
   if (device != TEST_BLT_DEVICE || !desc || !info ||
       visible_node_mask != 1 || desc->depth_or_array_size != 1 ||
       desc->mip_levels != 1 ||
       desc->sample_count != 1 || desc->layout != D3D12DDI_TL_ROW_MAJOR)
      return E_INVALIDARG;
   if (reused_expect_row_major
          ? (desc->dimension != D3D12DDI_RT_TEXTURE2D ||
             desc->width != 64 || desc->height != 4 ||
             desc->format != DXGI_FORMAT_R8G8B8A8_UNORM ||
             desc->row_pitch != 256 || desc->slice_pitch != 1024 ||
             !(desc->flags & D12_FRONTEND_RESOURCE_ALLOW_CROSS_ADAPTER))
          : (desc->dimension != D3D12DDI_RT_BUFFER ||
             desc->width != UINT64_C(0x10000) || desc->height != 1 ||
             desc->format != DXGI_FORMAT_UNKNOWN || desc->row_pitch ||
             desc->slice_pitch))
      return E_INVALIDARG;
   if (reused_allocation_info_fail)
      return E_FAIL;
   *info = (struct d12_frontend_resource_allocation_info){
      .size = reused_allocation_info_size,
      .alignment = UINT64_C(0x10000),
      .layout = D3D12DDI_TL_ROW_MAJOR,
   };
   return S_OK;
}

static HRESULT
test_create_reused_resource(
   void *device, void *source_resource, uint64_t source_offset,
   const struct d12_frontend_resource_desc *desc,
   const struct d12_frontend_clear_value *clear_value, void **out_resource)
{
   if (device != TEST_BLT_DEVICE || source_resource != TEST_REUSE_SOURCE ||
       source_offset != UINT64_C(0x10000) || !desc || clear_value ||
       !out_resource ||
       (reused_expect_row_major
           ? (desc->dimension != D3D12DDI_RT_TEXTURE2D ||
              desc->width != 64 || desc->row_pitch != 256 ||
              desc->slice_pitch != 1024)
           : (desc->dimension != D3D12DDI_RT_BUFFER ||
              desc->width != UINT64_C(0x10000) || desc->row_pitch ||
              desc->slice_pitch)))
      return E_INVALIDARG;
   ++reused_create_calls;
   *out_resource = reused_create_null ? NULL : TEST_REUSED_RESOURCE;
   return reused_create_result;
}

static uint64_t
test_reused_resource_gpu_va(void *resource)
{
   if (resource == TEST_REUSE_SOURCE)
      return TEST_REUSE_GPU_VA;
   return resource == TEST_REUSED_RESOURCE ? TEST_REUSED_GPU_VA : 0;
}

static void
test_destroy_reused_resource(void *resource)
{
   if (resource == TEST_REUSED_RESOURCE)
      ++reused_destroy_calls;
}

static const struct d12_umd_renderer_ops test_reused_renderer_ops = {
   .create_reused_resource = test_create_reused_resource,
   .destroy_resource = test_destroy_reused_resource,
   .resource_gpu_virtual_address = test_reused_resource_gpu_va,
   .get_resource_allocation_info = test_reused_resource_allocation_info,
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
test_create_root_signature(
   void *device, const struct d12_frontend_root_signature_desc *desc,
   void **out_root_signature)
{
   if (device != TEST_BLT_DEVICE || !desc || !out_root_signature ||
       desc->num_parameters != 2 || !desc->parameters ||
       desc->num_static_samplers || desc->static_samplers ||
       desc->parameters[0].parameter_type !=
          D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
       desc->parameters[0].table.num_ranges != 1 ||
       !desc->parameters[0].table.ranges ||
       desc->parameters[0].table.ranges[0].flags !=
          expected_root_range_flags ||
       desc->parameters[1].parameter_type !=
          D3D12DDI_ROOT_PARAMETER_TYPE_CBV ||
       desc->parameters[1].descriptor.flags !=
          expected_root_descriptor_flags)
      return E_INVALIDARG;

   ++root11_create_calls;
   *out_root_signature = TEST_ROOT_SIGNATURE;
   return S_OK;
}

static void
test_destroy_root_signature(void *root_signature)
{
   if (root_signature == TEST_ROOT_SIGNATURE)
      ++root11_destroy_calls;
}

static const struct d12_umd_renderer_ops test_root11_renderer_ops = {
   .create_root_signature = test_create_root_signature,
   .destroy_root_signature = test_destroy_root_signature,
};

#if D12_UMD_TARGETS_WDDM23
static HRESULT
test_revision_write_buffer_immediate(
   void *list, uint32_t count,
   const struct d12_frontend_write_buffer_immediate_parameter *parameters,
   const uint32_t *modes)
{
   if (list != (void *)(uintptr_t)1 || !parameters || !modes)
      return E_INVALIDARG;

   if (count == 2) {
      if (parameters[0].destination != UINT64_C(0x2000000040) ||
          parameters[0].value != UINT32_C(0x12345678) ||
          parameters[1].destination != UINT64_C(0x2000000080) ||
          parameters[1].value != UINT32_C(0xabcdef01) ||
          modes[0] != D12_FRONTEND_WRITE_BUFFER_IMMEDIATE_DEFAULT ||
          modes[1] != D12_FRONTEND_WRITE_BUFFER_IMMEDIATE_MARKER_OUT)
         return E_INVALIDARG;
   } else if (count == 33) {
      for (uint32_t index = 0; index < count; ++index) {
         if (parameters[index].destination != UINT64_C(0x3000000000) +
                                                index * sizeof(uint32_t) ||
             parameters[index].value != index ||
             modes[index] != D12_FRONTEND_WRITE_BUFFER_IMMEDIATE_DEFAULT)
            return E_INVALIDARG;
      }
   } else {
      return E_INVALIDARG;
   }
   ++revision_immediate_write_calls;
   return S_OK;
}

static void
test_revision_resource_barriers(
   void *list, uint32_t count,
   const struct d12_frontend_resource_barrier *barriers)
{
   if (list != (void *)(uintptr_t)1 || !count || !barriers)
      return;
   for (uint32_t index = 0; index < count; ++index) {
      if (barriers[index].type != D12_FRONTEND_BARRIER_UAV ||
          barriers[index].flags != 0 || barriers[index].uav.resource)
         return;
   }
   ++revision_ranged_barrier_calls;
}

static HRESULT
test_revision_resolve_subresource_region(
   void *list, void *destination, uint32_t destination_subresource,
   uint32_t destination_x, uint32_t destination_y, void *source,
   uint32_t source_subresource, const struct d12_frontend_rect *source_rect,
   uint32_t format, uint32_t resolve_mode)
{
   if (list != (void *)(uintptr_t)1 ||
       destination != TEST_BLT_DESTINATION || source != TEST_BLT_SOURCE ||
       destination_subresource != 1 || destination_x != 4 ||
       destination_y != 8 || source_subresource != 2 || !source_rect ||
       source_rect->left != 1 || source_rect->top != 2 ||
       source_rect->right != 9 || source_rect->bottom != 10 ||
       format != DXGI_FORMAT_R8G8B8A8_UNORM ||
       resolve_mode != D3D12DDI_RESOLVE_MODE_AVERAGE)
      return E_INVALIDARG;
   ++revision_resolve_region_calls;
   return S_OK;
}

static const struct d12_umd_renderer_ops test_revision_renderer_ops = {
   .resource_barriers = test_revision_resource_barriers,
   .resolve_subresource_region =
      test_revision_resolve_subresource_region,
   .write_buffer_immediate = test_revision_write_buffer_immediate,
};
#endif

static void
test_create_render_target_view(
   void *device, void *resource, const struct d12_frontend_rtv_desc *desc,
   uint64_t destination)
{
   if (device != TEST_BLT_DEVICE || !desc || !destination)
      return;
   ++rtv_create_calls;
   rtv_resource = resource;
   rtv_destination = destination;
   rtv_desc = *desc;
}

static void
test_create_depth_stencil_view(
   void *device, void *resource, const struct d12_frontend_dsv_desc *desc,
   uint64_t destination)
{
   if (device != TEST_BLT_DEVICE || !desc || !destination)
      return;
   ++dsv_create_calls;
   dsv_resource = resource;
   dsv_destination = destination;
   dsv_desc = *desc;
}

static void
test_create_unordered_access_view(
   void *device, void *resource, void *counter_resource,
   const struct d12_frontend_uav_desc *desc, uint64_t destination)
{
   if (device != TEST_BLT_DEVICE || !desc || !destination ||
       counter_resource)
      return;
   ++uav_create_calls;
   uav_resource = resource;
   uav_destination = destination;
   uav_desc = *desc;
}

static const struct d12_umd_renderer_ops test_descriptor_renderer_ops = {
   .create_render_target_view = test_create_render_target_view,
   .create_depth_stencil_view = test_create_depth_stencil_view,
   .create_unordered_access_view = test_create_unordered_access_view,
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
   *out_allocator = blt_allocator_output;
   return blt_allocator_create_result;
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
   *out_list = blt_list_output;
   return blt_list_create_result;
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

#if !D12_UMD_TARGETS_WDDM24
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
#endif

#if D12_UMD_TARGETS_WDDM24
static HRESULT APIENTRY
query_adapter_info2(HANDLE runtime_adapter,
                    const D3DDDICB_QUERYADAPTERINFO2 *query)
{
   const struct agxw_adapter_identity *identity =
      (const struct agxw_adapter_identity *)runtime_adapter;

   if (!identity || !query ||
       query->QueryType != D3DDDI_QUERYADAPTERTYPE_DRIVERPRIVATE ||
       !query->pPrivateDriverData ||
       query->PrivateDriverDataSize != sizeof(*identity))
      return E_INVALIDARG;

   memcpy(query->pPrivateDriverData, identity, sizeof(*identity));
   return S_OK;
}
#endif

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
set_command_list_error(D3D12DDI_HRTCOMMANDLIST runtime_list, HRESULT error)
{
   ++set_command_list_error_calls;
   set_error_runtime_command_list_valid =
      runtime_list.handle == TEST_RT_COMMAND_LIST;
   last_set_command_list_error = error;
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
   dxgi_offer_allocation_count = arguments->NumAllocations;
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
   dxgi_offer_allocation_count = arguments->NumAllocations;
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
   dxgi_reclaim_allocation_count = arguments->NumAllocations;
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
   dxgi_reclaim_allocation_count = arguments->NumAllocations;
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
   make_resident_allocation_count = arguments->NumAllocations;
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
   evict_allocation_count = arguments->NumAllocations;
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
   offer_allocation_count = arguments->NumAllocations;
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
   offer_allocation_count = arguments->NumAllocations;
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
   reclaim_allocation_count = arguments->NumAllocations;
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
   reclaim_allocation_count = arguments->NumAllocations;
   ++reclaim_calls;
   return E_PENDING;
}

static void require_result(int condition, const char *message);

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

#if D12_UMD_TARGETS_WDDM31
static void
require_release8_device_table(
   const D3D12DDI_DEVICE_FUNCS_CORE_0099 *table,
   const char *message)
{
   /* Release 4 is the inherited, fully populated prefix.  Release 8 then
    * changes several signatures inside that prefix and appends independently
    * advertised feature entrypoints.  Validate every changed required slot;
    * the strict compiler build proves each installer's PFN type as well. */
   require_pointer_slots(table, 96, SIZE_MAX, SIZE_MAX, message);
   require_result(table->pfnCalcPrivateCommandQueueSize != NULL &&
                     table->pfnCreateCommandQueue != NULL &&
                     table->pfnCalcPrivateHeapAndResourceSizes != NULL &&
                     table->pfnCreateHeapAndResource != NULL &&
                     table->pfnCalcPrivateOpenedHeapAndResourceSizes != NULL &&
                     table->pfnOpenHeapAndResource != NULL &&
                     table->pfnCheckResourceAllocationInfo != NULL &&
                     table->pfnCalcPrivateDepthStencilStateSize != NULL &&
                     table->pfnCreateDepthStencilState != NULL &&
                     table->pfnCalcPrivateRasterizerStateSize != NULL &&
                     table->pfnCreateRasterizerState != NULL &&
                     table->pfnCalcPrivatePipelineStateSize != NULL &&
                     table->pfnCreatePipelineState != NULL &&
                     table->pfnCreateSampler != NULL,
                  "Release 8 required device callbacks");
   require_pointer_slots(table, 122, SIZE_MAX, SIZE_MAX,
                         "Release 8 complete device table");
}

static void
require_release8_command_table(
   const D3D12DDI_COMMAND_LIST_FUNCS_3D_0099 *table,
   const char *message)
{
   require_pointer_slots(table, 58, SIZE_MAX, SIZE_MAX, message);
   require_result(table->pfnPresent != NULL &&
                     table->pfnOmSetFrontAndBackStencilRef != NULL &&
                     table->pfnRSSetDepthBias != NULL &&
                     table->pfnIASetIndexBufferStripCutValue != NULL,
                  "Release 8 required command-list callbacks");
   require_pointer_slots(table, 73, SIZE_MAX, SIZE_MAX,
                         "Release 8 complete command-list table");
}
#endif

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
      .submit_private_max_version = AGXW_SUBMIT_PRIVATE_VERSION_V4,
   };
   const D3DDDI_ADAPTERCALLBACKS callbacks = {
#if D12_UMD_TARGETS_WDDM24
      .pfnQueryAdapterInfoCb2 = query_adapter_info2,
#else
      .pfnQueryAdapterInfoCb = query_adapter_info,
#endif
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

   require_result(D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT ==
                     D12_FRONTEND_MAX_VERTEX_BUFFERS,
                  "D3D12 vertex-buffer slot count");

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
      UINT32 optional_count = 0;
      result = functions.pfnGetOptionalDDITables(
         open_arguments.hAdapter, &optional_count, NULL);
#if D12_UMD_TARGETS_WDDM31
      require_result(result == S_OK && optional_count == 1,
                     "Release 8 optional-table count");
      {
         D3D12DDI_TABLE_REQUEST request = {0};
         optional_count = 0;
         result = functions.pfnGetOptionalDDITables(
            open_arguments.hAdapter, &optional_count, &request);
         require_result(
            result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) &&
               optional_count == 1,
            "Release 8 optional-table short buffer");
         optional_count = 1;
         result = functions.pfnGetOptionalDDITables(
            open_arguments.hAdapter, &optional_count, &request);
         require_result(
            result == S_OK && optional_count == 1 &&
               request.tableType ==
                  D3D12DDI_TABLE_TYPE_0096_EXTENDED_FEATURES &&
               request.numTables == 1,
            "Release 8 optional extended-feature table request");
      }
#else
      require_result(result == S_OK && optional_count == 0,
                     "no optional tables before Release 8");
#endif
   }

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
                        data.SupportsRowMajorTexture,
                     "row-major texture-layout caps");
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

      {
         UINT info[2] = {
            D3D12DDI_TL_ROW_MAJOR,
            D3D12DDI_FUNCUNIT_COMBINED,
         };
         D3D12DDI_ROW_MAJOR_LAYOUT_CAPS row_major;
         D3D12DDIARG_GETCAPS row_major_query = {
            .Type = D3D12DDICAPS_TYPE_TEXTURE_LAYOUT_SETS,
            .pInfo = info,
            .pData = &row_major,
            .DataSize = sizeof(row_major),
         };

         memset(&row_major, 0xcc, sizeof(row_major));
         result = functions.pfnGetCaps(open_arguments.hAdapter,
                                       &row_major_query);
         require_result(
            result == S_OK && row_major.SubCaps[0].MaxElementSize == 16 &&
               row_major.SubCaps[0].BaseOffsetAlignment == 512 &&
               row_major.SubCaps[0].PitchAlignment == 256 &&
               row_major.SubCaps[0].DepthPitchAlignment == 256 &&
               !row_major.SubCaps[1].MaxElementSize &&
               row_major.Flags == D3D12DDI_ROW_MAJOR_LAYOUT_FLAG_NONE,
            "row-major combined-unit layout set");
         info[1] = D3D12DDI_FUNCUNIT_COPY_DST;
         result = functions.pfnGetCaps(open_arguments.hAdapter,
                                       &row_major_query);
         require_result(result == S_OK,
                        "row-major copy-unit layout set");
         info[0] = D3D12DDI_TL_UNDEFINED;
         result = functions.pfnGetCaps(open_arguments.hAdapter,
                                       &row_major_query);
         require_result(result == E_NOTIMPL,
                        "reject unknown texture-layout set");
         info[0] = D3D12DDI_TL_ROW_MAJOR;
         --row_major_query.DataSize;
         result = functions.pfnGetCaps(open_arguments.hAdapter,
                                       &row_major_query);
         require_result(result == E_INVALIDARG,
                        "reject wrong row-major-layout cap width");
      }
   }

#if D12_UMD_TARGETS_WDDM23
   {
      D3D12DDI_TEXTURE_LAYOUT_CAPS_0026 data;
      D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      UINT layout_index = 0;

      memset(&data, 0xcc, sizeof(data));
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == S_OK &&
                        data.DeviceDependentLayoutCount == 0 &&
                        data.DeviceDependentSwizzleCount == 0 &&
                        !data.Supports64KStandardSwizzle &&
                        data.SupportsRowMajorTexture &&
                        !data.IndexableSwizzlePatterns,
                     "DDI 0026 texture-layout caps");
      query.pInfo = &layout_index;
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == E_INVALIDARG,
                     "reject DDI 0022 device-layout query with zero layouts");
      query.pInfo = NULL;
      --query.DataSize;
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == E_INVALIDARG,
                     "reject wrong DDI 0026 texture-layout cap width");
   }

   {
      UINT node = 0;
      D3D12DDI_COMMAND_QUEUE_FLAGS flags =
         (D3D12DDI_COMMAND_QUEUE_FLAGS)UINT32_MAX;
      D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_0022_CPU_PAGE_TABLE_FALSE_POSITIVES,
         .pInfo = &node,
         .pData = &flags,
         .DataSize = sizeof(flags),
      };

      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == S_OK &&
                        flags == D3D12DDI_COMMAND_QUEUE_FLAG_NONE,
                     "no CPU page-table false-positive queues");
      node = 1;
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == E_INVALIDARG,
                     "reject CPU page-table cap for unsupported node");
   }

   {
      D3D12DDICAPS_UMD_BASED_COMMAND_QUEUE_PRIORITY_DATA_0023 data;
      D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_0023_UMD_BASED_COMMAND_QUEUE_PRIORITY,
         .pData = &data,
         .DataSize = sizeof(data),
      };

      memset(&data, 0xcc, sizeof(data));
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(
         result == S_OK &&
            data.SupportedQueueFlagsForGlobalRealtimeQueues ==
               D3D12DDI_COMMAND_QUEUE_FLAG_NONE,
         "global real-time command queues remain unadvertised");
      --query.DataSize;
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == E_INVALIDARG,
                     "reject wrong UMD queue-priority cap width");
   }

   {
      D3D12DDI_PROTECTED_RESOURCE_SESSION_SUPPORT_DATA_0030 data = {
         .NodeIndex = 0,
         .Support =
            D3D12DDI_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAG_0030_SUPPORTED,
      };
      D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_0030_PROTECTED_RESOURCE_SESSION_SUPPORT,
         .pData = &data,
         .DataSize = sizeof(data),
      };

      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(
         result == S_OK && data.NodeIndex == 0 &&
            data.Support ==
               D3D12DDI_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAG_0030_NONE,
         "protected resource sessions remain unadvertised");
      data.NodeIndex = 1;
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == E_INVALIDARG,
                     "reject protected-session cap for unsupported node");
   }
#endif

#if D12_UMD_TARGETS_WDDM24
   {
      BOOL compute_only = TRUE;
      D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_0033_ADAPTER_COMPUTE_ONLY,
         .pData = &compute_only,
         .DataSize = sizeof(compute_only),
      };

      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == S_OK && !compute_only,
                     "AGX adapter exposes the graphics pipeline");
      --query.DataSize;
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == E_INVALIDARG,
                     "reject wrong compute-only cap width");
   }
#endif

#if D12_UMD_TARGETS_WDDM31
   {
      D3D12DDICAPS_HARDWARE_SCHEDULING_CAPS_0050 scheduling = {
         .ComputeQueuesPer3DQueue = UINT_MAX,
      };
      BOOL parallel = TRUE;
      BOOL batched_markers = TRUE;
      D3D12DDICAPS_SAMPLER_FEEDBACK_CAPS_0073 sampler_feedback = {
         .Desc = {
            .Type = D3D12DDI_SHADING_RATE_COMBINER_0073_MINLOD,
            .LODRegion = {.Width = 4, .Height = 4},
         },
         .TargetedResource = {
            .Width = 1024,
            .Height = 1024,
            .DepthOrArraySize = 1,
            .MipLevels = 1,
         },
         .FeedbackMapSize = {.Width = UINT64_MAX, .Height = UINT64_MAX},
      };
      const struct {
         D3D12DDICAPS_TYPE type;
         void *data;
         UINT size;
      } queries[] = {
         {D3D12DDICAPS_TYPE_0050_HARDWARE_SCHEDULING_CAPS,
          &scheduling, sizeof(scheduling)},
         {D3D12DDICAPS_TYPE_EXECUTECOMMANDLISTS_PARALLELISM,
          &parallel, sizeof(parallel)},
         {D3D12DDICAPS_TYPE_0073_SUPPORT_BATCHED_MARKERS,
          &batched_markers, sizeof(batched_markers)},
         {D3D12DDICAPS_TYPE_SAMPLER_FEEDBACK_0073,
          &sampler_feedback, sizeof(sampler_feedback)},
      };

      for (UINT index = 0; index < ARRAYSIZE(queries); ++index) {
         D3D12DDIARG_GETCAPS query = {
            .Type = queries[index].type,
            .pData = queries[index].data,
            .DataSize = queries[index].size,
         };
         result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
         require_result(result == S_OK,
                        "Release 8 scheduling/marker cap query");
         --query.DataSize;
         result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
         require_result(result == E_INVALIDARG,
                        "reject wrong Release 8 scheduling/marker cap width");
      }
      require_result(
         scheduling.ComputeQueuesPer3DQueue == 0 && !parallel &&
            !batched_markers && sampler_feedback.FeedbackMapSize.Width == 0 &&
            sampler_feedback.FeedbackMapSize.Height == 0,
         "Release 8 scheduling/marker features remain unadvertised");
   }

   {
      D3D12DDI_D3D12_OPTIONS_DATA_0089 data;
      const D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_D3D12_OPTIONS,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      memset(&data, 0xcc, sizeof(data));
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(
         result == S_OK && data.ResourceBindingTier == 1 &&
            data.TiledResourcesTier == 1 && data.ResourceHeapTier == 1 &&
            data.OutputMergerLogicOp &&
            data.CopyQueueTimestampQueriesSupported &&
            !data.EnhancedBarriersSupported &&
            data.RaytracingTier == D3D12DDI_RAYTRACING_TIER_NOT_SUPPORTED &&
            data.VariableShadingRateTier ==
               D3D12DDI_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED &&
            data.MeshShaderTier == D3D12DDI_MESH_SHADER_TIER_NOT_SUPPORTED &&
            data.SamplerFeedbackTier ==
               D3D12DDI_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED &&
            !data.BackgroundProcessingSupported &&
            !data.DriverManagedShaderCachePresent,
         "DDI 0089 cumulative option caps");
   }

   {
      D3D12DDI_3DPIPELINESUPPORT1_DATA_0081 data = {
         .HighestRuntimeSupportedFeatureLevel =
            D3D12DDI_3DPIPELINELEVEL_12_1,
      };
      D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == S_OK &&
                        data.MaximumDriverSupportedFeatureLevel ==
                           D3D12DDI_3DPIPELINELEVEL_11_0,
                     "Release 8 pipeline cap clamps runtime maximum");
      data.HighestRuntimeSupportedFeatureLevel =
         D3D12DDI_3DPIPELINELEVEL_1_0_CORE;
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == S_OK &&
                        data.MaximumDriverSupportedFeatureLevel ==
                           D3D12DDI_3DPIPELINELEVEL_1_0_CORE,
                     "Release 8 pipeline cap preserves lower runtime max");
   }

   {
      D3D12DDI_OPTIONS_DATA_0090 options90;
      D3D12DDI_OPTIONS_DATA_0091 options91;
      D3D12DDI_OPTIONS_DATA_0093 options93;
      D3D12DDI_OPTIONS_DATA_0098 options98;
      const struct {
         D3D12DDICAPS_TYPE type;
         void *data;
         UINT size;
      } queries[] = {
         {D3D12DDICAPS_TYPE_OPTIONS_0090, &options90, sizeof(options90)},
         {D3D12DDICAPS_TYPE_OPTIONS_0091, &options91, sizeof(options91)},
         {D3D12DDICAPS_TYPE_OPTIONS_0093, &options93, sizeof(options93)},
         {D3D12DDICAPS_TYPE_OPTIONS_0098, &options98, sizeof(options98)},
      };

      memset(&options90, 0xcc, sizeof(options90));
      memset(&options91, 0xcc, sizeof(options91));
      memset(&options93, 0xcc, sizeof(options93));
      memset(&options98, 0xcc, sizeof(options98));
      for (UINT index = 0; index < ARRAYSIZE(queries); ++index) {
         const D3D12DDIARG_GETCAPS query = {
            .Type = queries[index].type,
            .pData = queries[index].data,
            .DataSize = queries[index].size,
         };
         result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
         require_result(result == S_OK,
                        "Release 8 incremental option cap query");
      }
      require_result(!options90.RelaxedFormatCastingSupported &&
                        !options91.UnrestrictedBufferTextureCopyPitchSupported &&
                        !options91.UnrestrictedVertexElementAlignmentSupported &&
                        !options91.InvertedViewportHeightFlipsYSupported &&
                        !options91.InvertedViewportDepthFlipsZSupported &&
                        !options93.AdvancedTextureOpsSupported &&
                        !options93.WriteableMSAATexturesSupported &&
                        options98.L1MemoryFullyCpuAccessible,
                     "Release 8 incremental option cap values");
   }
#endif

   {
      UINT32 count = 0;
      UINT64 versions[4] = {0};
      const UINT32 expected_count = D12_UMD_TARGETS_WDDM31 ? 2u :
         (D12_UMD_TARGETS_WDDM24_EXACT ? 2u :
            (D12_UMD_TARGETS_WDDM23_EXACT ? 2u :
               (D12_UMD_TARGETS_WDDM21_EXACT ? 4u : 1u)));
      result = functions.pfnGetSupportedVersions(open_arguments.hAdapter,
                                                  &count, NULL);
      require_result(result == S_OK && count == expected_count,
                     "supported-version count query");

      count = 0;
      result = functions.pfnGetSupportedVersions(open_arguments.hAdapter,
                                                  &count, versions);
      require_result(result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) &&
                        count == expected_count,
                     "supported-version short buffer");

      count = ARRAYSIZE(versions);
      result = functions.pfnGetSupportedVersions(open_arguments.hAdapter,
                                                  &count, versions);
      require_result(result == S_OK && count == expected_count &&
#if D12_UMD_TARGETS_WDDM31
                        versions[0] == D3D12DDI_SUPPORTED_0099 &&
                        versions[1] == D3D12DDI_SUPPORTED_0003,
#elif D12_UMD_TARGETS_WDDM24_EXACT
                        versions[0] == D3D12DDI_SUPPORTED_0042 &&
                        versions[1] == D3D12DDI_SUPPORTED_0003,
#elif D12_UMD_TARGETS_WDDM23_EXACT
                        versions[0] == D3D12DDI_SUPPORTED_0032 &&
                        versions[1] == D3D12DDI_SUPPORTED_0003,
#elif D12_UMD_TARGETS_WDDM21
                        versions[0] == D3D12DDI_SUPPORTED_0013 &&
                        versions[1] == D3D12DDI_SUPPORTED_0011 &&
                        versions[2] == D3D12DDI_SUPPORTED_0010 &&
                        versions[3] == D3D12DDI_SUPPORTED_0003,
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
      require_result(result == S_OK && data.UMA && !data.IOCoherent &&
                        !data.CacheCoherent,
                     "memory architecture caps");
   }

#if D12_UMD_TARGETS_WDDM24
   {
      UINT node = 0;
      D3D12DDI_MEMORY_ARCHITECTURE_CAPS_0041 data;
      const D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE,
         .pInfo = &node,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      memset(&data, 0xcc, sizeof(data));
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(
         result == S_OK && data.UMA && !data.IOCoherent &&
            !data.CacheCoherent &&
            data.HeapSerializationTier ==
               D3D12DDI_HEAP_SERIALIZATION_TIER_0041_0 &&
            data.ResourceSerializationTier ==
               D3D12DDI_RESOURCE_SERIALIZATION_TIER_0041_0,
         "DDI 0041 memory architecture caps");
   }
#endif

#if D12_UMD_TARGETS_WDDM31
   {
      D3D12DDI_SHADER_CAPS_0084 data;
      const D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_SHADER,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      memset(&data, 0xcc, sizeof(data));
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(
         result == S_OK &&
            data.MinPrecision == D3D12DDI_SHADER_MIN_PRECISION_NONE &&
            !data.DoubleOps && !data.ShaderSpecifiedStencilRef &&
            data.TypedUAVLoadAdditionalFormats && !data.ROVs &&
            data.WaveOps && data.WaveLaneCountMin == 32 &&
            data.WaveLaneCountMax == 32 && data.TotalLaneCount == 32 &&
            !data.Int64Ops && data.Native16BitOps &&
            !data.AtomicInt64OnTypedResource &&
            !data.AtomicInt64OnGroupShared &&
            !data.DerivativesInMeshAndAmplificationShaders &&
            data.WaveMMATier == D3D12DDI_WAVE_MMA_TIER_NOT_SUPPORTED &&
            !data.AtomicInt64OnDescriptorHeapResource,
         "DDI 0084 shader caps");
   }
#endif

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
                        data.ResourceHeapTier == 1 &&
                        data.OutputMergerLogicOp,
                     "D3D12 option caps");
   }

#if D12_UMD_TARGETS_WDDM23
   {
      D3D12DDI_D3D12_OPTIONS_DATA_0032 data;
      const D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_D3D12_OPTIONS,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      memset(&data, 0, sizeof(data));
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(
         result == S_OK && data.ResourceBindingTier == 1 &&
            data.TiledResourcesTier == 1 && data.ResourceHeapTier == 1 &&
            data.OutputMergerLogicOp &&
            !data.DepthBoundsTestSupported &&
            data.ProgrammableSamplePositionsTier ==
               D3D12DDI_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_NOT_SUPPORTED &&
            data.CopyQueueTimestampQueriesSupported &&
            data.WriteBufferImmediateQueueFlags ==
               (D3D12DDI_COMMAND_QUEUE_FLAG_3D |
                D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE |
                D3D12DDI_COMMAND_QUEUE_FLAG_COPY),
         "DDI 0032 cumulative option caps");
   }
#endif

#if D12_UMD_TARGETS_WDDM24
   {
      D3D12DDI_D3D12_OPTIONS_DATA_0041 data;
      const D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_D3D12_OPTIONS,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      memset(&data, 0xcc, sizeof(data));
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(
         result == S_OK && data.ResourceBindingTier == 1 &&
            data.TiledResourcesTier == 1 && data.ResourceHeapTier == 1 &&
            data.OutputMergerLogicOp &&
            data.CopyQueueTimestampQueriesSupported &&
            data.WriteBufferImmediateQueueFlags ==
               (D3D12DDI_COMMAND_QUEUE_FLAG_3D |
                D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE |
                D3D12DDI_COMMAND_QUEUE_FLAG_COPY) &&
            data.ViewInstancingTier ==
               D3D12DDI_VIEW_INSTANCING_TIER_NOT_SUPPORTED &&
            !data.BarycentricsSupported &&
            !data.ReservedBufferPlacementSupported &&
            !data.Deterministic64KBUndefinedSwizzle,
         "DDI 0041 cumulative option caps");
   }
#endif

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
      UINT node = 0;
      D3D12DDI_GPUVA_CAPS_0004 data = {0};
      D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_GPUVA_CAPS,
         .pInfo = &node,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == S_OK &&
                        data.MaxGPUVirtualAddressBitsPerResource == 39,
                     "GPU virtual-address cap");

      node = 1;
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == E_INVALIDARG,
                     "reject unsupported GPUVA node");
      node = 0;
      --query.DataSize;
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == E_INVALIDARG,
                     "reject wrong GPUVA cap width");
   }

   {
      D3D12DDI_SHADER_CAPS_0003 data;
      D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_SHADER,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      memset(&data, 0xcc, sizeof(data));
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == S_OK &&
                        data.MinPrecision ==
                           D3D12DDI_SHADER_MIN_PRECISION_NONE &&
                        !data.Doubles &&
                        !data.ShaderSpecifiedStencilRef &&
                        data.TypedUAVLoadAdditionalFormats && !data.ROVs,
                     "typed-UAV shader caps");

      --query.DataSize;
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(result == E_INVALIDARG,
                     "reject wrong shader cap width");

      {
         D3D12DDI_SHADER_CAPS_0012 expanded;
         memset(&expanded, 0xcc, sizeof(expanded));
         query.pData = &expanded;
         query.DataSize = sizeof(expanded);
         result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
#if D12_UMD_TARGETS_WDDM21
         require_result(result == S_OK &&
                           expanded.MinPrecision ==
                              D3D12DDI_SHADER_MIN_PRECISION_NONE &&
                           !expanded.Doubles &&
                           !expanded.ShaderSpecifiedStencilRef &&
                           expanded.TypedUAVLoadAdditionalFormats &&
                           !expanded.ROVs && expanded.WaveOps &&
                           expanded.WaveLaneCountMin == 32 &&
                           expanded.WaveLaneCountMax == 32 &&
                           expanded.TotalLaneCount == 32,
                        "Release 1 build 2 shader caps");
#else
         require_result(result == E_INVALIDARG,
                        "reject unadvertised expanded shader caps");
#endif
      }
   }

#if D12_UMD_TARGETS_WDDM24
   {
      D3D12DDI_SHADER_CAPS_0042 data;
      const D3D12DDIARG_GETCAPS query = {
         .Type = D3D12DDICAPS_TYPE_SHADER,
         .pData = &data,
         .DataSize = sizeof(data),
      };
      memset(&data, 0xcc, sizeof(data));
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(
         result == S_OK &&
            data.MinPrecision == D3D12DDI_SHADER_MIN_PRECISION_NONE &&
            !data.DoubleOps && !data.ShaderSpecifiedStencilRef &&
            data.TypedUAVLoadAdditionalFormats && !data.ROVs &&
            data.WaveOps && data.WaveLaneCountMin == 32 &&
            data.WaveLaneCountMax == 32 && data.TotalLaneCount == 32 &&
            !data.Int64Ops && data.Native16BitOps,
         "DDI 0042 native 16-bit shader caps");
   }
#endif

   {
      UINT count = 0;
#if D12_UMD_TARGETS_WDDM21
#if D12_UMD_TARGETS_WDDM24
      D3D12DDI_SHADER_MODEL models[4] = {0};
#else
      D3D12DDI_SHADER_MODEL models[2] = {0};
#endif
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
      require_result(result == S_OK &&
#if D12_UMD_TARGETS_WDDM24
                        count == 4,
#else
                        count == 2,
#endif
                     "shader-model count query");
      data.pShaderModelsSupported = models;
      count = ARRAYSIZE(models);
      result = functions.pfnGetCaps(open_arguments.hAdapter, &query);
      require_result(
         result == S_OK &&
#if D12_UMD_TARGETS_WDDM24
            count == 4 &&
            models[0] == D3D12DDI_SHADER_MODEL_6_2_RELEASE_0042 &&
            models[1] == D3D12DDI_SHADER_MODEL_6_1_RELEASE_0033 &&
            models[2] == D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011 &&
            models[3] == D3D12DDI_SHADER_MODEL_5_1_RELEASE_0011,
#else
            count == 2 &&
            models[0] == D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011 &&
            models[1] == D3D12DDI_SHADER_MODEL_5_1_RELEASE_0011,
#endif
         "shader-model payload");
#else
      require_result(result == E_NOTIMPL,
                     "WDDM 2.0 does not expose DXIL shader-model caps");
#endif
   }

   {
      D3D12DDIARG_CALCPRIVATEDEVICESIZE size_arguments = {
         .Interface = D12_TEST_PRIMARY_INTERFACE,
         .Version = D12_TEST_PRIMARY_BUILD,
         .Flags = D3D12DDI_CREATE_DEVICE_FLAG_NONE,
      };
      D3D12DDI_DEVICE_FUNCS_CORE_0003 device_table;
      d12_test_extended_device_table release1_table;
#if D12_UMD_TARGETS_WDDM21
      d12_test_root_device_table root11_table;
#endif
      d12_test_command_list_table command_list_table;
      D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 command_queue_table;
#if D12_UMD_TARGETS_WDDM31
      D3D12DDI_EXTENDED_FEATURES_FUNCS_0096 extended_features_table;
#endif
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
      d12_test_core_callbacks core_callbacks;
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
      require_result(result == S_OK, "fill extended device core table");
#if D12_UMD_TARGETS_WDDM31
      require_release8_device_table(&release1_table,
                                    "Release 8 device core table");
#else
      require_pointer_slots(&release1_table,
                            D12_UMD_TARGETS_WDDM24 ? 96 :
                               (D12_UMD_TARGETS_WDDM23_EXACT ? 92 : 91),
                            SIZE_MAX, SIZE_MAX,
                            "extended device core table");
#endif
      result = functions.pfnFillDDITable(
         open_arguments.hAdapter, D3D12DDI_TABLE_TYPE_DEVICE_CORE,
         &root11_table, sizeof(root11_table), 0,
         (D3D12DDI_HRTTABLE){0});
      require_result(result == S_OK,
                     "fill root-signature 1.1 device core table");
#if D12_UMD_TARGETS_WDDM31
      require_release8_device_table(&root11_table,
                                    "Release 8 root device core table");
#else
      require_pointer_slots(&root11_table,
                            D12_UMD_TARGETS_WDDM24 ? 96 : 92,
                            SIZE_MAX, SIZE_MAX,
                            "root-signature 1.1 device core table");
#endif
#if D12_UMD_TARGETS_WDDM24
      {
         D3D12DDI_DEVICE_FUNCS_CORE_0030 release3_device_table;
         D3D12DDI_COMMAND_LIST_FUNCS_3D_0032 release3_command_table;

         result = functions.pfnFillDDITable(
            open_arguments.hAdapter, D3D12DDI_TABLE_TYPE_DEVICE_CORE,
            &release3_device_table, sizeof(release3_device_table), 0,
            (D3D12DDI_HRTTABLE){0});
         require_result(result == E_INVALIDARG,
                        "DDI 0042 rejects Release 3 device table width");
         result = functions.pfnFillDDITable(
            open_arguments.hAdapter,
            D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D,
            &release3_command_table, sizeof(release3_command_table), 0,
            (D3D12DDI_HRTTABLE){.handle = TEST_RT_COMMAND_LIST_TABLE});
         require_result(result == E_INVALIDARG,
                        "DDI 0042 rejects Release 3 command table width");
      }
#endif
#else
      require_result(result == E_INVALIDARG,
                     "WDDM 2.0 rejects the DXIL Release 1 table");
#endif

      result = functions.pfnFillDDITable(
         open_arguments.hAdapter, D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D,
         &command_list_table, sizeof(command_list_table), 0,
         (D3D12DDI_HRTTABLE){.handle = TEST_RT_COMMAND_LIST_TABLE});
      require_result(result == S_OK, "fill command-list table");
#if D12_UMD_TARGETS_WDDM31
      require_release8_command_table(&command_list_table,
                                     "Release 8 command-list table");
#else
      require_pointer_slots(&command_list_table,
                            D12_UMD_TARGETS_WDDM24 ? 58 :
                               (D12_UMD_TARGETS_WDDM23_EXACT ? 57 : 51),
                            SIZE_MAX, SIZE_MAX,
                            "command-list table");
#endif
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
#if D12_UMD_TARGETS_WDDM31
      result = functions.pfnFillDDITable(
         open_arguments.hAdapter,
         D3D12DDI_TABLE_TYPE_0096_EXTENDED_FEATURES,
         &extended_features_table, sizeof(extended_features_table), 0,
         (D3D12DDI_HRTTABLE){0});
      require_result(result == S_OK,
                     "fill Release 8 extended-feature table");
      require_pointer_slots(&extended_features_table, 4, SIZE_MAX, SIZE_MAX,
                            "Release 8 extended-feature table");
      result = functions.pfnFillDDITable(
         open_arguments.hAdapter,
         D3D12DDI_TABLE_TYPE_0096_EXTENDED_FEATURES,
         &extended_features_table, sizeof(extended_features_table) - 1, 0,
         (D3D12DDI_HRTTABLE){0});
      require_result(result == E_INVALIDARG,
                     "reject wrong extended-feature table width");
      result = functions.pfnFillDDITable(
         open_arguments.hAdapter,
         D3D12DDI_TABLE_TYPE_0096_EXTENDED_FEATURES,
         &extended_features_table, sizeof(extended_features_table), 1,
         (D3D12DDI_HRTTABLE){0});
      require_result(result == E_INVALIDARG,
                     "reject unsupported extended-feature table number");
#endif
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
      core_callbacks.pfnSetCommandListErrorCb = set_command_list_error;
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
         .Interface = D12_TEST_PRIMARY_INTERFACE,
         .Version = D12_TEST_PRIMARY_BUILD,
         .pKTCallbacks = &kernel_callbacks,
         .hDrvDevice = driver_device,
      };
#if D12_UMD_TARGETS_WDDM23
      create_arguments.p12UMCallbacks_0022 = &core_callbacks;
#else
      create_arguments.p12UMCallbacks = &core_callbacks;
#endif
      core_callbacks.pfnSetCommandListErrorCb = NULL;
      result = functions.pfnCreateDevice(open_arguments.hAdapter,
                                         &create_arguments);
      require_result(result == E_INVALIDARG,
                     "CreateDevice requires command-list error callback");
      core_callbacks.pfnSetCommandListErrorCb = set_command_list_error;
      result = functions.pfnCreateDevice(open_arguments.hAdapter,
                                         &create_arguments);
      require_result(result == DXGI_STATUS_NO_REDIRECTION,
                     "CreateDevice explicit-present result");

#if D12_UMD_TARGETS_WDDM31
      {
         UINT meta_command_count = UINT_MAX;
         result = release1_table.pfnEnumerateMetaCommands(
            driver_device, &meta_command_count, NULL);
         require_result(result == S_OK && meta_command_count == 0,
                        "unadvertised meta-command enumeration is empty");
         result = release1_table.pfnEnumerateMetaCommands(
            driver_device, NULL, NULL);
         require_result(result == E_INVALIDARG,
                        "meta-command enumeration requires count output");
      }

      {
         UINT32 feature_count = UINT_MAX;
         UINT32 version_count = UINT_MAX;
         result = extended_features_table.pfnGetSupportedExtendedFeatures(
            driver_device, D3D12DDI_FEATURE_0084_STATE_OBJECTS_EXPERIMENT,
            &feature_count, NULL);
         require_result(result == S_OK && feature_count == 0,
                        "extended-feature enumeration is empty");
         result = extended_features_table.pfnGetSupportedExtendedFeatures(
            driver_device, D3D12DDI_FEATURE_0084_STATE_OBJECTS_EXPERIMENT,
            NULL, NULL);
         require_result(result == E_INVALIDARG,
                        "extended-feature enumeration requires count");
         result =
            extended_features_table.pfnGetSupportedExtendedFeatureVersions(
               driver_device, D3D12DDI_FEATURE_0020_VIDEO,
               &version_count, NULL);
         require_result(result == S_OK && version_count == 0,
                        "extended-feature version enumeration is empty");
         result = extended_features_table.pfnEnableExtendedFeature(
            driver_device, D3D12DDI_FEATURE_0020_VIDEO, 0);
         require_result(result == E_NOTIMPL,
                        "unadvertised extended feature fails closed");
         result = extended_features_table.pfnSetExtendedFeatureCallbacks(
            driver_device,
            D3D12DDI_TABLE_TYPE_0084_STATE_OBJECTS_EXPERIMENT, NULL, 0);
         require_result(result == E_NOTIMPL,
                        "unadvertised extended callbacks fail closed");
      }
#endif

#if D12_UMD_TARGETS_WDDM24
      {
         struct d12_umd_device *private_device =
            driver_device.pDrvPrivate;
         struct d12_device *saved_backend = private_device->backend;
         const struct d12_umd_renderer_ops *saved_renderer_ops =
            private_device->renderer_ops;
         const D3D12DDIARG_CREATE_COMMAND_POOL_0040 pool_arguments = {
            .PoolFlags = D3D12DDI_COMMAND_POOL_FLAG_NONE,
         };
         const D3D12DDIARG_CREATE_COMMAND_RECORDER_0040 recorder_arguments = {
            .QueueFlags = D3D12DDI_COMMAND_QUEUE_FLAG_3D,
            .RecorderFlags = D3D12DDI_COMMAND_RECORDER_FLAG_NONE,
         };
         const D3D12DDIARG_CREATE_COMMAND_LIST_0040 list_arguments = {
            .Type = D3D12DDI_COMMAND_LIST_TYPE_DIRECT,
            .QueueFlags = D3D12DDI_COMMAND_QUEUE_FLAG_3D,
            .ID = UINT64_C(0x41),
            .CommandListFlags = D3D12DDI_COMMAND_LIST_FLAG_NONE,
            .NodeMask = 1,
         };
         const D3D12DDIARG_RESETCOMMANDLIST_0040 first_reset = {
            .ID = UINT64_C(0x42),
            .CommandListFlags = D3D12DDI_COMMAND_LIST_FLAG_NONE,
         };
         D3D12DDIARG_RESETCOMMANDLIST_0040 reset = first_reset;
         D3D12DDI_HCOMMANDPOOL_0040 pool;
         D3D12DDI_HCOMMANDRECORDER_0040 recorder;
         D3D12DDI_HCOMMANDLIST list;
         SIZE_T pool_size;
         SIZE_T recorder_size;
         SIZE_T list_size;

         private_device->backend = (struct d12_device *)TEST_BLT_DEVICE;
         private_device->renderer_ops = &test_blt_renderer_ops;
         blt_allocator_create_calls = 0;
         blt_allocator_reset_calls = 0;
         blt_allocator_destroy_calls = 0;
         blt_list_create_calls = 0;
         blt_list_reset_calls = 0;
         blt_list_close_calls = 0;
         blt_list_destroy_calls = 0;
         blt_allocator_create_result = S_OK;
         blt_list_create_result = S_OK;
         blt_allocator_output = TEST_BLT_ALLOCATOR;
         blt_list_output = TEST_BLT_LIST;

         pool_size = release1_table.pfnCalcPrivateCommandPoolSize(
            driver_device, &pool_arguments);
         recorder_size =
            release1_table.pfnCalcPrivateCommandRecorderSize(
               driver_device, &recorder_arguments);
         list_size = release1_table.pfnCalcPrivateCommandListSize(
            driver_device, &list_arguments);
         require_result(pool_size > 0 && recorder_size > 0 && list_size > 0,
                        "DDI 0040 command private sizes");
         pool.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                      pool_size);
         recorder.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                          recorder_size);
         list.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                      list_size);
         require_result(pool.pDrvPrivate && recorder.pDrvPrivate &&
                           list.pDrvPrivate,
                        "allocate DDI 0040 command private storage");
         result = release1_table.pfnCreateCommandPool(
            driver_device, &pool_arguments, pool);
         require_result(result == S_OK,
                        "create DDI 0040 command pool");
         private_device->renderer_ops = NULL;
         set_error_calls = 0;
         release1_table.pfnResetCommandPool(driver_device, pool);
         require_result(set_error_calls == 0,
                        "reset empty DDI 0040 command pool is a no-op");
         private_device->renderer_ops = &test_blt_renderer_ops;
         result = release1_table.pfnCreateCommandRecorder(
            driver_device, &recorder_arguments, recorder);
         require_result(result == S_OK,
                        "create DDI 0040 command recorder");
         {
            const D3D12DDI_HRTPAGINGQUEUE runtime_paging_queue = {
               .handle = TEST_RT_PAGING_QUEUE,
            };
            const D3D12DDI_HANDLE_AND_TYPE command_objects[] = {
               {
                  .Handle = pool.pDrvPrivate,
                  .Type = D3D12DDI_HT_0040_COMMAND_POOL,
               },
               {
                  .Handle = recorder.pDrvPrivate,
                  .Type = D3D12DDI_HT_0040_COMMAND_RECORDER,
               },
            };
            UINT64 paging_fence = UINT64_MAX;
            D3D12DDIARG_MAKERESIDENT_0001 make_arguments = {
               .NumAdapters = 1,
               .pRTPagingQueue = &runtime_paging_queue,
               .NumObjects = ARRAYSIZE(command_objects),
               .pObjects = command_objects,
               .pPagingFenceValue = &paging_fence,
            };
            const D3D12DDIARG_EVICT evict_arguments = {
               .NumObjects = ARRAYSIZE(command_objects),
               .pObjects = command_objects,
            };

            result = release1_table.pfnMakeResident(driver_device,
                                                     &make_arguments);
            require_result(result == S_OK && make_resident_calls == 0 &&
                              make_arguments.WaitMask == 0 &&
                              paging_fence == 0,
                           "DDI 0040 command object residency is a "
                           "validated no-op");
            result = release1_table.pfnEvict(driver_device,
                                              &evict_arguments);
            require_result(result == S_OK && evict_calls == 0,
                           "DDI 0040 command object eviction is a "
                           "validated no-op");

            for (UINT index = 0; index < ARRAYSIZE(command_objects);
                 ++index) {
               D3D12DDI_DEBUG_VIRTUAL_ADDRESS_ALLOCATION_INFO_0012 va_info;
               d12_test_debug_kmt_allocation_info kmt_info;
               UINT va_count = 1;
               UINT kmt_count = 1;

               memset(&va_info, 0xcc, sizeof(va_info));
               memset(&kmt_info, 0xcc, sizeof(kmt_info));
               root11_table.pfnGetDebugAllocationInfo(
                  driver_device, command_objects[index], &va_count,
                  &va_info, &kmt_count, &kmt_info);
               require_result(va_count == 0 && kmt_count == 0,
                              "DDI 0040 command object has no "
                              "runtime-owned allocation");
            }
         }
         release1_table.pfnCommandRecorderSetCommandPoolAsTarget(
            driver_device, recorder, pool);
         result = release1_table.pfnCreateCommandList(
            driver_device, &list_arguments, list,
            (D3D12DDI_HRTCOMMANDLIST){.handle = TEST_RT_COMMAND_LIST});
         require_result(result == S_OK &&
                           blt_allocator_create_calls == 0 &&
                           blt_list_create_calls == 0,
                        "DDI 0040 list creation remains allocation-free");

         reset.hDrvCommandRecorder = recorder;
         command_list_table.pfnResetCommandList(list, &reset);
         require_result(blt_allocator_create_calls == 1 &&
                           blt_list_create_calls == 1 &&
                           blt_allocator_reset_calls == 0 &&
                           blt_list_reset_calls == 0,
                        "DDI 0040 first reset binds pool lazily");
         command_list_table.pfnCloseCommandList(list);
         release1_table.pfnResetCommandPool(driver_device, pool);
         reset.ID = UINT64_C(0x43);
         command_list_table.pfnResetCommandList(list, &reset);
         command_list_table.pfnCloseCommandList(list);
         require_result(blt_allocator_create_calls == 1 &&
                           blt_list_create_calls == 1 &&
                           blt_allocator_reset_calls == 1 &&
                           blt_list_reset_calls == 1 &&
                           blt_list_close_calls == 2,
                        "DDI 0040 pool/list steady state avoids churn");
         set_command_list_error_calls = 0;
         command_list_table.pfnSetViewInstanceMask(list, 1);
         require_result(set_command_list_error_calls == 1 &&
                           last_set_command_list_error ==
                              D3DDDIERR_APPLICATIONERROR,
                        "unsupported view-instance mask fails explicitly");

         release1_table.pfnDestroyCommandList(driver_device, list);
         release1_table.pfnDestroyCommandRecorder(driver_device, recorder);
         release1_table.pfnDestroyCommandPool(driver_device, pool);
         require_result(blt_list_destroy_calls == 1 &&
                           blt_allocator_destroy_calls == 1,
                        "destroy DDI 0040 command ownership graph");
         HeapFree(GetProcessHeap(), 0, list.pDrvPrivate);
         HeapFree(GetProcessHeap(), 0, recorder.pDrvPrivate);
         HeapFree(GetProcessHeap(), 0, pool.pDrvPrivate);
         private_device->renderer_ops = saved_renderer_ops;
         private_device->backend = saved_backend;
      }

      {
         D3D12DDI_VIEW_INSTANCE_LOCATION location = {0};
#if D12_UMD_TARGETS_WDDM31
         D3D12DDIARG_CREATE_PIPELINE_STATE_0099 pipeline_arguments = {
#else
         D3D12DDIARG_CREATE_PIPELINE_STATE_0033 pipeline_arguments = {
#endif
            .ViewInstancingDesc = {
               .ViewInstanceCount = 1,
               .pViewInstanceLocations = &location,
               .Flags = D3D12DDI_VIEW_INSTANCING_FLAG_NONE,
            },
         };
         require_result(
            release1_table.pfnCalcPrivatePipelineStateSize(
               driver_device, &pipeline_arguments) == 0,
            "reject unsupported DDI 0033 view-instanced PSO size");
         result = release1_table.pfnCreatePipelineState(
            driver_device, &pipeline_arguments,
            (D3D12DDI_HPIPELINESTATE){.pDrvPrivate = &pipeline_arguments},
            (D3D12DDI_HRTPIPELINESTATE){.handle = TEST_RT_COMMAND_LIST});
         require_result(result == E_INVALIDARG,
                        "reject unsupported DDI 0033 view-instanced PSO");
      }
#endif

      {
         struct d12_umd_command_list fake_list = {
            .signature = D12_UMD_COMMAND_LIST_SIGNATURE,
            .device = driver_device.pDrvPrivate,
            .type = D12_UMD_RENDERER_COMMAND_DIRECT,
            .runtime_list = {.handle = TEST_RT_COMMAND_LIST},
            .backend = (void *)(uintptr_t)1,
         };
         const D3D12DDI_HCOMMANDLIST fake_handle = {
            .pDrvPrivate = &fake_list,
         };

         set_error_calls = 0;
         set_command_list_error_calls = 0;
         set_error_runtime_command_list_valid = FALSE;
         last_set_command_list_error = S_OK;
         command_list_table.pfnRsSetViewports(
            fake_handle,
            D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE + 1,
            NULL);
         require_result(
            set_command_list_error_calls == 1 &&
               set_error_runtime_command_list_valid &&
               last_set_command_list_error ==
                  D3DDDIERR_APPLICATIONERROR &&
               set_error_calls == 0,
            "recording validation uses command-list error callback");

         {
            const D3D12DDI_VIEWPORT invalid_viewport = {
               .TopLeftX = 0.0f,
               .TopLeftY = 0.0f,
               .Width = -1.0f,
               .Height = 1.0f,
               .MinDepth = 0.0f,
               .MaxDepth = 1.0f,
            };
            command_list_table.pfnRsSetViewports(fake_handle, 1,
                                                  &invalid_viewport);
            require_result(
               set_command_list_error_calls == 2 &&
                  last_set_command_list_error ==
                     D3DDDIERR_APPLICATIONERROR &&
                  set_error_calls == 0,
               "invalid viewport remains command-list scoped");
         }

         command_list_table.pfnDispatch(fake_handle, 1, 1, 1);
         require_result(
            set_command_list_error_calls == 3 &&
               last_set_command_list_error ==
                  D3DDDIERR_APPLICATIONERROR &&
               set_error_calls == 0,
            "missing recorder operation remains command-list scoped");

#if D12_UMD_TARGETS_WDDM23
         {
            struct d12_umd_device *private_device =
               driver_device.pDrvPrivate;
            const struct d12_umd_renderer_ops *saved_renderer_ops =
               private_device->renderer_ops;
            struct d12_umd_resource destination_resource = {
               .signature = D12_UMD_RESOURCE_SIGNATURE,
               .device = private_device,
               .backend = TEST_BLT_DESTINATION,
            };
            struct d12_umd_resource source_resource = {
               .signature = D12_UMD_RESOURCE_SIGNATURE,
               .device = private_device,
               .backend = TEST_BLT_SOURCE,
            };
            D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032 parameters[2] = {
               {.Dst = UINT64_C(0x2000000040), .Value = UINT32_C(0x12345678)},
               {.Dst = UINT64_C(0x2000000080), .Value = UINT32_C(0xabcdef01)},
            };
            D3D12DDI_WRITEBUFFERIMMEDIATE_MODE_0032 modes[2] = {
               D3D12DDI_WRITEBUFFERIMMEDIATE_MODE_DEFAULT,
               D3D12DDI_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT,
            };
            D3D12DDIARG_RESOURCE_BARRIER_0022 ranged_barrier = {
               .Type = D3D12DDI_RESOURCE_BARRIER_TYPE_0022_RANGED,
               .Flags = D3D12DDI_RESOURCE_BARRIER_FLAG_0022_ALIASING,
               .Ranged = {
                  .hResource = {0},
                  .Subresource = 0,
                  .Range = {.Begin = 64, .End = 128},
               },
            };
            D3D12DDI_RECT source_rect = {1, 2, 9, 10};

            private_device->renderer_ops = &test_revision_renderer_ops;
            revision_immediate_write_calls = 0;
            revision_ranged_barrier_calls = 0;
            revision_resolve_region_calls = 0;
            set_command_list_error_calls = 0;
            command_list_table.pfnWriteBufferImmediate(
               fake_handle, ARRAYSIZE(parameters), parameters, modes);
            command_list_table.pfnResourceBarrier(fake_handle, 1,
                                                   &ranged_barrier);
            command_list_table.pfnResourceResolveSubresourceRegion(
               fake_handle,
               (D3D12DDI_HRESOURCE){.pDrvPrivate = &destination_resource},
               1, 4, 8,
               (D3D12DDI_HRESOURCE){.pDrvPrivate = &source_resource},
               2, &source_rect, DXGI_FORMAT_R8G8B8A8_UNORM,
               D3D12DDI_RESOLVE_MODE_AVERAGE);
            require_result(
               revision_immediate_write_calls == 1 &&
                  revision_ranged_barrier_calls == 1 &&
                  revision_resolve_region_calls == 1 &&
                  set_command_list_error_calls == 0,
               "DDI 0032 command translation");
            {
               D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032
                  warm_parameters[33];
               D3D12DDI_WRITEBUFFERIMMEDIATE_MODE_0032 warm_modes[33];
               D3D12DDIARG_RESOURCE_BARRIER_0022 warm_barriers[17];

               for (UINT index = 0; index < ARRAYSIZE(warm_parameters);
                    ++index) {
                  warm_parameters[index] =
                     (D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032){
                        .Dst = UINT64_C(0x3000000000) +
                               index * sizeof(uint32_t),
                        .Value = index,
                     };
                  warm_modes[index] =
                     D3D12DDI_WRITEBUFFERIMMEDIATE_MODE_DEFAULT;
               }
               for (UINT index = 0; index < ARRAYSIZE(warm_barriers);
                    ++index) {
                  warm_barriers[index] =
                     (D3D12DDIARG_RESOURCE_BARRIER_0022){
                        .Type = D3D12DDI_RESOURCE_BARRIER_TYPE_UAV,
                     };
               }

               command_list_table.pfnWriteBufferImmediate(
                  fake_handle, ARRAYSIZE(warm_parameters), warm_parameters,
                  warm_modes);
               command_list_table.pfnResourceBarrier(
                  fake_handle, ARRAYSIZE(warm_barriers), warm_barriers);
               void *parameter_scratch = fake_list.write_parameter_scratch;
               void *mode_scratch = fake_list.write_mode_scratch;
               void *barrier_scratch = fake_list.resource_barrier_scratch;

               command_list_table.pfnWriteBufferImmediate(
                  fake_handle, ARRAYSIZE(warm_parameters), warm_parameters,
                  warm_modes);
               command_list_table.pfnResourceBarrier(
                  fake_handle, ARRAYSIZE(warm_barriers), warm_barriers);
               require_result(
                  revision_immediate_write_calls == 3 &&
                     revision_ranged_barrier_calls == 3 &&
                     parameter_scratch == fake_list.write_parameter_scratch &&
                     mode_scratch == fake_list.write_mode_scratch &&
                     barrier_scratch == fake_list.resource_barrier_scratch &&
                     parameter_scratch && mode_scratch && barrier_scratch,
                  "warm command recording reuses translation storage");
            }
            command_list_table.pfnAtomicCopyBufferRegion(
               fake_handle, (D3D12DDIARG_BUFFER_PLACEMENT){0},
               (D3D12DDIARG_BUFFER_PLACEMENT){0}, 4);
            require_result(set_command_list_error_calls == 1 &&
                              last_set_command_list_error ==
                                 D3DDDIERR_APPLICATIONERROR,
                           "unadvertised atomic copy fails explicitly");
            HeapFree(GetProcessHeap(), 0,
                     fake_list.resource_barrier_scratch);
            HeapFree(GetProcessHeap(), 0, fake_list.write_parameter_scratch);
            HeapFree(GetProcessHeap(), 0, fake_list.write_mode_scratch);
            fake_list.resource_barrier_scratch = NULL;
            fake_list.write_parameter_scratch = NULL;
            fake_list.write_mode_scratch = NULL;
            private_device->renderer_ops = saved_renderer_ops;
         }
#endif
      }

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
         {
            const DXGI_FORMAT typed_uav_required[] = {
               DXGI_FORMAT_R32_FLOAT,
               DXGI_FORMAT_R32_UINT,
               DXGI_FORMAT_R32_SINT,
               DXGI_FORMAT_R32G32B32A32_FLOAT,
               DXGI_FORMAT_R32G32B32A32_UINT,
               DXGI_FORMAT_R32G32B32A32_SINT,
               DXGI_FORMAT_R16G16B16A16_FLOAT,
               DXGI_FORMAT_R16G16B16A16_UINT,
               DXGI_FORMAT_R16G16B16A16_SINT,
               DXGI_FORMAT_R8G8B8A8_UNORM,
               DXGI_FORMAT_R8G8B8A8_UINT,
               DXGI_FORMAT_R8G8B8A8_SINT,
               DXGI_FORMAT_R16_FLOAT,
               DXGI_FORMAT_R16_UINT,
               DXGI_FORMAT_R16_SINT,
               DXGI_FORMAT_R8_UNORM,
               DXGI_FORMAT_R8_UINT,
               DXGI_FORMAT_R8_SINT,
            };
            for (UINT index = 0; index < ARRAYSIZE(typed_uav_required);
                 ++index) {
               format_support = 0;
               device_table.pfnCheckFormatSupport(
                  driver_device, typed_uav_required[index], &format_support);
               require_result(
                  (format_support & D3D12DDI_FORMAT_SUPPORT_UAV_READS) &&
                     (format_support & D3D12DDI_FORMAT_SUPPORT_UAV_WRITES),
                  "mandatory FL12 typed UAV format is readable and writable");
            }
         }
         {
            const DXGI_FORMAT display_formats[] = {
               DXGI_FORMAT_R16G16B16A16_FLOAT,
               DXGI_FORMAT_R10G10B10A2_UNORM,
               DXGI_FORMAT_R8G8B8A8_UNORM,
               DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
               DXGI_FORMAT_B5G6R5_UNORM,
               DXGI_FORMAT_B5G5R5A1_UNORM,
               DXGI_FORMAT_B8G8R8A8_UNORM,
               DXGI_FORMAT_B8G8R8X8_UNORM,
               DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            };
            for (UINT index = 0; index < ARRAYSIZE(display_formats);
                 ++index) {
               format_support = 0;
               device_table.pfnCheckFormatSupport(
                  driver_device, display_formats[index], &format_support);
               require_result(
                  !!(format_support & D3D12DDI_FORMAT_SUPPORT_DISPLAY),
                  "all native display formats reach the DDI table");
            }
         }
         format_support = 0;
         device_table.pfnCheckFormatSupport(
            driver_device, DXGI_FORMAT_R32_UINT, &format_support);
         require_result(
            (format_support & D3D12DDI_FORMAT_SUPPORT_RENDERTARGET) &&
               !(format_support & D3D12DDI_FORMAT_SUPPORT_BLENDABLE),
            "integer render targets are not advertised as blendable");
         {
            const DXGI_FORMAT logic_op_required[] = {
               DXGI_FORMAT_R32G32B32A32_UINT,
               DXGI_FORMAT_R32G32B32_UINT,
               DXGI_FORMAT_R16G16B16A16_UINT,
               DXGI_FORMAT_R32G32_UINT,
               DXGI_FORMAT_R10G10B10A2_UINT,
               DXGI_FORMAT_R8G8B8A8_UINT,
               DXGI_FORMAT_R16G16_UINT,
               DXGI_FORMAT_R32_UINT,
               DXGI_FORMAT_R8G8_UINT,
               DXGI_FORMAT_R16_UINT,
               DXGI_FORMAT_R8_UINT,
            };

            for (UINT index = 0; index < ARRAYSIZE(logic_op_required);
                 ++index) {
               format_support = 0;
               device_table.pfnCheckFormatSupport(
                  driver_device, logic_op_required[index], &format_support);
               require_result(
                  !!(format_support &
                     D3D12DDI_FORMAT_SUPPORT_OUTPUT_MERGER_LOGIC_OP),
                  "mandatory FL12 UINT format supports logic operations");
            }

            format_support = 0;
            device_table.pfnCheckFormatSupport(
               driver_device, DXGI_FORMAT_R8G8B8A8_UNORM, &format_support);
            require_result(
               !(format_support &
                 D3D12DDI_FORMAT_SUPPORT_OUTPUT_MERGER_LOGIC_OP),
               "logic-op format support is restricted to required UINTs");
         }
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

         {
            D3D12DDI_DESCRIPTOR_RANGE ranges[17] = {0};
            for (UINT index = 0; index < ARRAYSIZE(ranges); ++index) {
               ranges[index] = (D3D12DDI_DESCRIPTOR_RANGE){
                  .RangeType = D3D12DDI_DESCRIPTOR_RANGE_TYPE_CBV,
                  .NumDescriptors = 1,
                  .BaseShaderRegister = index,
                  .RegisterSpace = 2,
                  .OffsetInDescriptorsFromTableStart = index,
               };
            }
            const D3D12DDI_ROOT_PARAMETER parameter = {
               .ParameterType = D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
               .DescriptorTable = {
                  .NumDescriptorRanges = ARRAYSIZE(ranges),
                  .pDescriptorRanges = ranges,
               },
               .ShaderVisibility = D3D12DDI_SHADER_VISIBILITY_ALL,
            };
            const D3D12DDI_ROOT_SIGNATURE wide_desc = {
               .NumParameters = 1,
               .pRootParameters = &parameter,
            };
            root_arguments.pRootSignature = &wide_desc;
            require_result(
               device_table.pfnCalcPrivateRootSignatureSize(
                  driver_device, &root_arguments) == root_signature_size,
               "descriptor tables may contain more than sixteen ranges");
            root_arguments.pRootSignature = &root_desc;
         }

         {
            D3D12DDI_STATIC_SAMPLER samplers[65] = {0};
            for (UINT index = 0; index < ARRAYSIZE(samplers); ++index) {
               samplers[index] = (D3D12DDI_STATIC_SAMPLER){
                  .Filter = D3D12DDI_FILTER_MIN_MAG_MIP_POINT,
                  .AddressU = D3D12DDI_TEXTURE_ADDRESS_MODE_WRAP,
                  .AddressV = D3D12DDI_TEXTURE_ADDRESS_MODE_WRAP,
                  .AddressW = D3D12DDI_TEXTURE_ADDRESS_MODE_WRAP,
                  .MaxAnisotropy = 1,
                  .ComparisonFunc = D3D12DDI_COMPARISON_FUNC_ALWAYS,
                  .BorderColor =
                     D3D12DDI_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
                  .MaxLOD = 16.0f,
                  .ShaderRegister = index & 15u,
                  .RegisterSpace = index >> 4,
                  .ShaderVisibility =
                     (D3D12DDI_SHADER_VISIBILITY)(1u + index % 5u),
               };
            }
            const D3D12DDI_ROOT_SIGNATURE sampler_desc = {
               .NumStaticSamplers = ARRAYSIZE(samplers),
               .pStaticSamplers = samplers,
            };
            root_arguments.pRootSignature = &sampler_desc;
            require_result(
               device_table.pfnCalcPrivateRootSignatureSize(
                  driver_device, &root_arguments) == root_signature_size,
               "stage-disjoint root signatures may exceed 64 samplers");
            root_arguments.pRootSignature = &root_desc;
         }

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
            const D3D12DDI_DESCRIPTOR_RANGE range = {
               .RangeType = D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV,
               .NumDescriptors = 8,
               .BaseShaderRegister = 3,
               .RegisterSpace = 2,
            };
            const D3D12DDI_ROOT_PARAMETER parameters[2] = {
               {
                  .ParameterType =
                     D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                  .DescriptorTable = {
                     .NumDescriptorRanges = 1,
                     .pDescriptorRanges = &range,
                  },
                  .ShaderVisibility = D3D12DDI_SHADER_VISIBILITY_ALL,
               },
               {
                  .ParameterType = D3D12DDI_ROOT_PARAMETER_TYPE_CBV,
                  .Descriptor = {
                     .ShaderRegister = 7,
                     .RegisterSpace = 4,
                  },
                  .ShaderVisibility = D3D12DDI_SHADER_VISIBILITY_PIXEL,
               },
            };
            const D3D12DDI_ROOT_SIGNATURE volatile_desc = {
               .NumParameters = ARRAYSIZE(parameters),
               .pRootParameters = parameters,
            };
            struct d12_umd_device *private_device =
               driver_device.pDrvPrivate;
            struct d12_device *saved_backend = private_device->backend;
            const struct d12_umd_renderer_ops *saved_renderer_ops =
               private_device->renderer_ops;

            root_arguments.pRootSignature = &volatile_desc;
            driver_root_signature.pDrvPrivate = HeapAlloc(
               GetProcessHeap(), HEAP_ZERO_MEMORY, root_signature_size);
            require_result(driver_root_signature.pDrvPrivate != NULL,
                           "allocate translated 1.0 root signature");
            private_device->backend =
               (struct d12_device *)TEST_BLT_DEVICE;
            private_device->renderer_ops = &test_root11_renderer_ops;
            root11_create_calls = 0;
            root11_destroy_calls = 0;
            expected_root_range_flags = UINT32_C(0x3);
            expected_root_descriptor_flags = UINT32_C(0x2);
            result = device_table.pfnCreateRootSignature(
               driver_device, &root_arguments, driver_root_signature);
            require_result(
               result == S_OK && root11_create_calls == 1,
               "preserve root-signature 1.0 volatile semantics");
            device_table.pfnDestroyRootSignature(
               driver_device, driver_root_signature);
            require_result(root11_destroy_calls == 1,
                           "destroy translated 1.0 root signature");
            private_device->renderer_ops = saved_renderer_ops;
            private_device->backend = saved_backend;
            HeapFree(GetProcessHeap(), 0,
                     driver_root_signature.pDrvPrivate);
            root_arguments.pRootSignature = &root_desc;
         }

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

#if D12_UMD_TARGETS_WDDM21
      {
         D3D12DDI_DESCRIPTOR_RANGE_0013 range = {
            .RangeType = D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV,
            .NumDescriptors = 8,
            .BaseShaderRegister = 3,
            .RegisterSpace = 2,
            .Flags =
               D3D12DDI_DESCRIPTOR_RANGE_FLAG_0013_DESCRIPTORS_VOLATILE |
               D3D12DDI_DESCRIPTOR_RANGE_FLAG_0013_DATA_STATIC_WHILE_SET_AT_EXECUTE,
            .OffsetInDescriptorsFromTableStart = 0,
         };
         D3D12DDI_ROOT_PARAMETER_0013 parameters[2] = {
            {
               .ParameterType =
                  D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
               .DescriptorTable = {
                  .NumDescriptorRanges = 1,
                  .pDescriptorRanges = &range,
               },
               .ShaderVisibility = D3D12DDI_SHADER_VISIBILITY_ALL,
            },
            {
               .ParameterType = D3D12DDI_ROOT_PARAMETER_TYPE_CBV,
               .Descriptor = {
                  .ShaderRegister = 7,
                  .RegisterSpace = 4,
                  .Flags = D3D12DDI_ROOT_DESCRIPTOR_FLAG_0013_DATA_STATIC,
               },
               .ShaderVisibility = D3D12DDI_SHADER_VISIBILITY_PIXEL,
            },
         };
         const D3D12DDI_ROOT_SIGNATURE_0013 root_desc = {
            .NumParameters = ARRAYSIZE(parameters),
            .pRootParameters = parameters,
         };
         D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013 root_arguments = {
            .Version = D3D12DDI_ROOT_SIGNATURE_VERSION_1_1,
            .pRootSignature_1_1 = &root_desc,
            .NodeMask = 1,
         };
         struct d12_umd_device *private_device =
            driver_device.pDrvPrivate;
         struct d12_device *saved_backend = private_device->backend;
         const struct d12_umd_renderer_ops *saved_renderer_ops =
            private_device->renderer_ops;
         D3D12DDI_HROOTSIGNATURE root_signature;
         const SIZE_T root_signature_size =
            root11_table.pfnCalcPrivateRootSignatureSize(
               driver_device, &root_arguments);

         require_result(root_signature_size > 0,
                        "root-signature 1.1 private size");
         root_signature.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, root_signature_size);
         require_result(root_signature.pDrvPrivate != NULL,
                        "allocate root-signature 1.1 storage");
         private_device->backend = (struct d12_device *)TEST_BLT_DEVICE;
         private_device->renderer_ops = &test_root11_renderer_ops;
         root11_create_calls = 0;
         root11_destroy_calls = 0;
         expected_root_range_flags = UINT32_C(0x5);
         expected_root_descriptor_flags = UINT32_C(0x8);
         result = root11_table.pfnCreateRootSignature(
            driver_device, &root_arguments, root_signature);
         require_result(result == S_OK && root11_create_calls == 1,
                        "translate root-signature 1.1 volatility flags");
         root11_table.pfnDestroyRootSignature(driver_device,
                                               root_signature);
         require_result(root11_destroy_calls == 1,
                        "destroy root-signature 1.1 backend object");
         private_device->renderer_ops = saved_renderer_ops;
         private_device->backend = saved_backend;
         HeapFree(GetProcessHeap(), 0, root_signature.pDrvPrivate);

         range.Flags =
            D3D12DDI_DESCRIPTOR_RANGE_FLAG_0013_DATA_VOLATILE |
            D3D12DDI_DESCRIPTOR_RANGE_FLAG_0013_DATA_STATIC;
         require_result(
            root11_table.pfnCalcPrivateRootSignatureSize(
               driver_device, &root_arguments) == 0,
            "reject conflicting root-signature 1.1 data volatility");
         range.Flags =
            D3D12DDI_DESCRIPTOR_RANGE_FLAG_0013_DATA_STATIC_WHILE_SET_AT_EXECUTE;
         range.RangeType = D3D12DDI_DESCRIPTOR_RANGE_TYPE_SAMPLER;
         require_result(
            root11_table.pfnCalcPrivateRootSignatureSize(
               driver_device, &root_arguments) == 0,
            "reject sampler data-volatility flags");
         range.RangeType = D3D12DDI_DESCRIPTOR_RANGE_TYPE_SRV;
         root_arguments.Version = D3D12DDI_ROOT_SIGNATURE_VERSION_1_2;
         require_result(
            root11_table.pfnCalcPrivateRootSignatureSize(
               driver_device, &root_arguments) == 0,
            "reject unsupported root-signature 1.2 ABI");
      }
#endif

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
#if D12_UMD_TARGETS_WDDM21
            D3D12DDIARG_SIGNATURE_ENTRY_0012 output_signature = {
#else
            D3D12DDIARG_SIGNATURE_ENTRY output_signature = {
#endif
               .SystemValue = D3D10_SB_NAME_UNDEFINED,
               .Register = 0,
               .Mask = 0xf,
#if D12_UMD_TARGETS_WDDM21
               .Stream = 1,
#endif
               .RegisterComponentType =
                  D3D10_SB_REGISTER_COMPONENT_FLOAT32,
            };
            const D3D12DDIARG_STAGE_IO_SIGNATURES signatures = {
#if D12_UMD_TARGETS_WDDM21
               .pOutputSignature = &output_signature,
#else
               .pOutputSignatureDeprecated = &output_signature,
#endif
               .NumOutputSignatureEntries = 1,
            };
            D3D12DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY declarations[] = {
               {
#if D12_UMD_TARGETS_WDDM21
                  .Stream = 1,
#else
                  .Stream = 0,
#endif
                  .OutputSlot = 0,
                  .RegisterIndex = UINT32_MAX,
                  .RegisterMask = 0xf,
               },
               {
#if D12_UMD_TARGETS_WDDM21
                  .Stream = 1,
#else
                  .Stream = 0,
#endif
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
            {
               const struct d12_umd_shader *private_shader =
                  stream_output_shader.pDrvPrivate;
               require_result(
                  private_shader->signature == D12_UMD_SHADER_SIGNATURE &&
                     private_shader->so_declaration_count ==
                        ARRAYSIZE(declarations) &&
#if D12_UMD_TARGETS_WDDM21
                     private_shader->so_declarations[1].stream == 1,
#else
                     private_shader->so_declarations[1].stream == 0,
#endif
                  "translate geometry output signature stream");
            }
#if D12_UMD_TARGETS_WDDM21
            declarations[1].Stream = 0;
            require_result(
               device_table.pfnCalcPrivateGeometryShaderWithStreamOutput(
                  driver_device, &stream_output_arguments,
                  &signatures) == 0,
               "reject stream-output declaration from another "
               "signature stream");
            declarations[1].Stream = 1;
            output_signature.Stream = D12_FRONTEND_MAX_SIGNATURE_STREAMS;
            require_result(
               device_table.pfnCalcPrivateGeometryShaderWithStreamOutput(
                  driver_device, &stream_output_arguments,
                  &signatures) == 0,
               "reject out-of-range geometry signature stream");
            output_signature.Stream = 1;
#endif
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
         struct d12_umd_device *private_device = driver_device.pDrvPrivate;
         struct d12_device *saved_backend = private_device->backend;
         const struct d12_umd_renderer_ops *saved_renderer_ops =
            private_device->renderer_ops;
         struct d12_umd_resource resource = {
            .signature = D12_UMD_RESOURCE_SIGNATURE,
            .device = private_device,
            .backend = TEST_TILED_RESOURCE,
         };
         D3D12DDIARG_CREATE_RENDER_TARGET_VIEW_0002 arguments = {
            .hDrvResource = {.pDrvPrivate = &resource},
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .ResourceDimension = D3D12DDI_RD_TEXTURE1D,
            .Tex1D = {
               .MipSlice = 4,
               .FirstArraySlice = 2,
               .ArraySize = 6,
            },
         };
         const D3D12DDI_CPU_DESCRIPTOR_HANDLE destination = {
            .ptr = (SIZE_T)(void *)mapped_heap,
         };

         private_device->backend = (struct d12_device *)TEST_BLT_DEVICE;
         private_device->renderer_ops = &test_descriptor_renderer_ops;
         rtv_create_calls = 0;
         rtv_resource = NULL;
         rtv_destination = 0;
         memset(&rtv_desc, 0, sizeof(rtv_desc));

         device_table.pfnCreateRenderTargetView(
            driver_device, &arguments, destination);
         require_result(
            rtv_create_calls == 1 && rtv_resource == TEST_TILED_RESOURCE &&
               rtv_destination == destination.ptr &&
               rtv_desc.format == DXGI_FORMAT_R8G8B8A8_UNORM &&
               rtv_desc.dimension == D3D12DDI_RD_TEXTURE1D &&
               rtv_desc.texture.mip_slice == 4 &&
               rtv_desc.texture.first_slice == 2 &&
               rtv_desc.texture.array_size == 6,
            "translate 1D array RTV mip and layer range");

         arguments.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
         arguments.Tex2D = (D3D12DDIARG_TEX2D_RENDER_TARGET_VIEW_0002){
            .MipSlice = 3,
            .FirstArraySlice = 5,
            .ArraySize = 7,
            .PlaneSlice = 0,
         };
         device_table.pfnCreateRenderTargetView(
            driver_device, &arguments, destination);
         require_result(
            rtv_create_calls == 2 &&
               rtv_desc.dimension == D3D12DDI_RD_TEXTURE2D &&
               rtv_desc.texture.mip_slice == 3 &&
               rtv_desc.texture.first_slice == 5 &&
               rtv_desc.texture.array_size == 7 &&
               rtv_desc.texture.plane_slice == 0,
            "translate 2D array RTV mip and layer range");

         arguments.ResourceDimension = D3D12DDI_RD_TEXTURE3D;
         arguments.Tex3D = (D3D12DDIARG_TEX3D_RENDER_TARGET_VIEW){
            .MipSlice = 2,
            .FirstW = 1,
            .WSize = UINT32_MAX,
         };
         device_table.pfnCreateRenderTargetView(
            driver_device, &arguments, destination);
         require_result(
            rtv_create_calls == 3 &&
               rtv_desc.dimension == D3D12DDI_RD_TEXTURE3D &&
               rtv_desc.texture.mip_slice == 2 &&
               rtv_desc.texture.first_slice == 1 &&
               rtv_desc.texture.array_size == UINT32_MAX,
            "translate 3D RTV mip and W-slice range");

         {
            const D3D12DDIARG_CREATE_DEPTH_STENCIL_VIEW dsv_arguments = {
               .hDrvResource = {.pDrvPrivate = &resource},
               .Format = DXGI_FORMAT_D16_UNORM,
               .ResourceDimension = D3D12DDI_RD_TEXTURE1D,
               .Flags = D3D12DDI_CREATE_DSV_FLAG_READ_ONLY_DEPTH,
               .Tex1D = {
                  .MipSlice = 5,
                  .FirstArraySlice = 3,
                  .ArraySize = 4,
               },
            };
            dsv_create_calls = 0;
            dsv_resource = NULL;
            dsv_destination = 0;
            memset(&dsv_desc, 0, sizeof(dsv_desc));
            device_table.pfnCreateDepthStencilView(
               driver_device, &dsv_arguments, destination);
            require_result(
               dsv_create_calls == 1 &&
                  dsv_resource == TEST_TILED_RESOURCE &&
                  dsv_destination == destination.ptr &&
                  dsv_desc.format == DXGI_FORMAT_D16_UNORM &&
                  dsv_desc.dimension == D3D12DDI_RD_TEXTURE1D &&
                  dsv_desc.flags ==
                     D3D12DDI_CREATE_DSV_FLAG_READ_ONLY_DEPTH &&
                  dsv_desc.mip_slice == 5 && dsv_desc.first_slice == 3 &&
                  dsv_desc.array_size == 4,
               "translate 1D array DSV mip and layer range");
         }

         {
            const D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002
               uav_arguments = {
                  .hDrvResource = {.pDrvPrivate = &resource},
                  .Format = DXGI_FORMAT_R32_UINT,
                  .ResourceDimension = D3D12DDI_RD_TEXTURE3D,
                  .Tex3D = {
                     .MipSlice = 4,
                     .FirstW = 2,
                     .WSize = 5,
                  },
               };
            uav_create_calls = 0;
            uav_resource = NULL;
            uav_destination = 0;
            memset(&uav_desc, 0, sizeof(uav_desc));
            device_table.pfnCreateUnorderedAccessView(
               driver_device, &uav_arguments, destination);
            require_result(
               uav_create_calls == 1 &&
                  uav_resource == TEST_TILED_RESOURCE &&
                  uav_destination == destination.ptr &&
                  uav_desc.format == DXGI_FORMAT_R32_UINT &&
                  uav_desc.dimension == D3D12DDI_RD_TEXTURE3D &&
                  uav_desc.texture.mip_slice == 4 &&
                  uav_desc.texture.first_slice == 2 &&
                  uav_desc.texture.array_size == 5,
               "translate 3D UAV mip and W-slice range");
         }

         private_device->renderer_ops = saved_renderer_ops;
         private_device->backend = saved_backend;
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
            D3D12DDI_QUERY_HEAP_TYPE_0032_COPY_QUEUE_TIMESTAMP;
         require_result(device_table.pfnCalcPrivateQueryHeapSize(
                           driver_device, &query_arguments) > 0,
                        "accept copy-queue timestamp heap");

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
            mutable_arguments[1].VertexBuffer.Slot = 31;
            require_result(
               device_table.pfnCalcPrivateCommandSignatureSize(
                  driver_device, &mutable_signature) > 0,
               "accept highest indirect vertex-buffer slot");
            mutable_arguments[1].VertexBuffer.Slot = 32;
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
         D3D12DDIARG_CREATECOMMANDQUEUE_0001 queue_arguments = {
            .hRTCommandQueue = {.handle = TEST_RT_QUEUE},
            .QueueFlags = D3D12DDI_COMMAND_QUEUE_FLAG_3D,
            .NodeMask = 0,
         };
         D3D12DDI_HCOMMANDQUEUE driver_queue;
         const SIZE_T queue_size =
            device_table.pfnCalcPrivateCommandQueueSize(driver_device,
                                                        &queue_arguments);

         driver_queue.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, queue_size);
         require_result(driver_queue.pDrvPrivate != NULL,
                        "allocate transactional queue storage");
         queue_arguments.hDrvCommandQueue = driver_queue;
         private_device->backend = (struct d12_device *)TEST_BLT_DEVICE;
         private_device->renderer_ops = &test_transaction_renderer_ops;
         transaction_queue_create_calls = 0;
         transaction_queue_destroy_calls = 0;

         transaction_queue_result = E_FAIL;
         transaction_queue_output = TEST_TRANSACTION_QUEUE;
         result = device_table.pfnCreateCommandQueue(driver_device,
                                                     &queue_arguments);
         require_result(
            result == E_FAIL && transaction_queue_create_calls == 1 &&
               transaction_queue_destroy_calls == 1 &&
               private_device->queue_count == 0 &&
               ((struct d12_umd_command_queue *)driver_queue.pDrvPrivate)
                     ->signature == 0,
            "rollback backend queue returned with failure");

         transaction_queue_result = S_OK;
         transaction_queue_output = NULL;
         result = device_table.pfnCreateCommandQueue(driver_device,
                                                     &queue_arguments);
         require_result(
            result == E_FAIL && transaction_queue_create_calls == 2 &&
               transaction_queue_destroy_calls == 1 &&
               private_device->queue_count == 0 &&
               ((struct d12_umd_command_queue *)driver_queue.pDrvPrivate)
                     ->signature == 0,
            "reject backend queue success without object");

         private_device->renderer_ops =
            &test_incomplete_transaction_renderer_ops;
         transaction_queue_output = TEST_TRANSACTION_QUEUE;
         result = device_table.pfnCreateCommandQueue(driver_device,
                                                     &queue_arguments);
         require_result(
            result == E_NOTIMPL && transaction_queue_create_calls == 2 &&
               transaction_queue_destroy_calls == 1 &&
               private_device->queue_count == 0,
            "require symmetric backend queue destruction");

         private_device->renderer_ops = &test_transaction_renderer_ops;
         result = device_table.pfnCreateCommandQueue(driver_device,
                                                     &queue_arguments);
         require_result(
            result == S_OK && transaction_queue_create_calls == 3 &&
               private_device->queue_count == 1,
            "create transactional backend queue");
         device_table.pfnDestroyCommandQueue(driver_device, driver_queue);
         require_result(transaction_queue_destroy_calls == 2 &&
                           private_device->queue_count == 0,
                        "destroy transactional backend queue");

         private_device->renderer_ops = saved_renderer_ops;
         private_device->backend = saved_backend;
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

#if D12_UMD_TARGETS_WDDM31
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
            const struct {
               D3D12DDI_BARRIER_LAYOUT layout;
               D3D12DDI_RESOURCE_STATES state;
            } accepted_layouts[] = {
               {D3D12DDI_BARRIER_LAYOUT_COMMON,
                D3D12DDI_RESOURCE_STATE_COMMON},
               {D3D12DDI_BARRIER_LAYOUT_GENERIC_READ, generic_read},
               {D3D12DDI_BARRIER_LAYOUT_RENDER_TARGET,
                D3D12DDI_RESOURCE_STATE_RENDER_TARGET},
               {D3D12DDI_BARRIER_LAYOUT_UNORDERED_ACCESS,
                D3D12DDI_RESOURCE_STATE_UNORDERED_ACCESS},
               {D3D12DDI_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
                D3D12DDI_RESOURCE_STATE_DEPTH_WRITE},
               {D3D12DDI_BARRIER_LAYOUT_DEPTH_STENCIL_READ,
                D3D12DDI_RESOURCE_STATE_DEPTH_READ},
               {D3D12DDI_BARRIER_LAYOUT_SHADER_RESOURCE, shader_resource},
               {D3D12DDI_BARRIER_LAYOUT_COPY_SOURCE,
                D3D12DDI_RESOURCE_STATE_COPY_SOURCE},
               {D3D12DDI_BARRIER_LAYOUT_COPY_DEST,
                D3D12DDI_RESOURCE_STATE_COPY_DEST},
               {D3D12DDI_BARRIER_LAYOUT_RESOLVE_SOURCE,
                D3D12DDI_RESOURCE_STATE_RESOLVE_SOURCE},
               {D3D12DDI_BARRIER_LAYOUT_RESOLVE_DEST,
                D3D12DDI_RESOURCE_STATE_RESOLVE_DEST},
               {D3D12DDI_BARRIER_LAYOUT_DIRECT_QUEUE_COMMON,
                D3D12DDI_RESOURCE_STATE_COMMON},
               {D3D12DDI_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ,
                generic_read},
               {D3D12DDI_BARRIER_LAYOUT_DIRECT_QUEUE_UNORDERED_ACCESS,
                D3D12DDI_RESOURCE_STATE_UNORDERED_ACCESS},
               {D3D12DDI_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE,
                shader_resource},
               {D3D12DDI_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_SOURCE,
                D3D12DDI_RESOURCE_STATE_COPY_SOURCE},
               {D3D12DDI_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_DEST,
                D3D12DDI_RESOURCE_STATE_COPY_DEST},
               {D3D12DDI_BARRIER_LAYOUT_COMPUTE_QUEUE_COMMON,
                D3D12DDI_RESOURCE_STATE_COMMON},
               {D3D12DDI_BARRIER_LAYOUT_COMPUTE_QUEUE_GENERIC_READ,
                generic_read},
               {D3D12DDI_BARRIER_LAYOUT_COMPUTE_QUEUE_UNORDERED_ACCESS,
                D3D12DDI_RESOURCE_STATE_UNORDERED_ACCESS},
               {D3D12DDI_BARRIER_LAYOUT_COMPUTE_QUEUE_SHADER_RESOURCE,
                D3D12DDI_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
               {D3D12DDI_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_SOURCE,
                D3D12DDI_RESOURCE_STATE_COPY_SOURCE},
               {D3D12DDI_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_DEST,
                D3D12DDI_RESOURCE_STATE_COPY_DEST},
               {D3D12DDI_BARRIER_LAYOUT_LEGACY_DIRECT_QUEUE_GENERIC_READ_COMPUTE_QUEUE_ACCESSIBLE,
                generic_read},
               {D3D12DDI_BARRIER_LAYOUT_LEGACY_COPY_SOURCE,
                D3D12DDI_RESOURCE_STATE_COPY_SOURCE},
               {D3D12DDI_BARRIER_LAYOUT_LEGACY_COPY_DEST,
                D3D12DDI_RESOURCE_STATE_COPY_DEST},
               {D3D12DDI_BARRIER_LAYOUT_LEGACY_SHADER_RESOURCE,
                shader_resource},
               {D3D12DDI_BARRIER_LAYOUT_LEGACY_PIXEL_SHADER_RESOURCE,
                D3D12DDI_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            };
            static const D3D12DDI_BARRIER_LAYOUT rejected_layouts[] = {
               D3D12DDI_BARRIER_LAYOUT_UNDEFINED,
               D3D12DDI_BARRIER_LAYOUT_SHADING_RATE_SOURCE,
               D3D12DDI_BARRIER_LAYOUT_VIDEO_DECODE_READ,
               D3D12DDI_BARRIER_LAYOUT_VIDEO_DECODE_WRITE,
               D3D12DDI_BARRIER_LAYOUT_VIDEO_PROCESS_READ,
               D3D12DDI_BARRIER_LAYOUT_VIDEO_PROCESS_WRITE,
               D3D12DDI_BARRIER_LAYOUT_VIDEO_ENCODE_READ,
               D3D12DDI_BARRIER_LAYOUT_VIDEO_ENCODE_WRITE,
               D3D12DDI_BARRIER_LAYOUT_VIDEO_QUEUE_COMMON,
            };
            D3D12DDIARG_CREATERESOURCE_0088 release8_resource = {
               .ResourceType = resource_arguments.ResourceType,
               .Width = resource_arguments.Width,
               .Height = resource_arguments.Height,
               .DepthOrArraySize = resource_arguments.DepthOrArraySize,
               .MipLevels = resource_arguments.MipLevels,
               .Format = resource_arguments.Format,
               .SampleDesc = resource_arguments.SampleDesc,
               .Layout = resource_arguments.Layout,
               .Flags = resource_arguments.Flags,
            };

            for (UINT index = 0; index < ARRAYSIZE(accepted_layouts);
                 ++index) {
               release8_resource.InitialBarrierLayout =
                  accepted_layouts[index].layout;
               sparse_expected_initial_state = accepted_layouts[index].state;
               const D3D12DDI_HEAP_AND_RESOURCE_SIZES release8_sizes =
                  release1_table.pfnCalcPrivateHeapAndResourceSizes(
                     driver_device, NULL, &release8_resource,
                     (D3D12DDI_HPROTECTEDRESOURCESESSION_0030){0});
               require_result(!release8_sizes.Heap &&
                                 release8_sizes.Resource > 0,
                              "accept representable DDI 0088 layout");
            }

            /* Exercise the creation callback too, and prove the translated
             * state crosses the DDI-to-renderer seam rather than only making
             * the size query succeed. */
            release8_resource.InitialBarrierLayout =
               D3D12DDI_BARRIER_LAYOUT_COPY_DEST;
            sparse_expected_initial_state =
               D3D12DDI_RESOURCE_STATE_COPY_DEST;
            {
               D3D12DDI_HEAP_AND_RESOURCE_SIZES release8_sizes =
                  release1_table.pfnCalcPrivateHeapAndResourceSizes(
                     driver_device, NULL, &release8_resource,
                     (D3D12DDI_HPROTECTEDRESOURCESESSION_0030){0});
               D3D12DDI_HRESOURCE release8_handle = {
                  .pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           release8_sizes.Resource),
               };
               require_result(release8_handle.pDrvPrivate != NULL,
                              "allocate DDI 0088 resource private storage");
               sparse_create_calls = 0;
               result = release1_table.pfnCreateHeapAndResource(
                  driver_device, NULL, (D3D12DDI_HHEAP){0},
                  (D3D12DDI_HRTRESOURCE){0}, &release8_resource, NULL,
                  (D3D12DDI_HPROTECTEDRESOURCESESSION_0030){0},
                  release8_handle);
               require_result(result == S_OK && sparse_create_calls == 1,
                              "preserve DDI 0088 state during creation");
               release1_table.pfnDestroyHeapAndResource(
                  driver_device, (D3D12DDI_HHEAP){0}, release8_handle);
               HeapFree(GetProcessHeap(), 0, release8_handle.pDrvPrivate);
               sparse_create_calls = 0;
            }

            for (UINT index = 0; index < ARRAYSIZE(rejected_layouts);
                 ++index) {
               release8_resource.InitialBarrierLayout =
                  rejected_layouts[index];
               const D3D12DDI_HEAP_AND_RESOURCE_SIZES release8_sizes =
                  release1_table.pfnCalcPrivateHeapAndResourceSizes(
                     driver_device, NULL, &release8_resource,
                     (D3D12DDI_HPROTECTEDRESOURCESESSION_0030){0});
               require_result(!release8_sizes.Heap &&
                                 !release8_sizes.Resource,
                              "reject unrepresentable DDI 0088 layout");
            }

            release8_resource = (D3D12DDIARG_CREATERESOURCE_0088){
               .ResourceType = D3D12DDI_RT_BUFFER,
               .Width = UINT64_C(0x20000),
               .Height = 1,
               .DepthOrArraySize = 1,
               .MipLevels = 1,
               .Format = DXGI_FORMAT_UNKNOWN,
               .SampleDesc = {.Count = 1},
               .Layout = D3D12DDI_TL_ROW_MAJOR,
               .InitialBarrierLayout = D3D12DDI_BARRIER_LAYOUT_UNDEFINED,
            };
            sparse_expected_initial_state = D3D12DDI_RESOURCE_STATE_COMMON;
            {
               const D3D12DDI_HEAP_AND_RESOURCE_SIZES release8_sizes =
                  release1_table.pfnCalcPrivateHeapAndResourceSizes(
                     driver_device, NULL, &release8_resource,
                     (D3D12DDI_HPROTECTEDRESOURCESESSION_0030){0});
               require_result(!release8_sizes.Heap &&
                                 release8_sizes.Resource > 0,
                              "DDI 0088 buffer requires UNDEFINED layout");
            }
            {
               D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 release8_info;
               memset(&release8_info, 0xcc, sizeof(release8_info));
               set_error_calls = 0;
               release1_table.pfnCheckResourceAllocationInfo(
                  driver_device, &release8_resource,
                  D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_NONE, 0, 1,
                  &release8_info);
               require_result(set_error_calls == 0 &&
                                 release8_info.ResourceDataSize ==
                                    release8_resource.Width &&
                                 release8_info.ResourceDataAlignment ==
                                    UINT64_C(0x10000) &&
                                 release8_info.Layout ==
                                    D3D12DDI_TL_ROW_MAJOR,
                              "reflect DDI 0088 buffer allocation info");
            }
            release8_resource.InitialBarrierLayout =
               D3D12DDI_BARRIER_LAYOUT_COMMON;
            {
               const D3D12DDI_HEAP_AND_RESOURCE_SIZES release8_sizes =
                  release1_table.pfnCalcPrivateHeapAndResourceSizes(
                     driver_device, NULL, &release8_resource,
                     (D3D12DDI_HPROTECTEDRESOURCESESSION_0030){0});
               require_result(!release8_sizes.Heap &&
                                 !release8_sizes.Resource,
                              "reject DDI 0088 buffer with texture layout");
            }
            {
               D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 release8_info;
               memset(&release8_info, 0xcc, sizeof(release8_info));
               set_error_calls = 0;
               release1_table.pfnCheckResourceAllocationInfo(
                  driver_device, &release8_resource,
                  D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_NONE, 0, 1,
                  &release8_info);
               require_result(set_error_calls == 1 &&
                                 last_set_error == E_NOTIMPL &&
                                 release8_info.ResourceDataSize == 0,
                              "reject DDI 0088 buffer allocation info with "
                              "texture layout");
            }
         }
#endif

         {
            D3D12DDIARG_CREATERESOURCE_0003 invalid = resource_arguments;
            struct d12_umd_resource invalid_private;
            D3D12DDI_HEAP_AND_RESOURCE_SIZES invalid_sizes;
            D3D12DDI_HRESOURCE invalid_handle = {
               .pDrvPrivate = &invalid_private,
            };

            invalid.ResourceType = D3D12DDI_RT_TEXTURE1D;
            invalid.Height = 1;
            invalid_sizes =
               device_table.pfnCalcPrivateHeapAndResourceSizes(
                  driver_device, NULL, &invalid);
            require_result(!invalid_sizes.Heap && !invalid_sizes.Resource,
                           "reject Tier-1 reserved Texture1D sizing");

            invalid = resource_arguments;
            invalid.ResourceType = D3D12DDI_RT_TEXTURE3D;
            invalid_sizes =
               device_table.pfnCalcPrivateHeapAndResourceSizes(
                  driver_device, NULL, &invalid);
            require_result(!invalid_sizes.Heap && !invalid_sizes.Resource,
                           "reject Tier-1 reserved Texture3D sizing");
            result = device_table.pfnCreateHeapAndResource(
               driver_device, NULL, (D3D12DDI_HHEAP){0},
               (D3D12DDI_HRTRESOURCE){0}, &invalid, NULL,
               invalid_handle);
            require_result(result == E_INVALIDARG &&
                              sparse_create_calls == 0,
                           "reject Tier-1 reserved Texture3D creation");

            invalid = resource_arguments;
            invalid.SampleDesc.Count = 2;
            invalid_sizes =
               device_table.pfnCalcPrivateHeapAndResourceSizes(
                  driver_device, NULL, &invalid);
            require_result(!invalid_sizes.Heap && !invalid_sizes.Resource,
                           "reject Tier-1 reserved 2x MSAA sizing");

            invalid = resource_arguments;
            invalid.DepthOrArraySize = 2;
            invalid_sizes =
               device_table.pfnCalcPrivateHeapAndResourceSizes(
                  driver_device, NULL, &invalid);
            require_result(!invalid_sizes.Heap && !invalid_sizes.Resource,
                           "reject Tier-1 packed array mip sizing");

            invalid = (D3D12DDIARG_CREATERESOURCE_0003){
               .ResourceType = D3D12DDI_RT_BUFFER,
               .Width = UINT64_C(0x20000),
               .Height = 1,
               .DepthOrArraySize = 1,
               .MipLevels = 1,
               .Format = DXGI_FORMAT_UNKNOWN,
               .SampleDesc = {.Count = 1},
               .Layout = D3D12DDI_TL_ROW_MAJOR,
               .InitialResourceState = D3D12DDI_RESOURCE_STATE_COMMON,
            };
            invalid_sizes =
               device_table.pfnCalcPrivateHeapAndResourceSizes(
                  driver_device, NULL, &invalid);
            require_result(!invalid_sizes.Heap && invalid_sizes.Resource > 0,
                           "accept Tier-1 reserved buffer sizing");
         }

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
#if D12_UMD_TARGETS_WDDM21
         {
            const D3D12DDI_HANDLE_AND_TYPE object = {
               .Handle = resource_handle.pDrvPrivate,
               .Type = D3D12DDI_HT_0012_RESOURCE,
            };
            D3D12DDI_DEBUG_VIRTUAL_ADDRESS_ALLOCATION_INFO_0012 va_info;
            d12_test_debug_kmt_allocation_info kmt_info;
            UINT va_count = 1;
            UINT kmt_count = 1;

            memset(&va_info, 0, sizeof(va_info));
            memset(&kmt_info, 0, sizeof(kmt_info));
            root11_table.pfnGetDebugAllocationInfo(
               driver_device, object, &va_count, &va_info, &kmt_count,
               &kmt_info);
            require_result(
               va_count == 1 && kmt_count == 0 &&
                  va_info.StartAddress == TEST_TILED_GPU_VA &&
                  va_info.EndAddress ==
                     TEST_TILED_GPU_VA + UINT64_C(0x500000),
               "reserved-resource debug GPUVA without KMT allocation");
         }
#endif
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
         struct d12_umd_device *private_device = driver_device.pDrvPrivate;
         struct d12_device *saved_backend = private_device->backend;
         const struct d12_umd_renderer_ops *saved_renderer_ops =
            private_device->renderer_ops;
         struct d12_umd_resource source_resource = {
            .signature = D12_UMD_RESOURCE_SIGNATURE,
            .device = private_device,
            .runtime_heap_resource =
               (HANDLE)(uintptr_t)UINT32_C(0xaef0),
            .allocation = {
               .allocation = TEST_REUSE_ALLOCATION,
               .gpu_virtual_address = UINT64_C(0x4400000000),
               .size = UINT64_C(0x80000),
            },
            .heap_offset = UINT64_C(0x10000),
            .backend = TEST_REUSE_SOURCE,
            .desc = {
               .ResourceType = D3D12DDI_RT_BUFFER,
               .Width = UINT64_C(0x40000),
               .Height = 1,
               .DepthOrArraySize = 1,
               .MipLevels = 1,
               .Format = DXGI_FORMAT_UNKNOWN,
               .SampleDesc = {.Count = 1},
               .Layout = D3D12DDI_TL_ROW_MAJOR,
            },
         };
         const D3D12DDIARG_CREATERESOURCE_0003 buffer_reused_arguments = {
            .ReuseBufferGPUVA.BaseAddress.UMD = {
               .hResource = {.pDrvPrivate = &source_resource},
               .Offset = UINT64_C(0x10000),
            },
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
         D3D12DDIARG_CREATERESOURCE_0003 reused_arguments =
            buffer_reused_arguments;
         const D3D12DDIARG_CREATEHEAP_0001 invalid_heap_arguments = {
            .ByteSize = UINT64_C(0x80000),
            .Alignment = UINT64_C(0x10000),
            .MemoryPool = D3D12DDI_MEMORY_POOL_L0,
            .CPUPageProperty = D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK,
            .Flags = D3D12DDI_HEAP_FLAG_BUFFERS,
            .CreationNodeMask = 1,
            .VisibleNodeMask = 1,
         };
         D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes;
         D3D12DDI_HRESOURCE resource_handle;
         struct d12_umd_resource *reused_resource;

         private_device->backend = (struct d12_device *)TEST_BLT_DEVICE;
         private_device->renderer_ops = &test_reused_renderer_ops;
         reused_create_calls = 0;
         reused_destroy_calls = 0;
         sizes = device_table.pfnCalcPrivateHeapAndResourceSizes(
            driver_device, NULL, &reused_arguments);
         require_result(sizes.Heap == 0 && sizes.Resource > 0,
                        "capture GPUVA resource private size");
         resource_handle.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Resource);
         require_result(resource_handle.pDrvPrivate != NULL,
                        "allocate capture GPUVA resource storage");
         result = device_table.pfnCreateHeapAndResource(
            driver_device, NULL, (D3D12DDI_HHEAP){0},
            (D3D12DDI_HRTRESOURCE){0}, &reused_arguments, NULL,
            resource_handle);
         reused_resource = resource_handle.pDrvPrivate;
         require_result(
            result == S_OK && reused_create_calls == 1 &&
               !reused_resource->reserved &&
               !reused_resource->runtime_heap_resource &&
               reused_resource->allocation.allocation ==
                  TEST_REUSE_ALLOCATION &&
               reused_resource->heap_offset == UINT64_C(0x20000) &&
               device_table.pfnCheckResourceVirtualAddress(
                  driver_device, resource_handle) == TEST_REUSED_GPU_VA &&
               device_table.pfnCheckResourceAllocationHandle(
                  driver_device,
                  (D3D10DDI_HRESOURCE){
                     .pDrvPrivate = resource_handle.pDrvPrivate,
                  }) == TEST_REUSE_ALLOCATION,
            "capture source buffer GPUVA without a new allocation");

         device_table.pfnDestroyHeapAndResource(
            driver_device, (D3D12DDI_HHEAP){0}, resource_handle);
         require_result(reused_destroy_calls == 1,
                        "destroy capture GPUVA resource only");
         HeapFree(GetProcessHeap(), 0, resource_handle.pDrvPrivate);

         reused_create_null = true;
         resource_handle.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Resource);
         require_result(resource_handle.pDrvPrivate != NULL,
                        "allocate failed capture resource storage");
         result = device_table.pfnCreateHeapAndResource(
            driver_device, NULL, (D3D12DDI_HHEAP){0},
            (D3D12DDI_HRTRESOURCE){0}, &reused_arguments, NULL,
            resource_handle);
         require_result(
            result == E_FAIL &&
               !((struct d12_umd_resource *)resource_handle.pDrvPrivate)
                   ->signature &&
               reused_destroy_calls == 1,
            "reject renderer success without capture resource object");
         HeapFree(GetProcessHeap(), 0, resource_handle.pDrvPrivate);
         reused_create_null = false;

         reused_create_result = E_FAIL;
         resource_handle.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Resource);
         require_result(resource_handle.pDrvPrivate != NULL,
                        "allocate rollback capture resource storage");
         result = device_table.pfnCreateHeapAndResource(
            driver_device, NULL, (D3D12DDI_HHEAP){0},
            (D3D12DDI_HRTRESOURCE){0}, &reused_arguments, NULL,
            resource_handle);
         require_result(
            result == E_FAIL && reused_destroy_calls == 2 &&
               !((struct d12_umd_resource *)resource_handle.pDrvPrivate)
                   ->signature,
            "rollback capture resource returned with failure");
         HeapFree(GetProcessHeap(), 0, resource_handle.pDrvPrivate);
         reused_create_result = S_OK;

         {
            const D3D12DDIARG_ROW_MAJOR_RESOURCE_LAYOUT row_major = {
               .RowPitch = 256,
               .SlicePitch = 1024,
            };

            reused_arguments = buffer_reused_arguments;
            reused_arguments.ResourceType = D3D12DDI_RT_TEXTURE2D;
            reused_arguments.Width = 64;
            reused_arguments.Height = 4;
            reused_arguments.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            reused_arguments.Flags =
               D3D12DDI_RESOURCE_FLAG_0003_CROSS_ADAPTER |
               D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE;
            reused_arguments.pRowMajorLayout = &row_major;
            reused_expect_row_major = true;
            sizes = device_table.pfnCalcPrivateHeapAndResourceSizes(
               driver_device, NULL, &reused_arguments);
            require_result(sizes.Heap == 0 && sizes.Resource > 0,
                           "row-major capture resource private size");
            resource_handle.pDrvPrivate = HeapAlloc(
               GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Resource);
            require_result(resource_handle.pDrvPrivate != NULL,
                           "allocate row-major capture resource storage");
            result = device_table.pfnCreateHeapAndResource(
               driver_device, NULL, (D3D12DDI_HHEAP){0},
               (D3D12DDI_HRTRESOURCE){0}, &reused_arguments, NULL,
               resource_handle);
            reused_resource = resource_handle.pDrvPrivate;
            require_result(
               result == S_OK && reused_resource->desc.pRowMajorLayout ==
                                    &reused_resource->row_major &&
                  reused_resource->row_major.RowPitch == 256 &&
                  reused_resource->row_major.SlicePitch == 1024 &&
                  reused_resource->allocation.allocation ==
                     TEST_REUSE_ALLOCATION,
               "preserve runtime row-major layout through capture seam");
            device_table.pfnDestroyHeapAndResource(
               driver_device, (D3D12DDI_HHEAP){0}, resource_handle);
            require_result(reused_destroy_calls == 3,
                           "destroy row-major capture resource");
            HeapFree(GetProcessHeap(), 0, resource_handle.pDrvPrivate);
            reused_expect_row_major = false;
            reused_arguments = buffer_reused_arguments;
         }

         reused_arguments.ReuseBufferGPUVA.BaseAddress.UMD.Offset = 1;
         sizes = device_table.pfnCalcPrivateHeapAndResourceSizes(
            driver_device, NULL, &reused_arguments);
         require_result(!sizes.Heap && !sizes.Resource,
                        "reject unaligned capture GPUVA offset");
         reused_arguments.ReuseBufferGPUVA.BaseAddress.UMD.Offset =
            UINT64_C(0x40000);
         sizes = device_table.pfnCalcPrivateHeapAndResourceSizes(
            driver_device, NULL, &reused_arguments);
         require_result(!sizes.Heap && !sizes.Resource,
                        "reject capture GPUVA range outside source buffer");
         reused_arguments.ReuseBufferGPUVA.BaseAddress.UMD.Offset =
            UINT64_C(0x10000);
         sizes = device_table.pfnCalcPrivateHeapAndResourceSizes(
            driver_device, &invalid_heap_arguments, &reused_arguments);
         require_result(!sizes.Heap && !sizes.Resource,
                        "reject capture GPUVA with simultaneous heap create");
         reused_arguments.ReuseBufferGPUVA.BaseAddress.UMD.hResource =
            (D3D12DDI_HRESOURCE){0};
         sizes = device_table.pfnCalcPrivateHeapAndResourceSizes(
            driver_device, NULL, &reused_arguments);
         require_result(!sizes.Heap && !sizes.Resource,
                        "reject capture GPUVA offset without source");

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
         {
            struct d12_umd_device *private_device =
               driver_device.pDrvPrivate;
            void *saved_backend = private_device->backend;
            const struct d12_umd_renderer_ops *saved_renderer_ops =
               private_device->renderer_ops;
            D3D12DDI_HEAP_AND_RESOURCE_SIZES rejected_sizes;

            reused_allocation_info_size = sizeof(mapped_heap) +
                                          UINT64_C(0x10000);
            private_device->backend = TEST_BLT_DEVICE;
            private_device->renderer_ops = &test_reused_renderer_ops;
            rejected_sizes =
               device_table.pfnCalcPrivateHeapAndResourceSizes(
                  driver_device, &heap_arguments, &resource_arguments);
            require_result(!rejected_sizes.Heap &&
                              !rejected_sizes.Resource,
                           "reject heap smaller than reflected resource");
            reused_allocation_info_size = UINT64_C(0x10000);
            private_device->backend = saved_backend;
            private_device->renderer_ops = saved_renderer_ops;
         }
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

#if D12_UMD_TARGETS_WDDM21
         {
            const D3D12DDI_HANDLE_AND_TYPE resource_object = {
               .Handle = resource_handle.pDrvPrivate,
               .Type = D3D12DDI_HT_0012_RESOURCE,
            };
            const D3D12DDI_HANDLE_AND_TYPE heap_object = {
               .Handle = heap_handle.pDrvPrivate,
               .Type = D3D12DDI_HT_HEAP,
            };
            D3D12DDI_DEBUG_VIRTUAL_ADDRESS_ALLOCATION_INFO_0012 va_info;
            d12_test_debug_kmt_allocation_info kmt_info;
            UINT va_count = 0;
            UINT kmt_count = 0;

            root11_table.pfnGetDebugAllocationInfo(
               driver_device, resource_object, &va_count, NULL, &kmt_count,
               NULL);
            require_result(va_count == 1 && kmt_count == 1,
                           "query resource debug-allocation counts");
            va_count = 1;
            kmt_count = 1;
            memset(&va_info, 0, sizeof(va_info));
            memset(&kmt_info, 0, sizeof(kmt_info));
            root11_table.pfnGetDebugAllocationInfo(
               driver_device, resource_object, &va_count, &va_info,
               &kmt_count, &kmt_info);
            require_result(
               va_count == 1 && kmt_count == 1 &&
                  va_info.PhysicalAdapterIndex == 0 &&
                  va_info.StartAddress == TEST_RESOURCE_GPU_VA &&
                  va_info.EndAddress ==
                     TEST_RESOURCE_GPU_VA + resource_arguments.Width &&
                  kmt_info.PhysicalAdapterIndex == 0 &&
                  kmt_info.hAllocation == TEST_ALLOCATION,
               "resource debug-allocation translation");

            va_count = 1;
            kmt_count = 1;
            memset(&va_info, 0, sizeof(va_info));
            memset(&kmt_info, 0, sizeof(kmt_info));
            root11_table.pfnGetDebugAllocationInfo(
               driver_device, heap_object, &va_count, &va_info, &kmt_count,
               &kmt_info);
            require_result(
               va_count == 1 && kmt_count == 1 &&
                  va_info.StartAddress == TEST_RESOURCE_GPU_VA &&
                  va_info.EndAddress ==
                     TEST_RESOURCE_GPU_VA + sizeof(mapped_heap) &&
                  kmt_info.hAllocation == TEST_ALLOCATION,
               "heap debug-allocation translation");
         }
#endif

         {
            DXGI_DDI_ARG_SETRESOURCEPRIORITY priority_arguments = {
               .hDevice = (DXGI_DDI_HDEVICE)(uintptr_t)
                  driver_device.pDrvPrivate,
               .hResource = (DXGI_DDI_HRESOURCE)(uintptr_t)
                  resource_handle.pDrvPrivate,
               .Priority = 7,
            };
            DXGI_DDI_HRESOURCE resources[17];
            DXGI_DDI_RESIDENCY residency[ARRAYSIZE(resources)] = {0};
            DXGI_DDI_ARG_QUERYRESOURCERESIDENCY residency_arguments = {
               .hDevice = (DXGI_DDI_HDEVICE)(uintptr_t)
                  driver_device.pDrvPrivate,
               .pResources = resources,
               .pStatus = residency,
               .Resources = ARRAYSIZE(resources),
            };

            for (UINT index = 0; index < ARRAYSIZE(resources); ++index) {
               resources[index] = (DXGI_DDI_HRESOURCE)(uintptr_t)
                  resource_handle.pDrvPrivate;
            }

            result = dxgi_table.pfnSetResourcePriority(
               &priority_arguments);
            require_result(result == S_OK && priority_calls == 1,
                           "DXGI allocation-priority callback");
            result = dxgi_table.pfnQueryResourceResidency(
               &residency_arguments);
            require_result(
               result == D12_UMD_S_RESIDENT_IN_SHARED_MEMORY &&
                  residency_query_calls == 1 &&
                  residency[0] ==
                     DXGI_DDI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY,
               "DXGI deduplicated residency callback");
            for (UINT index = 0; index < ARRAYSIZE(residency); ++index) {
               require_result(
                  residency[index] ==
                     DXGI_DDI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY,
                  "DXGI shared residency fan-out");
            }
            query_residency_status = D3DDDI_RESIDENCYSTATUS_NOTRESIDENT;
            result = dxgi_table.pfnQueryResourceResidency(
               &residency_arguments);
            require_result(result == D12_UMD_S_NOT_RESIDENT &&
                              residency_query_calls == 2 &&
                              residency[0] ==
                                 DXGI_DDI_RESIDENCY_EVICTED_TO_DISK,
                           "DXGI nonresident HRESULT and status");
            for (UINT index = 0; index < ARRAYSIZE(residency); ++index) {
               require_result(
                  residency[index] == DXGI_DDI_RESIDENCY_EVICTED_TO_DISK,
                  "DXGI nonresident residency fan-out");
            }
            for (UINT index = 0; index < ARRAYSIZE(residency); ++index)
               residency[index] = (DXGI_DDI_RESIDENCY)UINT32_MAX;
            query_residency_status =
               (D3DDDI_RESIDENCYSTATUS)UINT32_MAX;
            result = dxgi_table.pfnQueryResourceResidency(
               &residency_arguments);
            require_result(result == E_FAIL && residency_query_calls == 3,
                           "reject invalid kernel residency status");
            for (UINT index = 0; index < ARRAYSIZE(residency); ++index) {
               require_result(
                  residency[index] == (DXGI_DDI_RESIDENCY)UINT32_MAX,
                  "invalid residency leaves outputs unpublished");
            }
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
               require_result(result == S_OK && dxgi_offer_calls == 1 &&
                                 dxgi_offer_allocation_count == 1,
                              "DXGI OfferResources allocation deduplication");
               result = dxgi_table.pfnReclaimResources(&reclaim_arguments);
               require_result(result == S_OK && dxgi_reclaim_calls == 1 &&
                                 dxgi_reclaim_allocation_count == 1,
                              "DXGI ReclaimResources allocation deduplication");
               for (UINT index = 0; index < ARRAYSIZE(discarded); ++index) {
                  require_result(!discarded[index],
                                 "DXGI ReclaimResources aliased fan-out");
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
                  require_result(result == S_OK && dxgi_reclaim_calls == 3 &&
                                    dxgi_reclaim_allocation_count == 1,
                                 "WDDM 2.1 DXGI ReclaimResources1 callback");
                  require_result(dxgi_paging_wait_value == UINT64_C(13),
                                 "WDDM 2.1 DXGI paging-fence wait");
                  for (UINT index = 0; index < ARRAYSIZE(reclaim_results);
                       ++index) {
                     require_result(
                        reclaim_results[index] == D3DDDI_RECLAIM_RESULT_OK,
                        "WDDM 2.1 DXGI aliased reclaim-result fan-out");
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
               struct d12_umd_heap *saved_heaps = private_device->heaps;
               struct d12_umd_heap rotate_heaps[3] = {0};
               struct d12_umd_heap_storage rotate_storage[3] = {0};
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
                  .Layout = D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE,
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
                  rotate_storage[index].device = private_device;
                  rotate_storage[index].allocation =
                     rotate_heaps[index].allocation;
                  rotate_storage[index].runtime_resource =
                     runtime_handles[index];
                  rotate_storage[index].cpu_page_property =
                     D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK;
                  InitializeSRWLock(&rotate_storage[index].lock);
                  rotate_heaps[index].storage = &rotate_storage[index];

                  rotate_resources[index].signature =
                     D12_UMD_RESOURCE_SIGNATURE;
                  rotate_resources[index].device = private_device;
                  rotate_resources[index].allocation =
                     rotate_heaps[index].allocation;
                  rotate_resources[index].runtime_heap_resource =
                     runtime_handles[index];
                  rotate_resources[index].desc = rotate_resource_desc;
                  rotate_handles[index] =
                     (DXGI_DDI_HRESOURCE)(uintptr_t)&rotate_resources[index];
                  rotate_heaps[index].next =
                     index + 1 < ARRAYSIZE(rotate_heaps)
                        ? &rotate_heaps[index + 1]
                        : saved_heaps;
               }
               private_device->heaps = &rotate_heaps[0];

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
                        rotate_resources[index].allocation.allocation ==
                           0x92000000u + source &&
                        rotate_storage[index].allocation.allocation ==
                           0x92000000u + source &&
                        rotate_heaps[index].runtime_resource ==
                           runtime_handles[index],
                     "DXGI kernel identity rotates, runtime handle stays");
               }

               const struct d12_umd_allocation before =
                  rotate_heaps[0].allocation;
               rotate_storage[0].lifetime.retain_count = 1;
               result = dxgi_table.pfnRotateResourceIdentities(
                  &rotate_arguments);
               require_result(
                  result == E_INVALIDARG &&
                     memcmp(&before, &rotate_heaps[0].allocation,
                            sizeof(before)) == 0,
                  "reject rotation of CPU-mapped resource transactionally");
               rotate_storage[0].lifetime.retain_count = 0;

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

                  rotate_resources[1].desc.Format =
                     DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                  result = dxgi_table.pfnBlt1(&blt1);
                  require_result(
                     result == DXGI_DDI_ERR_UNSUPPORTED,
                     "DXGI BGRA8 sRGB conversion reaches renderer gate");

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
                     blt_allocator_create_result = S_OK;
                     blt_list_create_result = S_OK;
                     blt_allocator_output = TEST_BLT_ALLOCATOR;
                     blt_list_output = TEST_BLT_LIST;
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

                     blt_allocator_create_result = E_FAIL;
                     result = dxgi_table.pfnBlt1(&blt1);
                     require_result(
                        result == E_FAIL &&
                           blt_allocator_create_calls == 2 &&
                           blt_allocator_destroy_calls == 2 &&
                           blt_list_create_calls == 1 &&
                           blt_list_destroy_calls == 1 &&
                           !blt_queue.dxgi_blt_allocator &&
                           !blt_queue.dxgi_blt_list,
                        "rollback DXGI allocator returned with failure");

                     blt_allocator_create_result = S_OK;
                     blt_allocator_output = NULL;
                     result = dxgi_table.pfnBlt1(&blt1);
                     require_result(
                        result == E_FAIL &&
                           blt_allocator_create_calls == 3 &&
                           blt_allocator_destroy_calls == 2 &&
                           !blt_queue.dxgi_blt_allocator &&
                           !blt_queue.dxgi_blt_list,
                        "reject DXGI allocator success without object");

                     blt_allocator_output = TEST_BLT_ALLOCATOR;
                     blt_list_create_result = E_FAIL;
                     result = dxgi_table.pfnBlt1(&blt1);
                     require_result(
                        result == E_FAIL &&
                           blt_allocator_create_calls == 4 &&
                           blt_allocator_destroy_calls == 3 &&
                           blt_list_create_calls == 2 &&
                           blt_list_destroy_calls == 2 &&
                           !blt_queue.dxgi_blt_allocator &&
                           !blt_queue.dxgi_blt_list,
                        "rollback DXGI list returned with failure");

                     blt_list_create_result = S_OK;
                     blt_list_output = NULL;
                     result = dxgi_table.pfnBlt1(&blt1);
                     require_result(
                        result == E_FAIL &&
                           blt_allocator_create_calls == 5 &&
                           blt_allocator_destroy_calls == 4 &&
                           blt_list_create_calls == 3 &&
                           blt_list_destroy_calls == 2 &&
                           !blt_queue.dxgi_blt_allocator &&
                           !blt_queue.dxgi_blt_list,
                        "reject DXGI list success without object");

                     blt_execute_result = S_OK;
                     blt_list_output = TEST_BLT_LIST;
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
               private_device->heaps = saved_heaps;
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
            {
               struct d12_umd_device *private_device =
                  driver_device.pDrvPrivate;
               void *saved_backend = private_device->backend;
               const struct d12_umd_renderer_ops *saved_renderer_ops =
                  private_device->renderer_ops;

               private_device->backend = TEST_BLT_DEVICE;
               private_device->renderer_ops = &test_reused_renderer_ops;
               reused_allocation_info_fail = true;
               memset(&existing_info, 0xcc, sizeof(existing_info));
               existing_allocation = TEST_ALLOCATION;
               set_error_calls = 0;
               last_set_error = S_OK;
               device_table.pfnCheckExistingResourceAllocationInfo(
                  driver_device, resource_handle, &existing_info,
                  &existing_allocation);
               require_result(
                  set_error_calls == 1 && last_set_error == E_FAIL &&
                     existing_info.ResourceDataSize == 0 &&
                     existing_allocation == 0,
                  "existing allocation query failure is transactional");

               reused_allocation_info_fail = false;
               reused_allocation_info_size =
                  sizeof(mapped_heap) + UINT64_C(0x10000);
               memset(&existing_info, 0xcc, sizeof(existing_info));
               existing_allocation = TEST_ALLOCATION;
               set_error_calls = 0;
               last_set_error = S_OK;
               device_table.pfnCheckExistingResourceAllocationInfo(
                  driver_device, resource_handle, &existing_info,
                  &existing_allocation);
               require_result(
                  set_error_calls == 1 && last_set_error == E_FAIL &&
                     existing_info.ResourceDataSize == 0 &&
                     existing_allocation == 0,
                  "reject reflected resource larger than its allocation");

               reused_allocation_info_size = UINT64_C(0x10000);
               private_device->backend = saved_backend;
               private_device->renderer_ops = saved_renderer_ops;
            }
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
            struct d12_umd_resource_private rounded_private;
            struct d12_frontend_heap_desc rounded_heap =
               saved_resource_private.heap;
            struct agxw_alloc_private rounded_allocation =
               saved_allocation_private;
            D3DDDI_OPENALLOCATIONINFO rounded_open_allocation = {
               .hAllocation = TEST_ALLOCATION,
               .pPrivateDriverData = &rounded_allocation,
               .PrivateDriverDataSize = sizeof(rounded_allocation),
            };
            D3D12DDIARG_OPENHEAP_0003 rounded_arguments = {
               .NumAllocations = 1,
               .pOpenAllocationInfo = &rounded_open_allocation,
               .hKMResource = {.handle = TEST_KM_RESOURCE},
               .pPrivateDriverData = &rounded_private,
               .PrivateDriverDataSize = sizeof(rounded_private),
               .InitialResourceState = D3D12DDI_RESOURCE_STATE_COMMON,
            };
            D3D12DDI_HEAP_AND_RESOURCE_SIZES rounded_sizes;

            --rounded_heap.size;
            require_result(
               d12_umd_resource_private_pack(
                  &rounded_heap, &saved_resource_private.resource,
                  (saved_resource_private.flags &
                   D12_UMD_RESOURCE_PRIVATE_HAS_CLEAR_VALUE)
                     ? &saved_resource_private.clear_value
                     : NULL,
                  &rounded_private),
               "pack non-aligned logical heap fixture");
            rounded_sizes =
               device_table.pfnCalcPrivateOpenedHeapAndResourceSizes(
                  driver_device, &rounded_arguments);
            require_result(
               rounded_sizes.Heap > 0 && rounded_sizes.Resource > 0,
               "accept exact page-rounded WDDM backing allocation");

            rounded_allocation.size += AGXW_PAGE_SIZE;
            rounded_sizes =
               device_table.pfnCalcPrivateOpenedHeapAndResourceSizes(
                  driver_device, &rounded_arguments);
            require_result(!rounded_sizes.Heap && !rounded_sizes.Resource,
                           "reject oversized WDDM backing allocation");

            rounded_allocation.size = AGXW_PAGE_SIZE;
            rounded_sizes =
               device_table.pfnCalcPrivateOpenedHeapAndResourceSizes(
                  driver_device, &rounded_arguments);
            require_result(!rounded_sizes.Heap && !rounded_sizes.Resource,
                           "reject undersized WDDM backing allocation");
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
         mapping = (void *)(uintptr_t)UINTPTR_MAX;
         result = device_table.pfnMapHeap(
            driver_device, (D3D12DDI_HHEAP){0}, &mapping);
         require_result(result == E_INVALIDARG && mapping == NULL,
                        "MapHeap clears output for invalid heap");

         {
            struct d12_umd_heap *private_heap = heap_handle.pDrvPrivate;

            private_heap->storage->mapping = mapped_heap;
            private_heap->storage->lifetime.retain_count = UINT32_MAX;
            mapping = (void *)(uintptr_t)UINTPTR_MAX;
            result = device_table.pfnMapHeap(driver_device, heap_handle,
                                             &mapping);
            require_result(result == E_FAIL && mapping == NULL,
                           "MapHeap clears output on retain overflow");
            private_heap->storage->mapping = NULL;
            private_heap->storage->lifetime.retain_count = 0;
         }

         mapping = NULL;
         result = device_table.pfnMapHeap(driver_device, heap_handle, &mapping);
         require_result(result == S_OK && mapping == mapped_heap &&
                           lock_calls == 1,
                        "MapHeap Lock2 callback");
         device_table.pfnUnmapHeap(driver_device, heap_handle);
         require_result(unlock_calls == 1, "UnmapHeap Unlock2 callback");

         {
            struct d12_umd_device *private_device =
               driver_device.pDrvPrivate;
            struct d12_device *saved_backend = private_device->backend;
            const struct d12_umd_renderer_ops *saved_renderer_ops =
               private_device->renderer_ops;
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
            unsigned mapping_lock_base;
            unsigned mapping_unlock_base;
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

            private_device->backend = (struct d12_device *)TEST_BLT_DEVICE;
            private_device->renderer_ops = &test_transaction_renderer_ops;
            transaction_fence_create_calls = 0;
            transaction_fence_destroy_calls = 0;
            transaction_fence_owns_lifetime = false;
            transaction_fence_defer_lifetime_release = false;
            memset(&transaction_fence_placement, 0,
                   sizeof(transaction_fence_placement));
            mapping_lock_base = lock_calls;
            mapping_unlock_base = unlock_calls;

            transaction_fence_result = E_FAIL;
            transaction_fence_output = TEST_TRANSACTION_FENCE;
            result = device_table.pfnCreateFence(
               driver_device, driver_fence, &fence_arguments);
            require_result(
               result == E_FAIL && transaction_fence_create_calls == 1 &&
                  transaction_fence_destroy_calls == 1 &&
                  lock_calls == mapping_lock_base + 1 &&
                  unlock_calls == mapping_unlock_base + 1 &&
                  ((struct d12_umd_fence *)driver_fence.pDrvPrivate)
                        ->signature == 0,
               "rollback backend fence returned with failure");

            transaction_fence_result = S_OK;
            transaction_fence_output = NULL;
            result = device_table.pfnCreateFence(
               driver_device, driver_fence, &fence_arguments);
            require_result(
               result == E_FAIL && transaction_fence_create_calls == 2 &&
                  transaction_fence_destroy_calls == 1 &&
                  lock_calls == mapping_lock_base + 2 &&
                  unlock_calls == mapping_unlock_base + 2 &&
                  ((struct d12_umd_fence *)driver_fence.pDrvPrivate)
                        ->signature == 0,
               "reject backend fence success without object");

            private_device->renderer_ops =
               &test_incomplete_transaction_renderer_ops;
            transaction_fence_output = TEST_TRANSACTION_FENCE;
            result = device_table.pfnCreateFence(
               driver_device, driver_fence, &fence_arguments);
            require_result(
               result == E_NOTIMPL &&
                  transaction_fence_create_calls == 2 &&
                  transaction_fence_destroy_calls == 1 &&
                  lock_calls == mapping_lock_base + 2 &&
                  unlock_calls == mapping_unlock_base + 2,
               "require symmetric backend fence destruction");

            private_device->renderer_ops = &test_transaction_renderer_ops;
            transaction_fence_defer_lifetime_release = true;
            result = device_table.pfnCreateFence(
               driver_device, driver_fence, &fence_arguments);
            require_result(
               result == S_OK && transaction_fence_create_calls == 3 &&
                  lock_calls == mapping_lock_base + 3 &&
                  unlock_calls == mapping_unlock_base + 2,
               "create transactional backend fence");
            device_table.pfnDestroyFence(driver_device, driver_fence);
            require_result(
               transaction_fence_destroy_calls == 2 &&
                  unlock_calls == mapping_unlock_base + 2 &&
                  transaction_fence_owns_lifetime,
               "renderer retains placement after DDI fence destruction");

            private_device->renderer_ops = saved_renderer_ops;
            private_device->backend = saved_backend;

            fence_arguments.FenceCount = 2;
            require_result(device_table.pfnCalcPrivateFenceSize(
                              driver_device, &fence_arguments) == 0,
                           "reject unsupported multi-adapter fence");
            HeapFree(GetProcessHeap(), 0, driver_fence.pDrvPrivate);
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
                              make_resident_allocation_count == 1 &&
                              make_arguments.WaitMask == 1 &&
                              paging_fence == UINT64_C(11),
                           "MakeResident callback and paging fence");
            result = device_table.pfnEvict(driver_device, &evict_arguments);
            require_result(result == S_OK && evict_calls == 1 &&
                              evict_allocation_count == 1,
                           "Evict callback translation");
            result = device_table.pfnOfferResources(driver_device,
                                                     &offer_arguments);
            require_result(result == S_OK && offer_calls == 1 &&
                              offer_allocation_count == 1,
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
                              reclaim_allocation_count == 1 && discarded &&
                              reclaim_arguments.WaitMask == 1 &&
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

#if D12_UMD_TARGETS_WDDM21
               {
                  D3D12DDI_DEBUG_VIRTUAL_ADDRESS_ALLOCATION_INFO_0012
                     va_info;
                  d12_test_debug_kmt_allocation_info kmt_info;
                  UINT va_count = 1;
                  UINT kmt_count = 1;

                  memset(&va_info, 0xcc, sizeof(va_info));
                  memset(&kmt_info, 0xcc, sizeof(kmt_info));
                  root11_table.pfnGetDebugAllocationInfo(
                     driver_device, pageable_objects[1], &va_count,
                     &va_info, &kmt_count, &kmt_info);
                  require_result(va_count == 0 && kmt_count == 0,
                                 "CPU-only pageable debug allocation");
               }
#endif

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
               objects[0] = (D3D12DDI_HANDLE_AND_TYPE){
                  .Handle = heap_handle.pDrvPrivate,
                  .Type = D3D12DDI_HT_HEAP,
               };
               make_arguments.NumObjects = ARRAYSIZE(objects);
               make_arguments.pObjects = objects;
               paging_fence = 0;
               result = release1_table.pfnMakeResident(driver_device,
                                                        &make_arguments);
               require_result(result == E_PENDING &&
                                 make_resident_calls == 3 &&
                                 make_resident_allocation_count == 1 &&
                                 make_arguments.WaitMask == 1 &&
                                 paging_fence == UINT64_C(11),
                              "deduplicated large residency batch");
               result = release1_table.pfnEvict(driver_device, &large_evict);
               require_result(result == S_OK && evict_calls == 2 &&
                                 evict_allocation_count == 1,
                              "deduplicated large eviction batch");
               result = release1_table.pfnOfferResources(driver_device,
                                                          &large_offer);
               require_result(result == S_OK && offer_calls == 2 &&
                                 offer_allocation_count == 1,
                              "deduplicated large offer batch");

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
                                 reclaim_allocation_count == 1 &&
                                 reclaim_arguments.WaitMask == 1 &&
                                 paging_fence == UINT64_C(13),
                              "deduplicated large reclaim batch");
               for (UINT index = 0; index < ARRAYSIZE(objects); ++index) {
                  require_result(discarded_objects[index],
                                 "large reclaim maps every discarded result");
               }
            }
         }

         {
            const unsigned deallocation_base = deallocation_calls;
            const unsigned unlock_base = unlock_calls;

            device_table.pfnDestroyHeapAndResource(driver_device,
                                                   heap_handle,
                                                   resource_handle);
            require_result(
               deallocation_calls == deallocation_base &&
                  unlock_calls == unlock_base &&
                  transaction_fence_owns_lifetime,
               "defer heap teardown while queued fence owns placement");
            test_transaction_release_fence_lifetime();
            require_result(
               deallocation_calls == deallocation_base + 1 &&
                  unlock_calls == unlock_base + 1 &&
                  !transaction_fence_owns_lifetime,
               "final fence retirement unlocks and deallocates heap");
            transaction_fence_defer_lifetime_release = false;
         }
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
         D3D12DDIARG_CREATERESOURCE_0003 texture_arguments = {
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
         D3D12DDI_RESOURCE_ALLOCATION_INFO allocation_info;
         D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes;

         memset(&allocation_info, 0xcc, sizeof(allocation_info));
         set_error_calls = 0;
         device_table.pfnCheckResourceAllocationInfo(
            driver_device, &texture_arguments,
            D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_SHADER_RESOURCE, 0, 1,
            &allocation_info);
         require_result(
            set_error_calls == 0 &&
               allocation_info.ResourceDataSize == texture_arguments.Width &&
               allocation_info.ResourceDataAlignment == UINT64_C(0x10000) &&
               allocation_info.Layout ==
                  D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE,
            "resolve undefined texture layout during size reflection");

         sizes = device_table.pfnCalcPrivateHeapAndResourceSizes(
            driver_device, &heap_arguments, &texture_arguments);
         require_result(!sizes.Heap && !sizes.Resource,
                        "reject unresolved layout during resource creation");

         texture_arguments.Layout =
            D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE;
         sizes = device_table.pfnCalcPrivateHeapAndResourceSizes(
            driver_device, &heap_arguments, &texture_arguments);
         require_result(sizes.Heap > 0 && sizes.Resource > 0,
                        "resolved texture heap/resource private sizes");

         texture_arguments.Layout =
            D3D12DDI_TL_64KB_TILE_STANDARD_SWIZZLE;
         memset(&allocation_info, 0xcc, sizeof(allocation_info));
         set_error_calls = 0;
         device_table.pfnCheckResourceAllocationInfo(
            driver_device, &texture_arguments,
            D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_SHADER_RESOURCE, 0, 1,
            &allocation_info);
         require_result(set_error_calls == 1 &&
                           last_set_error == E_INVALIDARG &&
                           allocation_info.ResourceDataSize == 0 &&
                           allocation_info.Layout == D3D12DDI_TL_UNDEFINED,
                        "reject unadvertised standard-swizzle layout");
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
         const D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes =
            device_table.pfnCalcPrivateHeapAndResourceSizes(
               driver_device, &heap_arguments, NULL);
         const D3D12DDI_HRTRESOURCE runtime_resource = {
            .handle = TEST_RT_RESOURCE,
         };
         D3D12DDI_HHEAP heap_handle;
         void *mapping = NULL;
         const unsigned allocation_base = allocation_calls;
         const unsigned deallocation_base = deallocation_calls;
         const unsigned lock_base = lock_calls;
         const unsigned unlock_base = unlock_calls;
         struct d12_umd_device *private_device =
            driver_device.pDrvPrivate;

         require_result(sizes.Heap > 0 && sizes.Resource == 0,
                        "teardown-audit heap private size");
         heap_handle.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Heap);
         require_result(heap_handle.pDrvPrivate != NULL,
                        "allocate teardown-audit heap storage");
         result = device_table.pfnCreateHeapAndResource(
            driver_device, &heap_arguments, heap_handle, runtime_resource,
            NULL, NULL, (D3D12DDI_HRESOURCE){0});
         require_result(result == S_OK &&
                           allocation_calls == allocation_base + 1 &&
                           private_device->heap_storage_count == 1,
                        "register driver-owned heap storage");
         result = device_table.pfnMapHeap(driver_device, heap_handle,
                                          &mapping);
         require_result(result == S_OK && mapping == mapped_heap &&
                           lock_calls == lock_base + 1,
                        "retain application map through DestroyDevice");

         functions.pfnDestroyDevice(driver_device);
         require_result(unlock_calls == unlock_base + 1 &&
                           deallocation_calls == deallocation_base + 1,
                        "DestroyDevice abandons maps and drains heap storage");
         HeapFree(GetProcessHeap(), 0, heap_handle.pDrvPrivate);
      }
      HeapFree(GetProcessHeap(), 0, driver_device.pDrvPrivate);

      {
         const D3D12DDIARG_CALCPRIVATEDEVICESIZE release1_size_arguments = {
            .Interface = D3D12DDI_INTERFACE_VERSION_R1,
            .Version = D3D12DDI_BUILD_VERSION_0011,
            .Flags = D3D12DDI_CREATE_DEVICE_FLAG_NONE,
         };
         const D3D12DDIARG_CALCPRIVATEDEVICESIZE root11_size_arguments = {
            .Interface = D3D12DDI_INTERFACE_VERSION_R1,
            .Version = D3D12DDI_BUILD_VERSION_0013,
            .Flags = D3D12DDI_CREATE_DEVICE_FLAG_NONE,
         };
         const SIZE_T release1_device_size =
            functions.pfnCalcPrivateDeviceSize(
               open_arguments.hAdapter, &release1_size_arguments);
         const SIZE_T root11_device_size =
            functions.pfnCalcPrivateDeviceSize(
               open_arguments.hAdapter, &root11_size_arguments);

#if D12_UMD_TARGETS_WDDM21_EXACT
         require_result(release1_device_size == device_size &&
                           root11_device_size == device_size,
                        "Release 1 versioned private device sizes");
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

         driver_device.pDrvPrivate = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, root11_device_size);
         require_result(driver_device.pDrvPrivate != NULL,
                        "allocate root-signature 1.1 device storage");
         create_arguments.Version = D3D12DDI_BUILD_VERSION_0013;
         create_arguments.hDrvDevice = driver_device;
         result = functions.pfnCreateDevice(open_arguments.hAdapter,
                                             &create_arguments);
         require_result(result == DXGI_STATUS_NO_REDIRECTION,
                        "CreateDevice root-signature 1.1 result");
         functions.pfnDestroyDevice(driver_device);
         HeapFree(GetProcessHeap(), 0, driver_device.pDrvPrivate);
#else
         require_result(release1_device_size == 0 && root11_device_size == 0,
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
