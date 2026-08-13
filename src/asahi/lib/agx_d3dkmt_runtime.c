/*
 * Copyright 2026 the Asahi Linux contributors
 * SPDX-License-Identifier: MIT
 */

#include "agx_d3dkmt_runtime.h"

bool
agx_d3dkmt_runtime_ops_complete(const struct agx_d3dkmt_runtime_ops *ops)
{
   return ops && ops->CreateAllocation2 && ops->QueryResourceInfo &&
          ops->QueryResourceInfoFromNtHandle && ops->ShareObjects &&
          ops->OpenResource && ops->OpenResourceFromNtHandle &&
          ops->DestroyAllocation2 && ops->Lock2 && ops->Unlock2 &&
          ops->InvalidateCache && ops->MakeResident && ops->Evict &&
          ops->CreatePagingQueue &&
          ops->DestroyPagingQueue && ops->ReserveGpuVirtualAddress &&
          ops->MapGpuVirtualAddress && ops->UpdateGpuVirtualAddress &&
          ops->FreeGpuVirtualAddress && ops->CreateContextVirtual &&
          ops->DestroyContext && ops->SubmitCommand &&
          ops->CreateSynchronizationObject2 &&
          ops->DestroySynchronizationObject &&
          ops->OpenSyncObjectFromNtHandle2 && ops->OpenNtHandleFromName &&
          ops->OpenSyncObjectNtHandleFromName &&
          ops->SignalSynchronizationObjectFromCpu &&
          ops->WaitForSynchronizationObjectFromCpu &&
          ops->SignalSynchronizationObjectFromGpu &&
          ops->WaitForSynchronizationObjectFromGpu &&
          ops->QueryClockCalibration && ops->Escape;
}
