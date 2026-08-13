/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 core-runtime callback transport for one AGX command queue.
 */

#pragma once

#include "d12_umd_wddm.h"
#include <windows.h>
#include <d3d12umddi.h>

#include <stdbool.h>

#include "agxw_contract.h"
#include "d12_umd_kmt.h"

struct d12_umd_queue_runtime {
   D3D12DDI_HRTCOMMANDQUEUE runtime_queue;
   HANDLE context;
   D3DKMT_HANDLE paging_queue;
   D3DKMT_HANDLE paging_fence;
   const volatile uint64_t *paging_fence_cpu;
   D3DGPU_VIRTUAL_ADDRESS usc_base;
   uint64_t usc_size;
   bool owns_usc_reservation;
};

HRESULT d12_umd_queue_runtime_create(
   const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *callbacks,
   struct d12_umd_kmt *kmt, D3D12DDI_HRTCOMMANDQUEUE runtime_queue,
   enum agxw_queue_priority priority, uint64_t usc_exec_base,
   uint64_t usc_size, bool reserve_usc,
   struct d12_umd_queue_runtime *queue);

HRESULT d12_umd_queue_runtime_destroy(
   const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *callbacks,
   struct d12_umd_kmt *kmt, struct d12_umd_queue_runtime *queue);
