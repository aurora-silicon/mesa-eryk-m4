/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "d12_umd_wddm.h"
#include <windows.h>
#include <d3d12umddi.h>

#if D12_UMD_TARGETS_WDDM31
void d12_umd_install_unsupported_device_functions_0099(
   D3D12DDI_DEVICE_FUNCS_CORE_0099 *table);
void d12_umd_install_unsupported_command_list_functions_0099(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0099 *table);
HRESULT d12_umd_fill_unsupported_extended_features_table_0096(
   void *table, SIZE_T table_size, UINT table_number);
#endif
