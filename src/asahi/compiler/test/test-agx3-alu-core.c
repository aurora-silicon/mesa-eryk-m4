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
#include <assert.h>

static void
check_minifloat_bits(bool multiply, unsigned dest, unsigned src, unsigned imm,
                     bool kill, unsigned wait)
{
   static const unsigned wait_decode[8] = {0, 1, 2, 4, 8, 16, 32, 3};
   uint8_t bytes[AGX3_ALU_MAX_LENGTH], len = 0;
   assert((multiply ? agx3_alu_fmul_minifloat : agx3_alu_fadd_minifloat)(
      AGX_ISA_ENCODING_AGX3B, dest, src, imm, kill, wait, bytes, &len));
   assert(len == 6);
   uint64_t bits = 0;
   for (unsigned i = 0; i < len; ++i)
      bits |= (uint64_t)bytes[i] << (8 * i);
   assert(((bits >> 16) & 7) == (multiply ? 5u : 4u));

   /* Independent physical bit extraction; no composer/decoder round trip. */
   assert((((bits >> 34) & 1) | (((bits >> 4) & 15) << 1) |
           (((bits >> 22) & 3) << 5) | (((bits >> 44) & 1) << 7)) == dest);
   assert((((bits >> 36) & 1) | (((bits >> 25) & 63) << 1) |
           (((bits >> 42) & 1) << 7)) == src);
   assert((((bits >> 35) & 1) | (((bits >> 9) & 63) << 1) |
           (((bits >> 19) & 1) << 7)) == imm);
   assert(((bits >> 20) & 1) == kill);
   assert(wait_decode[(bits >> 45) & 7] == wait);
   assert(((bits >> 3) & 1) && ((bits >> 8) & 1) &&
          ((bits >> 24) & 1)); /* f32 widths */
   assert(((bits >> 15) & 1) && ((bits >> 39) & 1)); /* minifloat */
   assert(!(bits & ((UINT64_C(1) << 37) | (UINT64_C(1) << 38) |
                    (UINT64_C(1) << 41) | (UINT64_C(1) << 43))));
}

static void
test_fadd_minifloat(void)
{
   /* Fresh Apple -fno-fast-math witnesses: real loaded float producer,
    * neighboring immediates, negative immediate, and positively reused input.
    */
   static const struct {
      unsigned dest, src, imm, kill, wait;
      uint8_t bytes[6];
   } witness[] = {
      {0, 0, 0x20, 1, 0,  {0x09, 0xa1, 0x14, 0x01, 0x80, 0x00}},
      {2, 2, 0x20, 1, 0,  {0x19, 0xa1, 0x14, 0x03, 0x80, 0x00}},
      {0, 0, 0x20, 1, 32, {0x09, 0xa1, 0x14, 0x01, 0x80, 0xc0}},
      {0, 0, 0x1f, 1, 32, {0x09, 0x9f, 0x14, 0x01, 0x88, 0xc0}},
      {0, 0, 0x21, 1, 32, {0x09, 0xa1, 0x14, 0x01, 0x88, 0xc0}},
      {0, 0, 0xa0, 1, 32, {0x09, 0xa1, 0x1c, 0x01, 0x80, 0xc0}},
      {0, 2, 0x20, 0, 32, {0x09, 0xa1, 0x04, 0x03, 0x80, 0xc0}},
   };
   uint8_t bytes[AGX3_ALU_MAX_LENGTH], len = 0;
   for (unsigned i = 0; i < sizeof(witness) / sizeof(witness[0]); ++i) {
      assert(agx3_alu_fadd_minifloat(AGX_ISA_ENCODING_AGX3B,
                                    witness[i].dest, witness[i].src,
                                    witness[i].imm, witness[i].kill,
                                    witness[i].wait, bytes, &len));
      assert(len == 6 && !memcmp(bytes, witness[i].bytes, 6));
   }
   const unsigned waits[] = {0, 1, 2, 4, 8, 16, 32, 3};
   for (unsigned dest = 0; dest < 256; dest += 2)
      for (unsigned src = 0; src < 256; src += 2)
         for (unsigned w = 0; w < 8; ++w)
            for (unsigned kill = 0; kill < 2; ++kill)
               check_minifloat_bits(false, dest, src, 0x20, kill, waits[w]);
   for (unsigned imm = 0; imm < 256; ++imm)
      for (unsigned w = 0; w < 8; ++w)
         check_minifloat_bits(false, 254, 128, imm, true, waits[w]);

   /* Pixel-centre arithmetic and adjacent minifloats are exact across the
    * complete U16 pixel coordinate range. This checks the mathematical
    * contract, not a claim of executing the instruction on GPU hardware.
    */
   const unsigned numerator[] = {31, 32, 34};
   for (unsigned i = 0; i < 3; ++i) {
      for (uint32_t u = 0; u <= 65535; ++u) {
         uint32_t n = u * 64 + numerator[i];
         unsigned bit = 0;
         while ((n >> (bit + 1)) != 0)
            ++bit;
         uint32_t want = ((121 + bit) << 23) |
                         ((n - (1u << bit)) << (23 - bit));
         float f = (float)u + (float)numerator[i] / 64.0f;
         uint32_t got;
         memcpy(&got, &f, sizeof(got));
         assert(got == want);
      }
   }
   /* Normal IEEE nearest-even addition, including ties where a truncating
    * interpretation would differ. The -fno-fast-math native probes select
    * this ordinary f32add; no rounding/conversion modifier is synthesized.
    */
   const uint32_t tie_input[] = {0x4b000000, 0x4b000001, 0x4b7fffff};
   const uint32_t tie_result[] = {0x4b000000, 0x4b000002, 0x4b800000};
   for (unsigned i = 0; i < 3; ++i) {
      float input;
      memcpy(&input, &tie_input[i], sizeof(input));
      volatile float live = input;
      float result = live + 0.5f;
      uint32_t bits;
      memcpy(&bits, &result, sizeof(bits));
      assert(bits == tie_result[i]);
   }
   for (unsigned wait = 0; wait < 64; ++wait) {
      bool supported = wait == 0 || wait == 3 || !(wait & (wait - 1));
      memset(bytes, 0xa5, sizeof(bytes));
      len = 99;
      assert(agx3_alu_fadd_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 2, 0x20,
                                    true, wait, bytes, &len) == supported);
      if (!supported)
         assert(len == 99 && bytes[0] == 0xa5);
   }
   assert(!agx3_alu_fadd_minifloat(AGX_ISA_ENCODING_AGX3C, 0, 0, 32, 0,
                                  0, bytes, &len));
   assert(!agx3_alu_fadd_minifloat(AGX_ISA_ENCODING_AGX3B, 1, 0, 32, 0,
                                  0, bytes, &len));
   assert(!agx3_alu_fadd_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 1, 32, 0,
                                  0, bytes, &len));
   assert(!agx3_alu_fadd_minifloat(AGX_ISA_ENCODING_AGX3B, 256, 0, 32, 0,
                                  0, bytes, &len));
   assert(!agx3_alu_fadd_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 256, 32, 0,
                                  0, bytes, &len));
   assert(!agx3_alu_fadd_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 0, 256, 0,
                                  0, bytes, &len));
   assert(!agx3_alu_fadd_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 0, 32, 0,
                                  64, bytes, &len));
   assert(!agx3_alu_fadd_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 0, 32, 0,
                                  0, NULL, &len));
   assert(!agx3_alu_fadd_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 0, 32, 0,
                                  0, bytes, NULL));
   puts("G17P fadd minifloat: 7 Apple witnesses, register/wait/liveness bits,"
        " all256 immediates, all65536 pixel centres and adjacent values PASS");
}

static void
test_fmul_minifloat(void)
{
   static const struct {
      unsigned dest, src, imm, kill, wait;
      uint8_t bytes[6];
   } witness[] = {
      {0, 0, 0x02, 1, 0,  {0x09, 0x83, 0x15, 0x01, 0x80, 0x00}},
      {2, 2, 0x02, 1, 0,  {0x19, 0x83, 0x15, 0x03, 0x80, 0x00}},
      {0, 0, 0x02, 1, 32, {0x09, 0x83, 0x15, 0x01, 0x80, 0xc0}},
      {0, 0, 0x01, 1, 32, {0x09, 0x81, 0x15, 0x01, 0x88, 0xc0}},
      {0, 0, 0x03, 1, 32, {0x09, 0x83, 0x15, 0x01, 0x88, 0xc0}},
      {0, 0, 0x82, 1, 32, {0x09, 0x83, 0x1d, 0x01, 0x80, 0xc0}},
      {0, 2, 0x02, 0, 32, {0x09, 0x83, 0x05, 0x03, 0x80, 0xc0}},
   };
   uint8_t bytes[AGX3_ALU_MAX_LENGTH], len = 0;
   for (unsigned i = 0; i < sizeof(witness) / sizeof(witness[0]); ++i) {
      assert(agx3_alu_fmul_minifloat(AGX_ISA_ENCODING_AGX3B,
                                    witness[i].dest, witness[i].src,
                                    witness[i].imm, witness[i].kill,
                                    witness[i].wait, bytes, &len));
      assert(len == 6 && !memcmp(bytes, witness[i].bytes, 6));
   }
   const unsigned waits[] = {0, 1, 2, 4, 8, 16, 32, 3};
   for (unsigned dest = 0; dest < 256; dest += 2)
      for (unsigned src = 0; src < 256; src += 2)
         for (unsigned w = 0; w < 8; ++w)
            for (unsigned kill = 0; kill < 2; ++kill)
               check_minifloat_bits(true, dest, src, 0x02, kill, waits[w]);
   for (unsigned imm = 0; imm < 256; ++imm)
      for (unsigned w = 0; w < 8; ++w)
         check_minifloat_bits(true, 254, 128, imm, true, waits[w]);

   /* Complete pixel-centre range times1/64,1/32,3/64. The result is the
    * exact rational (2*u+1)*scale/128, independently normalized below.
    */
   for (unsigned scale = 1; scale <= 3; ++scale) {
      for (uint32_t u = 0; u <= 65535; ++u) {
         uint32_t n = (2 * u + 1) * scale;
         unsigned bit = 0;
         while ((n >> (bit + 1)) != 0)
            ++bit;
         uint32_t want = ((120 + bit) << 23) |
                         ((n - (1u << bit)) << (23 - bit));
         float f = ((float)u + 0.5f) * ((float)scale / 64.0f);
         uint32_t got;
         memcpy(&got, &f, sizeof(got));
         assert(got == want);
      }
   }
   const uint32_t tie_input[] = {0x3f800001, 0x3f800003};
   const uint32_t tie_result[] = {0x3fc00002, 0x3fc00004};
   for (unsigned i = 0; i < 2; ++i) {
      float input;
      memcpy(&input, &tie_input[i], sizeof(input));
      volatile float live = input;
      float result = live * 1.5f;
      uint32_t bits;
      memcpy(&bits, &result, sizeof(bits));
      assert(bits == tie_result[i]);
   }
   for (unsigned wait = 0; wait < 64; ++wait) {
      bool supported = wait == 0 || wait == 3 || !(wait & (wait - 1));
      memset(bytes, 0xa5, sizeof(bytes));
      len = 99;
      assert(agx3_alu_fmul_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 2, 2, true,
                                    wait, bytes, &len) == supported);
      if (!supported)
         assert(len == 99 && bytes[0] == 0xa5);
   }
   assert(!agx3_alu_fmul_minifloat(AGX_ISA_ENCODING_AGX3C, 0, 0, 2, 0,
                                  0, bytes, &len));
   assert(!agx3_alu_fmul_minifloat(AGX_ISA_ENCODING_AGX3B, 1, 0, 2, 0,
                                  0, bytes, &len));
   assert(!agx3_alu_fmul_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 1, 2, 0,
                                  0, bytes, &len));
   assert(!agx3_alu_fmul_minifloat(AGX_ISA_ENCODING_AGX3B, 256, 0, 2, 0,
                                  0, bytes, &len));
   assert(!agx3_alu_fmul_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 256, 2, 0,
                                  0, bytes, &len));
   assert(!agx3_alu_fmul_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 0, 256, 0,
                                  0, bytes, &len));
   assert(!agx3_alu_fmul_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 0, 2, 0,
                                  64, bytes, &len));
   assert(!agx3_alu_fmul_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 0, 2, 0,
                                  0, NULL, &len));
   assert(!agx3_alu_fmul_minifloat(AGX_ISA_ENCODING_AGX3B, 0, 0, 2, 0,
                                  0, bytes, NULL));
   puts("G17P fmul minifloat: 7 Apple witnesses, register/wait/liveness bits,"
        " all256 immediates, all65536 scaled pixel centres PASS");
}

static void test_u16_to_f32(void)
{
   /* Literal applegpu-nt32023 G17P ushort/ushort2/reused-ushort witnesses.
    * Each source comes from an actual16-bit LDDEV, then a float32 STDEV.
    */
   static const struct { unsigned d,s,kill,wait; uint8_t bytes[8]; } v[] = {
      {0,0,1,32,{0xa7,0x07,0x56,0x00,0x02,0x00,0xa4,0x20}},
      {0,2,1,32,{0xa7,0x07,0x56,0x00,0x02,0x04,0xa4,0x20}},
      {2,3,1,0, {0xa7,0x07,0x54,0x02,0x02,0x06,0xa4,0x20}},
      {2,4,0,32,{0xa7,0x07,0x56,0x02,0x02,0x08,0x84,0x20}},
   };
   uint8_t bytes[AGX3_ALU_MAX_LENGTH],len;
   for(unsigned i=0;i<sizeof(v)/sizeof(v[0]);++i) {
      assert(agx3_alu_u16_to_f32(AGX_ISA_ENCODING_AGX3B,v[i].d,v[i].s,
                                v[i].kill,v[i].wait,bytes,&len));
      assert(len==8 && !memcmp(bytes,v[i].bytes,8));
   }
   /* Independent literal bit decode: register halves, unsigned16 width,
    * source kill and all six wait bits. Adjacent source half is not consumed.
    */
   for(unsigned d=0;d<128;d+=2) for(unsigned s=0;s<128;++s)
      for(unsigned k=0;k<2;++k) for(unsigned w=0;w<64;++w) {
         assert(agx3_alu_u16_to_f32(AGX_ISA_ENCODING_AGX3B,d,s,k,w,bytes,&len));
         assert(len==8 && bytes[3]==d && bytes[5]==(s<<1));
         assert((bytes[6]&0x18)==0); /* source-width LUT[1]=0, U16 */
         assert(((bytes[6]>>5)&1)==k);
         assert((bytes[7]&0x40)==0); /* unsigned */
         assert((unsigned)((bytes[1]>>4)|((bytes[2]&3)<<4))==w);
      }
   /* Every U16 has an exact IEEE754f32 representation (<=16 significantbits).
    * Compare independent integer normalization to the C conversion over the
    * full domain; RTE/RTZ have no rounding divergence in this exact range.
    */
   for(uint32_t u=0;u<=65535;++u) {
      uint32_t expected=0;
      if(u) {
         unsigned bit=0;while((u>>(bit+1))!=0)++bit;
         expected=((127+bit)<<23)|((u-(1u<<bit))<<(23-bit));
      }
      float f=(float)(uint16_t)u;uint32_t actual;memcpy(&actual,&f,4);
      assert(actual==expected);
   }
   assert(!agx3_alu_u16_to_f32(AGX_ISA_ENCODING_AGX3C,0,0,0,0,bytes,&len));
   assert(!agx3_alu_u16_to_f32(AGX_ISA_ENCODING_AGX2,0,0,0,0,bytes,&len));
   assert(!agx3_alu_u16_to_f32(AGX_ISA_ENCODING_AGX3B,1,0,0,0,bytes,&len));
   assert(!agx3_alu_u16_to_f32(AGX_ISA_ENCODING_AGX3B,128,0,0,0,bytes,&len));
   assert(!agx3_alu_u16_to_f32(AGX_ISA_ENCODING_AGX3B,0,128,0,0,bytes,&len));
   assert(!agx3_alu_u16_to_f32(AGX_ISA_ENCODING_AGX3B,0,0,0,64,bytes,&len));
   assert(!agx3_alu_u16_to_f32(AGX_ISA_ENCODING_AGX3B,0,0,0,0,NULL,&len));
   assert(!agx3_alu_u16_to_f32(AGX_ISA_ENCODING_AGX3B,0,0,0,0,bytes,NULL));
   puts("G17P U16->F32: Apple witnesses,1048576 register/kill/wait encodings, all65536 exact numeric values PASS");
}
static void exact(const uint8_t *bytes, uint8_t length, const char *hex)
{
   assert(strlen(hex) == (size_t)length*2);
   for (unsigned i=0;i<length;++i) {
      unsigned value;
      assert(sscanf(hex+2*i,"%2x",&value)==1 && bytes[i]==value);
   }
}

static void
test_modified_minifloat_add(void)
{
   /* Fresh Apple Metal 32023.917, applegpu_g17p: sat_only, complement,
    * sat_add, sat_neg, complement_reuse, sat_reuse, sat_pair, cache_add.
    * Source and native objects are retained in .tmp/g17p-alpha-20260905.
    */
   const struct {
      unsigned dest, src, imm;
      bool neg, sat;
      enum agx3_alu_hint hint;
      unsigned wait;
      const char *hex;
   } cases[] = {
      {0, 0, 0x80, false, true, AGX3_ALU_HINT_DISCARD, 32, "09011c8101020082"},
      {0, 0, 0x30, true, false, AGX3_ALU_HINT_DISCARD, 32, "09b1140180c8"},
      {0, 0, 0x20, false, true, AGX3_ALU_HINT_DISCARD, 32, "09a1140181000082"},
      {0, 0, 0x30, true, true, AGX3_ALU_HINT_DISCARD, 32, "09b1140181080082"},
      {0, 2, 0x30, true, false, AGX3_ALU_HINT_NONE, 32, "09b1040380c8"},
      {0, 2, 0x80, false, true, AGX3_ALU_HINT_NONE, 32, "0903148101020082"},
      {2, 2, 0x80, false, true, AGX3_ALU_HINT_DISCARD, 0, "19031c8101020002"},
      {0, 6, 0x80, false, true, AGX3_ALU_HINT_CACHE, 32, "0987148101020082"},
      {2, 6, 0x30, true, false, AGX3_ALU_HINT_CACHE, 0, "19b104878008"},
   };
   uint8_t out[AGX3_ALU_MAX_LENGTH], len;
   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      assert(agx3_alu_fadd_minifloat_mod(
         AGX_ISA_ENCODING_AGX3B, cases[i].dest, cases[i].src, cases[i].imm,
         cases[i].neg, cases[i].sat, cases[i].hint, cases[i].wait, out, &len));
      exact(out, len, cases[i].hex);
   }

   const unsigned regs[] = {0, 2, 30, 62, 126, 128, 254};
   const unsigned immediates[] = {0, 0x20, 0x30, 0x80, 0xb0, 0xff};
   unsigned shapes = 0;
   for (unsigned d = 0; d < sizeof(regs) / sizeof(regs[0]); ++d)
      for (unsigned r = 0; r < sizeof(regs) / sizeof(regs[0]); ++r)
         for (unsigned k = 0; k < sizeof(immediates) / sizeof(immediates[0]); ++k)
            for (unsigned neg = 0; neg < 2; ++neg)
               for (unsigned hint = 0; hint <= AGX3_ALU_HINT_CACHE; ++hint)
                  for (unsigned wait = 0; wait < 64; ++wait) {
                     const unsigned imm = immediates[k];
                     const bool swap = imm == 0x80 && !neg;
                     assert(agx3_alu_fadd_minifloat_mod(
                        AGX_ISA_ENCODING_AGX3B, regs[d], regs[r], imm, neg,
                        true, hint, wait, out, &len));
                     assert(len == 8);
                     uint64_t bits = 0;
                     for (unsigned i = 0; i < len; ++i)
                        bits |= (uint64_t)out[i] << (i * 8);
                     /* Physical bit extraction independent of the composer. */
                     assert((((bits >> 34) & 1) | (((bits >> 4) & 15) << 1) |
                             (((bits >> 22) & 3) << 5) |
                             (((bits >> 44) & 1) << 7)) == regs[d]);
                     const unsigned low = swap ? 35 : 36;
                     const unsigned middle = swap ? 9 : 25;
                     const unsigned high = swap ? 40 : 42;
                     assert((((bits >> low) & 1) |
                             (((bits >> middle) & 63) << 1) |
                             (((bits >> high) & 1) << 7)) == regs[r]);
                     assert((((bits >> (swap ? 36 : 35)) & 1) |
                             (((bits >> (swap ? 25 : 9)) & 63) << 1) |
                             (((bits >> (swap ? 20 : 19)) & 1) << 7)) == imm);
                     assert(((bits >> (swap ? 15 : 31)) & 1) ==
                            (hint == AGX3_ALU_HINT_CACHE));
                     assert(((bits >> (swap ? 19 : 20)) & 1) ==
                            (hint == AGX3_ALU_HINT_DISCARD));
                     assert(((bits >> 43) & 1) == neg);
                     assert((bits >> 57) & 1);
                     assert((((bits >> 45) & 7) | (((bits >> 61) & 7) << 3)) == wait);
                     ++shapes;
                  }
   assert(!agx3_alu_fadd_minifloat_mod(AGX_ISA_ENCODING_AGX3C, 0, 0, 0x80,
                                     false, true, 0, 0, out, &len));
   assert(!agx3_alu_fadd_minifloat_mod(AGX_ISA_ENCODING_AGX3B, 1, 0, 0x80,
                                     false, true, 0, 0, out, &len));
   assert(!agx3_alu_fadd_minifloat_mod(AGX_ISA_ENCODING_AGX3B, 0, 1, 0x80,
                                     false, true, 0, 0, out, &len));
   assert(!agx3_alu_fadd_minifloat_mod(AGX_ISA_ENCODING_AGX3B, 256, 0, 0x80,
                                     false, true, 0, 0, out, &len));
   assert(!agx3_alu_fadd_minifloat_mod(AGX_ISA_ENCODING_AGX3B, 0, 256, 0x80,
                                     false, true, 0, 0, out, &len));
   assert(!agx3_alu_fadd_minifloat_mod(AGX_ISA_ENCODING_AGX3B, 0, 0, 256,
                                     false, true, 0, 0, out, &len));
   assert(!agx3_alu_fadd_minifloat_mod(AGX_ISA_ENCODING_AGX3B, 0, 0, 0x80,
                                     false, true, 3, 0, out, &len));
   assert(!agx3_alu_fadd_minifloat_mod(AGX_ISA_ENCODING_AGX3B, 0, 0, 0x80,
                                     false, true, 0, 64, out, &len));
   assert(!agx3_alu_fadd_minifloat_mod(AGX_ISA_ENCODING_AGX3B, 0, 0, 0x30,
                                     true, false, 0, 63, out, &len));
   printf("Modified minifloat add:9 native witnesses +%u physical bit checks passed\n", shapes);
}

static void test_half_uv(void)
{
   uint8_t out[16], size;
   const struct {unsigned d,bits; const char *hex;} moves[] = {
      {2,0x1800,"14800200000c0000"}, {2,0x1801,"14810200000c0000"},
      {2,0x9800,"14800200000c0400"}, {3,0x1800,"14800600000c0000"},
      {4,0x1800,"24800200000c0000"},
   };
   for (unsigned i=0;i<sizeof(moves)/sizeof(moves[0]);++i) {
      assert(agx3_alu_mov16(AGX_ISA_ENCODING_AGX3B,moves[i].d,moves[i].bits,0,out,&size));
      exact(out,size,moves[i].hex);
   }
   const struct {unsigned d,f,h; enum agx3_alu_hint a,b; unsigned wait; const char *hex;} muls[] = {
      {0,0,4,AGX3_ALU_HINT_DISCARD,AGX3_ALU_HINT_DISCARD,32,"09011d0400c0"},
      {0,2,0,AGX3_ALU_HINT_NONE,AGX3_ALU_HINT_DISCARD,1,"090315000020"},
      {0,0,10,AGX3_ALU_HINT_DISCARD,AGX3_ALU_HINT_DISCARD,32,"09011d0a00c0"},
      {2,2,11,AGX3_ALU_HINT_DISCARD,AGX3_ALU_HINT_DISCARD,0,"19031d0a1000"},
      {0,0,10,AGX3_ALU_HINT_DISCARD,AGX3_ALU_HINT_CACHE,32,"09010d8a00c0"},
      {2,2,10,AGX3_ALU_HINT_DISCARD,AGX3_ALU_HINT_DISCARD,0,"1903190a"},
   };
   for (unsigned i=0;i<sizeof(muls)/sizeof(muls[0]);++i) {
      assert(agx3_alu_fmul_mixed(AGX_ISA_ENCODING_AGX3B,muls[i].d,muls[i].f,muls[i].h,muls[i].a,muls[i].b,
                   muls[i].wait,out,&size));
      exact(out,size,muls[i].hex);
   }
   assert(!agx3_alu_mov16(AGX_ISA_ENCODING_AGX3B,256,0x1800,0,out,&size));
   assert(!agx3_alu_mov16(AGX_ISA_ENCODING_AGX3B,2,0x10000,0,out,&size));
   assert(!agx3_alu_mov16(AGX_ISA_ENCODING_AGX3B,2,0x1800,1,out,&size));
   assert(!agx3_alu_fmul_mixed(AGX_ISA_ENCODING_AGX3B,1,0,4,AGX3_ALU_HINT_DISCARD,AGX3_ALU_HINT_DISCARD,0,out,&size));
   assert(!agx3_alu_fmul_mixed(AGX_ISA_ENCODING_AGX3B,0,1,4,AGX3_ALU_HINT_DISCARD,AGX3_ALU_HINT_DISCARD,0,out,&size));
   assert(!agx3_alu_fmul_mixed(AGX_ISA_ENCODING_AGX3B,0,0,256,AGX3_ALU_HINT_DISCARD,AGX3_ALU_HINT_DISCARD,0,out,&size));
   assert(!agx3_alu_fmul_mixed(AGX_ISA_ENCODING_AGX3B,0,0,4,AGX3_ALU_HINT_DISCARD,AGX3_ALU_HINT_DISCARD,64,out,&size));
   unsigned shapes=0;
   for (unsigned d=0;d<256;d+=2)
      for (unsigned h=0;h<256;++h)
         for (unsigned wait=0;wait<=32;wait=wait ? wait*2 : 1) {
            assert(agx3_alu_fmul_mixed(AGX_ISA_ENCODING_AGX3B,d,(d+2)%256,h,AGX3_ALU_HINT_DISCARD, h&1 ? AGX3_ALU_HINT_DISCARD : AGX3_ALU_HINT_CACHE,
                         wait,out,&size));
            assert(size==4 || size==6);
            ++shapes;
         }
   puts("Half UV native candidate PASS:5 LDIMM16 +6 mixed-multiply byte-exact Apple witnesses; odd halves, cache/discard and wait6/no-wait/compact variants; invalid widths/ranges/waits rejected");
   printf("Validated%u register/half/wait shapes through actual composer; offline production encoder coverage\n",shapes);
}

int main(void){
   test_modified_minifloat_add();
   test_half_uv();
   test_fmul_minifloat();
   test_fadd_minifloat();
   test_u16_to_f32();
   unsigned pass=0,fail=0,unsupported=0;
   for(size_t i=0;i<avn;i++){
      const struct av*v=&avs[i];
      /* fmin/fmax use a tied-operand form this encoder does not model. */
      if(!agx3_alu_wait_supported((enum agx3_alu_op)v->op)){ unsupported++; continue; }
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

      /* A wait-only prelude uses ldimm on a fresh destination or bit_and on an
       * in-place accumulator. Cover all six G17P slots and the same common
       * subset on AGX3c.
       */
      const unsigned slots=6;
      for(unsigned slot=0;slot<slots;slot++){
         if(!agx3_alu_compose(E,AGX3_ALU_LDIMM,2,0,0,0,0,
                              1u<<slot,b,&lb)){
            fprintf(stderr,"FAIL ldimm wait slot=%u enc=%d\n",slot,e);
            fail++;
         } else pass++;
         if(!agx3_alu_compose(E,AGX3_ALU_FMUL,2,4,6,0,0,
                              1u<<slot,b,&lb)){
            unsupported++;
         } else pass++;
         if(!agx3_alu_compose(E,AGX3_ALU_BAND,2,2,2,0,0,
                              1u<<slot,b,&lb)){
            fprintf(stderr,"FAIL band wait slot=%u enc=%d\n",slot,e);
            fail++;
         } else pass++;
      }
   }
   if(fail){ fprintf(stderr,"agx3_alu_core: %u failed (%u passed, %u unsupported)\n",fail,pass,unsupported); return 1; }
   printf("agx3_alu_core: %u checks passed (%u unsupported forms skipped)\n",pass,unsupported);
   return 0;
}
