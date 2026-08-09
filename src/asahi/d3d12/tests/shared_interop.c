/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Live Windows acceptance test for the zero-copy interop primitives. The
 * shared-heap mapping and shared-fence checks prove cross-object visibility;
 * opening a shared render target through D3D11 covers the presentation seam.
 */

#define COBJMACROS
#include <wsl/winadapter.h>
#include <directx/d3d12.h>
#ifdef _WIN32
#include <d3d11_1.h>
#include <winternl.h>
#include "../../lib/agx_d3dkmt.h"
#endif

#include "d12_test_abi.h"

#include <stdio.h>
#include <stdlib.h>

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

#ifdef _WIN32
struct d3dkmt_reshare_fns {
   agx_kmt_status(AGX_KMT_API *OpenAdapterFromLuid)(
      struct agx_kmt_openadapterfromluid *);
   agx_kmt_status(AGX_KMT_API *CloseAdapter)(
      const struct agx_kmt_closeadapter *);
   agx_kmt_status(AGX_KMT_API *CreateDevice)(struct agx_kmt_createdevice *);
   agx_kmt_status(AGX_KMT_API *DestroyDevice)(
      const struct agx_kmt_destroydevice *);
   agx_kmt_status(AGX_KMT_API *QueryResourceInfoFromNtHandle)(
      struct agx_kmt_queryresourceinfofromnthandle *);
   agx_kmt_status(AGX_KMT_API *OpenResourceFromNtHandle)(
      struct agx_kmt_openresourcefromnthandle *);
   agx_kmt_status(AGX_KMT_API *ShareObjects)(uint32_t,
                                             const agx_kmt_handle *, void *,
                                             uint32_t, void **);
   agx_kmt_status(AGX_KMT_API *DestroyAllocation2)(
      const struct agx_kmt_destroyallocation2 *);
};

static bool
load_d3dkmt_reshare_fns(HMODULE gdi32, struct d3dkmt_reshare_fns *fns)
{
   memset(fns, 0, sizeof(*fns));

#define LOAD(field)                                                           \
   do {                                                                        \
      FARPROC proc = GetProcAddress(gdi32, "D3DKMT" #field);                  \
      if (!proc)                                                               \
         return false;                                                         \
      *(FARPROC *)(void *)&fns->field = proc;                                  \
   } while (0)

   LOAD(OpenAdapterFromLuid);
   LOAD(CloseAdapter);
   LOAD(CreateDevice);
   LOAD(DestroyDevice);
   LOAD(QueryResourceInfoFromNtHandle);
   LOAD(OpenResourceFromNtHandle);
   LOAD(ShareObjects);
   LOAD(DestroyAllocation2);

#undef LOAD
   return true;
}

/*
 * Model the resource handoff performed by a real D3D11 UMD. The D3D runtime
 * opens the application's NT handle and gives its UMD process-local
 * hKMResource/hAllocation values, not the original application handle. The
 * UMD can legally turn hKMResource back into an NT handle with ShareObjects;
 * DXVK/Honeykrisp then imports that handle through OPAQUE_WIN32 and aliases
 * the same WDDM allocation. No private Vulkan extension or copy is needed.
 */
static void
check_d3dkmt_resource_reshare(ID3D12Device *d3d12_device,
                              HANDLE resource_handle,
                              const D3D12_RESOURCE_DESC *resource_desc)
{
   enum { MAX_PRIVATE_DATA_SIZE = 1024 * 1024 };
   HMODULE gdi32 = LoadLibraryW(L"gdi32.dll");
   CHECK(gdi32, "load the D3DKMT thunk provider");
   if (!gdi32)
      return;

   struct d3dkmt_reshare_fns fns;
   const bool loaded = load_d3dkmt_reshare_fns(gdi32, &fns);
   CHECK(loaded,
         "resolve the D3DKMT resource-reshare surface");
   if (!loaded) {
      FreeLibrary(gdi32);
      return;
   }

   LUID adapter_luid = {0};
   ID3D12Device_GetAdapterLuid(d3d12_device, &adapter_luid);
   struct agx_kmt_openadapterfromluid adapter = {
      .AdapterLuid =
         {
            .LowPart = adapter_luid.LowPart,
            .HighPart = adapter_luid.HighPart,
         },
   };
   agx_kmt_status status = fns.OpenAdapterFromLuid(&adapter);
   CHECK(AGX_KMT_SUCCESS(status) && adapter.hAdapter,
         "open the D3D12 adapter through D3DKMT");
   if (!AGX_KMT_SUCCESS(status) || !adapter.hAdapter) {
      FreeLibrary(gdi32);
      return;
   }

   struct agx_kmt_createdevice device = {.hAdapter = adapter.hAdapter};
   status = fns.CreateDevice(&device);
   CHECK(AGX_KMT_SUCCESS(status) && device.hDevice,
         "create the D3DKMT reshare device");
   if (!AGX_KMT_SUCCESS(status) || !device.hDevice)
      goto close_adapter;

   struct agx_kmt_queryresourceinfofromnthandle query = {
      .hDevice = device.hDevice,
      .hNtHandle = resource_handle,
   };
   status = fns.QueryResourceInfoFromNtHandle(&query);
   bool query_valid =
      AGX_KMT_SUCCESS(status) && query.NumAllocations == 1 &&
      query.PrivateRuntimeDataSize <= MAX_PRIVATE_DATA_SIZE &&
      query.ResourcePrivateDriverDataSize <= MAX_PRIVATE_DATA_SIZE &&
      query.TotalPrivateDriverDataSize <= MAX_PRIVATE_DATA_SIZE;
   CHECK(query_valid, "query the shared resource's D3DKMT metadata");
   if (!query_valid)
      goto destroy_device;

   void *runtime_data =
      query.PrivateRuntimeDataSize ? calloc(1, query.PrivateRuntimeDataSize)
                                   : NULL;
   void *resource_data = query.ResourcePrivateDriverDataSize
                            ? calloc(1, query.ResourcePrivateDriverDataSize)
                            : NULL;
   void *allocation_data = query.TotalPrivateDriverDataSize
                              ? calloc(1, query.TotalPrivateDriverDataSize)
                              : NULL;
   bool buffers_valid =
      (!query.PrivateRuntimeDataSize || runtime_data) &&
      (!query.ResourcePrivateDriverDataSize || resource_data) &&
      (!query.TotalPrivateDriverDataSize || allocation_data);
   CHECK(buffers_valid, "allocate D3DKMT resource metadata buffers");
   if (!buffers_valid) {
      free(allocation_data);
      free(resource_data);
      free(runtime_data);
      goto destroy_device;
   }

   struct agx_kmt_openallocationinfo2 allocation = {0};
   struct agx_kmt_openresourcefromnthandle open = {
      .hDevice = device.hDevice,
      .hNtHandle = resource_handle,
      .NumAllocations = 1,
      .pOpenAllocationInfo2 = &allocation,
      .PrivateRuntimeDataSize = query.PrivateRuntimeDataSize,
      .pPrivateRuntimeData = runtime_data,
      .ResourcePrivateDriverDataSize = query.ResourcePrivateDriverDataSize,
      .pResourcePrivateDriverData = resource_data,
      .TotalPrivateDriverDataBufferSize = query.TotalPrivateDriverDataSize,
      .pTotalPrivateDriverDataBuffer = allocation_data,
   };
   status = fns.OpenResourceFromNtHandle(&open);
   bool opened = AGX_KMT_SUCCESS(status) && open.hResource &&
                 allocation.hAllocation && !open.hKeyedMutex &&
                 !open.hSyncObject;
   CHECK(opened, "open the NT resource as D3DKMT resource/allocation handles");

   HANDLE reshared_handle = NULL;
   ID3D12Resource *reshared_resource = NULL;
   if (opened) {
      OBJECT_ATTRIBUTES attributes;
      InitializeObjectAttributes(&attributes, NULL, 0, NULL, NULL);
      status = fns.ShareObjects(1, &open.hResource, &attributes,
                                AGX_KMT_SHARED_ALLOCATION_ALL_ACCESS,
                                (void **)&reshared_handle);
      CHECK(AGX_KMT_SUCCESS(status) && reshared_handle,
            "reshare the DDI-style hKMResource as an NT handle");

      if (reshared_handle) {
         HRESULT hr = ID3D12Device_OpenSharedHandle(
            d3d12_device, reshared_handle, &IID_ID3D12Resource,
            (void **)&reshared_resource);
         CHECK(SUCCEEDED(hr) && reshared_resource,
               "reopen the reshared D3DKMT resource through D3D12");
         if (reshared_resource) {
            D3D12_RESOURCE_DESC desc =
               d12_test_resource_get_desc(reshared_resource);
            CHECK(desc.Width == resource_desc->Width &&
                     desc.Height == resource_desc->Height &&
                     desc.Format == resource_desc->Format,
                  "reshared resource preserves the zero-copy descriptor");
         }
      }
   }

   if (reshared_resource)
      ID3D12Resource_Release(reshared_resource);
   if (reshared_handle)
      CloseHandle(reshared_handle);
   if (open.hResource) {
      const struct agx_kmt_destroyallocation2 destroy = {
         .hDevice = device.hDevice,
         .hResource = open.hResource,
      };
      fns.DestroyAllocation2(&destroy);
   }
   free(allocation_data);
   free(resource_data);
   free(runtime_data);

destroy_device:
   {
      const struct agx_kmt_destroydevice destroy = {
         .hDevice = device.hDevice,
      };
      fns.DestroyDevice(&destroy);
   }
close_adapter:
   {
      const struct agx_kmt_closeadapter close = {
         .hAdapter = adapter.hAdapter,
      };
      fns.CloseAdapter(&close);
   }
   FreeLibrary(gdi32);
}

static void
check_d3d11_interop(HANDLE resource_handle,
                    const D3D12_RESOURCE_DESC *resource_desc)
{
   HMODULE d3d11_module = LoadLibraryW(L"d3d11.dll");
   CHECK(d3d11_module, "load the system D3D11 runtime");
   if (!d3d11_module)
      return;

   PFN_D3D11_CREATE_DEVICE create_device =
      (PFN_D3D11_CREATE_DEVICE)(void *)GetProcAddress(d3d11_module,
                                                      "D3D11CreateDevice");
   CHECK(create_device, "resolve D3D11CreateDevice");

   ID3D11Device *device = NULL;
   ID3D11DeviceContext *context = NULL;
   ID3D11Device1 *device1 = NULL;
   ID3D11Texture2D *texture = NULL;

   if (create_device) {
      HRESULT hr = create_device(
         NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
         NULL, 0, D3D11_SDK_VERSION, &device, NULL, &context);
      CHECK(SUCCEEDED(hr) && device, "create the presentation D3D11 device");

      if (device) {
         hr = ID3D11Device_QueryInterface(device, &IID_ID3D11Device1,
                                          (void **)&device1);
         CHECK(SUCCEEDED(hr) && device1, "query the D3D11.1 device interface");
      }

      if (device1) {
         hr = ID3D11Device1_OpenSharedResource1(
            device1, resource_handle, &IID_ID3D11Texture2D,
            (void **)&texture);
         CHECK(SUCCEEDED(hr) && texture,
               "open the D3D12 render target through D3D11");

         if (texture) {
            D3D11_TEXTURE2D_DESC desc;
            ID3D11Texture2D_GetDesc(texture, &desc);
            CHECK(desc.Width == resource_desc->Width &&
                     desc.Height == resource_desc->Height &&
                     desc.Format == resource_desc->Format,
                  "D3D11 sees the shared render-target descriptor");
         }
      }
   }

   if (texture)
      ID3D11Texture2D_Release(texture);
   if (device1)
      ID3D11Device1_Release(device1);
   if (context)
      ID3D11DeviceContext_Release(context);
   if (device)
      ID3D11Device_Release(device);
   FreeLibrary(d3d11_module);
}
#endif

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
#ifdef _WIN32
         check_d3dkmt_resource_reshare(device, resource_handle,
                                       &resource_desc);
         check_d3d11_interop(resource_handle, &resource_desc);
#endif
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
