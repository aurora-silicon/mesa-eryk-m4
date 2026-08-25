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
      CHECK(r[2].slot==0 && r[2].wait_mask==0b11);  /* wait slots 1 and 2 */
      CHECK(r[3].slot==0 && r[3].wait_mask==0);      /* add result is sync */
   }

   /* Waited once = satisfied globally: two consumers of the same load; only the
    * first waits. */
   {
      struct agx3_slot_instr p[] = { ld(0), alu(2,0,0), alu(4,0,0) };
      CHECK(agx3_slot_assign(p, 3, 6, r));
      CHECK(r[0].slot==1);
      CHECK(r[1].wait_mask==0b1);  /* first consumer waits slot 1 */
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
      CHECK(r[7].wait_mask==0b1);
   }

   /* agx3c has 8 slots. */
   {
      struct agx3_slot_instr p[9];
      for (int i=0;i<8;i++) p[i]=ld((uint16_t)(i*2));
      p[8]=ld(100);
      CHECK(agx3_slot_assign(p, 9, 8, r));
      CHECK(r[7].slot==8 && r[8].slot==1);  /* wraps at 8 */
   }

   /* Malformed inputs. */
   CHECK(!agx3_slot_assign(NULL, 1, 6, r));
   { struct agx3_slot_instr p=ld(0); CHECK(!agx3_slot_assign(&p,1,0,r)); CHECK(!agx3_slot_assign(&p,1,9,r)); }

   if (failures){ fprintf(stderr,"agx3_slot_core: %u/%u failed\n",failures,checks); return 1; }
   printf("agx3_slot_core: all %u checks passed\n", checks);
   return 0;
}
