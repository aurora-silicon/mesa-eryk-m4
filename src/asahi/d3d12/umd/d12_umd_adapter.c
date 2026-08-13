/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Native D3D12 WDDM UMD adapter discovery and version/caps negotiation.
 */

#include "d12_umd_wddm.h"
#include <windows.h>
#include <d3d12umddi.h>

#include "../d12_core_caps.h"
#include "d12_umd_abi.h"
#ifndef D12_UMD_WITH_AGX_BACKEND
#define D12_UMD_WITH_AGX_BACKEND 0
#endif
#if D12_UMD_WITH_AGX_BACKEND
#include "d12_umd_backend.h"
#endif
#include "d12_umd_internal.h"
#include "d12_umd_present_funcs.h"
#include "d12_umd_tables.h"

#include <string.h>

_Static_assert(D3D12DDI_SUPPORTED_0003 == D12_UMD_DDI_SUPPORTED_0003,
               "pinned WDK D3D12 DDI 0003 value changed");
_Static_assert(D3D12DDI_SUPPORTED_0010 == D12_UMD_DDI_SUPPORTED_0010,
               "pinned WDK D3D12 DDI 0010 value changed");
_Static_assert(D3D12DDI_SUPPORTED_0011 == D12_UMD_DDI_SUPPORTED_0011,
               "pinned WDK D3D12 DDI 0011 value changed");

struct d12_umd_adapter {
   D3D12DDI_HRTADAPTER runtime_adapter;
   D3DDDI_ADAPTERCALLBACKS callbacks;
   struct agxw_adapter_identity identity;
   PVOID volatile command_list_3d_runtime_table;
   DXGI_DDI_BASE_CALLBACKS *volatile dxgi_callbacks;
};

HRESULT APIENTRY OpenAdapter12(D3D12DDIARG_OPENADAPTER *arguments);

static struct d12_umd_adapter *
d12_umd_adapter_from_handle(D3D12DDI_HADAPTER handle)
{
   return (struct d12_umd_adapter *)handle.pDrvPrivate;
}

static HRESULT APIENTRY
d12_umd_get_supported_versions(D3D12DDI_HADAPTER handle, UINT32 *entries,
                               UINT64 *versions)
{
   const size_t count = d12_umd_supported_version_count();

   if (!d12_umd_adapter_from_handle(handle) || !entries ||
       count > UINT32_MAX)
      return E_INVALIDARG;

   if (versions && *entries < (UINT32)count)
      return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

   *entries = (UINT32)count;
   if (versions)
      memcpy(versions, d12_umd_supported_versions(), count * sizeof(*versions));

   return S_OK;
}

static SIZE_T APIENTRY
d12_umd_calc_private_device_size(
   D3D12DDI_HADAPTER handle,
   const D3D12DDIARG_CALCPRIVATEDEVICESIZE *arguments)
{
   if (!d12_umd_adapter_from_handle(handle) || !arguments ||
       !d12_umd_version_supported(arguments->Interface,
                                  arguments->Version))
      return 0;

   return sizeof(struct d12_umd_device);
}

static HRESULT APIENTRY
d12_umd_create_device(D3D12DDI_HADAPTER handle,
                      const D3D12DDIARG_CREATEDEVICE_0003 *arguments)
{
   struct d12_umd_adapter *adapter = d12_umd_adapter_from_handle(handle);
   struct d12_umd_device *device;
   HRESULT result;

   if (!adapter || !arguments ||
       !arguments->hDrvDevice.pDrvPrivate || !arguments->pKTCallbacks ||
       !arguments->p12UMCallbacks ||
       !arguments->p12UMCallbacks->pfnSetCommandListDDITableCb)
      return E_INVALIDARG;
   if (!d12_umd_version_supported(arguments->Interface,
                                  arguments->Version))
      return DXGI_ERROR_UNSUPPORTED;

   device = (struct d12_umd_device *)arguments->hDrvDevice.pDrvPrivate;
   memset(device, 0, sizeof(*device));
   device->signature = D12_UMD_DEVICE_SIGNATURE;
   device->interface_version = arguments->Interface;
   device->runtime_version = arguments->Version;
   device->runtime_device = arguments->hRTDevice;
   device->driver_device = arguments->hDrvDevice;
   device->command_list_3d_runtime_table =
      &adapter->command_list_3d_runtime_table;
   device->dxgi_callbacks = &adapter->dxgi_callbacks;
   device->kernel_callbacks = *arguments->pKTCallbacks;
   device->core_callbacks = *arguments->p12UMCallbacks;
   InitializeSRWLock(&device->queue_lock);
   InitializeSRWLock(&device->heap_lock);
   InitializeSRWLock(&device->rotation_lock);

   result = d12_umd_kmt_init(&device->kmt, arguments->hRTDevice.handle,
                             &device->kernel_callbacks);
   if (FAILED(result)) {
      SecureZeroMemory(device, sizeof(*device));
      return result;
   }

#if D12_UMD_WITH_AGX_BACKEND
   result = d12_umd_backend_create(device, adapter->runtime_adapter,
                                   &adapter->identity);
   if (FAILED(result)) {
      SecureZeroMemory(device, sizeof(*device));
      return result;
   }
#endif

   /* The native D3D12 path has a complete explicit Present handoff, but the
    * DWM shared-resource redirection contract remains gated on foreign-UMD
    * resource metadata.  This success status makes DXGI use Present instead
    * of silently assuming that unsupported shared redirection is available. */
   return DXGI_STATUS_NO_REDIRECTION;
}

static void APIENTRY
d12_umd_destroy_device(D3D12DDI_HDEVICE handle)
{
   struct d12_umd_device *device =
      (struct d12_umd_device *)handle.pDrvPrivate;
   if (device && device->signature == D12_UMD_DEVICE_SIGNATURE) {
#if D12_UMD_WITH_AGX_BACKEND
      d12_umd_backend_destroy(device);
#endif
      SecureZeroMemory(device, sizeof(*device));
   }
}

static HRESULT
d12_umd_copy_cap(const D3D12DDIARG_GETCAPS *arguments, const void *source,
                 size_t size)
{
   if (!arguments->pData || arguments->DataSize != size)
      return E_INVALIDARG;

   memcpy(arguments->pData, source, size);
   return S_OK;
}

static HRESULT
d12_umd_validate_node_cap(const D3D12DDIARG_GETCAPS *arguments)
{
   if (!arguments->pInfo || *(const UINT *)arguments->pInfo != 0)
      return E_INVALIDARG;
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_get_caps(D3D12DDI_HADAPTER handle,
                 const D3D12DDIARG_GETCAPS *arguments)
{
   const struct d12_core_caps *caps = d12_core_get_caps();

   if (!d12_umd_adapter_from_handle(handle) || !arguments)
      return E_INVALIDARG;

   switch (arguments->Type) {
   case D3D12DDICAPS_TYPE_TEXTURE_LAYOUT:
      if (arguments->pInfo)
         return E_INVALIDARG;
      if (arguments->DataSize == sizeof(D3D12DDI_TEXTURE_LAYOUT_CAPS)) {
         const D3D12DDI_TEXTURE_LAYOUT_CAPS data = {
            .DeviceDependentLayoutCount = 0,
            .DeviceDependentSwizzleCount = 0,
            .Supports64KStandardSwizzle = FALSE,
         };
         return d12_umd_copy_cap(arguments, &data, sizeof(data));
      }
      if (arguments->DataSize == sizeof(D3D12DDI_TEXTURE_LAYOUT_CAPS_0001)) {
         const D3D12DDI_TEXTURE_LAYOUT_CAPS_0001 data = {
            .DeviceDependentLayoutCount = 0,
            .DeviceDependentSwizzleCount = 0,
            .Supports64KStandardSwizzle = FALSE,
            .SupportsRowMajorTexture = TRUE,
         };
         return d12_umd_copy_cap(arguments, &data, sizeof(data));
      }
      return E_INVALIDARG;

   case D3D12DDICAPS_TYPE_TEXTURE_LAYOUT_SETS: {
      const UINT *info = arguments->pInfo;
      if (!info || info[0] != D3D12DDI_TL_ROW_MAJOR ||
          info[1] > D3D12DDI_FUNCUNIT_COPY_DST)
         return E_NOTIMPL;
      const D3D12DDI_ROW_MAJOR_LAYOUT_CAPS data = {
         .SubCaps = {
            {
               .MaxElementSize = 16,
               .BaseOffsetAlignment = 512,
               .PitchAlignment = 256,
               .DepthPitchAlignment = 256,
            },
            {0},
         },
         .Flags = D3D12DDI_ROW_MAJOR_LAYOUT_FLAG_NONE,
      };
      return d12_umd_copy_cap(arguments, &data, sizeof(data));
   }

   case D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE: {
      D3D12DDI_MEMORY_ARCHITECTURE_CAPS data = {
         .UMA = caps->uma,
         .IOCoherent = caps->cache_coherent_uma,
         .CacheCoherent = caps->cache_coherent_uma,
      };
      HRESULT result = d12_umd_validate_node_cap(arguments);
      return FAILED(result) ? result
                            : d12_umd_copy_cap(arguments, &data, sizeof(data));
   }

   case D3D12DDICAPS_TYPE_ARCHITECTURE_INFO: {
      const D3D12DDI_ARCHITECTURE_INFO_DATA data = {
         .TileBasedDeferredRenderer = caps->tile_based_deferred_renderer,
      };
      return d12_umd_copy_cap(arguments, &data, sizeof(data));
   }

   case D3D12DDICAPS_TYPE_D3D12_OPTIONS: {
      const D3D12DDI_D3D12_OPTIONS_DATA_0003 data = {
         .ResourceBindingTier =
            (D3D12DDI_RESOURCE_BINDING_TIER)caps->resource_binding_tier,
         .ConservativeRasterizationTier =
            D3D12DDI_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED,
         .TiledResourcesTier =
            (D3D12DDI_TILED_RESOURCES_TIER)caps->tiled_resources_tier,
         .CrossNodeSharingTier = D3D12DDI_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED,
         .VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation =
            FALSE,
         .OutputMergerLogicOp = FALSE,
         .ResourceHeapTier =
            (D3D12DDI_RESOURCE_HEAP_TIER)caps->resource_heap_tier,
      };
      return d12_umd_copy_cap(arguments, &data, sizeof(data));
   }

   case D3D12DDICAPS_TYPE_3DPIPELINESUPPORT: {
      const D3D12DDI_3DPIPELINELEVEL data =
         D3D12DDI_3DPIPELINELEVEL_11_0;
      return d12_umd_copy_cap(arguments, &data, sizeof(data));
   }

   case D3D12DDICAPS_TYPE_GPUVA_CAPS: {
      const D3D12DDI_GPUVA_CAPS_0004 data = {
         .MaxGPUVirtualAddressBitsPerResource = 40,
      };
      HRESULT result = d12_umd_validate_node_cap(arguments);
      return FAILED(result) ? result
                            : d12_umd_copy_cap(arguments, &data, sizeof(data));
   }

   case D3D12DDICAPS_TYPE_SHADER: {
      const D3D12DDI_SHADER_CAPS_0003 data = {
         .MinPrecision = D3D12DDI_SHADER_MIN_PRECISION_NONE,
         .Doubles = FALSE,
         .ShaderSpecifiedStencilRef = FALSE,
         .TypedUAVLoadAdditionalFormats = FALSE,
         .ROVs = FALSE,
      };
      return d12_umd_copy_cap(arguments, &data, sizeof(data));
   }

   case D3D12DDICAPS_TYPE_0011_SHADER_MODELS: {
#if !D12_UMD_TARGETS_WDDM21
      return E_NOTIMPL;
#else
      static const D3D12DDI_SHADER_MODEL models[] = {
         D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011,
         D3D12DDI_SHADER_MODEL_5_1_RELEASE_0011,
      };
      const UINT model_count = (UINT)ARRAYSIZE(models);
      D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 *data = arguments->pData;
      UINT capacity;

      if (!data ||
          arguments->DataSize != sizeof(*data) ||
          !data->pNumShaderModelsSupported)
         return E_INVALIDARG;
      capacity = *data->pNumShaderModelsSupported;
      *data->pNumShaderModelsSupported = model_count;
      if (!data->pShaderModelsSupported)
         return S_OK;
      if (capacity < model_count)
         return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
      memcpy(data->pShaderModelsSupported, models, sizeof(models));
      return S_OK;
#endif
   }

   default:
      return E_NOTIMPL;
   }
}

static HRESULT APIENTRY
d12_umd_get_optional_ddi_tables(D3D12DDI_HADAPTER handle, UINT32 *entries,
                                D3D12DDI_TABLE_REQUEST *requests)
{
   if (!d12_umd_adapter_from_handle(handle) || !entries)
      return E_INVALIDARG;

   (void)requests;
   *entries = 0;
   return S_OK;
}

static HRESULT APIENTRY
d12_umd_fill_ddi_table(D3D12DDI_HADAPTER handle,
                       D3D12DDI_TABLE_TYPE table_type, void *table,
                       SIZE_T table_size, UINT table_number,
                       D3D12DDI_HRTTABLE runtime_table)
{
   struct d12_umd_adapter *adapter = d12_umd_adapter_from_handle(handle);
   HRESULT result;

   if (!adapter || !table || !table_size)
      return E_INVALIDARG;
   if (table_type == D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D &&
       !runtime_table.handle)
      return E_INVALIDARG;

   if (table_type == D3D12DDI_TABLE_TYPE_DXGI) {
      DXGI_DDI_BASE_ARGS *dxgi = table;

      result = d12_umd_fill_dxgi_table(table, table_size, table_number);
      if (SUCCEEDED(result)) {
         InterlockedExchangePointer(
            (PVOID volatile *)&adapter->dxgi_callbacks,
            dxgi->pDXGIBaseCallbacks);
      }
      return result;
   }

   result = d12_umd_fill_baseline_table(table_type, table, table_size,
                                        table_number);
   if (SUCCEEDED(result) &&
       table_type == D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D) {
      InterlockedExchangePointer(&adapter->command_list_3d_runtime_table,
                                 runtime_table.handle);
   }
   return result;
}

static HRESULT APIENTRY
d12_umd_close_adapter(D3D12DDI_HADAPTER handle)
{
   struct d12_umd_adapter *adapter = d12_umd_adapter_from_handle(handle);
   if (!adapter)
      return E_INVALIDARG;

   SecureZeroMemory(adapter, sizeof(*adapter));
   HeapFree(GetProcessHeap(), 0, adapter);
   return S_OK;
}

static const D3D12DDI_ADAPTERFUNCS d12_umd_adapter_functions = {
   .pfnCalcPrivateDeviceSize = d12_umd_calc_private_device_size,
   .pfnCreateDevice = d12_umd_create_device,
   .pfnCloseAdapter = d12_umd_close_adapter,
   .pfnGetSupportedVersions = d12_umd_get_supported_versions,
   .pfnGetCaps = d12_umd_get_caps,
   .pfnGetOptionalDDITables = d12_umd_get_optional_ddi_tables,
   .pfnFillDDITable = d12_umd_fill_ddi_table,
   .pfnDestroyDevice = d12_umd_destroy_device,
};

HRESULT APIENTRY
OpenAdapter12(D3D12DDIARG_OPENADAPTER *arguments)
{
   struct agxw_adapter_identity identity;
   struct d12_umd_adapter *adapter;
   D3DDDICB_QUERYADAPTERINFO query;
   HRESULT result;

   if (!arguments || !arguments->pAdapterCallbacks ||
       !arguments->pAdapterCallbacks->pfnQueryAdapterInfoCb ||
       !arguments->pAdapterFuncs)
      return E_INVALIDARG;

   memset(&identity, 0, sizeof(identity));
   query = (D3DDDICB_QUERYADAPTERINFO){
      .pPrivateDriverData = &identity,
      .PrivateDriverDataSize = sizeof(identity),
   };
   result = arguments->pAdapterCallbacks->pfnQueryAdapterInfoCb(
      arguments->hRTAdapter.handle, &query);
   if (FAILED(result))
      return result;

   if (d12_umd_validate_identity(&identity, sizeof(identity), &identity) !=
       D12_UMD_IDENTITY_OK)
      return DXGI_ERROR_UNSUPPORTED;

   adapter = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*adapter));
   if (!adapter)
      return E_OUTOFMEMORY;

   adapter->runtime_adapter = arguments->hRTAdapter;
   adapter->callbacks = *arguments->pAdapterCallbacks;
   adapter->identity = identity;

   arguments->hAdapter.pDrvPrivate = adapter;
   *arguments->pAdapterFuncs = d12_umd_adapter_functions;
   return S_OK;
}
