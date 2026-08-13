/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * This is cross-compiled, not host-run.  It makes every runtime-facing width
 * and offset used by the adapter milestone a build-breaking contract.
 */

#include "d12_umd_wddm.h"
#include <windows.h>
#include <d3d12umddi.h>
#include <dxgiddi.h>

#include "../d12_umd_abi.h"
#include "../d12_umd_resource_private.h"
#include "../../d12_dxbc_wrap.h"
#include "../../d12_frontend.h"

#include <stddef.h>

_Static_assert(sizeof(void *) == 8, "the UMD must be native ARM64");
_Static_assert(sizeof(D3D12DDI_HADAPTER) == 8, "HADAPTER width");
_Static_assert(sizeof(D3D12DDI_HDEVICE) == 8, "HDEVICE width");
_Static_assert(sizeof(D3D12DDI_HRTADAPTER) == 8, "HRTADAPTER width");
_Static_assert(sizeof(D3D12DDIARG_OPENADAPTER) == 32,
               "OPENADAPTER width");
_Static_assert(offsetof(D3D12DDIARG_OPENADAPTER, hRTAdapter) == 0,
               "OPENADAPTER hRTAdapter offset");
_Static_assert(offsetof(D3D12DDIARG_OPENADAPTER, hAdapter) == 8,
               "OPENADAPTER hAdapter offset");
_Static_assert(offsetof(D3D12DDIARG_OPENADAPTER, pAdapterCallbacks) == 16,
               "OPENADAPTER callbacks offset");
_Static_assert(offsetof(D3D12DDIARG_OPENADAPTER, pAdapterFuncs) == 24,
               "OPENADAPTER functions offset");
_Static_assert(sizeof(D3D12DDI_ADAPTERFUNCS) == 64,
               "adapter table width");
_Static_assert(sizeof(D3D12DDIARG_CALCPRIVATEDEVICESIZE) == 12,
               "private device size arguments width");
_Static_assert(sizeof(D3D12DDIARG_CREATEDEVICE_0003) == 48,
               "CREATEDEVICE_0003 width");
_Static_assert(sizeof(DXGI_DDI_BASE_ARGS) == 16,
               "DXGI base arguments width");
#if D12_UMD_TARGETS_WDDM24
_Static_assert(sizeof(D3DDDI_ADAPTERCALLBACKS) == 24,
               "WDDM 2.4 adapter callback table width");
_Static_assert(offsetof(D3DDDI_ADAPTERCALLBACKS,
                        pfnQueryAdapterInfoCb2) == 16,
               "WDDM 2.4 query-adapter-info2 callback offset");
_Static_assert(sizeof(D3DDDICB_QUERYADAPTERINFO2) == 24,
               "WDDM 2.4 query-adapter-info2 arguments width");
_Static_assert(offsetof(D3DDDICB_QUERYADAPTERINFO2,
                        pPrivateDriverData) == 8,
               "WDDM 2.4 query-adapter-info2 data offset");
_Static_assert(sizeof(D3DDDI_DEVICECALLBACKS) == 504,
               "WDDM 2.4.2 device callback table width");
_Static_assert(offsetof(D3DDDI_DEVICECALLBACKS,
                        pfnSubmitPresentBltToHwQueueCb) == 496,
               "WDDM 2.4.2 present-blt callback tail offset");
#elif D12_UMD_TARGETS_WDDM23_EXACT
_Static_assert(sizeof(D3DDDI_DEVICECALLBACKS) == 496,
               "WDDM 2.3 device callback table width");
_Static_assert(offsetof(D3DDDI_DEVICECALLBACKS, pfnCreateHwContextCb) == 440,
               "WDDM 2.3 create-hardware-context callback offset");
_Static_assert(offsetof(D3DDDI_DEVICECALLBACKS,
                        pfnSubmitSignalSyncObjectsToHwQueueCb) == 488,
               "WDDM 2.3 hardware-queue callback tail offset");
#elif D12_UMD_TARGETS_WDDM21
_Static_assert(sizeof(D3DDDI_DEVICECALLBACKS) == 440,
               "WDDM 2.1 device callback table width");
_Static_assert(offsetof(D3DDDI_DEVICECALLBACKS,
                        pfnUpdateAllocationPropertyCb) == 400,
               "WDDM 2.1 update-allocation callback offset");
_Static_assert(offsetof(D3DDDI_DEVICECALLBACKS, pfnOfferAllocations2Cb) == 408,
               "WDDM 2.1 offer callback offset");
_Static_assert(offsetof(D3DDDI_DEVICECALLBACKS, pfnReclaimAllocations3Cb) == 416,
               "WDDM 2.1 reclaim callback offset");
_Static_assert(offsetof(D3DDDI_DEVICECALLBACKS, pfnAcquireResourceCb) == 424,
               "WDDM 2.1 acquire-resource callback offset");
_Static_assert(offsetof(D3DDDI_DEVICECALLBACKS, pfnReleaseResourceCb) == 432,
               "WDDM 2.1 release-resource callback offset");
_Static_assert(sizeof(D3DDDICB_OFFERALLOCATIONS2) == 32,
               "WDDM 2.1 offer arguments width");
_Static_assert(sizeof(D3DDDICB_RECLAIMALLOCATIONS3) == 40,
               "WDDM 2.1 reclaim arguments width");
_Static_assert(offsetof(D3DDDICB_RECLAIMALLOCATIONS3, PagingFenceValue) == 32,
               "WDDM 2.1 reclaim fence offset");
_Static_assert(sizeof(D3DDDICB_SYNCTOKEN) == 24,
               "WDDM 2.1 sync-token arguments width");
#else
_Static_assert(sizeof(D3DDDI_DEVICECALLBACKS) == 400,
               "WDDM 2.0 device callback table width");
_Static_assert(sizeof(DXGI1_4_DDI_BASE_FUNCTIONS) == 168,
               "WDDM 2.0 DXGI table width");
_Static_assert(offsetof(DXGI1_4_DDI_BASE_FUNCTIONS, pfnReserved1) == 104,
               "DXGI reserved slot 1 offset");
_Static_assert(offsetof(DXGI1_4_DDI_BASE_FUNCTIONS, pfnReserved2) == 120,
               "DXGI reserved slot 2 offset");
#endif
#if D12_UMD_TARGETS_WDDM21
_Static_assert(sizeof(DXGI1_5_DDI_BASE_FUNCTIONS) == 176,
               "WDDM 2.1+ DXGI table width");
_Static_assert(offsetof(DXGI1_5_DDI_BASE_FUNCTIONS,
                        pfnOfferResources1) == 72,
               "WDDM 2.1+ DXGI offer slot offset");
_Static_assert(offsetof(DXGI1_5_DDI_BASE_FUNCTIONS,
                        pfnReclaimResources1) == 168,
               "WDDM 2.1+ DXGI reclaim slot offset");
#endif
_Static_assert(offsetof(D3D12DDIARG_CREATEDEVICE_0003, pKTCallbacks) == 16,
               "CREATEDEVICE kernel callbacks offset");
_Static_assert(offsetof(D3D12DDIARG_CREATEDEVICE_0003, hDrvDevice) == 24,
               "CREATEDEVICE private storage offset");
_Static_assert(offsetof(D3D12DDIARG_CREATEDEVICE_0003, p12UMCallbacks) == 32,
               "CREATEDEVICE core callbacks offset");
_Static_assert(sizeof(D3D12DDI_TABLE_REQUEST) == 8,
               "optional table request width");
#if D12_UMD_TARGETS_WDDM23
_Static_assert(sizeof(D3D12DDI_CORELAYER_DEVICECALLBACKS_0003) == 96,
               "baseline core callback width");
_Static_assert(sizeof(D3D12DDI_CORELAYER_DEVICECALLBACKS_0022) == 112,
               "DDI 0022 core callback width");
_Static_assert(offsetof(D3D12DDI_CORELAYER_DEVICECALLBACKS_0022,
                        pfnAllocateCb) == 96,
               "DDI 0022 allocation callback offset");
_Static_assert(sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0030) == 736,
               "DDI 0030 device table width");
_Static_assert(sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0032) == 456,
               "DDI 0032 command-list table width");
_Static_assert(sizeof(D3D12DDI_D3D12_OPTIONS_DATA_0032) == 44,
               "DDI 0032 options width");
_Static_assert(sizeof(D3D12DDI_RESOURCE_ALLOCATION_INFO_0022) == 48,
               "DDI 0022 resource allocation info width");
_Static_assert(sizeof(D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032) == 16,
               "DDI 0032 immediate-write parameter width");
#endif
#if D12_UMD_TARGETS_WDDM24
_Static_assert(sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0040) == 768,
               "DDI 0040 device table width");
_Static_assert(sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0040) == 464,
               "DDI 0040 command-list table width");
_Static_assert(sizeof(D3D12DDI_D3D12_OPTIONS_DATA_0041) == 60,
               "DDI 0041 options width");
_Static_assert(sizeof(D3D12DDI_MEMORY_ARCHITECTURE_CAPS_0041) == 20,
               "DDI 0041 memory-architecture caps width");
_Static_assert(sizeof(D3D12DDI_SHADER_CAPS_0042) == 44,
               "DDI 0042 shader caps width");
_Static_assert(D3D12DDI_SHADER_MODEL_6_2_RELEASE_0042 == UINT32_C(0x00060025),
               "DDI 0042 Shader Model 6.2 value");
_Static_assert(sizeof(D3D12DDIARG_CREATE_COMMAND_POOL_0040) == 4,
               "DDI 0040 command-pool arguments width");
_Static_assert(sizeof(D3D12DDIARG_CREATE_COMMAND_RECORDER_0040) == 8,
               "DDI 0040 command-recorder arguments width");
_Static_assert(sizeof(D3D12DDIARG_CREATE_COMMAND_LIST_0040) == 24,
               "DDI 0040 command-list arguments width");
_Static_assert(sizeof(D3D12DDIARG_RESETCOMMANDLIST_0040) == 24,
               "DDI 0040 reset-command-list arguments width");
_Static_assert(offsetof(D3D12DDIARG_RESETCOMMANDLIST_0040, ID) == 8,
               "DDI 0040 reset-command-list id offset");
#endif
_Static_assert(sizeof(D3D12DDIARG_CREATECOMMANDALLOCATOR) == 16,
               "CREATECOMMANDALLOCATOR width");
_Static_assert(offsetof(D3D12DDIARG_CREATECOMMANDALLOCATOR,
                        hDrvCommandAllocator) == 0,
               "CREATECOMMANDALLOCATOR handle offset");
_Static_assert(offsetof(D3D12DDIARG_CREATECOMMANDALLOCATOR, Type) == 8,
               "CREATECOMMANDALLOCATOR type offset");
_Static_assert(offsetof(D3D12DDIARG_CREATECOMMANDALLOCATOR, QueueFlags) == 12,
               "CREATECOMMANDALLOCATOR queue flags offset");
_Static_assert(sizeof(D3D12DDIARG_CREATE_COMMAND_LIST_0001) == 48,
               "CREATE_COMMAND_LIST_0001 width");
_Static_assert(offsetof(D3D12DDIARG_CREATE_COMMAND_LIST_0001,
                        hDrvCommandAllocator) == 16,
               "CREATE_COMMAND_LIST allocator offset");
_Static_assert(offsetof(D3D12DDIARG_CREATE_COMMAND_LIST_0001, ID) == 32,
               "CREATE_COMMAND_LIST id offset");
_Static_assert(sizeof(D3D12DDIARG_RESETCOMMANDLIST) == 32,
               "RESETCOMMANDLIST width");
_Static_assert(offsetof(D3D12DDIARG_RESETCOMMANDLIST, ID) == 16,
               "RESETCOMMANDLIST id offset");
_Static_assert(sizeof(D3D12DDI_DESCRIPTOR_RANGE) == 20,
               "DESCRIPTOR_RANGE width");
_Static_assert(sizeof(D3D12DDI_ROOT_DESCRIPTOR_TABLE) == 16,
               "ROOT_DESCRIPTOR_TABLE width");
_Static_assert(sizeof(D3D12DDI_ROOT_CONSTANTS) == 12,
               "ROOT_CONSTANTS width");
_Static_assert(sizeof(D3D12DDI_ROOT_PARAMETER) == 32,
               "ROOT_PARAMETER width");
_Static_assert(offsetof(D3D12DDI_ROOT_PARAMETER, ShaderVisibility) == 24,
               "ROOT_PARAMETER visibility offset");
_Static_assert(sizeof(D3D12DDI_STATIC_SAMPLER) == 52,
               "STATIC_SAMPLER width");
_Static_assert(sizeof(D3D12DDI_ROOT_SIGNATURE) == 40,
               "ROOT_SIGNATURE width");
_Static_assert(offsetof(D3D12DDI_ROOT_SIGNATURE, pRootParameters) == 8,
               "ROOT_SIGNATURE parameter pointer offset");
_Static_assert(offsetof(D3D12DDI_ROOT_SIGNATURE, pStaticSamplers) == 24,
               "ROOT_SIGNATURE sampler pointer offset");
_Static_assert(sizeof(D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001) == 16,
               "CREATE_ROOT_SIGNATURE width");
_Static_assert(sizeof(D3D12DDIARG_SIGNATURE_ENTRY) == 20,
               "SIGNATURE_ENTRY width");
_Static_assert(offsetof(D3D12DDIARG_SIGNATURE_ENTRY, Mask) == 8,
               "SIGNATURE_ENTRY mask offset");
_Static_assert(sizeof(D3D12DDIARG_STAGE_IO_SIGNATURES) == 32,
               "STAGE_IO_SIGNATURES width");
_Static_assert(sizeof(D3D12DDIARG_TESSELLATION_IO_SIGNATURES) == 48,
               "TESSELLATION_IO_SIGNATURES width");
_Static_assert(sizeof(D3D12DDIARG_INPUT_ELEMENT_DESC) == 24,
               "INPUT_ELEMENT_DESC width");
_Static_assert(sizeof(D3D12DDIARG_CREATEELEMENTLAYOUT) == 16,
               "CREATEELEMENTLAYOUT width");
_Static_assert(sizeof(D3D12DDI_RENDER_TARGET_BLEND_DESC) == 40,
               "RENDER_TARGET_BLEND_DESC width");
_Static_assert(sizeof(D3D12DDI_BLEND_DESC) == 328,
               "BLEND_DESC width");
_Static_assert(sizeof(D3D12DDI_DEPTH_STENCIL_DESC) == 60,
               "DEPTH_STENCIL_DESC width");
_Static_assert(sizeof(D3D12DDI_RASTERIZER_DESC) == 48,
               "RASTERIZER_DESC width");
_Static_assert(sizeof(D3D12DDIARG_CREATE_PIPELINE_STATE_0001) == 168,
               "CREATE_PIPELINE_STATE_0001 width");
_Static_assert(sizeof(PFND3D12DDI_RETRIEVE_SHADER_COMMENT_0003) ==
                  sizeof(void *),
               "RETRIEVE_SHADER_COMMENT function-pointer width");
_Static_assert(sizeof(D3D12DDI_HANDLETYPE) == 4,
               "serialized-object handle-type width");
_Static_assert(D3D12DDI_HT_PIPELINE_STATE == 21,
               "pipeline-state handle-type encoding");
_Static_assert(offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0001,
                        hRootSignature) == 56,
               "PIPELINE_STATE root-signature offset");
_Static_assert(offsetof(D3D12DDIARG_CREATE_PIPELINE_STATE_0001,
                        RTVFormats) == 116,
               "PIPELINE_STATE render-target formats offset");
_Static_assert(sizeof(D3D12DDIARG_CREATEHEAP_0001) == 40,
               "CREATEHEAP_0001 width");
_Static_assert(offsetof(D3D12DDIARG_CREATEHEAP_0001, Flags) == 24,
               "CREATEHEAP flags offset");
_Static_assert(sizeof(D3D12DDIARG_CREATERESOURCE_0003) == 72,
               "CREATERESOURCE_0003 width");
_Static_assert(offsetof(D3D12DDIARG_CREATERESOURCE_0003, Width) == 24,
               "CREATERESOURCE width offset");
_Static_assert(offsetof(D3D12DDIARG_CREATERESOURCE_0003,
                        pRowMajorLayout) == 64,
               "CREATERESOURCE row-major pointer offset");
_Static_assert(sizeof(D3D12DDI_CLEAR_VALUES) == 20,
               "CLEAR_VALUES width");
_Static_assert(sizeof(D3D12DDI_RESOURCE_ALLOCATION_INFO) == 56,
               "RESOURCE_ALLOCATION_INFO width");
_Static_assert(sizeof(D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001) == 16,
               "CREATE_DESCRIPTOR_HEAP_0001 width");
_Static_assert(offsetof(D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001,
                        NumDescriptors) == 4,
               "descriptor heap count offset");
_Static_assert(sizeof(D3D12DDI_CPU_DESCRIPTOR_HANDLE) == 8,
               "CPU descriptor handle width");
_Static_assert(sizeof(D3D12DDI_GPU_DESCRIPTOR_HANDLE) == 8,
               "GPU descriptor handle width");
_Static_assert(sizeof(D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW) == 24,
               "stream-output buffer-view width");
_Static_assert(offsetof(D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW,
                        BufferFilledSizeLocation) == 16,
               "stream-output filled-size address offset");
_Static_assert(sizeof(D3D12DDI_CONSTANT_BUFFER_VIEW_DESC) == 16,
               "constant-buffer view width");
_Static_assert(sizeof(D3D12DDI_SAMPLER_DESC) == 52,
               "sampler description width");
_Static_assert(sizeof(D3D12DDIARG_CREATE_SAMPLER) == 8,
               "CREATE_SAMPLER width");
_Static_assert(sizeof(D3D12DDIARG_CREATE_QUERY_HEAP_0001) == 12,
               "CREATE_QUERY_HEAP_0001 width");
_Static_assert(offsetof(D3D12DDIARG_CREATE_QUERY_HEAP_0001, Count) == 4,
               "CREATE_QUERY_HEAP count offset");
_Static_assert(sizeof(D3D12DDIARG_OFFERRESOURCES) == 24,
               "OFFERRESOURCES width");
_Static_assert(offsetof(D3D12DDIARG_OFFERRESOURCES, pObjects) == 8,
               "OFFERRESOURCES object-array offset");
_Static_assert(sizeof(D3D12DDIARG_RECLAIMRESOURCES_0001) == 56,
               "RECLAIMRESOURCES_0001 width");
_Static_assert(offsetof(D3D12DDIARG_RECLAIMRESOURCES_0001,
                        pPagingFenceValue) == 40,
               "RECLAIMRESOURCES paging-fence offset");
_Static_assert(sizeof(D3D12DDI_INDIRECT_ARGUMENT_DESC) == 16,
               "INDIRECT_ARGUMENT_DESC width");
_Static_assert(D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW == 0 &&
                  D3D12DDI_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED == 1 &&
                  D3D12DDI_INDIRECT_ARGUMENT_TYPE_DISPATCH == 2 &&
                  D3D12DDI_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW == 3 &&
                  D3D12DDI_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW == 4 &&
                  D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT == 5 &&
                  D3D12DDI_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW == 6 &&
                  D3D12DDI_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW == 7 &&
                  D3D12DDI_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW == 8,
               "baseline indirect argument encodings");
_Static_assert(D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS == 1 &&
                  D3D12DDI_ROOT_PARAMETER_TYPE_CBV == 2 &&
                  D3D12DDI_ROOT_PARAMETER_TYPE_SRV == 3 &&
                  D3D12DDI_ROOT_PARAMETER_TYPE_UAV == 4,
               "baseline root-parameter encodings");
_Static_assert(sizeof(D3D12DDI_VERTEX_BUFFER_VIEW) == 16,
               "indirect vertex-buffer view width");
_Static_assert(offsetof(D3D12DDI_VERTEX_BUFFER_VIEW, SizeInBytes) == 8,
               "indirect vertex-buffer size offset");
_Static_assert(offsetof(D3D12DDI_VERTEX_BUFFER_VIEW, StrideInBytes) == 12,
               "indirect vertex-buffer stride offset");
_Static_assert(sizeof(D3D12DDI_INDEX_BUFFER_VIEW) == 16,
               "indirect index-buffer view width");
_Static_assert(offsetof(D3D12DDI_INDEX_BUFFER_VIEW, Format) == 12,
               "indirect index-buffer format offset");
_Static_assert(sizeof(D3D12DDI_DRAW_ARGUMENTS) == 16,
               "indirect draw arguments width");
_Static_assert(sizeof(D3D12DDI_DRAW_INDEXED_ARGUMENTS) == 20,
               "indirect indexed-draw arguments width");
_Static_assert(sizeof(D3D12DDI_DISPATCH_ARGUMENTS) == 12,
               "indirect dispatch arguments width");
_Static_assert(sizeof(D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001) == 32,
               "CREATE_COMMAND_SIGNATURE_0001 width");
_Static_assert(offsetof(D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001,
                        hRootSignature) == 16,
               "CREATE_COMMAND_SIGNATURE root-signature offset");
_Static_assert(sizeof(D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002) == 48,
               "CREATE_SHADER_RESOURCE_VIEW_0002 width");
_Static_assert(offsetof(D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002,
                        hDrvResource) == 0,
               "SRV resource handle offset");
_Static_assert(sizeof(D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002) == 56,
               "CREATE_UNORDERED_ACCESS_VIEW_0002 width");
_Static_assert(sizeof(D3D12DDIARG_CREATE_RENDER_TARGET_VIEW_0002) == 32,
               "CREATE_RENDER_TARGET_VIEW_0002 width");
_Static_assert(sizeof(D3D12DDIARG_CREATE_DEPTH_STENCIL_VIEW) == 32,
               "CREATE_DEPTH_STENCIL_VIEW width");
_Static_assert(sizeof(D3D12DDI_FENCE_PLACEMENT) == 8,
               "fence placement width");
_Static_assert(sizeof(D3D12DDI_FENCE) == 24, "fence description width");
_Static_assert(offsetof(D3D12DDI_FENCE, FenceMonitoredValue) == 8,
               "monitored fence placement offset");
_Static_assert(offsetof(D3D12DDI_FENCE, Flags) == 16,
               "fence flags offset");
_Static_assert(sizeof(D3D12DDIARG_CREATE_FENCE) == 16,
               "CREATE_FENCE width");
_Static_assert(sizeof(D3D12DDIARG_FENCE_OPERATION) == 24,
               "fence operation width");
_Static_assert(offsetof(D3D12DDIARG_FENCE_OPERATION, Value) == 8,
               "fence operation value offset");
_Static_assert(offsetof(D3D12DDIARG_FENCE_OPERATION,
                        PhysicalAdapterMask) == 16,
               "fence operation adapter-mask offset");
_Static_assert(sizeof(D3D12DDIARG_OPENHEAP_0003) == 40,
               "OPENHEAP_0003 width");
_Static_assert(offsetof(D3D12DDIARG_OPENHEAP_0003,
                        pOpenAllocationInfo) == 8,
               "OPENHEAP allocation-array offset");
_Static_assert(offsetof(D3D12DDIARG_OPENHEAP_0003,
                        pPrivateDriverData) == 24,
               "OPENHEAP private-data offset");
_Static_assert(sizeof(D3D12DDIARG_PRESENT_0001) == 72,
               "PRESENT_0001 width");
_Static_assert(sizeof(D3D12DDIARG_BLT) == 72, "BLT width");
_Static_assert(offsetof(D3D12DDIARG_BLT, hSrcResource) == 32,
               "BLT source-resource offset");
_Static_assert(offsetof(D3D12DDIARG_BLT, Flags) == 60,
               "BLT flags offset");
_Static_assert(offsetof(D3D12DDIARG_PRESENT_0001,
                        pPrivateDriverData) == 56,
               "PRESENT private-data offset");
_Static_assert(sizeof(D3D12DDI_PRESENT_0003) == 1056,
               "PRESENT_0003 width");
_Static_assert(offsetof(D3D12DDI_PRESENT_0003, AddedGpuWork) == 1048,
               "PRESENT AddedGpuWork offset");
_Static_assert(sizeof(struct d12_frontend_heap_desc) == 40,
               "frontend heap description width");
_Static_assert(sizeof(struct d12_frontend_resource_desc) == 64,
               "frontend resource description width");
_Static_assert(sizeof(struct d12_frontend_clear_value) == 20,
               "frontend clear value width");
_Static_assert(sizeof(struct d12_frontend_heap_allocation) == 24,
               "frontend heap allocation width");
_Static_assert(sizeof(struct d12_frontend_descriptor_heap_desc) == 16,
               "frontend descriptor heap description width");
_Static_assert(sizeof(struct d12_frontend_srv_desc) == 40,
               "frontend SRV description width");
_Static_assert(sizeof(struct d12_frontend_uav_desc) == 40,
               "frontend UAV description width");
_Static_assert(sizeof(struct d12_frontend_rtv_desc) == 24,
               "frontend RTV description width");
_Static_assert(sizeof(struct d12_frontend_dsv_desc) == 24,
               "frontend DSV description width");
_Static_assert(sizeof(struct d12_frontend_sampler_desc) == 52,
               "frontend sampler description width");
_Static_assert(sizeof(struct d12_frontend_stream_output_buffer_view) == 24,
               "frontend stream-output view width");
_Static_assert(sizeof(D3D12DDI_TILED_RESOURCE_COORDINATE) == 16,
               "DDI tiled coordinate width");
_Static_assert(sizeof(struct d12_frontend_tiled_resource_coordinate) == 16,
               "frontend tiled coordinate width");
_Static_assert(sizeof(D3D12DDI_TILE_REGION_SIZE) == 16,
               "DDI tile region width");
_Static_assert(sizeof(struct d12_frontend_tile_region_size) == 16,
               "frontend tile region width");
_Static_assert(offsetof(D3D12DDI_TILE_REGION_SIZE, Height) == 12 &&
                  offsetof(D3D12DDI_TILE_REGION_SIZE, Depth) == 14,
               "DDI tile region packed-field offsets");
_Static_assert(offsetof(struct d12_frontend_tile_region_size, height) == 12 &&
                  offsetof(struct d12_frontend_tile_region_size, depth) == 14,
               "frontend tile region packed-field offsets");
_Static_assert((uint32_t)D3D12DDI_TILE_RANGE_FLAG_NULL ==
                  (uint32_t)D12_FRONTEND_TILE_RANGE_NULL &&
                  (uint32_t)D3D12DDI_TILE_RANGE_FLAG_SKIP ==
                     (uint32_t)D12_FRONTEND_TILE_RANGE_SKIP &&
                  (uint32_t)D3D12DDI_TILE_RANGE_FLAG_REUSE_SINGLE_TILE ==
                     (uint32_t)D12_FRONTEND_TILE_RANGE_REUSE_SINGLE_TILE,
               "tile range flag encodings");
_Static_assert((uint32_t)D3D12DDI_TILE_MAPPING_FLAG_NO_HAZARD ==
                  (uint32_t)D12_FRONTEND_TILE_MAPPING_NO_HAZARD,
               "tile mapping flag encoding");
_Static_assert(
   (uint32_t)D3D12DDI_TILE_COPY_FLAG_NO_HAZARD ==
         (uint32_t)D12_FRONTEND_TILE_COPY_NO_HAZARD &&
      (uint32_t)D3D12DDI_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE ==
         (uint32_t)D12_FRONTEND_TILE_COPY_LINEAR_TO_TILED &&
      (uint32_t)D3D12DDI_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER ==
         (uint32_t)D12_FRONTEND_TILE_COPY_TILED_TO_LINEAR,
   "tile copy flag encodings");
_Static_assert(sizeof(D3D12DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY) == 16,
               "DDI stream-output declaration width");
_Static_assert(sizeof(D3D12DDIARG_SIGNATURE_ENTRY) == 20,
               "baseline DDI signature entry width");
_Static_assert(sizeof(D3D12DDIARG_SIGNATURE_ENTRY_0012) == 20,
               "Release 1 build 2 DDI signature entry width");
_Static_assert(offsetof(D3D12DDIARG_SIGNATURE_ENTRY_0012, Stream) == 9,
               "Release 1 build 2 signature stream offset");
_Static_assert(sizeof(struct d12_frontend_signature_entry) == 24,
               "frontend signature entry width");
_Static_assert(
   sizeof(D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT) == 48,
   "DDI geometry shader with stream-output width");
_Static_assert(
   offsetof(D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT,
            BufferStridesInBytes) == 32,
   "DDI stream-output stride offset");
_Static_assert(sizeof(struct d12_frontend_stream_output_declaration) == 24,
               "frontend stream-output declaration width");
_Static_assert(sizeof(struct d12_frontend_indirect_argument_desc) == 16,
               "frontend indirect-argument width");
_Static_assert(sizeof(struct d12_frontend_fence_placement) == 48,
               "frontend fence placement width");
_Static_assert(sizeof(struct d12_umd_resource_private) == 176,
               "opened-resource private-data width");
_Static_assert(sizeof(D3D12DDI_MEMORY_ARCHITECTURE_CAPS) == 12,
               "memory architecture cap width");
_Static_assert(sizeof(D3D12DDI_GPUVA_CAPS_0004) == 4,
               "GPUVA cap width");
_Static_assert(sizeof(D3D12DDI_SHADER_CAPS_0003) == 20,
               "shader cap width");
_Static_assert(sizeof(D3D12DDI_SHADER_CAPS_0012) == 36,
               "Release 1 build 2 shader cap width");
_Static_assert(sizeof(D3D12DDI_ROW_MAJOR_LAYOUT_CAPS) == 20,
               "row-major layout cap width");
_Static_assert(sizeof(D3D12DDIARG_ROW_MAJOR_RESOURCE_LAYOUT) == 8,
               "row-major resource layout width");
/* The WDK spells D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT as a static const
 * UINT rather than a C integer constant expression, so pin our side here and
 * compare both spellings in the executable callback probe. */
_Static_assert(D12_FRONTEND_MAX_VERTEX_BUFFERS == 32,
               "frontend vertex-buffer slot count");
_Static_assert(sizeof(struct agxw_adapter_identity) == 48,
               "KMD identity width");
_Static_assert(D3D12DDI_SUPPORTED_0003 == D12_UMD_DDI_SUPPORTED_0003,
               "supported DDI encoding");
_Static_assert(D3D12DDI_SUPPORTED_0010 == D12_UMD_DDI_SUPPORTED_0010,
               "Release 1 DDI encoding");
_Static_assert(D3D12DDI_SUPPORTED_0011 == D12_UMD_DDI_SUPPORTED_0011,
               "Release 1 build 1 DDI encoding");
_Static_assert(D3D12DDI_SUPPORTED_0013 == D12_UMD_DDI_SUPPORTED_0013,
               "Release 1 build 3 DDI encoding");
_Static_assert(sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0010) == 91 * sizeof(void *),
               "Release 1 device table width");
_Static_assert(sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0013) == 92 * sizeof(void *),
               "Release 1 build 3 device table width");
_Static_assert(sizeof(D3D12DDI_DESCRIPTOR_RANGE_0013) == 24,
               "root-signature 1.1 descriptor-range width");
_Static_assert(sizeof(D3D12DDI_ROOT_DESCRIPTOR_0013) == 12,
               "root-signature 1.1 root-descriptor width");
_Static_assert(sizeof(D3D12DDI_ROOT_PARAMETER_0013) == 32,
               "root-signature 1.1 root-parameter width");
_Static_assert(sizeof(D3D12DDI_ROOT_SIGNATURE_0013) == 40,
               "root-signature 1.1 descriptor width");
_Static_assert(sizeof(D3D12DDIARG_CREATE_ROOT_SIGNATURE_0013) == 24,
               "root-signature 1.1 create-argument width");
_Static_assert(
   sizeof(D3D12DDI_DEBUG_VIRTUAL_ADDRESS_ALLOCATION_INFO_0012) == 24,
   "debug GPU virtual-address allocation width");
_Static_assert(sizeof(D3D12DDI_DEBUG_KMT_ALLOCATION_INFO_0012) == 8,
               "debug KMT allocation width");
_Static_assert(sizeof(D3D12DDI_LIBRARY_REFERENCE_0010) == 16,
               "Release 1 library-reference width");
_Static_assert(sizeof(D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010) == 16,
               "Release 1 pipeline-library descriptor width");
_Static_assert(sizeof(D3D12DDIARG_CREATE_PIPELINE_STATE_0010) == 176,
               "Release 1 pipeline-state descriptor width");

int d12_umd_winabi_compile_only;
