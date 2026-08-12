/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */
#pragma once

#define D3D_UMD_INTERFACE_VERSION 0x5002
#include <windows.h>
#include <d3d12umddi.h>

void d12_umd_install_command_device_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table);
void d12_umd_install_command_list_functions(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *table);
