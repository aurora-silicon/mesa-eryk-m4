/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Bounded G17P/HAL200 fixed-layout PPP graphics-state records.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_PPP_HAL200_FS_LENGTH 20u
#define AGX_PPP_HAL200_OUTPUT_LENGTH 28u
#define AGX_PPP_HAL200_PRIMARY_FACE_LENGTH 28u
#define AGX_PPP_HAL200_SECONDARY_FACE_LENGTH 20u
#define AGX_PPP_HAL200_CULL_LENGTH 8u
#define AGX_PPP_HAL200_GRAPHICS_MAX_LENGTH 28u

#define AGX_PPP_HAL200_FS_HEADER UINT32_C(0x00800000)
#define AGX_PPP_HAL200_OUTPUT_HEADER UINT32_C(0x0c0a0000)
#define AGX_PPP_HAL200_PRIMARY_FACE_HEADER UINT32_C(0x010000b4)
#define AGX_PPP_HAL200_SECONDARY_FACE_HEADER UINT32_C(0x02000048)
#define AGX_PPP_HAL200_CULL_HEADER UINT32_C(0x00200000)

/* The fourth word is still anonymous, but the compact direct-draw profile
 * carries this value in every hardware-positive G17P fragment record. */
#define AGX_PPP_HAL200_FS_DIRECT_CONTROL UINT32_C(0x01000000)

enum agx_ppp_hal200_graphics_bundle {
   AGX_PPP_HAL200_GRAPHICS_FS,
   AGX_PPP_HAL200_GRAPHICS_OUTPUT,
   AGX_PPP_HAL200_GRAPHICS_PRIMARY_FACE,
   AGX_PPP_HAL200_GRAPHICS_SECONDARY_FACE,
   AGX_PPP_HAL200_GRAPHICS_CULL,
};

struct agx_ppp_hal200_graphics_fields {
   uint32_t words[6];
};

struct agx_ppp_hal200_graphics_core {
   uint8_t bytes[AGX_PPP_HAL200_GRAPHICS_MAX_LENGTH];
   uint8_t length;
};

struct agx_ppp_hal200_fragment_fields {
   uint32_t resource_counts;
   uint32_t pipeline;
   uint32_t cf_bindings;
   uint32_t control;
};

/*
 * Pack one complete fixed-layout G17P record.  Each record has its own header
 * and outer VDM PPP_STATE pointer; records must never be concatenated behind a
 * single legacy selector header.
 *
 * Payload order and word count by bundle:
 *
 *   FS:             FS0, FS1, FS2, FS3
 *   OUTPUT:         output-select, varying-counts-16, output-unknown,
 *                   output-size, primary-face header, fragment-control
 *                   (the traced seven-word window deliberately overlaps the
 *                   first two words of the following primary-face record)
 *   PRIMARY_FACE:   fragment-control, front-face, front-stencil, back-face,
 *                   back-stencil, occlusion-query
 *   SECONDARY_FACE: fragment-control-2, front-face-2, back-face-2,
 *                   occlusion-query-2
 *   CULL:           cull
 */
bool agx_ppp_hal200_graphics_core_supported(enum agx_chip chip);

bool agx_ppp_hal200_graphics_core_pack(
   enum agx_chip chip, enum agx_ppp_hal200_graphics_bundle bundle,
   const struct agx_ppp_hal200_graphics_fields *fields,
   struct agx_ppp_hal200_graphics_core *out);

bool agx_ppp_hal200_graphics_core_unpack(
   enum agx_chip chip, enum agx_ppp_hal200_graphics_bundle bundle,
   const void *bytes, size_t size,
   struct agx_ppp_hal200_graphics_fields *fields);

/* Pack the VDM-referenced G17P fragment record by semantic field. Pipeline is
 * relative to USC_EXEC_BASE_ISP, just like the legacy Fragment Shader Word 1;
 * CF bindings use the same shader-window-relative encoding. */
bool agx_ppp_hal200_fragment_core_pack(
   enum agx_chip chip, const struct agx_ppp_hal200_fragment_fields *fields,
   struct agx_ppp_hal200_graphics_core *out);

#ifdef __cplusplus
}
#endif
