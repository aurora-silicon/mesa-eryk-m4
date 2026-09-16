/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "agx_pack.h"
#include "hk_private.h"
#include "hk_native_descriptor_pack.h"

#include "vk_buffer_view.h"

struct hk_physical_device;

VkFormatFeatureFlags2
hk_get_buffer_format_features(struct hk_physical_device *pdevice,
                              VkFormat format);

struct hk_texel_buffer_residency {
   uint64_t map;
   uint32_t page_offset_B;
   uint32_t texel_size_B;
   uint32_t elements;
   uint32_t reserved;
};
static_assert(sizeof(struct hk_texel_buffer_residency) == 24,
              "texel buffer residency fills the Gen9 descriptor tail");

struct hk_buffer_view {
   struct vk_buffer_view vk;
   struct hk_texel_buffer_residency residency;
   struct hk_native_texture_descriptor tex;
   struct hk_native_pbe_descriptor pbe;
   /* Gen9 descriptor-set copy in the software PBE ABI (type, channels,
    * sRGB bit 125) consumed by the unformatted store lowering; the heap
    * entry above keeps native bytes with a linear colour space. */
   struct hk_native_pbe_descriptor pbe_software;
   uint32_t sampled_heap_index;
   uint32_t storage_heap_index;
   bool storage_pbe_valid;
   bool storage_software_valid;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(hk_buffer_view, vk.base, VkBufferView,
                               VK_OBJECT_TYPE_BUFFER_VIEW)
