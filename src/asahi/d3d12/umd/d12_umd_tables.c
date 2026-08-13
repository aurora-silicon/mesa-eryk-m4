/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Complete, non-NULL baseline DDI tables.  The committed generator derives
 * every slot from the pinned WDK header.  Implemented entries replace these
 * ARM64 traps as backend object translations land.
 */

#include "d12_umd_tables.h"

#include "d12_umd_internal.h"
#include "d12_umd_command_funcs.h"
#include "d12_umd_device_funcs.h"
#include "d12_umd_descriptor_funcs.h"
#include "d12_umd_pipeline_funcs.h"
#include "d12_umd_present_funcs.h"
#include "d12_umd_query_funcs.h"
#include "d12_umd_resource_command_funcs.h"
#include "d12_umd_state_funcs.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if !defined(_M_ARM64) && !defined(__aarch64__)
#error "D3D12 DDI trap thunks are valid only for Windows on ARM64"
#endif

static void
d12_umd_report_unimplemented(D3D12DDI_HDEVICE handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);

   if (!device)
      return;
   InterlockedIncrement(&device->unimplemented_hits);
   if (device->core_callbacks.pfnSetErrorCb)
      device->core_callbacks.pfnSetErrorCb(device->runtime_device, E_NOTIMPL);
}

static void APIENTRY
d12_umd_trap_void(D3D12DDI_HDEVICE handle)
{
   d12_umd_report_unimplemented(handle);
}

static void APIENTRY
d12_umd_trap_destroy_void(D3D12DDI_HDEVICE handle)
{
   (void)handle;
}

static HRESULT APIENTRY
d12_umd_trap_hresult(D3D12DDI_HDEVICE handle)
{
   d12_umd_report_unimplemented(handle);
   return E_NOTIMPL;
}

static SIZE_T APIENTRY
d12_umd_trap_size(D3D12DDI_HDEVICE handle)
{
   (void)handle;
   return 64;
}

static UINT APIENTRY
d12_umd_trap_uint(D3D12DDI_HDEVICE handle)
{
   d12_umd_report_unimplemented(handle);
   return 0;
}

static D3D12DDI_CPU_DESCRIPTOR_HANDLE APIENTRY
d12_umd_trap_cpu_descriptor(D3D12DDI_HDEVICE handle)
{
   d12_umd_report_unimplemented(handle);
   return (D3D12DDI_CPU_DESCRIPTOR_HANDLE){0};
}

static D3D12DDI_GPU_DESCRIPTOR_HANDLE APIENTRY
d12_umd_trap_gpu_descriptor(D3D12DDI_HDEVICE handle)
{
   d12_umd_report_unimplemented(handle);
   return (D3D12DDI_GPU_DESCRIPTOR_HANDLE){0};
}

static D3D12DDI_HEAP_AND_RESOURCE_SIZES APIENTRY
d12_umd_trap_heap_resource_sizes(D3D12DDI_HDEVICE handle)
{
   d12_umd_report_unimplemented(handle);
   return (D3D12DDI_HEAP_AND_RESOURCE_SIZES){0};
}

static D3D12DDI_GPU_VIRTUAL_ADDRESS APIENTRY
d12_umd_trap_gpu_virtual_address(D3D12DDI_HDEVICE handle)
{
   d12_umd_report_unimplemented(handle);
   return 0;
}

static D3DKMT_HANDLE APIENTRY
d12_umd_trap_kmt_handle(D3D12DDI_HDEVICE handle)
{
   d12_umd_report_unimplemented(handle);
   return 0;
}

static void APIENTRY
d12_umd_command_list_trap_void(D3D12DDI_HCOMMANDLIST handle)
{
   (void)handle;
}

static void APIENTRY
d12_umd_command_queue_trap_void(D3D12DDI_HCOMMANDQUEUE handle)
{
   (void)handle;
}

#include "d12_umd_tables_generated.inc"

_Static_assert(D12_UMD_DEVICE_CORE_ENTRY_COUNT == 89,
               "WDK 26100 DDI 0003 device table width changed");
_Static_assert(D12_UMD_COMMAND_LIST_ENTRY_COUNT == 51,
               "WDK 26100 DDI 0003 command-list table width changed");
_Static_assert(D12_UMD_COMMAND_QUEUE_ENTRY_COUNT == 7,
               "WDK 26100 DDI 0003 command-queue table width changed");
_Static_assert(sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0003) ==
                  D12_UMD_DEVICE_CORE_ENTRY_COUNT * sizeof(void *),
               "device table contains non-pointer fields or changed width");
_Static_assert(sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0010) ==
                  91u * sizeof(void *),
               "WDK 26100 DDI 0010 device table width changed");
#if D12_UMD_TARGETS_WDDM21
_Static_assert(sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0013) ==
                  92u * sizeof(void *),
               "WDK 26100 DDI 0013 device table width changed");
_Static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0013,
                        pfnCalcPrivateRootSignatureSize) ==
                  54u * sizeof(void *),
               "DDI 0013 root-signature table offset changed");
_Static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0013,
                        pfnGetDebugAllocationInfo) ==
                  91u * sizeof(void *),
               "DDI 0013 debug-allocation table offset changed");
#endif
#if D12_UMD_TARGETS_WDDM23
_Static_assert(sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0030) ==
                  92u * sizeof(void *),
               "WDK 26100 DDI 0030 device table width changed");
_Static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0030,
                        pfnCalcPrivateCommandQueueSize) ==
                  26u * sizeof(void *),
               "DDI 0030 command-queue table offset changed");
_Static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0030,
                        pfnCheckResourceAllocationInfo) ==
                  75u * sizeof(void *),
               "DDI 0030 allocation-info table offset changed");
_Static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0030,
                        pfnGetDebugAllocationInfo) ==
                  91u * sizeof(void *),
               "DDI 0030 debug-allocation table offset changed");
_Static_assert(sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0032) ==
                  57u * sizeof(void *),
               "WDK 26100 DDI 0032 command-list table width changed");
_Static_assert(offsetof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0032,
                        pfnAtomicCopyBufferRegion) ==
                  51u * sizeof(void *),
               "DDI 0032 command-list extension offset changed");
_Static_assert(offsetof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0032,
                        pfnWriteBufferImmediate) ==
                  56u * sizeof(void *),
               "DDI 0032 immediate-write offset changed");
#endif
_Static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0010,
                        pfnCalcPrivatePipelineLibrarySize) ==
                  85u * sizeof(void *),
               "DDI 0010 pipeline-library table offset changed");
_Static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0010,
                        pfnMapHeap) == 57u * sizeof(void *),
               "DDI 0010 post-serialization-table offset changed");
_Static_assert(offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0003,
                        pfnMapHeap) == 61u * sizeof(void *),
               "DDI 0003 object-serialization table width changed");
_Static_assert(sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003) ==
                  D12_UMD_COMMAND_LIST_ENTRY_COUNT * sizeof(void *),
               "command-list table changed width");
_Static_assert(sizeof(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001) ==
                  D12_UMD_COMMAND_QUEUE_ENTRY_COUNT * sizeof(void *),
               "command-queue table changed width");

HRESULT
d12_umd_fill_baseline_table(D3D12DDI_TABLE_TYPE table_type, void *table,
                            SIZE_T table_size, UINT table_number)
{
   if (!table || table_number != 0)
      return E_INVALIDARG;

   switch (table_type) {
   case D3D12DDI_TABLE_TYPE_DEVICE_CORE:
      if (table_size != sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0003)
#if D12_UMD_TARGETS_WDDM23
          && table_size != sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0030)
#elif D12_UMD_TARGETS_WDDM21
          && table_size != sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0010) &&
          table_size != sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0013)
#endif
      )
         return E_INVALIDARG;
      {
         D3D12DDI_DEVICE_FUNCS_CORE_0003 baseline;
         memset(&baseline, 0, sizeof(baseline));
         d12_umd_fill_device_core_traps(&baseline);
         d12_umd_install_device_core_functions(&baseline);
         d12_umd_install_command_device_functions(&baseline);
         d12_umd_install_state_device_functions(&baseline);
         d12_umd_install_pipeline_device_functions(&baseline);
         d12_umd_install_descriptor_device_functions(&baseline);
         d12_umd_install_query_device_functions(&baseline);
         d12_umd_install_present_device_functions(&baseline);

         memset(table, 0, table_size);
         if (table_size == sizeof(D3D12DDI_DEVICE_FUNCS_CORE_0003)) {
            memcpy(table, &baseline, sizeof(baseline));
         } else {
            D3D12DDI_DEVICE_FUNCS_CORE_0010 release1;
            const size_t prefix =
               offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0003,
                        pfnSerializeObject);
            const size_t suffix_source =
               offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnMapHeap);
            const size_t suffix_destination =
               offsetof(D3D12DDI_DEVICE_FUNCS_CORE_0010, pfnMapHeap);
            const size_t suffix_size = sizeof(baseline) - suffix_source;
            memset(&release1, 0, sizeof(release1));
            memcpy(&release1, &baseline, prefix);
            memcpy((uint8_t *)&release1 + suffix_destination,
                   (const uint8_t *)&baseline + suffix_source, suffix_size);
            d12_umd_install_pipeline_device_functions_0010(&release1);
            if (table_size == sizeof(release1)) {
               memcpy(table, &release1, sizeof(release1));
#if D12_UMD_TARGETS_WDDM23
            } else {
               D3D12DDI_DEVICE_FUNCS_CORE_0030 *release3 = table;
               memcpy(release3, &release1, sizeof(release1));
               d12_umd_install_state_device_functions_0013(
                  (D3D12DDI_DEVICE_FUNCS_CORE_0013 *)release3);
               d12_umd_install_device_core_functions_0030(release3);
               d12_umd_install_pipeline_device_functions_0030(release3);
#elif D12_UMD_TARGETS_WDDM21
            } else {
               D3D12DDI_DEVICE_FUNCS_CORE_0013 *release1_root11 = table;
               memcpy(release1_root11, &release1, sizeof(release1));
               d12_umd_install_state_device_functions_0013(release1_root11);
               d12_umd_install_device_core_functions_0013(release1_root11);
#endif
            }
         }
      }
      return S_OK;
   case D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D:
      if (table_size != sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0003)
#if D12_UMD_TARGETS_WDDM23
          && table_size != sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0032)
#endif
      )
         return E_INVALIDARG;
      {
         D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 baseline;
         memset(&baseline, 0, sizeof(baseline));
         d12_umd_fill_command_list_traps(&baseline);
         d12_umd_install_command_list_functions(&baseline);
         d12_umd_install_state_command_list_functions(&baseline);
         d12_umd_install_pipeline_command_list_functions(&baseline);
         d12_umd_install_descriptor_command_list_functions(&baseline);
         d12_umd_install_query_command_list_functions(&baseline);
         d12_umd_install_resource_command_list_functions(&baseline);
         d12_umd_install_present_command_list_functions(&baseline);
         memset(table, 0, table_size);
         memcpy(table, &baseline, sizeof(baseline));
#if D12_UMD_TARGETS_WDDM23
         if (table_size == sizeof(D3D12DDI_COMMAND_LIST_FUNCS_3D_0032)) {
            D3D12DDI_COMMAND_LIST_FUNCS_3D_0032 *release3 = table;
            d12_umd_install_command_list_functions_0032(release3);
            d12_umd_install_resource_command_list_functions_0032(release3);
            d12_umd_install_present_command_list_functions_0032(release3);
         }
#endif
      }
      return S_OK;
   case D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D:
      if (table_size != sizeof(D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001))
         return E_INVALIDARG;
      memset(table, 0, table_size);
      d12_umd_fill_command_queue_traps(table);
      d12_umd_install_command_queue_functions(table);
      return S_OK;
   default:
      return E_NOTIMPL;
   }
}
