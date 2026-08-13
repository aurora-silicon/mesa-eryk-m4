/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_umd_lifetime_core.h"

#include <assert.h>
#include <stdio.h>

int
main(void)
{
   struct d12_umd_lifetime_core lifetime = {0};

   assert(d12_umd_lifetime_retain(&lifetime) == D12_UMD_LIFETIME_OK);
   assert(d12_umd_lifetime_retain(&lifetime) == D12_UMD_LIFETIME_OK);
   assert(lifetime.retain_count == 2);
   assert(d12_umd_lifetime_request_destroy(&lifetime) ==
          D12_UMD_LIFETIME_OK);
   assert(d12_umd_lifetime_retain(&lifetime) ==
          D12_UMD_LIFETIME_INVALID);
   assert(d12_umd_lifetime_release(&lifetime) == D12_UMD_LIFETIME_OK);
   assert(d12_umd_lifetime_release(&lifetime) ==
          D12_UMD_LIFETIME_FINALIZE);
   assert(lifetime.finalizing);
   assert(d12_umd_lifetime_release(&lifetime) ==
          D12_UMD_LIFETIME_INVALID);

   lifetime = (struct d12_umd_lifetime_core){0};
   assert(d12_umd_lifetime_request_destroy(&lifetime) ==
          D12_UMD_LIFETIME_FINALIZE);
   assert(lifetime.destroy_requested && lifetime.finalizing);

   lifetime = (struct d12_umd_lifetime_core){
      .retain_count = UINT32_MAX,
   };
   assert(d12_umd_lifetime_retain(&lifetime) ==
          D12_UMD_LIFETIME_OVERFLOW);
   assert(lifetime.retain_count == UINT32_MAX);

   lifetime = (struct d12_umd_lifetime_core){0};
   assert(d12_umd_lifetime_retain(&lifetime) == D12_UMD_LIFETIME_OK);
   assert(d12_umd_lifetime_retain(&lifetime) == D12_UMD_LIFETIME_OK);
   assert(d12_umd_lifetime_request_destroy(&lifetime) ==
          D12_UMD_LIFETIME_OK);
   assert(d12_umd_lifetime_abandon_retains(&lifetime, 1) ==
          D12_UMD_LIFETIME_INVALID);
   assert(lifetime.retain_count == 2 && !lifetime.finalizing);
   assert(d12_umd_lifetime_abandon_retains(&lifetime, 2) ==
          D12_UMD_LIFETIME_FINALIZE);
   assert(!lifetime.retain_count && lifetime.finalizing);

   lifetime = (struct d12_umd_lifetime_core){.retain_count = 1};
   assert(d12_umd_lifetime_abandon_retains(&lifetime, 1) ==
          D12_UMD_LIFETIME_INVALID);
   assert(!lifetime.destroy_requested && !lifetime.finalizing);

   assert(d12_umd_lifetime_retain(NULL) == D12_UMD_LIFETIME_INVALID);
   assert(d12_umd_lifetime_release(NULL) == D12_UMD_LIFETIME_INVALID);
   assert(d12_umd_lifetime_request_destroy(NULL) ==
          D12_UMD_LIFETIME_INVALID);
   assert(d12_umd_lifetime_abandon_retains(NULL, 1) ==
          D12_UMD_LIFETIME_INVALID);

   puts("d12_umd_lifetime_core_tests: PASS");
   return 0;
}
