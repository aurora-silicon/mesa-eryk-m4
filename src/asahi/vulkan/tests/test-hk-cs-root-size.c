/* SPDX-License-Identifier: MIT */
#include "../hk_cmd_pool.h"
#include "../hk_cs_root_size.h"
#include "util/macros.h"

#include <assert.h>
#include <stdio.h>

int
main(void)
{
   assert(hk_cs_root_size(AGX_CHIP_G17P, false) == 12288);
   assert(hk_cs_root_size(AGX_CHIP_G17P, true) == 65536);
   assert(hk_cs_root_size(AGX_CHIP_G14G, false) == 65536);
   assert(hk_cs_root_size(AGX_CHIP_G14G, true) == 65536);

   const size_t vdm_link_length = 8;
   assert(!hk_cs_space_fits(4096 - HK_G17P_VDM_INITIAL_SIZE,
                            HK_GFX_DRAW_RESERVATION, vdm_link_length));
   assert(hk_cs_space_fits(HK_G17P_VDM_ROOT_SIZE -
                              HK_G17P_VDM_INITIAL_SIZE,
                           HK_GFX_DRAW_RESERVATION, vdm_link_length));

   /* The command uploader aligns each root to 1 KiB. G17P graphics roots are
    * therefore densely shareable instead of consuming half a 128 KiB BO.
    */
   size_t offset = 0;
   for (unsigned i = 0; i < 10; ++i) {
      offset = ALIGN_POT(offset, 1024);
      assert(HK_G17P_VDM_ROOT_SIZE <= HK_CMD_BO_SIZE - offset);
      offset += HK_G17P_VDM_ROOT_SIZE;
   }
   assert(offset == 120 * 1024);
   assert(HK_G17P_VDM_ROOT_SIZE > HK_CMD_BO_SIZE - offset);

   puts("G17P graphics root sizing and command-BO sharing PASS");
   return 0;
}
