/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Live Windows acceptance test for the zero-copy interop primitives. It uses
 * only public D3D12 calls. The shared-heap mapping and shared-fence checks
 * prove cross-object visibility; descriptor checks cover texture metadata.
 */

#define COBJMACROS
#include <wsl/winadapter.h>
#include <directx/d3d12.h>

#include "d12_test_abi.h"

#include <stdio.h>

static unsigned failures;

#define CHECK(condition, message)                                             \
   do {                                                                        \
      if (condition)                                                           \
         printf("ok   - %s\n", message);                                      \
      else {                                                                   \
         printf("FAIL - %s\n", message);                                     \
         failures++;                                                          \
      }                                                                        \
   } while (0)

static void
close_shared_handle(HANDLE handle)
{
#ifdef _WIN32
   if (handle)
      CloseHandle(handle);
#else
   (void)handle;
#endif
}

int
main(void)
{
   ID3D12Device *device = NULL;
   HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
                                  &IID_ID3D12Device, (void **)&device);
   CHECK(SUCCEEDED(hr) && device, "create device");
   if (!device)
      return failures;

   const D3D12_HEAP_PROPERTIES default_heap = {
      .Type = D3D12_HEAP_TYPE_DEFAULT,
      .CreationNodeMask = 1,
      .VisibleNodeMask = 1,
   };
   const D3D12_RESOURCE_DESC resource_desc = {
      .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
      .Width = 64,
      .Height = 64,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {.Count = 1},
      .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
      .Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
               D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS,
   };

   ID3D12Resource *resource = NULL;
   hr = ID3D12Device_CreateCommittedResource(
      device, &default_heap, D3D12_HEAP_FLAG_SHARED, &resource_desc,
      D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource,
      (void **)&resource);
   CHECK(SUCCEEDED(hr) && resource, "create shareable render target");

   HANDLE resource_handle = NULL;
   ID3D12Resource *opened_resource = NULL;
   if (resource) {
      hr = ID3D12Device_CreateSharedHandle(
         device, (ID3D12DeviceChild *)resource, NULL, GENERIC_ALL, NULL,
         &resource_handle);
      CHECK(SUCCEEDED(hr) && resource_handle, "export resource NT handle");
      if (resource_handle) {
         hr = ID3D12Device_OpenSharedHandle(
            device, resource_handle, &IID_ID3D12Resource,
            (void **)&opened_resource);
         CHECK(SUCCEEDED(hr) && opened_resource, "reopen shared resource");
         if (opened_resource) {
            D3D12_RESOURCE_DESC opened_desc =
               d12_test_resource_get_desc(opened_resource);
            CHECK(opened_desc.Width == resource_desc.Width &&
                     opened_desc.Height == resource_desc.Height &&
                     opened_desc.Format == resource_desc.Format,
                  "shared resource descriptor round-trips");
         }
      }
   }

   const D3D12_HEAP_DESC heap_desc = {
      .SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
      .Properties =
         {
            .Type = D3D12_HEAP_TYPE_CUSTOM,
            .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_L0,
            .CreationNodeMask = 1,
            .VisibleNodeMask = 1,
         },
      .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
      .Flags = D3D12_HEAP_FLAG_SHARED,
   };
   ID3D12Heap *heap = NULL;
   ID3D12Heap *opened_heap = NULL;
   HANDLE heap_handle = NULL;
   hr = ID3D12Device_CreateHeap(device, &heap_desc, &IID_ID3D12Heap,
                                (void **)&heap);
   CHECK(SUCCEEDED(hr) && heap, "create shareable heap");
   if (heap) {
      hr = ID3D12Device_CreateSharedHandle(
         device, (ID3D12DeviceChild *)heap, NULL, GENERIC_ALL, NULL,
         &heap_handle);
      CHECK(SUCCEEDED(hr) && heap_handle, "export heap NT handle");
      if (heap_handle) {
         hr = ID3D12Device_OpenSharedHandle(
            device, heap_handle, &IID_ID3D12Heap, (void **)&opened_heap);
         CHECK(SUCCEEDED(hr) && opened_heap, "reopen shared heap");
         if (opened_heap) {
            D3D12_HEAP_DESC opened_desc = d12_test_heap_get_desc(opened_heap);
            CHECK(opened_desc.SizeInBytes == heap_desc.SizeInBytes &&
                     (opened_desc.Flags & D3D12_HEAP_FLAG_SHARED),
                  "shared heap descriptor round-trips");

            const D3D12_RESOURCE_DESC buffer_desc = {
               .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
               .Width = 4096,
               .Height = 1,
               .DepthOrArraySize = 1,
               .MipLevels = 1,
               .Format = DXGI_FORMAT_UNKNOWN,
               .SampleDesc = {.Count = 1},
               .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
            };
            ID3D12Resource *placed = NULL;
            ID3D12Resource *opened_placed = NULL;
            hr = ID3D12Device_CreatePlacedResource(
               device, heap, 0, &buffer_desc,
               D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
               &IID_ID3D12Resource, (void **)&placed);
            CHECK(SUCCEEDED(hr) && placed,
                  "place buffer in original shared heap");
            hr = ID3D12Device_CreatePlacedResource(
               device, opened_heap, 0, &buffer_desc,
               D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
               &IID_ID3D12Resource, (void **)&opened_placed);
            CHECK(SUCCEEDED(hr) && opened_placed,
                  "place buffer in reopened shared heap");

            uint32_t *map = NULL;
            uint32_t *opened_map = NULL;
            if (placed && opened_placed) {
               hr = ID3D12Resource_Map(placed, 0, NULL, (void **)&map);
               CHECK(SUCCEEDED(hr) && map, "map original shared heap");
               hr = ID3D12Resource_Map(opened_placed, 0, NULL,
                                       (void **)&opened_map);
               CHECK(SUCCEEDED(hr) && opened_map,
                     "map reopened shared heap");
               if (map && opened_map) {
                  map[17] = 0xa61a5e12u;
                  CHECK(opened_map[17] == 0xa61a5e12u,
                        "reopened heap aliases the original allocation");
               }
               if (opened_map)
                  ID3D12Resource_Unmap(opened_placed, 0, NULL);
               if (map)
                  ID3D12Resource_Unmap(placed, 0, NULL);
            }
            if (opened_placed)
               ID3D12Resource_Release(opened_placed);
            if (placed)
               ID3D12Resource_Release(placed);
         }
      }
   }

   ID3D12Fence *fence = NULL;
   ID3D12Fence *opened_fence = NULL;
   HANDLE fence_handle = NULL;
   hr = ID3D12Device_CreateFence(device, 3, D3D12_FENCE_FLAG_SHARED,
                                 &IID_ID3D12Fence, (void **)&fence);
   CHECK(SUCCEEDED(hr) && fence, "create shared monitored fence");
   if (fence) {
      hr = ID3D12Device_CreateSharedHandle(
         device, (ID3D12DeviceChild *)fence, NULL, GENERIC_ALL, NULL,
         &fence_handle);
      CHECK(SUCCEEDED(hr) && fence_handle, "export fence NT handle");
      if (fence_handle) {
         hr = ID3D12Device_OpenSharedHandle(
            device, fence_handle, &IID_ID3D12Fence,
            (void **)&opened_fence);
         CHECK(SUCCEEDED(hr) && opened_fence, "reopen shared fence");
         if (opened_fence) {
            hr = ID3D12Fence_Signal(opened_fence, 9);
            CHECK(SUCCEEDED(hr) &&
                     ID3D12Fence_GetCompletedValue(fence) >= 9,
                  "signal is visible through original fence");
         }
      }
   }

   if (opened_fence)
      ID3D12Fence_Release(opened_fence);
   if (fence)
      ID3D12Fence_Release(fence);
   if (opened_heap)
      ID3D12Heap_Release(opened_heap);
   if (heap)
      ID3D12Heap_Release(heap);
   if (opened_resource)
      ID3D12Resource_Release(opened_resource);
   if (resource)
      ID3D12Resource_Release(resource);
   close_shared_handle(fence_handle);
   close_shared_handle(heap_handle);
   close_shared_handle(resource_handle);
   ID3D12Device_Release(device);

   printf(failures ? "SHARED INTEROP: %u FAILURES\n"
                   : "SHARED INTEROP: ALL PASS\n",
          failures);
   return failures;
}
