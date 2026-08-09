/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_private.h"

#include <dxgi.h>

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
