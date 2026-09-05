/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Every AGX3 control-flow encoder must reproduce Apple's witnessed bytes, and
 * the branch fixup must reconstruct real displaced branches from a reserved
 * template. Replays composer-generated vectors covering the full condition,
 * slot, push/pop and displacement domains.
 */

#include "agx3_cf_core.h"
#include "agx3_cf_core_vectors.c"

#include <stdio.h>
#include <string.h>

static unsigned checks, failures;

#define REJECT(expr)                                                           \
   do {                                                                        \
      checks++;                                                                \
      if ((expr)) {                                                            \
         fprintf(stderr, "FAIL accepted ungrounded form: %s\n", #expr);        \
         failures++;                                                           \
      }                                                                        \
   } while (0)

static void
expect(const struct cf_vec *v, const uint8_t *got, uint8_t len, bool ok)
{
   checks++;
   if (!ok || len != v->nbytes || memcmp(got, v->bytes, v->nbytes) != 0) {
      if (failures < 10)
         fprintf(stderr, "FAIL %s enc=%d args=%d,%d,%d,%d\n", v->kind, v->enc,
                 v->a0, v->a1, v->a2, v->a3);
      failures++;
   }
}

static void
test_half_comparisons(void)
{
   const struct {
      uint16_t src[2];
      struct agx3_cf_source_hints hints[2];
      enum agx3_cf_icond cond;
      uint8_t len, bytes[10];
   } native[] = {
      {{2,3},{{false,true},{false,true}},AGX3_CF_ICOND_SLT,10,
       {0x0a,0x02,0x3b,0x02,0x02,0x00,0x17,0xc8,0x02,0x00}},
      {{2,3},{{false,true},{false,true}},AGX3_CF_ICOND_ULT,6,
       {0x0a,0x02,0x3a,0x02,0x15,0xc0}},
      {{2,3},{{false,false},{false,false}},AGX3_CF_ICOND_SLT,10,
       {0x0a,0x02,0x23,0x02,0x02,0x00,0x17,0xc8,0x02,0x00}},
      {{3,2},{{false,true},{false,false}},AGX3_CF_ICOND_SLT,10,
       {0x0a,0x02,0x2b,0x02,0x02,0x00,0x0f,0xc8,0x02,0x00}},
   };
   for(unsigned i=0;i<sizeof(native)/sizeof(native[0]);++i) {
      uint8_t out[AGX3_CF_MAX_LENGTH],len=0;
      bool ok=agx3_cf_icmp_reg16(AGX_ISA_ENCODING_AGX3B,native[i].cond,0,
                               native[i].src,native[i].hints,32,out,&len);
      ++checks;
      if(!ok || len!=native[i].len || memcmp(out,native[i].bytes,len)) {
         fprintf(stderr,"half compare %u failed len%u: ",i,len);
         for(unsigned j=0;j<len;++j)fprintf(stderr,"%02x",out[j]);
         fprintf(stderr,"\n"); ++failures;
      }
   }
   for(unsigned bad=0;bad<6;++bad) {
      uint16_t src[]={18,1};
      struct agx3_cf_source_hints hints[2]={{false,false},{false,false}};
      uint8_t out[AGX3_CF_MAX_LENGTH],len=0xa5;
      memset(out,0xa5,sizeof(out));
      unsigned slot=0,wait=0;
      enum agx3_cf_icond cond=AGX3_CF_ICOND_SLT;
      if(bad==0)src[0]=256;
      if(bad==1)src[1]=256;
      if(bad==2)hints[0].cache=hints[0].discard=true;
      if(bad==3)slot=7;
      if(bad==4)wait=3;
      if(bad==5)cond=3;
      REJECT(agx3_cf_icmp_reg16(AGX_ISA_ENCODING_AGX3B,cond,slot,src,hints,wait,out,&len));
      ++checks;
      if(len!=0xa5)++failures;
      for(unsigned j=0;j<sizeof(out);++j)if(out[j]!=0xa5)++failures;
   }
}

int
main(void)
{
   test_half_comparisons();
   for (size_t i = 0; i < cf_vec_count; ++i) {
      const struct cf_vec *v = &cf_vecs[i];
      const enum agx_isa_encoding e =
         v->enc == 0 ? AGX_ISA_ENCODING_AGX3B : AGX_ISA_ENCODING_AGX3C;
      uint8_t out[AGX3_CF_MAX_LENGTH];
      uint8_t len = 0;
      bool ok = false;

      if (!strcmp(v->kind, "icmp_rr"))
         ok = agx3_cf_icmp_reg(e, v->a0, v->a1, v->a2, v->a3, out, &len);
      else if (!strcmp(v->kind, "icmp_imm"))
         ok =
            agx3_cf_icmp_imm(e, v->a0, v->a1, v->a2, (uint8_t)v->a3, out, &len);
      else if (!strcmp(v->kind, "fcmp_rr"))
         ok = agx3_cf_fcmp_reg(e, v->a0, v->a1, v->a2, v->a3, /*src1*/ 4, out,
                               &len);
      else if (!strcmp(v->kind, "cndst"))
         ok = agx3_cf_cndst(e, v->a0, v->a1, v->a2, out, &len);
      else if (!strcmp(v->kind, "cndlt"))
         ok = agx3_cf_cndlt(e, v->a0, v->a1, out, &len);
      else if (!strcmp(v->kind, "cndef"))
         ok = agx3_cf_cndef(e, out, &len);
      else if (!strcmp(v->kind, "cndend"))
         ok = agx3_cf_cndend(e, v->a0, out, &len);
      else if (!strcmp(v->kind, "branch"))
         ok = agx3_cf_branch(e, v->a0, out, &len);
      else if (!strcmp(v->kind, "branch_disp")) {
         ok = agx3_cf_branch(e, v->a0, out, &len) &&
              agx3_cf_patch_branch(e, v->a0, out, v->a1);
      }
      expect(v, out, len, ok);
   }

   /* The public codec is itself fail-closed, not just its production caller:
    * Apple's integer condition 3 remains unknown, float condition 3 is
    * represented only through LT+or_nan, and no other raw enum values are
    * admitted. */
   {
      uint8_t out[AGX3_CF_MAX_LENGTH];
      uint8_t len = 0;
      REJECT(agx3_cf_icmp_reg(AGX_ISA_ENCODING_AGX3B, (enum agx3_cf_icond)3, 0,
                              0, 2, out, &len));
      REJECT(agx3_cf_icmp_imm(AGX_ISA_ENCODING_AGX3C, (enum agx3_cf_icond)7, 0,
                              0, 1, out, &len));
      REJECT(agx3_cf_fcmp_reg(AGX_ISA_ENCODING_AGX3B, (enum agx3_cf_fcond)3,
                              false, 0, 0, 2, out, &len));
      REJECT(agx3_cf_fcmp_reg(AGX_ISA_ENCODING_AGX3C, (enum agx3_cf_fcond)7,
                              false, 0, 0, 2, out, &len));
   }

   if (failures) {
      fprintf(stderr, "agx3_cf_core: %u of %u checks failed\n", failures,
              checks);
      return 1;
   }
   printf("agx3_cf_core: all %u checks passed\n", checks);
   return 0;
}
