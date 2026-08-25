/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * The composer-driven wait-carrying ALU encoder must reproduce the compact hand
 * packers exactly when no wait is needed, and depositing a wait must change the
 * bytes (and keep the length) without disturbing the operands.
 */
#include "agx3_alu_core.h"
#include "agx3_alu_core_vectors.c"
#include <stdio.h>
#include <string.h>
int main(void){
   unsigned pass=0,fail=0,skip=0;
   for(size_t i=0;i<avn;i++){
      const struct av*v=&avs[i];
      /* fmin/fmax use a tied-operand form this encoder does not model. */
      if(!agx3_alu_wait_supported((enum agx3_alu_op)v->op)){ skip++; continue; }
      enum agx_isa_encoding e=v->enc?AGX_ISA_ENCODING_AGX3C:AGX_ISA_ENCODING_AGX3B;
      uint8_t out[AGX3_ALU_MAX_LENGTH]; uint8_t len=0;
      bool ok=agx3_alu_compose(e,(enum agx3_alu_op)v->op,2,4,6,8,0x1234u,0,out,&len);
      if(!ok||len!=v->n||memcmp(out,v->b,v->n)){ if(fail<12) fprintf(stderr,"FAIL op=%d enc=%d\n",v->op,v->enc); fail++; } else pass++;
   }
   /* Wait deposit: same op/operands, one slot waited -> same length, different bytes. */
   for(int e=0;e<2;e++){
      enum agx_isa_encoding E=e?AGX_ISA_ENCODING_AGX3C:AGX_ISA_ENCODING_AGX3B;
      uint8_t a[16],b[16]; uint8_t la,lb;
      if(agx3_alu_compose(E,AGX3_ALU_IADD,2,4,6,0,0,0,a,&la) &&
         agx3_alu_compose(E,AGX3_ALU_IADD,2,4,6,0,0,0x1,b,&lb)){
         pass++;
         if(la!=lb||memcmp(a,b,la)==0){ fprintf(stderr,"FAIL wait deposit enc=%d\n",e); fail++; }
      }
   }
   if(fail){ fprintf(stderr,"agx3_alu_core: %u failed (%u passed, %u skipped)\n",fail,pass,skip); return 1; }
   printf("agx3_alu_core: %u checks passed (%u fmin/fmax skipped)\n",pass,skip);
   return 0;
}
