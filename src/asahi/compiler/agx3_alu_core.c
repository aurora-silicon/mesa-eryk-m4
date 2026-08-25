/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */
#include "agx3_alu_core.h"
#include "agx3_binder.h"

bool
agx3_alu_wait_supported(enum agx3_alu_op op)
{
   /* fmin/fmax use a tied-operand fsel form whose duplicated compare fields
    * this table does not carry; exclude them. */
   return op < AGX3_ALU_NUM && op != AGX3_ALU_FMIN && op != AGX3_ALU_FMAX;
}

bool
agx3_alu_compose(enum agx_isa_encoding enc, enum agx3_alu_op op, uint16_t dest,
                 uint16_t src0, uint16_t src1, uint16_t src2, uint32_t imm,
                 uint16_t wait_mask, uint8_t out[AGX3_ALU_MAX_LENGTH],
                 uint8_t *len)
{
   if (op >= AGX3_ALU_NUM)
      return false;
   const unsigned ei = (enc == AGX_ISA_ENCODING_AGX3C) ? 1
                       : (enc == AGX_ISA_ENCODING_AGX3B) ? 0
                                                         : 2;
   if (ei == 2)
      return false;
   const struct agx3_alu_form *F = &agx3_alu_forms[op][ei];
   if (F->mnemonic == 0)
      return false;
   if (wait_mask && !agx3_alu_wait_supported(op))
      return false;

   struct agx3_field_kv base[20];
   for (unsigned i = 0; i < F->nbase; ++i) {
      base[i].field = F->base[i].f;
      base[i].value = F->base[i].v;
   }

   struct agx3_field_kv ov[24];
   unsigned n = 0;
   if (F->f_dest >= 0) { ov[n].field = F->f_dest; ov[n].value = dest; n++; }
   if (F->f_src0 >= 0) { ov[n].field = F->f_src0; ov[n].value = src0; n++; }
   if (F->f_src1 >= 0) { ov[n].field = F->f_src1; ov[n].value = src1; n++; }
   if (F->f_src2 >= 0) { ov[n].field = F->f_src2; ov[n].value = src2; n++; }
   if (F->f_imm >= 0)  { ov[n].field = F->f_imm;  ov[n].value = imm;  n++; }
   for (unsigned i = 0; i < F->nwait; ++i)
      if (wait_mask & (1u << i)) { ov[n].field = F->wait[i]; ov[n].value = 1; n++; }

   return agx3_bind_compose(enc, F->mnemonic, F->ep, base, F->nbase, ov, n, out,
                            len);
}
