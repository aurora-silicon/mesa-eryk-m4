/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_slot_core.h"

#include <string.h>

bool
agx3_slot_assign(const struct agx3_slot_instr *instrs, unsigned n,
                 unsigned nslots, struct agx3_slot_result *out)
{
   if (instrs == NULL || out == NULL || nslots < 1 || nslots > 8)
      return false;

   /* reg_slot[r] = slot (1..nslots) of the async op whose result currently
    * lives in half-register r, or 0 if r was last written by a synchronous op
    * (or never). */
   uint8_t reg_slot[AGX3_SLOT_NUM_TOKENS];
   memset(reg_slot, 0, sizeof(reg_slot));

   /* slot_waited[s] = the slot has been synchronised since it was last
    * allocated, so later consumers need not wait it again. */
   bool slot_waited[9] = {false};

   unsigned next = 0; /* round-robin cursor into 0..nslots-1 */

   for (unsigned i = 0; i < n; ++i) {
      const struct agx3_slot_instr *I = &instrs[i];
      if (I->nr_srcs > AGX3_SLOT_MAX_REGS || I->nr_dsts > AGX3_SLOT_MAX_REGS)
         return false;

      uint16_t wait_mask = 0;
      for (unsigned s = 0; s < I->nr_srcs; ++s) {
         const uint16_t r = I->src[s];
         if (r >= AGX3_SLOT_NUM_TOKENS)
            return false;
         const uint8_t slot = reg_slot[r];
         if (slot != 0 && !slot_waited[slot]) {
            wait_mask |= (uint16_t)(1u << (slot - 1));
            slot_waited[slot] = true;
         }
      }

      out[i].wait_mask = wait_mask;

      if (I->is_async) {
         const uint8_t slot = (uint8_t)(next + 1);
         next = (next + 1) % nslots;
         out[i].slot = slot;
         slot_waited[slot] = false; /* freshly outstanding */
         for (unsigned d = 0; d < I->nr_dsts; ++d) {
            if (I->dst[d] >= AGX3_SLOT_NUM_TOKENS)
               return false;
            reg_slot[I->dst[d]] = slot;
         }
      } else {
         out[i].slot = 0;
         for (unsigned d = 0; d < I->nr_dsts; ++d) {
            if (I->dst[d] >= AGX3_SLOT_NUM_TOKENS)
               return false;
            reg_slot[I->dst[d]] = 0; /* synchronous result, no wait needed */
         }
      }
   }

   return true;
}
