/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Frontend-neutral render-attachment subresource validation.  Both the COM
 * and WDDM DDI frontends ultimately bind the same AGX miptree, so mip and
 * layer range resolution belongs below either public ABI.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct d12_attachment_view_request {
   uint32_t width;
   uint32_t height;
   uint32_t levels;
   uint32_t layers;
   uint32_t mip;
   uint32_t first_layer;
   uint32_t layer_count;
   bool mipmapped_layers;
};

struct d12_attachment_view {
   uint32_t width;
   uint32_t height;
   uint32_t mip;
   uint32_t first_layer;
   uint32_t layer_count;
};

bool d12_attachment_view_resolve(
   const struct d12_attachment_view_request *request,
   struct d12_attachment_view *view);
