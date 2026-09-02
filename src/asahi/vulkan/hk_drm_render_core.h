/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "drm-uapi/asahi_drm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HK_DRM_MAX_FRAGMENT_ATTACHMENTS 20

struct hk_drm_attachment_set {
   struct drm_asahi_attachment list[HK_DRM_MAX_FRAGMENT_ATTACHMENTS];
   uint32_t count;
   bool complete;
};

struct hk_drm_command_span {
   size_t end_offset;
   size_t first_hardware_offset;
   unsigned hardware_count;
   unsigned render_count;
   unsigned compute_count;
};

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

void hk_drm_attachment_set_init(struct hk_drm_attachment_set *set);

bool hk_drm_attachment_set_add(struct hk_drm_attachment_set *set,
                               uint64_t pointer, uint64_t size);

bool hk_drm_next_command_span(const void *cmdbuf, size_t cmdbuf_size,
                              size_t start_offset, unsigned hardware_limit,
                              struct hk_drm_command_span *span);
