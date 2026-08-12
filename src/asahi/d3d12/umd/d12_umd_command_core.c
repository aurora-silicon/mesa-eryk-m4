/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_command_core.h"

bool
d12_umd_command_type_from_ddi(
   uint32_t list_type, uint32_t queue_flags,
   enum d12_umd_renderer_command_type *renderer_type)
{
   if (!renderer_type || !queue_flags ||
       (queue_flags & ~D12_UMD_DDI_QUEUE_SUPPORTED_MASK))
      return false;

   if (list_type == D12_UMD_DDI_COMMAND_LIST_BUNDLE) {
      if (queue_flags != D12_UMD_DDI_QUEUE_3D)
         return false;
      *renderer_type = D12_UMD_RENDERER_COMMAND_BUNDLE;
      return true;
   }

   if (list_type != D12_UMD_DDI_COMMAND_LIST_DIRECT)
      return false;

   /* QueueFlags describes engine capabilities. A 3D-capable queue is the
    * public DIRECT class even when it also exposes compute/copy; otherwise
    * choose the strongest remaining class.
    */
   if (queue_flags & D12_UMD_DDI_QUEUE_3D)
      *renderer_type = D12_UMD_RENDERER_COMMAND_DIRECT;
   else if (queue_flags & D12_UMD_DDI_QUEUE_COMPUTE)
      *renderer_type = D12_UMD_RENDERER_COMMAND_COMPUTE;
   else
      *renderer_type = D12_UMD_RENDERER_COMMAND_COPY;
   return true;
}

bool
d12_umd_command_list_flags_valid(uint32_t flags, bool release1)
{
   uint32_t supported = D12_UMD_DDI_COMMAND_LIST_ENABLE_MARKERS;

   if (release1)
      supported |= D12_UMD_DDI_COMMAND_LIST_ENABLE_FULL_PIPELINE_MARKERS;

   return !(flags & ~supported);
}
