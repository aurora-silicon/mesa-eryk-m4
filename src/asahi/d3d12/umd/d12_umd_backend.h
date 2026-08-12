/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */
#pragma once

#define D3D_UMD_INTERFACE_VERSION 0x5002
#include <windows.h>
#include <d3d12umddi.h>
#include <stdint.h>

#include "agxw_contract.h"

struct d12_umd_device;

HRESULT d12_umd_backend_create(struct d12_umd_device *device,
                               D3D12DDI_HRTADAPTER runtime_adapter,
                               const struct agxw_adapter_identity *identity);
void d12_umd_backend_destroy(struct d12_umd_device *device);
