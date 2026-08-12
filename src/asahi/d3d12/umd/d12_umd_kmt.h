/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * WDDM runtime-callback transport used by the D3D12 DDI frontend.  This is
 * intentionally separate from the public-COM D3DKMT winsys: dxgkrnl already
 * owns the DDI device and supplies these callbacks.
 */

#pragma once

#define D3D_UMD_INTERFACE_VERSION 0x5002
#include <windows.h>
#include <d3d12umddi.h>

#include "agxw_contract.h"

struct d12_umd_kmt {
   HANDLE runtime_device;
   const D3DDDI_DEVICECALLBACKS *callbacks;
};

struct d12_umd_allocation {
   D3DKMT_HANDLE allocation;
   D3DKMT_HANDLE resource;
   D3DGPU_VIRTUAL_ADDRESS gpu_virtual_address;
   uint64_t size;
};

HRESULT d12_umd_kmt_init(struct d12_umd_kmt *kmt, HANDLE runtime_device,
                         const D3DDDI_DEVICECALLBACKS *callbacks);

HRESULT d12_umd_kmt_allocate(struct d12_umd_kmt *kmt,
                             HANDLE runtime_resource, uint64_t size,
                             uint64_t alignment, uint32_t agxw_flags,
                             const void *resource_private_data,
                             uint32_t resource_private_data_size,
                             struct d12_umd_allocation *allocation);

HRESULT d12_umd_kmt_deallocate(struct d12_umd_kmt *kmt,
                               HANDLE runtime_resource,
                               struct d12_umd_allocation *allocation);

/*
 * Submit an already-populated AGX DMA allocation.  The allocation must begin
 * with struct agxw_dma_header followed by cmdbuf_size bytes of native AGX
 * commands.  Synchronization is deliberately expressed with the WDDM fence
 * callbacks rather than embedded KMD-private handles.
 */
HRESULT d12_umd_kmt_submit_dma(struct d12_umd_kmt *kmt, HANDLE context,
                               D3DGPU_VIRTUAL_ADDRESS commands,
                               uint32_t cmdbuf_size,
                               D3DKMT_HANDLE result_allocation);

HRESULT d12_umd_kmt_reserve_gpu_va(
   struct d12_umd_kmt *kmt, D3DKMT_HANDLE paging_queue,
   D3DKMT_HANDLE paging_fence, const volatile uint64_t *paging_fence_cpu,
   D3DGPU_VIRTUAL_ADDRESS base, uint64_t size);

HRESULT d12_umd_kmt_free_gpu_va(struct d12_umd_kmt *kmt,
                                D3DGPU_VIRTUAL_ADDRESS base, uint64_t size);

HRESULT d12_umd_kmt_lock(struct d12_umd_kmt *kmt, D3DKMT_HANDLE allocation,
                         void **mapping);

HRESULT d12_umd_kmt_unlock(struct d12_umd_kmt *kmt,
                           D3DKMT_HANDLE allocation);
