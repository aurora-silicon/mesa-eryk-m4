/* SPDX-License-Identifier: MIT */
#include "hk_cmd_buffer.h"

void
hk_cmd_buffer_retain_bo(struct hk_cmd_buffer *cmd, struct agx_bo *bo)
{
   if (!bo)
      return;

   util_dynarray_foreach(&cmd->large_bos, struct agx_bo *, retained) {
      if (*retained == bo)
         return;
   }

   agx_bo_reference(bo);
   util_dynarray_append(&cmd->large_bos, bo);
}

void
hk_cmd_buffer_release_bos(struct hk_cmd_buffer *cmd, struct agx_device *dev)
{
   util_dynarray_foreach(&cmd->large_bos, struct agx_bo *, bo) {
      agx_bo_unreference(dev, *bo);
   }

   util_dynarray_clear(&cmd->large_bos);
}
