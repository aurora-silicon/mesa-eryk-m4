/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "d12_umd_wddm.h"
#include <windows.h>
#include <d3d12umddi.h>

void d12_umd_install_command_device_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table);
void d12_umd_install_command_list_functions(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *table);
#if D12_UMD_TARGETS_WDDM23
void d12_umd_install_command_list_functions_0032(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0032 *table);
#endif

#if D12_UMD_TARGETS_WDDM24
void d12_umd_install_command_device_functions_0040(
   D3D12DDI_DEVICE_FUNCS_CORE_0040 *table);
void d12_umd_install_command_list_functions_0040(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0040 *table);
#endif
