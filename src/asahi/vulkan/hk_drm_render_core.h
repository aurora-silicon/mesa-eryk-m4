/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "drm-uapi/asahi_drm.h"

#include <stdbool.h>
#include <stdint.h>

struct hk_drm_render_resource_specs {
   uint64_t bg;
   uint64_t eot;
   uint64_t partial_bg;
   uint64_t partial_eot;
};

void hk_drm_render_resource_specs_pack(
   const struct hk_drm_render_resource_specs *specs,
   struct drm_asahi_cmd_render *cmd);

unsigned hk_drm_submit_command_limit(bool batch_enabled,
                                     uint32_t kernel_limit);
