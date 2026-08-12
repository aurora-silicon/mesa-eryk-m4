/*
 * Copyright 2026 the Asahi Linux contributors
 * SPDX-License-Identifier: MIT
 */
#pragma once

/*
 * Context-bearing WDDM transport for a runtime-owned graphics device.
 *
 * The ordinary Windows winsys resolves process-global D3DKMT entrypoints and
 * opens its own adapter/device.  A D3D UMD must instead use the callback table
 * supplied by the Direct3D runtime.  These operations deliberately retain the
 * source-pinned, frontend-neutral agx_kmt structures while adding `userdata`;
 * the D3D12 DDI adapter translates them to D3DDDICB_* structures.  Context
 * HANDLEs that are wider than D3DKMT_HANDLE are represented by bridge-owned
 * 32-bit tokens, never truncated.
 */

#include <stdbool.h>
#include <stdint.h>

#include "agx_d3dkmt.h"

struct agx_d3dkmt_runtime_ops {
   agx_kmt_status (*CreateAllocation2)(
      void *userdata, struct agx_kmt_createallocation *arguments);
   agx_kmt_status (*QueryResourceInfo)(
      void *userdata, struct agx_kmt_queryresourceinfo *arguments);
   agx_kmt_status (*QueryResourceInfoFromNtHandle)(
      void *userdata,
      struct agx_kmt_queryresourceinfofromnthandle *arguments);
   agx_kmt_status (*ShareObjects)(void *userdata, uint32_t count,
                                  const agx_kmt_handle *objects,
                                  void *object_attributes, uint32_t access,
                                  void **handle);
   agx_kmt_status (*OpenResource)(void *userdata,
                                  struct agx_kmt_openresource *arguments);
   agx_kmt_status (*OpenResourceFromNtHandle)(
      void *userdata, struct agx_kmt_openresourcefromnthandle *arguments);
   agx_kmt_status (*DestroyAllocation2)(
      void *userdata, const struct agx_kmt_destroyallocation2 *arguments);
   agx_kmt_status (*Lock2)(void *userdata,
                           struct agx_kmt_lock2 *arguments);
   agx_kmt_status (*Unlock2)(void *userdata,
                             const struct agx_kmt_unlock2 *arguments);
   agx_kmt_status (*MakeResident)(void *userdata,
                                  struct agx_kmt_makeresident *arguments);
   agx_kmt_status (*Evict)(void *userdata,
                           struct agx_kmt_evict *arguments);

   agx_kmt_status (*CreatePagingQueue)(
      void *userdata, struct agx_kmt_createpagingqueue *arguments);
   agx_kmt_status (*DestroyPagingQueue)(
      void *userdata, struct agx_kmt_destroypagingqueue *arguments);
   agx_kmt_status (*ReserveGpuVirtualAddress)(
      void *userdata, struct agx_kmt_reservegpuva *arguments);
   agx_kmt_status (*MapGpuVirtualAddress)(
      void *userdata, struct agx_kmt_mapgpuva *arguments);
   agx_kmt_status (*UpdateGpuVirtualAddress)(
      void *userdata, const struct agx_kmt_updategpuva *arguments);
   agx_kmt_status (*FreeGpuVirtualAddress)(
      void *userdata, const struct agx_kmt_freegpuva *arguments);

   agx_kmt_status (*CreateContextVirtual)(
      void *userdata, struct agx_kmt_createcontextvirtual *arguments);
   agx_kmt_status (*DestroyContext)(
      void *userdata, const struct agx_kmt_destroycontext *arguments);
   agx_kmt_status (*SubmitCommand)(
      void *userdata, const struct agx_kmt_submitcommand *arguments);

   agx_kmt_status (*CreateSynchronizationObject2)(
      void *userdata, struct agx_kmt_createsyncobj2 *arguments);
   agx_kmt_status (*DestroySynchronizationObject)(
      void *userdata, const struct agx_kmt_destroysyncobj *arguments);
   agx_kmt_status (*OpenSyncObjectFromNtHandle2)(
      void *userdata, struct agx_kmt_opensyncobjfromnthandle2 *arguments);
   agx_kmt_status (*OpenNtHandleFromName)(
      void *userdata, struct agx_kmt_openhandlefromname *arguments);
   agx_kmt_status (*OpenSyncObjectNtHandleFromName)(
      void *userdata, struct agx_kmt_openhandlefromname *arguments);
   agx_kmt_status (*SignalSynchronizationObjectFromCpu)(
      void *userdata, const struct agx_kmt_signalsyncfromcpu *arguments);
   agx_kmt_status (*WaitForSynchronizationObjectFromCpu)(
      void *userdata, const struct agx_kmt_waitsyncfromcpu *arguments);
   agx_kmt_status (*SignalSynchronizationObjectFromGpu)(
      void *userdata, const struct agx_kmt_signalsyncfromgpu *arguments);
   agx_kmt_status (*WaitForSynchronizationObjectFromGpu)(
      void *userdata, const struct agx_kmt_waitsyncfromgpu *arguments);

   agx_kmt_status (*QueryClockCalibration)(
      void *userdata, struct agx_kmt_queryclockcalibration *arguments);
   agx_kmt_status (*Escape)(void *userdata,
                            const struct agx_kmt_escape *arguments);
};

struct agx_d3dkmt_runtime_transport {
   const struct agx_d3dkmt_runtime_ops *ops;
   void *userdata;
};

struct agx_d3dkmt_runtime_device {
   struct agx_d3dkmt_runtime_transport transport;
   struct agx_kmt_luid process_key;
   uint32_t params_size;
};

bool agx_d3dkmt_runtime_ops_complete(
   const struct agx_d3dkmt_runtime_ops *ops);
