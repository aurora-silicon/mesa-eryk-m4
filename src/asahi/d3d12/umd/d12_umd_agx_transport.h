/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "d12_umd_wddm.h"
#include <windows.h>
#include <d3d12umddi.h>

#include "asahi/lib/agx_d3dkmt_runtime.h"

#define D12_UMD_AGX_CONTEXT_CAPACITY 64u

struct d12_umd_agx_context_slot {
   HANDLE runtime_context;
};

struct d12_umd_agx_transport {
   D3D12DDI_HRTADAPTER runtime_adapter;
   D3D12DDI_HRTDEVICE runtime_device;
   const D3DDDI_DEVICECALLBACKS *callbacks;
   SRWLOCK context_lock;
   struct d12_umd_agx_context_slot contexts[D12_UMD_AGX_CONTEXT_CAPACITY];
};

HRESULT d12_umd_agx_transport_init(
   struct d12_umd_agx_transport *transport,
   D3D12DDI_HRTADAPTER runtime_adapter,
   D3D12DDI_HRTDEVICE runtime_device,
   const D3DDDI_DEVICECALLBACKS *callbacks);

void d12_umd_agx_transport_finish(
   struct d12_umd_agx_transport *transport);

struct agx_d3dkmt_runtime_device d12_umd_agx_runtime_device(
   struct d12_umd_agx_transport *transport,
   struct agx_kmt_luid process_key, uint32_t params_size);

HANDLE d12_umd_agx_transport_context_handle(
   struct d12_umd_agx_transport *transport, uint32_t token);
