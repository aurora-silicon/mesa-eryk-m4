/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Correctness of the AGX3 scoreboard slot-assignment algorithm.
 */
#include "agx3_slot_core.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do{ checks++; if(!(x)){ fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x); failures++; } }while(0)

static struct agx3_slot_instr
ld(uint16_t d){ struct agx3_slot_instr I={0}; I.is_async=true; I.nr_dsts=1; I.dst[0]=d; return I; }
static struct agx3_slot_instr
alu(uint16_t d, uint16_t a, uint16_t b){ struct agx3_slot_instr I={0}; I.nr_dsts=1; I.dst[0]=d; I.nr_srcs=2; I.src[0]=a; I.src[1]=b; return I; }
static struct agx3_slot_instr
st(uint16_t a){ struct agx3_slot_instr I={0}; I.nr_srcs=1; I.src[0]=a; return I; }

int main(void)
{
   struct agx3_slot_result r[64];

   /* Two loads then an add consuming both: each load gets a distinct slot, the
    * add waits both. */
   {
      struct agx3_slot_instr p[] = { ld(0), ld(2), alu(4,0,2), st(4) };
      CHECK(agx3_slot_assign(p, 4, 6, r));
      CHECK(r[0].slot==1 && r[0].wait_mask==0);
      CHECK(r[1].slot==2 && r[1].wait_mask==0);
      CHECK(r[2].slot==0 && r[2].wait_mask==0x3u);  /* wait slots 1 and 2 */
      CHECK(r[3].slot==0 && r[3].wait_mask==0);      /* add result is sync */
   }

   /* Waited once = satisfied globally: two consumers of the same load; only the
    * first waits. */
   {
      struct agx3_slot_instr p[] = { ld(0), alu(2,0,0), alu(4,0,0) };
      CHECK(agx3_slot_assign(p, 3, 6, r));
      CHECK(r[0].slot==1);
      CHECK(r[1].wait_mask==0x1u);  /* first consumer waits slot 1 */
      CHECK(r[2].wait_mask==0);    /* already satisfied */
   }

   /* Pure ALU: no slots, no waits. */
   {
      struct agx3_slot_instr p[] = { alu(0,2,4), alu(6,0,2) };
      CHECK(agx3_slot_assign(p, 2, 6, r));
      CHECK(r[0].slot==0 && r[0].wait_mask==0);
      CHECK(r[1].slot==0 && r[1].wait_mask==0);
   }

   /* A load result overwritten by an ALU op before use: the later read of that
    * register must NOT wait (the value is synchronous now). */
   {
      struct agx3_slot_instr p[] = { ld(0), alu(0,2,4), alu(6,0,0) };
      CHECK(agx3_slot_assign(p, 3, 6, r));
      CHECK(r[0].slot==1);
      CHECK(r[1].wait_mask==0);   /* alu writes r0, does not read the load */
      CHECK(r[2].wait_mask==0);   /* r0 is now the alu (sync) result */
   }

   /* Round-robin over nslots: the 7th async op on a 6-slot part reuses slot 1;
    * a consumer of the 1st load (still in slot 1) forces its wait first. */
   {
      struct agx3_slot_instr p[8];
      for (int i=0;i<7;i++) p[i]=ld((uint16_t)(i*2));
      p[7]=st(0);  /* consume the first load, slot 1 */
      CHECK(agx3_slot_assign(p, 8, 6, r));
      CHECK(r[0].slot==1 && r[1].slot==2 && r[5].slot==6 && r[6].slot==1);
      CHECK(r[7].wait_mask==0x1u);
   }

   /* agx3c has 8 slots. */
   {
      struct agx3_slot_instr p[9];
      for (int i=0;i<8;i++) p[i]=ld((uint16_t)(i*2));
      p[8]=ld(100);
      CHECK(agx3_slot_assign(p, 9, 8, r));
      CHECK(r[7].slot==8 && r[8].slot==1);  /* wraps at 8 */
   }

   /* Static copy produces a TPS dependency token and a later rld consumes it.
    * The reload must wait the copy even though the two operations share no
    * architectural GPR. Its own GPR result then remains async as usual. */
   {
      struct agx3_slot_instr copy = {0}, rld = {0}, consume;
      copy.is_async = true;
      copy.nr_srcs = 2;
      copy.src[0] = 8;
      copy.src[1] = 9;
      copy.src[copy.nr_srcs++] = AGX3_SLOT_TPS_TOKEN_BASE + 18;
      copy.src[copy.nr_srcs++] = AGX3_SLOT_TPS_TOKEN_BASE + 19;
      copy.nr_dsts = 2;
      copy.dst[0] = AGX3_SLOT_TPS_TOKEN_BASE + 18;
      copy.dst[1] = AGX3_SLOT_TPS_TOKEN_BASE + 19;
      rld.is_async = true;
      rld.nr_srcs = 2;
      rld.src[0] = AGX3_SLOT_TPS_TOKEN_BASE + 18;
      rld.src[1] = AGX3_SLOT_TPS_TOKEN_BASE + 19;
      rld.nr_dsts = 2;
      rld.dst[0] = 12;
      rld.dst[1] = 13;
      rld.dst[rld.nr_dsts++] = AGX3_SLOT_TPS_TOKEN_BASE + 18;
      rld.dst[rld.nr_dsts++] = AGX3_SLOT_TPS_TOKEN_BASE + 19;
      consume = alu(16, 12, 12);
      struct agx3_slot_instr p[] = {copy, rld, consume};
      CHECK(agx3_slot_assign(p, 3, 6, r));
      CHECK(r[0].slot == 1 && r[0].wait_mask == 0);
      CHECK(r[1].slot == 2 && r[1].wait_mask == 1);
      CHECK(r[2].slot == 0 && r[2].wait_mask == 2);
   }

   /* A TPS access token is both read and rewritten, serializing WAW as well as
    * RAW hazards. The second copy waits the first before replacing its slot. */
   {
      struct agx3_slot_instr copy1 = {0}, copy2 = {0}, rld = {0};
      const uint16_t tps = AGX3_SLOT_TPS_TOKEN_BASE + 24;
      copy1.is_async = copy2.is_async = rld.is_async = true;
      copy1.nr_srcs = copy1.nr_dsts = 1;
      copy1.src[0] = copy1.dst[0] = tps;
      copy2 = copy1;
      rld.nr_srcs = rld.nr_dsts = 1;
      rld.src[0] = rld.dst[0] = tps;
      struct agx3_slot_instr p[] = {copy1, copy2, rld};
      CHECK(agx3_slot_assign(p, 3, 6, r));
      CHECK(r[0].slot == 1 && r[0].wait_mask == 0);
      CHECK(r[1].slot == 2 && r[1].wait_mask == 1);
      CHECK(r[2].slot == 3 && r[2].wait_mask == 2);
   }

   /* The top legal Mesa byte-immediate TPS token is modeled distinctly. */
   {
      struct agx3_slot_instr p[] = {
         ld(AGX3_SLOT_TPS_TOKEN_BASE + AGX3_SLOT_TPS_HALF_TOKENS - 1),
         st(AGX3_SLOT_TPS_TOKEN_BASE + AGX3_SLOT_TPS_HALF_TOKENS - 1),
      };
      CHECK(agx3_slot_assign(p, 2, 6, r));
      CHECK(r[1].wait_mask == 1);
      p[1].src[0] = AGX3_SLOT_NUM_TOKENS;
      CHECK(!agx3_slot_assign(p, 2, 6, r));
   }

   /* Malformed inputs. */
   CHECK(!agx3_slot_assign(NULL, 1, 6, r));
   { struct agx3_slot_instr p=ld(0); CHECK(!agx3_slot_assign(&p,1,0,r)); CHECK(!agx3_slot_assign(&p,1,9,r)); }

   if (failures){ fprintf(stderr,"agx3_slot_core: %u/%u failed\n",failures,checks); return 1; }
   printf("agx3_slot_core: all %u checks passed\n", checks);
   return 0;
}
