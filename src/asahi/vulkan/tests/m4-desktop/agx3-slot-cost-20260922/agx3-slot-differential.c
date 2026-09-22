/* SPDX-License-Identifier: MIT
 * Compare slot-assignment implementations on deterministic dependency streams.
 * This is a host-only algorithm check, not GPU execution or a timing model.
 */
#include "agx3_slot_core.h"
#include <stdio.h>
#include <stdlib.h>

bool agx3_slot_assign_before(const struct agx3_slot_instr *, unsigned,
                            unsigned, struct agx3_slot_result *);

static uint32_t state = 0x4d340123;

static uint32_t
random_word(void)
{
   state ^= state << 13;
   state ^= state >> 17;
   state ^= state << 5;
   return state;
}

static uint16_t
token(unsigned iteration)
{
   const uint16_t edges[] = {
      0, 1, 255, AGX3_SLOT_TPS_TOKEN_BASE,
      AGX3_SLOT_TPS_TOKEN_BASE + 19, AGX3_SLOT_STAGING_TOKEN_BASE - 1,
      AGX3_SLOT_STAGING_TOKEN_BASE, AGX3_SLOT_IMAGE_WRITE_TOKEN - 1,
      AGX3_SLOT_IMAGE_WRITE_TOKEN, AGX3_SLOT_DYNAMIC_TPS_TOKEN,
   };
   uint32_t value = random_word();
   /* Alternate dense low registers, all token domains, and sparse TPS. */
   if (iteration % 3 == 0)
      return value % 32;
   if (iteration % 3 == 1)
      return edges[value % (sizeof(edges) / sizeof(edges[0]))];
   return value % AGX3_SLOT_NUM_TOKENS;
}

int
main(void)
{
   unsigned checked = 0;
   for (unsigned slots = 1; slots <= 8; ++slots) {
      for (unsigned iteration = 0; iteration < 256; ++iteration) {
         struct agx3_slot_instr instructions[128] = {0};
         struct agx3_slot_result before[128] = {0}, after[128] = {0};
         for (unsigned i = 0; i < 128; ++i) {
            struct agx3_slot_instr *ins = &instructions[i];
            ins->is_async = random_word() & 1;
            ins->drain = random_word() % 13 == 0;
            if (ins->is_async && random_word() % 4 == 0)
               ins->preferred_slot = 1 + random_word() % slots;
            ins->nr_srcs = random_word() % 5;
            ins->nr_dsts = random_word() % 5;
            for (unsigned j = 0; j < ins->nr_srcs; ++j)
               ins->src[j] = token(iteration);
            for (unsigned j = 0; j < ins->nr_dsts; ++j)
               ins->dst[j] = token(iteration);
         }
         if (!agx3_slot_assign_before(instructions, 128, slots, before) ||
             !agx3_slot_assign(instructions, 128, slots, after)) {
            fprintf(stderr, "Rejected valid stream: slots=%u iteration=%u\n",
                    slots, iteration);
            return 1;
         }
         for (unsigned i = 0; i < 128; ++i) {
            if (before[i].slot != after[i].slot ||
                before[i].wait_mask != after[i].wait_mask) {
               fprintf(stderr, "Mismatch: slots=%u iteration=%u instruction=%u\n",
                       slots, iteration, i);
               return 1;
            }
            ++checked;
         }
      }
   }
   printf("PASS: %u identical slot/wait assignments across 2048 streams\n",
          checked);
   return 0;
}
