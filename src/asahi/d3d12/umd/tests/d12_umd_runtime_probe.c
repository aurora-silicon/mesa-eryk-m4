/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Installed-driver probe.  Unlike d12_umd_ddiprobe, this enters through the
 * Microsoft D3D12/DXGI runtime, selects the adapter by AppleAgxWddm's pinned
 * UMDRIVERPRIVATE identity, and proves a byte pattern crossed the GPU through
 * upload -> default -> readback resources.
 */

#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dkmthk.h>

#include "../d12_umd_abi.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define PROBE_BYTES 4097u
#define PROBE_SIZE 8192u
#define PROBE_TIMEOUT_MS 15000u

static HRESULT
query_agx_identity(LUID luid, struct agxw_adapter_identity *identity)
{
   D3DKMT_OPENADAPTERFROMLUID open = {.AdapterLuid = luid};
   D3DKMT_QUERYADAPTERINFO query;
   D3DKMT_CLOSEADAPTER close;
   NTSTATUS status = D3DKMTOpenAdapterFromLuid(&open);

   if (status < 0)
      return HRESULT_FROM_NT(status);
   memset(identity, 0, sizeof(*identity));
   query = (D3DKMT_QUERYADAPTERINFO){
      .hAdapter = open.hAdapter,
      .Type = KMTQAITYPE_UMDRIVERPRIVATE,
      .pPrivateDriverData = identity,
      .PrivateDriverDataSize = sizeof(*identity),
   };
   status = D3DKMTQueryAdapterInfo(&query);
   close = (D3DKMT_CLOSEADAPTER){.hAdapter = open.hAdapter};
   if (D3DKMTCloseAdapter(&close) < 0 && status >= 0)
      return E_FAIL;
   if (status < 0)
      return HRESULT_FROM_NT(status);
   return d12_umd_validate_identity(identity, sizeof(*identity), identity) ==
                D12_UMD_IDENTITY_OK
             ? S_OK
             : DXGI_ERROR_UNSUPPORTED;
}

static HRESULT
find_agx_adapter(IDXGIFactory1 *factory, IDXGIAdapter1 **out_adapter,
                 DXGI_ADAPTER_DESC1 *out_desc,
                 struct agxw_adapter_identity *out_identity)
{
   IDXGIAdapter1 *match = NULL;
   UINT matches = 0;

   if (!factory || !out_adapter || !out_desc || !out_identity)
      return E_INVALIDARG;
   *out_adapter = NULL;

   for (UINT index = 0;; ++index) {
      IDXGIAdapter1 *adapter = NULL;
      DXGI_ADAPTER_DESC1 desc;
      struct agxw_adapter_identity identity;
      HRESULT result = IDXGIFactory1_EnumAdapters1(factory, index, &adapter);

      if (result == DXGI_ERROR_NOT_FOUND)
         break;
      if (FAILED(result))
         goto fail;
      result = IDXGIAdapter1_GetDesc1(adapter, &desc);
      if (SUCCEEDED(result) &&
          SUCCEEDED(query_agx_identity(desc.AdapterLuid, &identity))) {
         ++matches;
         if (!match) {
            match = adapter;
            *out_desc = desc;
            *out_identity = identity;
            adapter = NULL;
         }
      }
      if (adapter)
         IDXGIAdapter1_Release(adapter);
   }
   if (matches != 1 || !match) {
      if (match)
         IDXGIAdapter1_Release(match);
      return matches ? E_UNEXPECTED : DXGI_ERROR_NOT_FOUND;
   }
   *out_adapter = match;
   return S_OK;

fail:
   if (match)
      IDXGIAdapter1_Release(match);
   return E_FAIL;
}

static HRESULT
create_buffer(ID3D12Device *device, D3D12_HEAP_TYPE heap_type,
              D3D12_RESOURCE_STATES initial_state, ID3D12Resource **out)
{
   const D3D12_HEAP_PROPERTIES heap = {.Type = heap_type};
   const D3D12_RESOURCE_DESC desc = {
      .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
      .Width = PROBE_SIZE,
      .Height = 1,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .SampleDesc = {.Count = 1},
      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
   };
   return ID3D12Device_CreateCommittedResource(
      device, &heap, D3D12_HEAP_FLAG_NONE, &desc, initial_state, NULL,
      &IID_ID3D12Resource, (void **)out);
}

static int
fail_stage(const char *stage, HRESULT result, ID3D12Device *device)
{
   HRESULT removed = device ? ID3D12Device_GetDeviceRemovedReason(device)
                            : S_OK;
   fprintf(stderr, "FAIL stage=%s hr=0x%08" PRIx32
                   " removed=0x%08" PRIx32 "\n",
           stage, (uint32_t)result, (uint32_t)removed);
   return 2;
}

int
main(void)
{
   IDXGIFactory1 *factory = NULL;
   IDXGIAdapter1 *adapter = NULL;
   DXGI_ADAPTER_DESC1 adapter_desc;
   struct agxw_adapter_identity identity;
   ID3D12Device *device = NULL;
   ID3D12Resource *upload = NULL, *scratch = NULL, *readback = NULL;
   ID3D12CommandAllocator *allocator = NULL;
   ID3D12GraphicsCommandList *list = NULL;
   ID3D12CommandQueue *queue = NULL;
   ID3D12Fence *fence = NULL;
   HANDLE event = NULL;
   uint8_t *upload_map = NULL, *readback_map = NULL;
   int exit_code = 2;
   HRESULT result;

   result = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
   if (FAILED(result))
      return fail_stage("CreateDXGIFactory1", result, NULL);
   result = find_agx_adapter(factory, &adapter, &adapter_desc, &identity);
   if (FAILED(result)) {
      exit_code = fail_stage("find_agx_adapter", result, NULL);
      goto cleanup;
   }
   result = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
                              &IID_ID3D12Device, (void **)&device);
   if (FAILED(result)) {
      exit_code = fail_stage("D3D12CreateDevice", result, device);
      goto cleanup;
   }

   {
      D3D12_FEATURE_DATA_FORMAT_SUPPORT format = {
         .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      };
      result = ID3D12Device_CheckFeatureSupport(
         device, D3D12_FEATURE_FORMAT_SUPPORT, &format, sizeof(format));
      if (FAILED(result) ||
          !(format.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET)) {
         exit_code = fail_stage("CheckFeatureSupport(FORMAT)",
                                FAILED(result) ? result : E_FAIL, device);
         goto cleanup;
      }
   }

   result = create_buffer(device, D3D12_HEAP_TYPE_UPLOAD,
                          D3D12_RESOURCE_STATE_GENERIC_READ, &upload);
   if (FAILED(result) ||
       FAILED(result = create_buffer(device, D3D12_HEAP_TYPE_DEFAULT,
                                     D3D12_RESOURCE_STATE_COPY_DEST,
                                     &scratch)) ||
       FAILED(result = create_buffer(device, D3D12_HEAP_TYPE_READBACK,
                                     D3D12_RESOURCE_STATE_COPY_DEST,
                                     &readback))) {
      exit_code = fail_stage("CreateCommittedResource", result, device);
      goto cleanup;
   }

   result = ID3D12Resource_Map(upload, 0, NULL, (void **)&upload_map);
   if (FAILED(result) || !upload_map) {
      exit_code = fail_stage("Map(upload)", result, device);
      goto cleanup;
   }
   for (UINT index = 0; index < PROBE_BYTES; ++index)
      upload_map[index] = (uint8_t)((index * 29u + 17u) ^ (index >> 3));

   result = ID3D12Device_CreateCommandAllocator(
      device, D3D12_COMMAND_LIST_TYPE_DIRECT,
      &IID_ID3D12CommandAllocator, (void **)&allocator);
   if (FAILED(result)) {
      exit_code = fail_stage("CreateCommandAllocator", result, device);
      goto cleanup;
   }
   result = ID3D12Device_CreateCommandList(
      device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
      &IID_ID3D12GraphicsCommandList, (void **)&list);
   if (FAILED(result)) {
      exit_code = fail_stage("CreateCommandList", result, device);
      goto cleanup;
   }

   ID3D12GraphicsCommandList_CopyBufferRegion(list, scratch, 0, upload, 0,
                                              PROBE_BYTES);
   {
      const D3D12_RESOURCE_BARRIER barrier = {
         .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
         .Transition = {
            .pResource = scratch,
            .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
            .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE,
         },
      };
      ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
   }
   ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, 0, scratch, 0,
                                              PROBE_BYTES);
   result = ID3D12GraphicsCommandList_Close(list);
   if (FAILED(result)) {
      exit_code = fail_stage("CloseCommandList", result, device);
      goto cleanup;
   }

   {
      const D3D12_COMMAND_QUEUE_DESC desc = {
         .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      };
      result = ID3D12Device_CreateCommandQueue(
         device, &desc, &IID_ID3D12CommandQueue, (void **)&queue);
   }
   if (FAILED(result)) {
      exit_code = fail_stage("CreateCommandQueue", result, device);
      goto cleanup;
   }
   result = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
                                     &IID_ID3D12Fence, (void **)&fence);
   if (FAILED(result)) {
      exit_code = fail_stage("CreateFence", result, device);
      goto cleanup;
   }
   event = CreateEventW(NULL, FALSE, FALSE, NULL);
   if (!event) {
      exit_code = fail_stage("CreateEvent", HRESULT_FROM_WIN32(GetLastError()),
                             device);
      goto cleanup;
   }

   {
      ID3D12CommandList *lists[] = {(ID3D12CommandList *)list};
      ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
   }
   result = ID3D12CommandQueue_Signal(queue, fence, 1);
   if (FAILED(result)) {
      exit_code = fail_stage("Signal", result, device);
      goto cleanup;
   }
   result = ID3D12Fence_SetEventOnCompletion(fence, 1, event);
   if (FAILED(result)) {
      exit_code = fail_stage("SetEventOnCompletion", result, device);
      goto cleanup;
   }
   if (WaitForSingleObject(event, PROBE_TIMEOUT_MS) != WAIT_OBJECT_0) {
      exit_code = fail_stage("GPU timeout", DXGI_ERROR_DEVICE_HUNG, device);
      goto cleanup;
   }

   {
      const D3D12_RANGE read_range = {.Begin = 0, .End = PROBE_BYTES};
      result = ID3D12Resource_Map(readback, 0, &read_range,
                                  (void **)&readback_map);
   }
   if (FAILED(result) || !readback_map) {
      exit_code = fail_stage("Map(readback)", result, device);
      goto cleanup;
   }
   for (UINT index = 0; index < PROBE_BYTES; ++index) {
      if (readback_map[index] != upload_map[index]) {
         fprintf(stderr,
                 "FAIL stage=verify offset=%u got=0x%02x expected=0x%02x\n",
                 index, readback_map[index], upload_map[index]);
         goto cleanup;
      }
   }

   printf("PASS stage=d3d12_gpu_copy luid=%08" PRIx32 ":%08" PRIx32
          " chip=0x%04" PRIx32 " generation=%" PRIu32
          " variant=%c revision=0x%02" PRIx32 " bytes=%u\n",
          (uint32_t)adapter_desc.AdapterLuid.HighPart,
          (uint32_t)adapter_desc.AdapterLuid.LowPart, identity.chip_id,
          identity.gpu_generation, (char)identity.gpu_variant,
          identity.gpu_revision, PROBE_BYTES);
   exit_code = 0;

cleanup:
   if (readback_map)
      ID3D12Resource_Unmap(readback, 0, NULL);
   if (upload_map)
      ID3D12Resource_Unmap(upload, 0, NULL);
   if (event)
      CloseHandle(event);
   if (fence)
      ID3D12Fence_Release(fence);
   if (queue)
      ID3D12CommandQueue_Release(queue);
   if (list)
      ID3D12GraphicsCommandList_Release(list);
   if (allocator)
      ID3D12CommandAllocator_Release(allocator);
   if (readback)
      ID3D12Resource_Release(readback);
   if (scratch)
      ID3D12Resource_Release(scratch);
   if (upload)
      ID3D12Resource_Release(upload);
   if (device)
      ID3D12Device_Release(device);
   if (adapter)
      IDXGIAdapter1_Release(adapter);
   if (factory)
      IDXGIFactory1_Release(factory);
   return exit_code;
}
