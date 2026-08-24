/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Frontend-neutral format capability policy. Values are the stable
 * DXGI_FORMAT numeric ABI so portable tests do not need Windows headers.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Direct3D 12 feature level 12_0 requires output-merger logic operations
 * for exactly these unsigned-integer formats. Keep this list literal and
 * reviewable against Microsoft's Direct3D 12.0 format-support table:
 * https://learn.microsoft.com/windows/win32/direct3ddxgi/
 * hardware-support-for-direct3d-12-0-formats
 */
#define D12_FORMAT_CORE_LOGIC_OP_LIST(X)                                   \
   X(R32G32B32A32_UINT, 3)                                                 \
   X(R32G32B32_UINT, 7)                                                    \
   X(R16G16B16A16_UINT, 12)                                                \
   X(R32G32_UINT, 17)                                                      \
   X(R10G10B10A2_UINT, 25)                                                 \
   X(R8G8B8A8_UINT, 30)                                                    \
   X(R16G16_UINT, 36)                                                      \
   X(R32_UINT, 42)                                                         \
   X(R8G8_UINT, 50)                                                        \
   X(R16_UINT, 57)                                                         \
   X(R8_UINT, 62)

/*
 * Direct3D 12 requires typed UAV loads for R32_FLOAT/UINT/SINT on every
 * device.  Setting TypedUAVLoadAdditionalFormats promises these fifteen as
 * well, so the promise is only honest if every one of them really reports a
 * typed UAV load.  Keep the list literal and reviewable against Microsoft's
 * typed unordered-access-view loads table:
 * https://learn.microsoft.com/windows/win32/direct3d12/
 * typed-unordered-access-view-loads
 */
#define D12_FORMAT_CORE_TYPED_UAV_LOAD_BASELINE_LIST(X)                    \
   X(R32_FLOAT, 41)                                                        \
   X(R32_UINT, 42)                                                         \
   X(R32_SINT, 43)

#define D12_FORMAT_CORE_TYPED_UAV_LOAD_ADDITIONAL_LIST(X)                  \
   X(R32G32B32A32_FLOAT, 2)                                                \
   X(R32G32B32A32_UINT, 3)                                                 \
   X(R32G32B32A32_SINT, 4)                                                 \
   X(R16G16B16A16_FLOAT, 10)                                               \
   X(R16G16B16A16_UINT, 12)                                                \
   X(R16G16B16A16_SINT, 14)                                                \
   X(R8G8B8A8_UNORM, 28)                                                   \
   X(R8G8B8A8_UINT, 30)                                                    \
   X(R8G8B8A8_SINT, 32)                                                    \
   X(R16_FLOAT, 54)                                                        \
   X(R16_UINT, 57)                                                         \
   X(R16_SINT, 59)                                                         \
   X(R8_UNORM, 61)                                                         \
   X(R8_UINT, 62)                                                          \
   X(R8_SINT, 64)

#define D12_FORMAT_CORE_TYPED_UAV_LOAD_BASELINE_COUNT 3u
#define D12_FORMAT_CORE_TYPED_UAV_LOAD_ADDITIONAL_COUNT 15u

enum d12_format_core_format {
#define D12_FORMAT_CORE_ENUM(name, value)                                  \
   D12_FORMAT_CORE_##name = (value),
   /* The typed-UAV lists deliberately do not contribute enumerators: they
    * overlap the logic-op list on the unsigned-integer formats, and the
    * numeric DXGI values are the ABI either way. */
   D12_FORMAT_CORE_LOGIC_OP_LIST(D12_FORMAT_CORE_ENUM)
#undef D12_FORMAT_CORE_ENUM
};

#define D12_FORMAT_CORE_LOGIC_OP_COUNT 11u

static inline bool
d12_format_core_requires_typed_uav_load(uint32_t format, bool additional)
{
   switch (format) {
#define D12_FORMAT_CORE_CASE(name, value) case (value):
      D12_FORMAT_CORE_TYPED_UAV_LOAD_BASELINE_LIST(D12_FORMAT_CORE_CASE)
#undef D12_FORMAT_CORE_CASE
      return true;
   default:
      break;
   }

   if (!additional)
      return false;

   switch (format) {
#define D12_FORMAT_CORE_CASE(name, value) case (value):
      D12_FORMAT_CORE_TYPED_UAV_LOAD_ADDITIONAL_LIST(D12_FORMAT_CORE_CASE)
#undef D12_FORMAT_CORE_CASE
      return true;
   default:
      return false;
   }
}

static inline bool
d12_format_core_supports_output_merger_logic_op(uint32_t format)
{
   switch (format) {
#define D12_FORMAT_CORE_CASE(name, value)                                  \
   case D12_FORMAT_CORE_##name:
      D12_FORMAT_CORE_LOGIC_OP_LIST(D12_FORMAT_CORE_CASE)
#undef D12_FORMAT_CORE_CASE
      return true;
   default:
      return false;
   }
}
