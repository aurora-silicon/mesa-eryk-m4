/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_subgroup_core.h"

#include "agx3_binder.h"

#define ADD_FIELD(F, V)                                                        \
   do {                                                                        \
      base[nb++] = (struct agx3_field_kv){(F), (V)};                           \
   } while (0)

static bool
is_c(enum agx_isa_encoding enc)
{
   return enc == AGX_ISA_ENCODING_AGX3C;
}

static bool
valid_reg(enum agx_isa_encoding enc, uint16_t reg)
{
   const uint16_t limit = enc == AGX_ISA_ENCODING_AGX3B   ? 192
                          : enc == AGX_ISA_ENCODING_AGX3C ? 256
                                                          : 0;
   return limit != 0 && (reg & 1) == 0 && reg < limit;
}

static const char *
shuffle_mnemonic(bool quad, enum agx3_subgroup_shuffle_op op)
{
   static const char *const simd[] = {
      "simdbcast",
      "simdbfly",
      "simdshftup",
      "simdshftdn",
   };
   static const char *const q[] = {
      "quadshfl",
      "quadbfly",
      "quadshftup",
      "quadshftdn",
   };
   return (unsigned)op < AGX3_SUBGROUP_SHUFFLE_NUM
             ? (quad ? q[op] : simd[op])
             : NULL;
}

bool
agx3_subgroup_encode_shuffle(enum agx_isa_encoding enc,
                             const struct agx3_subgroup_shuffle_desc *d,
                             uint8_t out[AGX3_SUBGROUP_MAX_LENGTH],
                             uint8_t *length)
{
   if (d == NULL || out == NULL || length == NULL || d->wait_mask != 0 ||
       !valid_reg(enc, d->dest) || !valid_reg(enc, d->src) ||
       (d->lane_kind != AGX3_SUBGROUP_LANE_REGISTER &&
        d->lane_kind != AGX3_SUBGROUP_LANE_UNIFORM) ||
       (d->lane & 1) != 0 ||
       (d->lane_kind == AGX3_SUBGROUP_LANE_REGISTER ? !valid_reg(enc, d->lane)
                                                    : d->lane >= 512))
      return false;

   const char *mnemonic = shuffle_mnemonic(d->quad, d->op);
   if (mnemonic == NULL)
      return false;

   struct agx3_field_kv base[16];
   unsigned nb = 0;
   ADD_FIELD(9, 2);
   ADD_FIELD(10, 2);
   ADD_FIELD(23, 2);
   ADD_FIELD(24, 2);
   ADD_FIELD(37, d->lane_kind);
   ADD_FIELD(38, 1);

   if (!is_c(enc)) {
      if (d->lane_kind == AGX3_SUBGROUP_LANE_REGISTER) {
         if (!d->quad && d->op == AGX3_SUBGROUP_SHUFFLE_BROADCAST) {
            ADD_FIELD(11, 1);
            ADD_FIELD(25, 1);
            ADD_FIELD(41, 1);
         } else if (d->op == AGX3_SUBGROUP_SHUFFLE_UP ||
                    d->op == AGX3_SUBGROUP_SHUFFLE_DOWN) {
            ADD_FIELD(11, 1);
            ADD_FIELD(25, 1);
            ADD_FIELD(39, 1);
         } else {
            ADD_FIELD(27, 1);
            ADD_FIELD(41, 1);
         }
      } else if (d->op == AGX3_SUBGROUP_SHUFFLE_UP ||
                 d->op == AGX3_SUBGROUP_SHUFFLE_DOWN) {
         ADD_FIELD(11, 1);
         ADD_FIELD(25, 1);
      } else {
         ADD_FIELD(27, 1);
      }
   } else {
      if (d->lane_kind == AGX3_SUBGROUP_LANE_REGISTER && !d->quad &&
          d->op == AGX3_SUBGROUP_SHUFFLE_BROADCAST) {
         ADD_FIELD(11, 1);
         ADD_FIELD(25, 1);
      } else {
         ADD_FIELD(27, 1);
      }
      if (d->lane_kind == AGX3_SUBGROUP_LANE_REGISTER)
         ADD_FIELD(41, 1);
      static const uint8_t ids[2][AGX3_SUBGROUP_SHUFFLE_NUM] = {
         {154, 155, 161, 160},
         {140, 137, 142, 141},
      };
      ADD_FIELD(42, 3);
      ADD_FIELD(43, ids[d->quad][d->op]);
   }

   struct agx3_field_kv ov[] = {
      {2, d->dest},
      {16, d->src},
      {d->lane_kind == AGX3_SUBGROUP_LANE_REGISTER ? 30 : 31, d->lane},
   };
   return agx3_bind_compose(enc, mnemonic, 0, base, nb, ov, 3, out, length);
}

bool
agx3_subgroup_encode_ballot(enum agx_isa_encoding enc,
                            const struct agx3_subgroup_ballot_desc *d,
                            uint8_t out[AGX3_SUBGROUP_MAX_LENGTH],
                            uint8_t *length)
{
   if (d == NULL || out == NULL || length == NULL || d->wait_mask != 0 ||
       !valid_reg(enc, d->dest) || !valid_reg(enc, d->src) ||
       (unsigned)d->type > AGX3_SUBGROUP_BALLOT_FLOAT)
      return false;

   if (d->type == AGX3_SUBGROUP_BALLOT_INTEGER) {
      if (d->condition != 4 && d->condition != 5 && d->condition != 6)
         return false;
   } else if (d->condition != 0 && d->condition != 1 && d->condition != 2 &&
              d->condition != 5 && d->condition != 6) {
      return false;
   }

   struct agx3_field_kv base[16];
   unsigned nb = 0;
   ADD_FIELD(9, 2);
   ADD_FIELD(10, 2);
   if (d->quad)
      ADD_FIELD(11, 1);
   ADD_FIELD(23, 2);
   ADD_FIELD(24, 2);
   ADD_FIELD(27, 1);

   const char *mnemonic;
   uint16_t cond_field, invert_field;
   if (d->type == AGX3_SUBGROUP_BALLOT_INTEGER) {
      mnemonic = d->quad ? "quadiballot" : "simdiballot";
      if (d->condition == 4) {
         ADD_FIELD(38, 2);
         ADD_FIELD(39, 2);
         ADD_FIELD(42, 1);
      } else {
         ADD_FIELD(38, 1);
         ADD_FIELD(39, 1);
      }
      cond_field = 54;
      invert_field = 55;
      if (is_c(enc)) {
         ADD_FIELD(44, 3);
         ADD_FIELD(45, d->quad ? 139 : 157);
      }
   } else {
      mnemonic = d->quad ? "quadfballot" : "simdfballot";
      ADD_FIELD(40, 1);
      ADD_FIELD(41, 2);
      cond_field = 58;
      invert_field = 59;
      if (is_c(enc)) {
         ADD_FIELD(48, 3);
         ADD_FIELD(49, d->quad ? 138 : 156);
      }
   }

   struct agx3_field_kv ov[] = {
      {2, d->dest},
      {16, d->src},
      {cond_field, d->condition},
      {invert_field, d->invert},
   };
   return agx3_bind_compose(enc, mnemonic, 0, base, nb, ov, 4, out, length);
}

struct reduce_form {
   const char *simd;
   const char *quad;
   uint8_t c_simd;
   uint8_t c_quad;
   bool is_float;
   uint8_t integer_class;
};

static const struct reduce_form reduce_forms[AGX3_SUBGROUP_REDUCE_NUM] = {
   [AGX3_SUBGROUP_REDUCE_AND] = {"sRedAND", "", 169, 0, false, 2},
   [AGX3_SUBGROUP_REDUCE_FADD] = {"sRedf32add", "qRedf32add", 172, 126, true,
                                  0},
   [AGX3_SUBGROUP_REDUCE_OR] = {"sRedOR", "", 177, 0, false, 2},
   [AGX3_SUBGROUP_REDUCE_FMUL] = {"sRedf32mul", "qRedf32mul", 173, 127, true,
                                  0},
   [AGX3_SUBGROUP_REDUCE_XOR] = {"sRedXOR", "", 182, 0, false, 2},
   [AGX3_SUBGROUP_REDUCE_FMIN] = {"sRedfmin", "qRedfmin", 175, 129, true, 0},
   [AGX3_SUBGROUP_REDUCE_FMAX] = {"sRedfmax", "qRedfmax", 174, 128, true, 0},
   [AGX3_SUBGROUP_REDUCE_IADD] = {"sRediadd", "qRediadd", 176, 130, false, 0},
   [AGX3_SUBGROUP_REDUCE_SMIN] = {"sRedsmin", "", 179, 0, false, 1},
   [AGX3_SUBGROUP_REDUCE_SMAX] = {"sRedsmax", "", 178, 0, false, 1},
   [AGX3_SUBGROUP_REDUCE_UMIN] = {"sRedumin", "", 181, 0, false, 2},
   [AGX3_SUBGROUP_REDUCE_UMAX] = {"sRedumax", "", 180, 0, false, 2},
};

bool
agx3_subgroup_encode_reduce(enum agx_isa_encoding enc,
                            const struct agx3_subgroup_reduce_desc *d,
                            uint8_t out[AGX3_SUBGROUP_MAX_LENGTH],
                            uint8_t *length)
{
   if (d == NULL || out == NULL || length == NULL || d->wait_mask != 0 ||
       (unsigned)d->op >= AGX3_SUBGROUP_REDUCE_NUM ||
       !valid_reg(enc, d->dest) ||
       !valid_reg(enc, d->src))
      return false;

   const struct reduce_form *f = &reduce_forms[d->op];
   const char *mnemonic = d->quad ? f->quad : f->simd;
   if (mnemonic == NULL || mnemonic[0] == '\0')
      return false;
   if (d->prefix && (d->quad || (d->op != AGX3_SUBGROUP_REDUCE_FADD &&
                                 d->op != AGX3_SUBGROUP_REDUCE_FMUL &&
                                 d->op != AGX3_SUBGROUP_REDUCE_IADD)))
      return false;

   struct agx3_field_kv base[16];
   unsigned nb = 0;
   ADD_FIELD(9, 2);
   ADD_FIELD(10, 2);
   if (f->is_float) {
      ADD_FIELD(25, 2);
      ADD_FIELD(26, 2);
      ADD_FIELD(29, 1);
      if (is_c(enc)) {
         ADD_FIELD(47, 3);
         ADD_FIELD(48, d->quad ? f->c_quad : f->c_simd);
      } else {
         ADD_FIELD(34, 1);
         ADD_FIELD(42, 1);
         ADD_FIELD(43, 1);
      }
   } else {
      ADD_FIELD(23, 2);
      ADD_FIELD(24, 2);
      ADD_FIELD(27, 1);
      if (f->integer_class == 1)
         ADD_FIELD(28, 1);
      if (is_c(enc)) {
         if (f->integer_class == 2) {
            ADD_FIELD(42, 3);
            ADD_FIELD(43, d->quad ? f->c_quad : f->c_simd);
         } else {
            ADD_FIELD(43, 3);
            ADD_FIELD(44, d->quad ? f->c_quad : f->c_simd);
         }
      } else if (f->integer_class == 2) {
         ADD_FIELD(29, 1);
         ADD_FIELD(37, 1);
         ADD_FIELD(38, 1);
      } else {
         ADD_FIELD(30, 1);
         ADD_FIELD(38, 1);
         ADD_FIELD(39, 1);
      }
   }

   if (d->prefix)
      ADD_FIELD(f->is_float ? 57 : 53, 1);

   struct agx3_field_kv ov[] = {
      {2, d->dest},
      {f->is_float ? 18 : 16, d->src},
   };
   return agx3_bind_compose(enc, mnemonic, 0, base, nb, ov, 2, out, length);
}

#undef ADD_FIELD
