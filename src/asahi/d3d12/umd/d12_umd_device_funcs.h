/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "d12_umd_wddm.h"
#include <windows.h>
#include <d3d12umddi.h>

void d12_umd_install_device_core_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table);
void d12_umd_install_command_queue_functions(
   D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *table);
