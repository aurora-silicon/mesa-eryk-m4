/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "d12_umd_wddm.h"
#include <windows.h>
#include <d3d12umddi.h>

struct d12_umd_device;

void d12_umd_install_device_core_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table);
#if D12_UMD_TARGETS_WDDM21
void d12_umd_install_device_core_functions_0013(
   D3D12DDI_DEVICE_FUNCS_CORE_0013 *table);
#endif
#if D12_UMD_TARGETS_WDDM23
void d12_umd_install_device_core_functions_0030(
   D3D12DDI_DEVICE_FUNCS_CORE_0030 *table);
#endif
#if D12_UMD_TARGETS_WDDM31
void d12_umd_install_device_core_functions_0099(
   D3D12DDI_DEVICE_FUNCS_CORE_0099 *table);
#endif
void d12_umd_install_command_queue_functions(
   D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *table);

/* DestroyDevice is the final runtime-private-memory boundary.  Retire queue
 * work and release driver-owned heap storage before the shared renderer and
 * the runtime-provided device block disappear. */
HRESULT d12_umd_prepare_device_destroy(struct d12_umd_device *device);
