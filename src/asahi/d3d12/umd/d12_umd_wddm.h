/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* Select the WDK's conditional user-mode ABI before any Windows graphics
 * header is included. The source supports the live-proven WDDM 2.0 baseline
 * and an isolated WDDM 2.1 build, the minimum model that carries DXIL. */
#define D12_UMD_WDDM20_INTERFACE_VERSION 0x5002
#define D12_UMD_WDDM21_INTERFACE_VERSION 0x6003

#if defined(D3D_UMD_INTERFACE_VERSION)
#if D3D_UMD_INTERFACE_VERSION != D12_UMD_WDDM20_INTERFACE_VERSION && \
    D3D_UMD_INTERFACE_VERSION != D12_UMD_WDDM21_INTERFACE_VERSION
#error Native AGX D3D12 UMD supports only the pinned WDDM 2.0 or 2.1 ABI
#endif
#else
#define D3D_UMD_INTERFACE_VERSION D12_UMD_WDDM20_INTERFACE_VERSION
#endif

#define D12_UMD_TARGETS_WDDM21 \
   (D3D_UMD_INTERFACE_VERSION == D12_UMD_WDDM21_INTERFACE_VERSION)
