/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_drm_render_core.h"

static uint32_t
low32(uint64_t value)
{
   return (uint32_t)value;
}

static uint32_t
high32(uint64_t value)
{
   return (uint32_t)(value >> 32);
}

void
hk_drm_render_resource_specs_pack(
   const struct hk_drm_render_resource_specs *specs,
   struct drm_asahi_cmd_render *cmd)
{
   cmd->bg.rsrc_spec = low32(specs->bg);
   cmd->eot.rsrc_spec = low32(specs->eot);
   cmd->partial_bg.rsrc_spec = low32(specs->partial_bg);
   cmd->partial_eot.rsrc_spec = low32(specs->partial_eot);

   cmd->bg_eot_rsrc_spec_hi = high32(specs->eot);
   cmd->bg_eot_partial_rsrc_spec_hi = high32(specs->partial_eot);
   cmd->bg_rsrc_spec_hi = high32(specs->bg);
   cmd->bg_partial_rsrc_spec_hi = high32(specs->partial_bg);

   const uint32_t high = cmd->bg_eot_rsrc_spec_hi |
                         cmd->bg_eot_partial_rsrc_spec_hi |
                         cmd->bg_rsrc_spec_hi | cmd->bg_partial_rsrc_spec_hi;
   if (high)
      cmd->flags |= DRM_ASAHI_RENDER_RSRC_SPEC_HI;
   else
      cmd->flags &= ~DRM_ASAHI_RENDER_RSRC_SPEC_HI;
}

unsigned
hk_drm_submit_command_limit(bool batch_enabled, uint32_t kernel_limit)
{
   const unsigned userspace_limit = batch_enabled ? 64 : 1;

   if (kernel_limit == 0)
      return 1;

   return userspace_limit < kernel_limit ? userspace_limit : kernel_limit;
}
