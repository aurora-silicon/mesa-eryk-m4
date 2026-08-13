/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct d12_umd_lifetime_core {
   uint32_t retain_count;
   bool destroy_requested;
   bool finalizing;
};

enum d12_umd_lifetime_result {
   D12_UMD_LIFETIME_OK,
   D12_UMD_LIFETIME_FINALIZE,
   D12_UMD_LIFETIME_INVALID,
   D12_UMD_LIFETIME_OVERFLOW,
};

enum d12_umd_lifetime_result
d12_umd_lifetime_retain(struct d12_umd_lifetime_core *lifetime);

enum d12_umd_lifetime_result
d12_umd_lifetime_release(struct d12_umd_lifetime_core *lifetime);

enum d12_umd_lifetime_result
d12_umd_lifetime_request_destroy(struct d12_umd_lifetime_core *lifetime);
