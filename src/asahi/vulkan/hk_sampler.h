/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "hk_device.h"
#include "hk_physical_device.h"
#include "hk_private.h"

#include "vk_sampler.h"
#include "vk_ycbcr_conversion.h"

#include "vk_format.h"

#include "hk_sampler_flags.h"

struct hk_sampler {
   struct vk_sampler vk;
   VkClearColorValue custom_border;
   bool has_border;
   /* The recorded border is VkBorderColor integer data. A float-typed
    * sample of a scaled view converts it to the texel value. */
   bool border_is_int;

   /* Shader-visible immutable sampling state for explicit reconstruction. */
   struct vk_ycbcr_conversion_state ycbcr;
   VkFilter min_filter, mag_filter;
   float mip_lod_bias;
   /* Software filtering state: fp16 min/max in word 0, fp16 bias plus
    * filter flags and reduction mode in word 1. Reduction uses nearest
    * planes[]; ordinary samplers with view minima have centre companions. */
   struct agx_sampler_packed reduction_state;
   bool shader_reduction;
   bool shader_view_min;
   struct hk_rc_sampler *centre[2];

   uint8_t plane_count;
   uint16_t lod_bias_fp16;

   struct {
      struct hk_rc_sampler *hw;
   } planes[2];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(hk_sampler, vk.base, VkSampler,
                               VK_OBJECT_TYPE_SAMPLER)
