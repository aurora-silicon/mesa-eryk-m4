/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "d12_umd_wddm.h"
#include <windows.h>
#include <d3d12umddi.h>

void d12_umd_install_pipeline_device_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table);
void d12_umd_install_pipeline_device_functions_0010(
   D3D12DDI_DEVICE_FUNCS_CORE_0010 *table);
#if D12_UMD_TARGETS_WDDM23
void d12_umd_install_pipeline_device_functions_0030(
   D3D12DDI_DEVICE_FUNCS_CORE_0030 *table);
#endif
void d12_umd_install_pipeline_command_list_functions(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *table);
