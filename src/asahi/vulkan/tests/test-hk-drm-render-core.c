/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_drm_render_core.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void
test_uapi_extension_layout(void)
{
   static_assert(offsetof(struct drm_asahi_cmd_render,
                          bg_eot_rsrc_spec_hi) == 240,
                 "append-only render command ABI");
   static_assert(offsetof(struct drm_asahi_cmd_render,
                          bg_eot_partial_rsrc_spec_hi) == 244,
                 "append-only render command ABI");
   static_assert(offsetof(struct drm_asahi_cmd_render, bg_rsrc_spec_hi) == 248,
                 "append-only render command ABI");
   static_assert(offsetof(struct drm_asahi_cmd_render,
                          bg_partial_rsrc_spec_hi) == 252,
                 "append-only render command ABI");
   static_assert(sizeof(struct drm_asahi_cmd_render) == 256,
                 "extended render command ABI");
}

static void
test_exact_g17p_specs(void)
{
   struct drm_asahi_cmd_render cmd;
   memset(&cmd, 0, sizeof(cmd));
   cmd.flags = DRM_ASAHI_RENDER_VERTEX_SCRATCH;
   cmd.bg.usc = UINT32_C(0x12345644);
   cmd.eot.usc = UINT32_C(0x23456784);

   const struct hk_drm_render_resource_specs specs = {
      .bg = UINT64_C(0x0007800000000040),
      .eot = UINT64_C(0x8),
      .partial_bg = UINT64_C(0x0007800000000040),
      .partial_eot = UINT64_C(0x8),
   };
   hk_drm_render_resource_specs_pack(&specs, &cmd);

   assert(cmd.bg.rsrc_spec == UINT32_C(0x40));
   assert(cmd.eot.rsrc_spec == UINT32_C(0x8));
   assert(cmd.partial_bg.rsrc_spec == UINT32_C(0x40));
   assert(cmd.partial_eot.rsrc_spec == UINT32_C(0x8));
   assert(cmd.bg_eot_rsrc_spec_hi == 0);
   assert(cmd.bg_eot_partial_rsrc_spec_hi == 0);
   assert(cmd.bg_rsrc_spec_hi == UINT32_C(0x00078000));
   assert(cmd.bg_partial_rsrc_spec_hi == UINT32_C(0x00078000));
   assert(cmd.flags == (DRM_ASAHI_RENDER_VERTEX_SCRATCH |
                        DRM_ASAHI_RENDER_RSRC_SPEC_HI));
   assert(cmd.bg.usc == UINT32_C(0x12345644));
   assert(cmd.eot.usc == UINT32_C(0x23456784));
}

static void
test_field_order_and_flag_clear(void)
{
   struct drm_asahi_cmd_render cmd;
   memset(&cmd, 0, sizeof(cmd));
   const struct hk_drm_render_resource_specs distinct = {
      .bg = UINT64_C(0x1111111122222222),
      .eot = UINT64_C(0x3333333344444444),
      .partial_bg = UINT64_C(0x5555555566666666),
      .partial_eot = UINT64_C(0x7777777788888888),
   };
   hk_drm_render_resource_specs_pack(&distinct, &cmd);
   assert(cmd.bg_eot_rsrc_spec_hi == UINT32_C(0x33333333));
   assert(cmd.bg_eot_partial_rsrc_spec_hi == UINT32_C(0x77777777));
   assert(cmd.bg_rsrc_spec_hi == UINT32_C(0x11111111));
   assert(cmd.bg_partial_rsrc_spec_hi == UINT32_C(0x55555555));
   assert(cmd.eot.rsrc_spec == UINT32_C(0x44444444));
   assert(cmd.partial_eot.rsrc_spec == UINT32_C(0x88888888));
   assert(cmd.bg.rsrc_spec == UINT32_C(0x22222222));
   assert(cmd.partial_bg.rsrc_spec == UINT32_C(0x66666666));
   assert(cmd.flags & DRM_ASAHI_RENDER_RSRC_SPEC_HI);

   const struct hk_drm_render_resource_specs low_only = {
      .bg = 1,
      .eot = 2,
      .partial_bg = 3,
      .partial_eot = 4,
   };
   hk_drm_render_resource_specs_pack(&low_only, &cmd);
   assert((cmd.flags & DRM_ASAHI_RENDER_RSRC_SPEC_HI) == 0);
   assert(cmd.bg_eot_rsrc_spec_hi == 0);
   assert(cmd.bg_eot_partial_rsrc_spec_hi == 0);
   assert(cmd.bg_rsrc_spec_hi == 0);
   assert(cmd.bg_partial_rsrc_spec_hi == 0);
}

static void
test_submit_command_limit(void)
{
   assert(hk_drm_submit_command_limit(false, 64) == 1);
   assert(hk_drm_submit_command_limit(true, 64) == 64);
   assert(hk_drm_submit_command_limit(true, 32) == 32);
   assert(hk_drm_submit_command_limit(true, 1) == 1);
   assert(hk_drm_submit_command_limit(false, 0) == 1);
}

int
main(void)
{
   test_uapi_extension_layout();
   test_exact_g17p_specs();
   test_field_order_and_flag_clear();
   test_submit_command_limit();
   puts("Honeykrisp DRM render resource specs: all checks passed");
   return 0;
}
