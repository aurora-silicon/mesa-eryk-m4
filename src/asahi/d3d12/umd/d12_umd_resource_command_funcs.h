/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */
#pragma once

#define D3D_UMD_INTERFACE_VERSION 0x5002
#include <windows.h>
#include <d3d12umddi.h>
#include <stdbool.h>

struct d12_umd_device;
struct d12_umd_resource;

struct d12_umd_blt_desc {
   UINT destination_subresource;
   UINT destination_left;
   UINT destination_top;
   UINT destination_right;
   UINT destination_bottom;
   UINT source_subresource;
   UINT source_left;
   UINT source_top;
   UINT source_right;
   UINT source_bottom;
   UINT flags;
   DXGI_DDI_MODE_ROTATION rotation;
};

HRESULT d12_umd_record_blt(struct d12_umd_device *device, void *command_list,
                           struct d12_umd_resource *destination,
                           struct d12_umd_resource *source,
                           const struct d12_umd_blt_desc *desc);
HRESULT d12_umd_validate_blt(struct d12_umd_device *device,
                             struct d12_umd_resource *destination,
                             struct d12_umd_resource *source,
                             const struct d12_umd_blt_desc *desc);
bool d12_umd_resource_subresource_extent(
   const struct d12_umd_resource *resource, UINT subresource,
   UINT *width, UINT *height);

void d12_umd_install_resource_command_list_functions(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *table);
