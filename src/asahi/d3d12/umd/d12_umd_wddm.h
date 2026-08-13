/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* Select the WDK's conditional user-mode ABI before any Windows graphics
 * header is included. WDDM 2.1 is the installable DXIL minimum. WDDM 2.3 is
 * a host-only forward target for the Release 3 D3D12 DDI; its KMD half is not
 * claimed until Aurora's DXGK table is uplifted independently. */
#define D12_UMD_WDDM20_INTERFACE_VERSION 0x5002
#define D12_UMD_WDDM21_INTERFACE_VERSION 0x6003
#define D12_UMD_WDDM23_INTERFACE_VERSION 0x8001

#if defined(D3D_UMD_INTERFACE_VERSION)
#if D3D_UMD_INTERFACE_VERSION != D12_UMD_WDDM20_INTERFACE_VERSION && \
    D3D_UMD_INTERFACE_VERSION != D12_UMD_WDDM21_INTERFACE_VERSION && \
    D3D_UMD_INTERFACE_VERSION != D12_UMD_WDDM23_INTERFACE_VERSION
#error Native AGX D3D12 UMD supports only the pinned WDDM 2.0, 2.1, or 2.3 ABI
#endif
#else
#define D3D_UMD_INTERFACE_VERSION D12_UMD_WDDM20_INTERFACE_VERSION
#endif

#define D12_UMD_TARGETS_WDDM21_EXACT \
   (D3D_UMD_INTERFACE_VERSION == D12_UMD_WDDM21_INTERFACE_VERSION)
/* Existing feature/layout gates use this historical name for the WDDM 2.1
 * ABI lineage, which all later targets inherit. */
#define D12_UMD_TARGETS_WDDM21 \
   (D3D_UMD_INTERFACE_VERSION >= D12_UMD_WDDM21_INTERFACE_VERSION)
#define D12_UMD_TARGETS_WDDM23 \
   (D3D_UMD_INTERFACE_VERSION == D12_UMD_WDDM23_INTERFACE_VERSION)
