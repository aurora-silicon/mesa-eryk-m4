/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#define D3D_UMD_INTERFACE_VERSION 0x5002
#include <windows.h>
#include <d3d12umddi.h>
#include <dxgiddi.h>

/* QueryResourceResidencyDXGI uses the two successful Direct3D residency
 * status HRESULTs from the Windows SDK's d3d9.h contract.  Keep the tiny ABI
 * constants local so the D3D12 UMD does not pull the legacy public API in. */
#define D12_UMD_S_NOT_RESIDENT ((HRESULT)UINT32_C(0x08760875))
#define D12_UMD_S_RESIDENT_IN_SHARED_MEMORY \
   ((HRESULT)UINT32_C(0x08760876))

HRESULT d12_umd_fill_dxgi_table(void *table, SIZE_T table_size,
                                UINT table_number);

void d12_umd_install_present_device_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table);
void d12_umd_install_present_command_list_functions(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *table);
