/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_private.h"

#include <errno.h>
#include <dxgi.h>
#include <wchar.h>
#include <winternl.h>

#include "asahi/lib/agx_device_d3dkmt.h"

static const GUID d12_IID_IDXGIAdapter = {
   0x2411e7e1,
   0x12ac,
   0x4ccf,
   {0xbd, 0x14, 0x97, 0x98, 0xe8, 0x53, 0x4d, 0xc0},
};

static bool
d12_adapter_luid(IUnknown *adapter, struct agx_kmt_luid *luid)
{
   if (adapter) {
      IDXGIAdapter *dxgi = NULL;
      HRESULT hr = IUnknown_QueryInterface(adapter, &d12_IID_IDXGIAdapter,
                                           (void **)&dxgi);
      if (FAILED(hr))
         return false;

      DXGI_ADAPTER_DESC desc;
      hr = IDXGIAdapter_GetDesc(dxgi, &desc);
      IDXGIAdapter_Release(dxgi);
      if (FAILED(hr))
         return false;

      *luid = (struct agx_kmt_luid){
         .LowPart = desc.AdapterLuid.LowPart,
         .HighPart = desc.AdapterLuid.HighPart,
      };
      return true;
   }

   uint32_t count = 0;
   if (!agx_d3dkmt_enumerate_adapters(luid, 1, &count))
      return false;

   return count > 0;
}

bool
d12_platform_open_device(struct d12_device *device, IUnknown *adapter)
{
   struct agx_kmt_luid luid;
   if (!d12_adapter_luid(adapter, &luid))
      return false;

   device->dev.fd = -1;
   if (!agx_d3dkmt_open_device(&device->dev, &luid))
      return false;

   if (!agx_open_device(NULL, &device->dev)) {
      agx_d3dkmt_close_device(&device->dev);
      return false;
   }

   device->sync = util_sync_provider_d3dkmt(&device->dev);
   if (!device->sync) {
      agx_close_device(&device->dev);
      agx_d3dkmt_close_device(&device->dev);
      return false;
   }

   luid = agx_d3dkmt_device_luid(&device->dev);
   device->adapter_luid = (LUID){
      .LowPart = luid.LowPart,
      .HighPart = luid.HighPart,
   };
   return true;
}

void
d12_platform_close_device(struct d12_device *device)
{
   if (device->sync) {
      device->sync->finalize(device->sync);
      device->sync = NULL;
   }

   agx_close_device(&device->dev);
   agx_d3dkmt_close_device(&device->dev);
}

void
d12_platform_signal_event(HANDLE event)
{
   if (!SetEvent(event))
      mesa_loge("SetEvent failed: %lu", (unsigned long)GetLastError());
}

static HRESULT
d12_platform_external_error(void)
{
   switch (errno) {
   case ENOMEM:
      return E_OUTOFMEMORY;
   case EINVAL:
   case ENOSPC:
      return E_INVALIDARG;
   case EACCES:
      return E_ACCESSDENIED;
   default:
      return E_FAIL;
   }
}

HRESULT
d12_platform_create_shared_bo(struct d12_device *device, size_t size,
                              unsigned align, enum agx_bo_flags flags,
                              const void *runtime_data,
                              uint32_t runtime_data_size, const char *label,
                              struct agx_bo **bo)
{
   if (!bo)
      return E_POINTER;

   *bo = agx_d3dkmt_bo_create_external_with_runtime_data(
      &device->dev, size, align, flags,
      AGX_D3DKMT_EXTERNAL_MEMORY_OPAQUE_WIN32, runtime_data,
      runtime_data_size, label);
   return *bo ? S_OK : d12_platform_external_error();
}

static bool
d12_platform_is_kmt_handle(HANDLE handle)
{
   uintptr_t value = (uintptr_t)handle;
   return (value & 0x40000000u) && ((value - 2u) % 4u) == 0;
}

HRESULT
d12_platform_import_shared_bo(struct d12_device *device, HANDLE handle,
                              void *runtime_data,
                              uint32_t *runtime_data_size, const char *label,
                              struct agx_bo **bo)
{
   if (!bo || !runtime_data_size)
      return E_POINTER;
   *bo = NULL;
   if (!handle || handle == INVALID_HANDLE_VALUE)
      return E_INVALIDARG;

   enum agx_d3dkmt_external_memory_type type =
      d12_platform_is_kmt_handle(handle)
         ? AGX_D3DKMT_EXTERNAL_MEMORY_OPAQUE_WIN32_KMT
         : AGX_D3DKMT_EXTERNAL_MEMORY_OPAQUE_WIN32;
   *bo = agx_d3dkmt_bo_import_external_with_runtime_data(
      &device->dev, (uintptr_t)handle, type, runtime_data,
      runtime_data_size, label);
   return *bo ? S_OK : d12_platform_external_error();
}

struct d12_platform_object_attributes {
   OBJECT_ATTRIBUTES object;
   UNICODE_STRING name;
   WCHAR *buffer;
};

static HRESULT
d12_platform_object_attributes_init(
   struct d12_platform_object_attributes *out,
   const SECURITY_ATTRIBUTES *attributes, LPCWSTR name)
{
   memset(out, 0, sizeof(*out));
   WCHAR prefix[64];
   ULONG object_flags = 0;

   if (name) {
      DWORD session = 0;
      if (!ProcessIdToSessionId(GetCurrentProcessId(), &session))
         return HRESULT_FROM_WIN32(GetLastError());

      int prefix_length = swprintf(
         prefix, ARRAY_SIZE(prefix), L"\\Sessions\\%lu\\BaseNamedObjects\\",
         (unsigned long)session);
      if (prefix_length < 0)
         return E_FAIL;

      size_t name_length = wcslen(name);
      size_t total_length = (size_t)prefix_length + name_length;
      if (total_length > (UINT16_MAX / sizeof(WCHAR)) - 1)
         return E_INVALIDARG;

      out->buffer = malloc((total_length + 1) * sizeof(WCHAR));
      if (!out->buffer)
         return E_OUTOFMEMORY;
      memcpy(out->buffer, prefix, (size_t)prefix_length * sizeof(WCHAR));
      memcpy(out->buffer + prefix_length, name,
             (name_length + 1) * sizeof(WCHAR));

      out->name = (UNICODE_STRING){
         .Length = (USHORT)(total_length * sizeof(WCHAR)),
         .MaximumLength = (USHORT)((total_length + 1) * sizeof(WCHAR)),
         .Buffer = out->buffer,
      };
      object_flags |= OBJ_CASE_INSENSITIVE;
   }

   if (attributes && attributes->bInheritHandle)
      object_flags |= OBJ_INHERIT;

   InitializeObjectAttributes(
      &out->object, name ? &out->name : NULL, object_flags, NULL,
      attributes ? attributes->lpSecurityDescriptor : NULL);
   return S_OK;
}

HRESULT
d12_platform_export_shared_bo(struct d12_device *device, struct agx_bo *bo,
                              const SECURITY_ATTRIBUTES *attributes,
                              DWORD access, LPCWSTR name, HANDLE *handle)
{
   if (!handle)
      return E_POINTER;
   *handle = NULL;
   if (!bo)
      return E_INVALIDARG;

   struct d12_platform_object_attributes object_attributes;
   HRESULT hr = d12_platform_object_attributes_init(
      &object_attributes, attributes, name);
   if (FAILED(hr))
      return hr;

   uintptr_t exported = 0;
   int ret = agx_d3dkmt_bo_export_external_with_attributes(
      &device->dev, bo, AGX_D3DKMT_EXTERNAL_MEMORY_OPAQUE_WIN32,
      &object_attributes.object, access, &exported);
   free(object_attributes.buffer);
   if (ret)
      return d12_platform_external_error();

   *handle = (HANDLE)exported;
   return S_OK;
}

HRESULT
d12_platform_create_shared_sync(struct d12_device *device,
                                uint64_t initial_value, uint32_t *syncobj)
{
   if (!syncobj)
      return E_POINTER;
   return agx_d3dkmt_syncobj_create_external(&device->dev, initial_value,
                                             syncobj)
             ? d12_platform_external_error()
             : S_OK;
}

HRESULT
d12_platform_import_shared_sync(struct d12_device *device, HANDLE handle,
                                uint32_t *syncobj)
{
   if (!syncobj)
      return E_POINTER;
   *syncobj = 0;
   if (!handle || handle == INVALID_HANDLE_VALUE)
      return E_INVALIDARG;
   return agx_d3dkmt_syncobj_import_external(&device->dev,
                                             (uintptr_t)handle, syncobj)
             ? d12_platform_external_error()
             : S_OK;
}

HRESULT
d12_platform_export_shared_sync(
   struct d12_device *device, uint32_t syncobj,
   const SECURITY_ATTRIBUTES *attributes, DWORD access, LPCWSTR name,
   HANDLE *handle)
{
   if (!handle)
      return E_POINTER;
   *handle = NULL;

   struct d12_platform_object_attributes object_attributes;
   HRESULT hr = d12_platform_object_attributes_init(
      &object_attributes, attributes, name);
   if (FAILED(hr))
      return hr;

   uintptr_t exported = 0;
   int ret = agx_d3dkmt_syncobj_export_external(
      &device->dev, syncobj, &object_attributes.object, access, &exported);
   free(object_attributes.buffer);
   if (ret)
      return d12_platform_external_error();

   *handle = (HANDLE)exported;
   return S_OK;
}

HRESULT
d12_platform_query_shared_sync(struct d12_device *device, uint32_t syncobj,
                               uint64_t *value)
{
   if (!value)
      return E_POINTER;
   return agx_d3dkmt_syncobj_query(&device->dev, syncobj, value)
             ? d12_platform_external_error()
             : S_OK;
}

HRESULT
d12_platform_set_bo_resident(struct d12_device *device, struct agx_bo *bo,
                             bool resident)
{
   int ret = resident ? agx_d3dkmt_bo_make_resident(&device->dev, bo)
                      : agx_d3dkmt_bo_evict(&device->dev, bo);
   return ret ? d12_platform_external_error() : S_OK;
}

HRESULT
d12_platform_open_shared_handle_by_name(struct d12_device *device,
                                        LPCWSTR name, DWORD access,
                                        HANDLE *handle)
{
   if (!handle)
      return E_POINTER;
   *handle = NULL;
   if (!name)
      return E_INVALIDARG;

   struct d12_platform_object_attributes object_attributes;
   HRESULT hr = d12_platform_object_attributes_init(&object_attributes, NULL,
                                                    name);
   if (FAILED(hr))
      return hr;

   uintptr_t opened = 0;
   int ret = agx_d3dkmt_open_nt_handle_from_name(
      &device->dev, &object_attributes.object, access, &opened);
   free(object_attributes.buffer);
   if (ret)
      return d12_platform_external_error();

   *handle = (HANDLE)opened;
   return S_OK;
}
