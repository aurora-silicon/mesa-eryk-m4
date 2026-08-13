/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Wire values shared by the CPU recorder and the precompiled AGX
 * presentation-blit kernel. Keep this header valid in both C and OpenCL C.
 */
#pragma once

#define LIBAGX_PRESENT_FORMAT_INVALID 0u
#define LIBAGX_PRESENT_FORMAT_RGBA8 1u
#define LIBAGX_PRESENT_FORMAT_RGBA8_SRGB 2u
#define LIBAGX_PRESENT_FORMAT_BGRA8 3u
#define LIBAGX_PRESENT_FORMAT_BGRX8 4u
#define LIBAGX_PRESENT_FORMAT_RGB10A2 5u
#define LIBAGX_PRESENT_FORMAT_RGBA16_FLOAT 6u
#define LIBAGX_PRESENT_FORMAT_BGR565 7u
#define LIBAGX_PRESENT_FORMAT_BGRA5551 8u
#define LIBAGX_PRESENT_FORMAT_BGRA8_SRGB 9u

#define LIBAGX_PRESENT_FORMAT_IS_SRGB(format)                              \
   ((format) == LIBAGX_PRESENT_FORMAT_RGBA8_SRGB ||                        \
    (format) == LIBAGX_PRESENT_FORMAT_BGRA8_SRGB)

#define LIBAGX_PRESENT_ROTATION_UNSPECIFIED 0u
#define LIBAGX_PRESENT_ROTATION_IDENTITY 1u
#define LIBAGX_PRESENT_ROTATION_CCW_90 2u
#define LIBAGX_PRESENT_ROTATION_CCW_180 3u
#define LIBAGX_PRESENT_ROTATION_CCW_270 4u
