/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#define D3D_UMD_INTERFACE_VERSION 0x5002
#include <windows.h>
#include <d3d12umddi.h>

HRESULT d12_umd_fill_baseline_table(D3D12DDI_TABLE_TYPE table_type,
                                    void *table, SIZE_T table_size,
                                    UINT table_number);
