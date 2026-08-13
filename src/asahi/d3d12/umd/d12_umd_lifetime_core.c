/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_lifetime_core.h"

#include <limits.h>

enum d12_umd_lifetime_result
d12_umd_lifetime_retain(struct d12_umd_lifetime_core *lifetime)
{
   if (!lifetime || lifetime->destroy_requested || lifetime->finalizing)
      return D12_UMD_LIFETIME_INVALID;
   if (lifetime->retain_count == UINT32_MAX)
      return D12_UMD_LIFETIME_OVERFLOW;
   ++lifetime->retain_count;
   return D12_UMD_LIFETIME_OK;
}

enum d12_umd_lifetime_result
d12_umd_lifetime_release(struct d12_umd_lifetime_core *lifetime)
{
   if (!lifetime || !lifetime->retain_count || lifetime->finalizing)
      return D12_UMD_LIFETIME_INVALID;
   --lifetime->retain_count;
   if (lifetime->destroy_requested && !lifetime->retain_count) {
      lifetime->finalizing = true;
      return D12_UMD_LIFETIME_FINALIZE;
   }
   return D12_UMD_LIFETIME_OK;
}

enum d12_umd_lifetime_result
d12_umd_lifetime_request_destroy(struct d12_umd_lifetime_core *lifetime)
{
   if (!lifetime || lifetime->destroy_requested || lifetime->finalizing)
      return D12_UMD_LIFETIME_INVALID;
   lifetime->destroy_requested = true;
   if (!lifetime->retain_count) {
      lifetime->finalizing = true;
      return D12_UMD_LIFETIME_FINALIZE;
   }
   return D12_UMD_LIFETIME_OK;
}
