/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * End-to-end backend test: build AGX IR directly, run agx3_pack_binary, and
 * check the emitted AGX3 byte stream against the independently validated
 * encoders. This exercises the real IR walk, per-instruction translation,
 * modifier routing, and the terminal-end footer -- the lowering glue that the
 * portable encoder-core host tests do not cover.
 */

#include "agx_test.h"

#include <vector>
#include <gtest/gtest.h>

#include "agx3_alu_core.h"
#include "agx3_atomic_core.h"
#include "agx3_cf_core.h"
#include "agx3_falu_mod_core.h"
#include "agx3_local_tps_core.h"
#include "agx3_mem_core.h"
#include "agx3_pack.h"
#include "agx3_scs_core.h"
#include "agx3_sr_core.h"
#include "agx3_stage_core.h"
#include "agx3_static_spill_core.h"
#include "agx3_subgroup_core.h"
#include "agx3_tex_core.h"
#include "agx3_tile_core.h"
#include "agx3_pixel_core.h"
#include "agx3_vary_core.h"

/* agx_nir_lower_tilebuffer's ALL_SAMPLES: the pixel-rate sample mask, and the
 * only immediate AGX3's single-sample tile group can represent. */
static constexpr unsigned AGX_ALL_SAMPLES = 0xFF;

static void
append(std::vector<uint8_t> &v, const uint8_t *b, unsigned n)
{
   v.insert(v.end(), b, b + n);
}

class AGX3PackBinary : public testing::Test {
 protected:
   void *mem_ctx;
   agx_shader_key key;

   AGX3PackBinary()
   {
      mem_ctx = ralloc_context(NULL);
      memset(&key, 0, sizeof(key));
   }
   ~AGX3PackBinary()
   {
      ralloc_free(mem_ctx);
   }

   agx_builder *builder(enum agx_isa_encoding enc)
   {
      key.dev.encoding = enc;
      agx_builder *b = agx_test_builder(mem_ctx);
      b->shader->key = &key;
      return b;
   }

   std::vector<uint8_t> pack(agx_builder *b, bool expect_ok = true)
   {
      struct util_dynarray bin;
      util_dynarray_init(&bin, mem_ctx);
      bool ok = agx3_pack_binary(b->shader, &bin);
      EXPECT_EQ(ok, expect_ok);
      const uint8_t *d = (const uint8_t *)bin.data;
      return std::vector<uint8_t>(d, d + bin.size);
   }
};

TEST_F(AGX3PackBinary, StraightLineALU)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e == 0 ? AGX_ISA_ENCODING_AGX3B : AGX_ISA_ENCODING_AGX3C;
      agx_builder *b = builder(enc);
      agx_index w0 = agx_register(0, AGX_SIZE_32);
      agx_index w2 = agx_register(2, AGX_SIZE_32);
      agx_index w4 = agx_register(4, AGX_SIZE_32);
      agx_index w6 = agx_register(6, AGX_SIZE_32);

      agx_mov_imm_to(b, w0, 0x12345678);
      agx_fadd_to(b, w4, w0, w2);
      agx_fmul_to(b, w6, w4, w2);
      agx_iadd_to(b, w4, w0, w2, 0);
      agx_stop(b);

      std::vector<uint8_t> want;
      struct agx3_encoded_ldimm ld;
      ASSERT_TRUE(agx3_pack_ldimm_u32(enc, 0, 0x12345678, &ld));
      append(want, ld.bytes, ld.length);
      struct agx3_encoded_fadd_f32_reg_reg fa;
      ASSERT_TRUE(agx3_pack_fadd_f32_reg_reg(enc, 4, 0, 2, &fa));
      append(want, fa.bytes, fa.length);
      struct agx3_encoded_fmul_f32_reg_reg fm;
      ASSERT_TRUE(agx3_pack_fmul_f32_reg_reg(enc, 6, 4, 2, &fm));
      append(want, fm.bytes, fm.length);
      struct agx3_encoded_iadd_u32_reg_reg ia;
      ASSERT_TRUE(agx3_pack_iadd_u32_reg_reg(enc, 4, 0, 2, &ia));
      append(want, ia.bytes, ia.length);
      struct agx3_encoded_end en;
      ASSERT_TRUE(agx3_pack_end(enc, &en));
      append(want, en.bytes, en.length);

      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, HalfUVPreservesSharedHalfAndOperandOrder)
{
   for (unsigned order = 0; order < 2; ++order) {
      agx_builder *b = builder(AGX_ISA_ENCODING_AGX3B);
      agx_index half = agx_register(10, AGX_SIZE_16);
      agx_mov_imm_to(b, half, 0x1800);
      agx_index x = agx_register(0, AGX_SIZE_32);
      agx_index y = agx_register(2, AGX_SIZE_32);
      x.discard = y.discard = true;
      half.cache = true;
      agx_fmul_to(b, agx_register(0, AGX_SIZE_32),
                    order ? half : x, order ? x : half);
      half.cache = false;
      half.discard = true;
      agx_fmul_to(b, agx_register(2, AGX_SIZE_32), y, half);
      agx_stop(b);
      /* Native LDIMM and shared-half MUL forms, independently decoded. */
      const std::vector<uint8_t> want = {
         0x54,0x80,0x02,0x00,0x00,0x0c,0x00,0x00,
         0x09,0x01,0x09,0x8a, 0x19,0x03,0x19,0x0a, 0x0e,0,0,0,
      };
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, HalfUVRefusesUngroundedModes)
{
   for (unsigned mode = 0; mode < 9; ++mode) {
      agx_builder *b = builder(mode == 0 ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B);
      agx_index half = agx_register(10, AGX_SIZE_16);
      agx_index wide = agx_register(0, AGX_SIZE_32);
      if (mode == 1) half.neg = true;
      if (mode == 2) half.abs = true;
      if (mode == 3) half.cache = half.discard = true;
      if (mode == 4) half.kill = true;
      if (mode == 5) wide.value = 1;
      if (mode == 6) half.value = 256;
      if (mode == 7) wide.size = AGX_SIZE_16;
      agx_instr *mul = agx_fmul_to(b, agx_register(2, AGX_SIZE_32), wide, half);
      mul->saturate = mode == 8;
      EXPECT_TRUE(pack(b, false).empty());
   }
}

TEST_F(AGX3PackBinary, U16ToF32PreservesHalfLivenessAndLoadWait)
{
   const enum agx_isa_encoding enc = AGX_ISA_ENCODING_AGX3B;
   agx_builder *b = builder(enc);
   agx_get_sr_to(b, agx_register(0, AGX_SIZE_32), AGX_SR_THREAD_POSITION_IN_GRID_X);
   agx_index even = agx_register(0, AGX_SIZE_16);
   agx_index odd = agx_register(1, AGX_SIZE_16);
   even.discard = false;
   odd.discard = true;
   agx_convert_to(b, agx_register(4, AGX_SIZE_32),
                   agx_immediate(AGX_CONVERT_U16_TO_F), even, AGX_ROUND_RTE);
   agx_convert_to(b, agx_register(6, AGX_SIZE_32),
                   agx_immediate(AGX_CONVERT_U16_TO_F), odd, AGX_ROUND_RTE);
   agx_stop(b);
   struct agx3_sr_desc sr = {};
   sr.dest = 0;
   sr.width = AGX3_SR_W32;
   sr.sr = 23;
   sr.slot = 1;
   uint8_t bytes[AGX3_ALU_MAX_LENGTH], len;
   std::vector<uint8_t> want;
   ASSERT_TRUE(agx3_sr_encode(enc, &sr, bytes, &len));
   append(want, bytes, len);
   ASSERT_TRUE(agx3_alu_u16_to_f32(enc, 4, 0, AGX3_ALU_HINT_NONE, 1, bytes, &len));
   append(want, bytes, len);
   ASSERT_TRUE(agx3_alu_u16_to_f32(enc, 6, 1, AGX3_ALU_HINT_DISCARD, 0, bytes, &len));
   append(want, bytes, len);
   struct agx3_encoded_end end;
   ASSERT_TRUE(agx3_pack_end(enc, &end));
   append(want, end.bytes, end.length);
   EXPECT_EQ(pack(b), want);
}

TEST_F(AGX3PackBinary, U16ToF32RejectsUnwitnessedDomains)
{
   for (unsigned variant = 0; variant < 6; ++variant) {
      agx_builder *b = builder(variant == 0 ? AGX_ISA_ENCODING_AGX3C
                                           : AGX_ISA_ENCODING_AGX3B);
      agx_index dest = agx_register(4, variant == 1 ? AGX_SIZE_16 : AGX_SIZE_32);
      agx_index src = variant == 2 ? agx_uniform(0, AGX_SIZE_16)
                                   : agx_register(0, AGX_SIZE_16);
      if (variant == 3)
         src.neg = true;
      agx_convert_to(b, dest,
         agx_immediate(variant == 4 ? AGX_CONVERT_S16_TO_F : AGX_CONVERT_U16_TO_F),
         src, variant == 5 ? AGX_ROUND_RTZ : AGX_ROUND_RTE);
      EXPECT_TRUE(pack(b, false).empty());
   }
}

TEST_F(AGX3PackBinary, FaddMinifloatPreservesWaitLivenessAndCommutativity)
{
   const enum agx_isa_encoding enc = AGX_ISA_ENCODING_AGX3B;
   for (unsigned order = 0; order < 2; ++order) {
      agx_builder *b = builder(enc);
      agx_get_sr_to(b, agx_register(0, AGX_SIZE_32),
                    AGX_SR_THREAD_POSITION_IN_GRID_X);
      agx_index live = agx_register(0, AGX_SIZE_32);
      agx_index last = live;
      live.discard = false;
      last.discard = true;
      agx_index imm = agx_immediate(0x20);
      agx_fadd_to(b, agx_register(4, AGX_SIZE_32),
                  order ? imm : live, order ? live : imm);
      agx_fadd_to(b, agx_register(6, AGX_SIZE_32), last,
                  agx_immediate(0x21));
      agx_stop(b);
      struct agx3_sr_desc sr = {};
      sr.dest = 0;
      sr.width = AGX3_SR_W32;
      sr.sr = 23;
      sr.slot = 1;
      uint8_t bytes[AGX3_ALU_MAX_LENGTH], len;
      std::vector<uint8_t> want;
      ASSERT_TRUE(agx3_sr_encode(enc, &sr, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_alu_fadd_minifloat(enc, 4, 0, 0x20, AGX3_ALU_HINT_NONE, 1,
                                         bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_alu_fadd_minifloat(enc, 6, 0, 0x21, AGX3_ALU_HINT_DISCARD, 0,
                                         bytes, &len));
      append(want, bytes, len);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, FaddMinifloatRefusesUngroundedModes)
{
   for (unsigned variant = 0; variant < 10; ++variant) {
      agx_builder *b = builder(variant == 0 ? AGX_ISA_ENCODING_AGX3C
                                           : AGX_ISA_ENCODING_AGX3B);
      agx_index dest = agx_register(4, variant == 1 ? AGX_SIZE_16 : AGX_SIZE_32);
      agx_index src = variant == 2 ? agx_uniform(0, AGX_SIZE_32)
                                   : agx_register(0, AGX_SIZE_32);
      agx_index imm = agx_immediate(variant == 3 ? 256 : 0x20);
      if (variant == 4)
         src.neg = true;
      if (variant == 5)
         src.abs = true;
      if (variant == 6)
         imm.neg = true;
      if (variant == 7)
         src = agx_immediate(0x20);
      if (variant == 8)
         src.size = AGX_SIZE_16;
      agx_instr *add = agx_fadd_to(b, dest, src, imm);
      add->saturate = variant == 9;
      EXPECT_TRUE(pack(b, false).empty());
   }
}

TEST_F(AGX3PackBinary, FmulMinifloatPreservesWaitLivenessAndCommutativity)
{
   const enum agx_isa_encoding enc = AGX_ISA_ENCODING_AGX3B;
   for (unsigned order = 0; order < 2; ++order) {
      agx_builder *b = builder(enc);
      agx_get_sr_to(b, agx_register(0, AGX_SIZE_32),
                    AGX_SR_THREAD_POSITION_IN_GRID_X);
      agx_index live = agx_register(0, AGX_SIZE_32);
      agx_index last = live;
      live.discard = false;
      last.discard = true;
      agx_index imm = agx_immediate(0x02);
      agx_fmul_to(b, agx_register(4, AGX_SIZE_32),
                  order ? imm : live, order ? live : imm);
      agx_fmul_to(b, agx_register(6, AGX_SIZE_32), last,
                  agx_immediate(0x03));
      agx_stop(b);
      struct agx3_sr_desc sr = {};
      sr.dest = 0;
      sr.width = AGX3_SR_W32;
      sr.sr = 23;
      sr.slot = 1;
      uint8_t bytes[AGX3_ALU_MAX_LENGTH], len;
      std::vector<uint8_t> want;
      ASSERT_TRUE(agx3_sr_encode(enc, &sr, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_alu_fmul_minifloat(enc, 4, 0, 0x02, AGX3_ALU_HINT_NONE, 1,
                                         bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_alu_fmul_minifloat(enc, 6, 0, 0x03, AGX3_ALU_HINT_DISCARD, 0,
                                         bytes, &len));
      append(want, bytes, len);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, FmulMinifloatRefusesUngroundedModes)
{
   for (unsigned variant = 0; variant < 10; ++variant) {
      agx_builder *b = builder(variant == 0 ? AGX_ISA_ENCODING_AGX3C
                                           : AGX_ISA_ENCODING_AGX3B);
      agx_index dest = agx_register(4, variant == 1 ? AGX_SIZE_16 : AGX_SIZE_32);
      agx_index src = variant == 2 ? agx_uniform(0, AGX_SIZE_32)
                                   : agx_register(0, AGX_SIZE_32);
      agx_index imm = agx_immediate(variant == 3 ? 256 : 0x02);
      if (variant == 4)
         src.neg = true;
      if (variant == 5)
         src.abs = true;
      if (variant == 6)
         imm.neg = true;
      if (variant == 7)
         src = agx_immediate(0x02);
      if (variant == 8)
         src.size = AGX_SIZE_16;
      agx_instr *mul = agx_fmul_to(b, dest, src, imm);
      mul->saturate = variant == 9;
      EXPECT_TRUE(pack(b, false).empty());
   }
}

TEST_F(AGX3PackBinary, DeviceAtomicsUseGenerationSpecificReturnRegisters)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      const agx_index value = agx_register(6, AGX_SIZE_32);
      const agx_index result =
         agx_register(e ? 4 : 6, AGX_SIZE_32); /* b is physically in-place */
      agx_atomic_to(b, result, value, agx_uniform(0, AGX_SIZE_64),
                    agx_immediate(0), AGX_ATOMIC_OPC_XOR);
      agx_stop(b);

      struct agx3_device_atomic_desc d = {
         .op = AGX3_ATOMIC_XOR,
         .value = 6,
         .returns_value = true,
         .result = (uint16_t)(e ? 4 : 6),
         .slot = 1,
         .base_kind = AGX3_ATOMIC_BASE_UNIFORM,
         .index_kind = AGX3_ATOMIC_INDEX_IMMEDIATE,
      };
      uint8_t atomic[AGX3_ATOMIC_MAX_LENGTH], atomic_len = 0;
      ASSERT_TRUE(agx3_atomic_encode_device(enc, &d, atomic, &atomic_len));
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));

      std::vector<uint8_t> want;
      append(want, atomic, atomic_len);
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, DeviceAtomicFoldsElementOffsetToBytes)
{
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3C);
   agx_atomic_to(b, agx_register(4, AGX_SIZE_32),
                 agx_register(6, AGX_SIZE_32),
                 agx_uniform(4, AGX_SIZE_64), agx_immediate(7),
                 AGX_ATOMIC_OPC_ADD);
   agx_stop(b);

   struct agx3_device_atomic_desc d = {
      .op = AGX3_ATOMIC_ADD,
      .value = 6,
      .returns_value = true,
      .result = 4,
      .slot = 1,
      .base_kind = AGX3_ATOMIC_BASE_UNIFORM,
      .base = 4,
      .index_kind = AGX3_ATOMIC_INDEX_IMMEDIATE,
      .byte_offset = 28,
   };
   uint8_t atomic[AGX3_ATOMIC_MAX_LENGTH], atomic_len = 0;
   ASSERT_TRUE(agx3_atomic_encode_device(AGX_ISA_ENCODING_AGX3C, &d, atomic,
                                         &atomic_len));
   struct agx3_encoded_end end;
   ASSERT_TRUE(agx3_pack_end(AGX_ISA_ENCODING_AGX3C, &end));

   std::vector<uint8_t> want;
   append(want, atomic, atomic_len);
   append(want, end.bytes, end.length);
   EXPECT_EQ(pack(b), want);
}

TEST_F(AGX3PackBinary, DeviceAtomicMapsPostRaDiscardHints)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);

      agx_index value = agx_register(6, AGX_SIZE_32);
      value.discard = true;
      agx_index base = agx_register(8, AGX_SIZE_64);
      base.discard = true; /* no atomicdev base-hint field: safely omitted */
      agx_index index = agx_register(12, AGX_SIZE_32);
      index.discard = true;
      const agx_index result = agx_register(e ? 4 : 6, AGX_SIZE_32);

      agx_atomic_to(b, result, value, base, index, AGX_ATOMIC_OPC_ADD);
      agx_stop(b);

      struct agx3_device_atomic_desc d = {
         .op = AGX3_ATOMIC_ADD,
         .value = 6,
         .value_kill = true,
         .returns_value = true,
         .result = (uint16_t)(e ? 4 : 6),
         .slot = 1,
         .base_kind = AGX3_ATOMIC_BASE_REGISTER,
         .base = 8,
         .index_kind = AGX3_ATOMIC_INDEX_REGISTER,
         .index = 12,
         .index_kill = true,
         .index_shift = 2,
      };
      uint8_t atomic[AGX3_ATOMIC_MAX_LENGTH], atomic_len = 0;
      ASSERT_TRUE(agx3_atomic_encode_device(enc, &d, atomic, &atomic_len));
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));

      std::vector<uint8_t> want;
      append(want, atomic, atomic_len);
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, LocalAtomicScalarZeroAddress)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      agx_local_atomic_to(b, agx_register(6, AGX_SIZE_32),
                          agx_register(4, AGX_SIZE_32), agx_immediate(0),
                          agx_immediate(0), AGX_ATOMIC_OPC_UMIN);
      agx_stop(b);

      const struct agx3_local_atomic_desc d = {
         .op = AGX3_ATOMIC_UMIN,
         .value = 4,
         .result = 6,
         .slot = 1,
      };
      uint8_t atomic[AGX3_ATOMIC_MAX_LENGTH], atomic_len = 0;
      ASSERT_TRUE(agx3_atomic_encode_local(enc, &d, atomic, &atomic_len));
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));

      std::vector<uint8_t> want;
      append(want, atomic, atomic_len);
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, AtomicsRefuseUngroundedStructuralAxes)
{
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3B);
   agx_atomic_to(b, agx_register(4, AGX_SIZE_32),
                 agx_register(6, AGX_SIZE_32),
                 agx_uniform(0, AGX_SIZE_64), agx_immediate(0),
                 AGX_ATOMIC_OPC_ADD);
   agx_stop(b);
   pack(b, /*expect_ok=*/false); /* AGX3b return was not RA-coalesced */

   b = builder(AGX_ISA_ENCODING_AGX3C);
   agx_local_atomic_to(b, agx_register(6, AGX_SIZE_32),
                       agx_register(4, AGX_SIZE_32),
                       agx_register(8, AGX_SIZE_16), agx_immediate(0),
                       AGX_ATOMIC_OPC_ADD);
   agx_stop(b);
   pack(b, /*expect_ok=*/false); /* dynamic local address is not isolated */
}

TEST_F(AGX3PackBinary, SimdBroadcastImmediateWitnesses)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      for (uint8_t lane : {0, 3}) {
         agx_builder *b = builder(enc);
         agx_shuffle_to(b, agx_register(4, AGX_SIZE_32),
                        agx_register(0, AGX_SIZE_32), agx_immediate(lane));
         agx_stop(b);

         struct agx3_encoded_simd_broadcast_f32_imm broadcast;
         ASSERT_TRUE(agx3_pack_simd_broadcast_f32_imm(enc, 4, 0, lane,
                                                       &broadcast));
         struct agx3_encoded_end end;
         ASSERT_TRUE(agx3_pack_end(enc, &end));

         std::vector<uint8_t> want;
         append(want, broadcast.bytes, broadcast.length);
         append(want, end.bytes, end.length);
         EXPECT_EQ(pack(b), want);
      }
   }
}

TEST_F(AGX3PackBinary, SimdBroadcastRefusesUnwitnessedLane)
{
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3B);
   agx_shuffle_to(b, agx_register(4, AGX_SIZE_32),
                  agx_register(0, AGX_SIZE_32), agx_immediate(1));
   agx_stop(b);
   pack(b, /*expect_ok=*/false);
}

static void
build_shuffle(agx_builder *b, bool quad, unsigned op, agx_index dest,
              agx_index src, agx_index lane)
{
   if (!quad) {
      switch (op) {
      case AGX3_SUBGROUP_SHUFFLE_BROADCAST:
         agx_shuffle_to(b, dest, src, lane);
         break;
      case AGX3_SUBGROUP_SHUFFLE_XOR:
         agx_shuffle_xor_to(b, dest, src, lane);
         break;
      case AGX3_SUBGROUP_SHUFFLE_UP:
         agx_shuffle_up_to(b, dest, src, lane);
         break;
      case AGX3_SUBGROUP_SHUFFLE_DOWN:
         agx_shuffle_down_to(b, dest, src, lane);
         break;
      default:
         UNREACHABLE("bad shuffle");
      }
   } else {
      switch (op) {
      case AGX3_SUBGROUP_SHUFFLE_BROADCAST:
         agx_quad_shuffle_to(b, dest, src, lane);
         break;
      case AGX3_SUBGROUP_SHUFFLE_XOR:
         agx_quad_shuffle_xor_to(b, dest, src, lane);
         break;
      case AGX3_SUBGROUP_SHUFFLE_UP:
         agx_quad_shuffle_up_to(b, dest, src, lane);
         break;
      case AGX3_SUBGROUP_SHUFFLE_DOWN:
         agx_quad_shuffle_down_to(b, dest, src, lane);
         break;
      default:
         UNREACHABLE("bad shuffle");
      }
   }
}

TEST_F(AGX3PackBinary, RegisterAndUniformSubgroupShuffles)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      for (unsigned quad = 0; quad < 2; ++quad) {
         for (unsigned op = 0; op < AGX3_SUBGROUP_SHUFFLE_NUM; ++op) {
            for (unsigned uniform = 0; uniform < 2; ++uniform) {
               agx_builder *b = builder(enc);
               const agx_index lane = uniform ? agx_uniform(8, AGX_SIZE_32)
                                              : agx_register(8, AGX_SIZE_32);
               build_shuffle(b, quad, op, agx_register(4, AGX_SIZE_32),
                             agx_register(2, AGX_SIZE_32), lane);
               agx_stop(b);

               const struct agx3_subgroup_shuffle_desc d = {
                  .quad = (bool)quad,
                  .op = (enum agx3_subgroup_shuffle_op)op,
                  .lane_kind = uniform ? AGX3_SUBGROUP_LANE_UNIFORM
                                       : AGX3_SUBGROUP_LANE_REGISTER,
                  .dest = 4,
                  .src = 2,
                  .lane = 8,
               };
               uint8_t subgroup[AGX3_SUBGROUP_MAX_LENGTH], subgroup_len = 0;
               ASSERT_TRUE(agx3_subgroup_encode_shuffle(enc, &d, subgroup,
                                                         &subgroup_len));
               struct agx3_encoded_end end;
               ASSERT_TRUE(agx3_pack_end(enc, &end));
               std::vector<uint8_t> want;
               append(want, subgroup, subgroup_len);
               append(want, end.bytes, end.length);
               EXPECT_EQ(pack(b), want);
            }
         }
      }
   }
}

TEST_F(AGX3PackBinary, BallotCompareZero)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      for (unsigned quad = 0; quad < 2; ++quad) {
         for (unsigned floating = 0; floating < 2; ++floating) {
            agx_builder *b = builder(enc);
            if (floating) {
               if (quad)
                  agx_fcmp_quad_ballot_to(
                     b, agx_register(4, AGX_SIZE_32),
                     agx_register(2, AGX_SIZE_32), agx_zero(), AGX_FCOND_LT,
                     false);
               else
                  agx_fcmp_ballot_to(b, agx_register(4, AGX_SIZE_32),
                                     agx_register(2, AGX_SIZE_32), agx_zero(),
                                     AGX_FCOND_LT, false);
            } else {
               if (quad)
                  agx_icmp_quad_ballot_to(
                     b, agx_register(4, AGX_SIZE_32),
                     agx_register(2, AGX_SIZE_32), agx_zero(), AGX_ICOND_UEQ,
                     true);
               else
                  agx_icmp_ballot_to(b, agx_register(4, AGX_SIZE_32),
                                     agx_register(2, AGX_SIZE_32), agx_zero(),
                                     AGX_ICOND_UEQ, true);
            }
            agx_stop(b);

            const struct agx3_subgroup_ballot_desc d = {
               .quad = (bool)quad,
               .type = floating ? AGX3_SUBGROUP_BALLOT_FLOAT
                                : AGX3_SUBGROUP_BALLOT_INTEGER,
               .dest = 4,
               .src = 2,
               .condition = (uint8_t)(floating ? 1 : 4),
               .invert = !floating,
            };
            uint8_t subgroup[AGX3_SUBGROUP_MAX_LENGTH], subgroup_len = 0;
            ASSERT_TRUE(agx3_subgroup_encode_ballot(enc, &d, subgroup,
                                                     &subgroup_len));
            struct agx3_encoded_end end;
            ASSERT_TRUE(agx3_pack_end(enc, &end));
            std::vector<uint8_t> want;
            append(want, subgroup, subgroup_len);
            append(want, end.bytes, end.length);
            EXPECT_EQ(pack(b), want);
         }
      }
   }
}

static enum agx_simd_op
mesa_reduce_op(unsigned op)
{
   static const enum agx_simd_op map[] = {
      AGX_SIMD_OP_AND,  AGX_SIMD_OP_FADD, AGX_SIMD_OP_OR,
      AGX_SIMD_OP_FMUL, AGX_SIMD_OP_XOR,  AGX_SIMD_OP_FMIN,
      AGX_SIMD_OP_FMAX, AGX_SIMD_OP_IADD, AGX_SIMD_OP_SMIN,
      AGX_SIMD_OP_SMAX, AGX_SIMD_OP_UMIN, AGX_SIMD_OP_UMAX,
   };
   return map[op];
}

TEST_F(AGX3PackBinary, SimdReductionsAndExactPrefixes)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      for (unsigned op = 0; op < AGX3_SUBGROUP_REDUCE_NUM; ++op) {
         agx_builder *b = builder(enc);
         agx_simd_reduce_to(b, agx_register(4, AGX_SIZE_32),
                            agx_register(2, AGX_SIZE_32), mesa_reduce_op(op));
         agx_stop(b);
         const struct agx3_subgroup_reduce_desc d = {
            .op = (enum agx3_subgroup_reduce_op)op,
            .dest = 4,
            .src = 2,
         };
         uint8_t subgroup[AGX3_SUBGROUP_MAX_LENGTH], subgroup_len = 0;
         ASSERT_TRUE(agx3_subgroup_encode_reduce(enc, &d, subgroup,
                                                  &subgroup_len));
         struct agx3_encoded_end end;
         ASSERT_TRUE(agx3_pack_end(enc, &end));
         std::vector<uint8_t> want;
         append(want, subgroup, subgroup_len);
         append(want, end.bytes, end.length);
         EXPECT_EQ(pack(b), want);
      }

      for (unsigned op : {AGX3_SUBGROUP_REDUCE_FADD,
                          AGX3_SUBGROUP_REDUCE_FMUL,
                          AGX3_SUBGROUP_REDUCE_FMIN,
                          AGX3_SUBGROUP_REDUCE_FMAX,
                          AGX3_SUBGROUP_REDUCE_IADD}) {
         agx_builder *b = builder(enc);
         agx_quad_reduce_to(b, agx_register(4, AGX_SIZE_32),
                            agx_register(2, AGX_SIZE_32), mesa_reduce_op(op));
         agx_stop(b);
         const struct agx3_subgroup_reduce_desc d = {
            .quad = true,
            .op = (enum agx3_subgroup_reduce_op)op,
            .dest = 4,
            .src = 2,
         };
         uint8_t subgroup[AGX3_SUBGROUP_MAX_LENGTH], subgroup_len = 0;
         ASSERT_TRUE(agx3_subgroup_encode_reduce(enc, &d, subgroup,
                                                  &subgroup_len));
         struct agx3_encoded_end end;
         ASSERT_TRUE(agx3_pack_end(enc, &end));
         std::vector<uint8_t> want;
         append(want, subgroup, subgroup_len);
         append(want, end.bytes, end.length);
         EXPECT_EQ(pack(b), want);
      }

      for (unsigned op : {AGX3_SUBGROUP_REDUCE_FADD,
                          AGX3_SUBGROUP_REDUCE_FMUL,
                          AGX3_SUBGROUP_REDUCE_IADD}) {
         agx_builder *b = builder(enc);
         agx_simd_prefix_to(b, agx_register(4, AGX_SIZE_32),
                            agx_register(2, AGX_SIZE_32), mesa_reduce_op(op));
         agx_stop(b);
         const struct agx3_subgroup_reduce_desc d = {
            .prefix = true,
            .op = (enum agx3_subgroup_reduce_op)op,
            .dest = 4,
            .src = 2,
         };
         uint8_t subgroup[AGX3_SUBGROUP_MAX_LENGTH], subgroup_len = 0;
         ASSERT_TRUE(agx3_subgroup_encode_reduce(enc, &d, subgroup,
                                                  &subgroup_len));
         struct agx3_encoded_end end;
         ASSERT_TRUE(agx3_pack_end(enc, &end));
         std::vector<uint8_t> want;
         append(want, subgroup, subgroup_len);
         append(want, end.bytes, end.length);
         EXPECT_EQ(pack(b), want);
      }
   }
}

TEST_F(AGX3PackBinary, SubgroupUnknownAxesFailClosed)
{
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3C);
   agx_icmp_ballot_to(b, agx_register(4, AGX_SIZE_32),
                      agx_register(2, AGX_SIZE_32), agx_immediate(1),
                      AGX_ICOND_SEQ, false);
   agx_stop(b);
   pack(b, /*expect_ok=*/false);

   b = builder(AGX_ISA_ENCODING_AGX3B);
   agx_simd_reduce_to(b, agx_register(4, AGX_SIZE_16),
                      agx_register(2, AGX_SIZE_16), AGX_SIMD_OP_IADD);
   agx_stop(b);
   pack(b, /*expect_ok=*/false);

   b = builder(AGX_ISA_ENCODING_AGX3C);
   agx_index killed = agx_register(2, AGX_SIZE_32);
   killed.kill = true;
   agx_simd_reduce_to(b, agx_register(4, AGX_SIZE_32), killed,
                      AGX_SIMD_OP_IADD);
   agx_stop(b);
   pack(b, /*expect_ok=*/false);
}

TEST_F(AGX3PackBinary, ScopedMemoryBarriers)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      for (unsigned s = AGX3_BARRIER_SCOPE_EXECUTION;
           s <= AGX3_BARRIER_SCOPE_TEXTURE; ++s) {
         agx_builder *b = builder(enc);
         agx_scoped_barrier_agx3(b, s);
         agx_stop(b);

         struct agx3_encoded_scoped_barrier barrier;
         ASSERT_TRUE(agx3_pack_scoped_barrier(
            enc, (enum agx3_barrier_scope)s, &barrier));
         struct agx3_encoded_end end;
         ASSERT_TRUE(agx3_pack_end(enc, &end));

         std::vector<uint8_t> want;
         append(want, barrier.bytes, barrier.length);
         append(want, end.bytes, end.length);
         EXPECT_EQ(pack(b), want);
      }
   }
}

TEST_F(AGX3PackBinary, ScopedMemoryBarrierRefusesUnknownScope)
{
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3C);
   agx_scoped_barrier_agx3(b, AGX3_BARRIER_SCOPE_TEXTURE + 1);
   agx_stop(b);
   pack(b, /*expect_ok=*/false);
}

TEST_F(AGX3PackBinary, DirectScsLoadStoreCarriesWait)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      const agx_index value = agx_register(2, AGX_SIZE_32);
      agx_scs_load_agx3_to(b, value, 4, AGX_FORMAT_I32);
      agx_scs_store_agx3(b, value, 8, AGX_FORMAT_I32);
      agx_stop(b);

      uint8_t bytes[AGX3_SCS_MAX_LENGTH];
      uint8_t length = 0;
      std::vector<uint8_t> want;
      struct agx3_scs_desc d = {
         .data = 2,
         .width = AGX3_MEM_W32,
         .byte_offset = 4,
         .slot = 1,
      };
      ASSERT_TRUE(agx3_scs_encode_load(enc, &d, bytes, &length));
      append(want, bytes, length);
      d.byte_offset = 8;
      d.slot = 0;
      d.wait_mask = 1;
      ASSERT_TRUE(agx3_scs_encode_store(enc, &d, bytes, &length));
      append(want, bytes, length);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, DirectScsScalarWidthsAndSignedOffsets)
{
   struct {
      enum agx_format format;
      enum agx_size size;
      enum agx3_mem_width width;
      int16_t offset;
   } cases[] = {
      {AGX_FORMAT_I8, AGX_SIZE_16, AGX3_MEM_W8, -8},
      {AGX_FORMAT_I16, AGX_SIZE_16, AGX3_MEM_W16, 10},
      {AGX_FORMAT_I32, AGX_SIZE_32, AGX3_MEM_W32, -12},
   };

   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      for (const auto &c : cases) {
         agx_builder *b = builder(enc);
         const agx_index value = agx_register(6, c.size);
         agx_scs_load_agx3_to(b, value, (uint64_t)(int64_t)c.offset, c.format);
         agx_stop(b);

         uint8_t bytes[AGX3_SCS_MAX_LENGTH];
         uint8_t length = 0;
         std::vector<uint8_t> want;
         const struct agx3_scs_desc d = {
            .data = 6,
            .width = c.width,
            .byte_offset = c.offset,
            .slot = 1,
         };
         ASSERT_TRUE(agx3_scs_encode_load(enc, &d, bytes, &length));
         append(want, bytes, length);
         struct agx3_encoded_end end;
         ASSERT_TRUE(agx3_pack_end(enc, &end));
         append(want, end.bytes, end.length);
         EXPECT_EQ(pack(b), want);
      }
   }
}

TEST_F(AGX3PackBinary, DirectScsRefusesVectorAccess)
{
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3C);
   agx_index value = agx_register(2, AGX_SIZE_32);
   value.channels_m1 = 1;
   agx_scs_load_agx3_to(b, value, 0, AGX_FORMAT_I32);
   agx_stop(b);
   pack(b, /*expect_ok=*/false);
}

TEST_F(AGX3PackBinary, UniformIndexedScsScalarWidthsAndSignedOffsets)
{
   struct {
      enum agx_format format;
      enum agx_size size;
      enum agx3_mem_width width;
      int16_t offset;
   } cases[] = {
      {AGX_FORMAT_I8, AGX_SIZE_16, AGX3_MEM_W8, INT16_MAX},
      {AGX_FORMAT_I16, AGX_SIZE_16, AGX3_MEM_W16, INT16_MIN},
      {AGX_FORMAT_I32, AGX_SIZE_32, AGX3_MEM_W32, -12},
   };

   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      for (const auto &c : cases) {
         agx_builder *b = builder(enc);
         const agx_index value = agx_register(6, c.size);
         const agx_index base = agx_uniform(126, AGX_SIZE_64);
         const agx_index index = agx_register(10, AGX_SIZE_32);
         const uint64_t offset = (uint64_t)(int64_t)c.offset;
         agx_scs_load_uniform_indexed_agx3_to(b, value, base, index, offset,
                                               c.format,
                                               enc == AGX_ISA_ENCODING_AGX3C &&
                                                  c.width != AGX3_MEM_W32,
                                               enc == AGX_ISA_ENCODING_AGX3C);
         agx_scs_store_uniform_indexed_agx3(b, value, base, index, offset,
                                             c.format);
         agx_stop(b);

         uint8_t bytes[AGX3_SCS_MAX_LENGTH];
         uint8_t length = 0;
         std::vector<uint8_t> want;
         struct agx3_scs_desc d = {
            .data = 6,
            .width = c.width,
            .byte_offset = c.offset,
            .address_kind = AGX3_SCS_ADDRESS_UNIFORM_INDEXED,
            .index = 10,
            .uniform_base = 126,
            .slot = 1,
            .zero_extend = enc == AGX_ISA_ENCODING_AGX3C &&
                           c.width != AGX3_MEM_W32,
            .c_load_marker = enc == AGX_ISA_ENCODING_AGX3C,
         };
         ASSERT_TRUE(agx3_scs_encode_load(enc, &d, bytes, &length));
         append(want, bytes, length);
         d.slot = 0;
         d.wait_mask = 1;
         d.zero_extend = false;
         d.c_load_marker = false;
         ASSERT_TRUE(agx3_scs_encode_store(enc, &d, bytes, &length));
         append(want, bytes, length);
         struct agx3_encoded_end end;
         ASSERT_TRUE(agx3_pack_end(enc, &end));
         append(want, end.bytes, end.length);
         EXPECT_EQ(pack(b), want);
      }
   }
}

TEST_F(AGX3PackBinary, UniformIndexedScsCarriesHighestSlotWait)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      const unsigned slots = e ? 8 : 6;
      agx_builder *b = builder(enc);
      const agx_index base = agx_uniform(4, AGX_SIZE_64);
      const agx_index index = agx_register(32, AGX_SIZE_32);
      for (unsigned i = 0; i < slots; ++i) {
         agx_scs_load_uniform_indexed_agx3_to(
            b, agx_register(2 * i, AGX_SIZE_32), base, index, 0,
            AGX_FORMAT_I32, false, enc == AGX_ISA_ENCODING_AGX3C);
      }
      agx_scs_store_uniform_indexed_agx3(
         b, agx_register(2 * (slots - 1), AGX_SIZE_32), base, index, 0,
         AGX_FORMAT_I32);
      agx_stop(b);

      uint8_t bytes[AGX3_SCS_MAX_LENGTH];
      uint8_t length = 0;
      std::vector<uint8_t> want;
      for (unsigned i = 0; i < slots; ++i) {
         const struct agx3_scs_desc d = {
            .data = (uint16_t)(2 * i),
            .width = AGX3_MEM_W32,
            .address_kind = AGX3_SCS_ADDRESS_UNIFORM_INDEXED,
            .index = 32,
            .uniform_base = 4,
            .slot = (uint8_t)(i + 1),
            .c_load_marker = enc == AGX_ISA_ENCODING_AGX3C,
         };
         ASSERT_TRUE(agx3_scs_encode_load(enc, &d, bytes, &length));
         append(want, bytes, length);
      }
      const struct agx3_scs_desc store = {
         .data = (uint16_t)(2 * (slots - 1)),
         .width = AGX3_MEM_W32,
         .address_kind = AGX3_SCS_ADDRESS_UNIFORM_INDEXED,
         .index = 32,
         .uniform_base = 4,
         .wait_mask = (uint16_t)(1u << (slots - 1)),
      };
      ASSERT_TRUE(agx3_scs_encode_store(enc, &store, bytes, &length));
      append(want, bytes, length);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, UniformIndexedScsSelectorBoundaries)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      const unsigned index_value = e ? 254 : 190;
      agx_builder *b = builder(enc);
      agx_scs_load_uniform_indexed_agx3_to(
         b, agx_register(2, AGX_SIZE_32), agx_uniform(511, AGX_SIZE_64),
         agx_register(index_value, AGX_SIZE_32), 0, AGX_FORMAT_I32, false,
         enc == AGX_ISA_ENCODING_AGX3C);
      agx_stop(b);

      uint8_t bytes[AGX3_SCS_MAX_LENGTH];
      uint8_t length = 0;
      std::vector<uint8_t> want;
      const struct agx3_scs_desc d = {
         .data = 2,
         .width = AGX3_MEM_W32,
         .address_kind = AGX3_SCS_ADDRESS_UNIFORM_INDEXED,
         .index = (uint16_t)index_value,
         .uniform_base = 511,
         .slot = 1,
         .c_load_marker = enc == AGX_ISA_ENCODING_AGX3C,
      };
      ASSERT_TRUE(agx3_scs_encode_load(enc, &d, bytes, &length));
      append(want, bytes, length);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, UniformIndexedScsRefusesUnprovenAxes)
{
   const auto expect_load_failure = [this](enum agx_isa_encoding enc,
                                            agx_index value, agx_index base,
                                            agx_index index, uint64_t offset) {
      agx_builder *b = builder(enc);
      agx_scs_load_uniform_indexed_agx3_to(b, value, base, index, offset,
                                            AGX_FORMAT_I32, false,
                                            enc == AGX_ISA_ENCODING_AGX3C);
      agx_stop(b);
      pack(b, /*expect_ok=*/false);
   };

   const agx_index value = agx_register(2, AGX_SIZE_32);
   const agx_index base = agx_uniform(4, AGX_SIZE_64);
   const agx_index index = agx_register(6, AGX_SIZE_32);

   expect_load_failure(AGX_ISA_ENCODING_AGX3C, value,
                       agx_register(4, AGX_SIZE_64), index, 0);
   expect_load_failure(AGX_ISA_ENCODING_AGX3C, value,
                       agx_uniform(4, AGX_SIZE_32), index, 0);

   agx_index killed_index = index;
   killed_index.kill = true;
   expect_load_failure(AGX_ISA_ENCODING_AGX3C, value, base, killed_index, 0);

   agx_index vector_index = index;
   vector_index.channels_m1 = 1;
   expect_load_failure(AGX_ISA_ENCODING_AGX3C, value, base, vector_index, 0);

   expect_load_failure(AGX_ISA_ENCODING_AGX3C, value, base,
                       agx_register(6, AGX_SIZE_16), 0);

   agx_index base_oob = base;
   base_oob.value = 512;
   expect_load_failure(AGX_ISA_ENCODING_AGX3C, value, base_oob, index, 0);

   agx_index b_index_oob = index;
   b_index_oob.value = 192;
   expect_load_failure(AGX_ISA_ENCODING_AGX3B, value, base, b_index_oob, 0);

   agx_index c_index_oob = index;
   c_index_oob.value = 256;
   expect_load_failure(AGX_ISA_ENCODING_AGX3C, value, base, c_index_oob, 0);

   expect_load_failure(AGX_ISA_ENCODING_AGX3C, value, base, index, 32768);

   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3B);
   agx_scs_load_uniform_indexed_agx3_to(b, value, base, index, 0,
                                         AGX_FORMAT_I32, false, true);
   agx_stop(b);
   pack(b, /*expect_ok=*/false);

   b = builder(AGX_ISA_ENCODING_AGX3C);
   agx_scs_load_uniform_indexed_agx3_to(b, value, base, index, 0,
                                         AGX_FORMAT_I32, true, true);
   agx_stop(b);
   pack(b, /*expect_ok=*/false);

   b = builder(AGX_ISA_ENCODING_AGX3C);
   agx_index killed_value = value;
   killed_value.kill = true;
   agx_scs_store_uniform_indexed_agx3(b, killed_value, base, index, 0,
                                       AGX_FORMAT_I32);
   agx_stop(b);
   pack(b, /*expect_ok=*/false);
}

TEST_F(AGX3PackBinary, StaticSpillCopyReloadDependency)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      agx_index w0 = agx_register(0, AGX_SIZE_32);
      agx_index w2 = agx_register(2, AGX_SIZE_32);
      agx_index w4 = agx_register(4, AGX_SIZE_32);

      agx_stack_store(b, w0, agx_immediate(32), AGX_FORMAT_I32, 1);
      agx_stack_load_to(b, w2, agx_immediate(32), AGX_FORMAT_I32, 1);
      agx_iadd_to(b, w4, w2, w2, 0);
      agx_stop(b);

      std::vector<uint8_t> want;
      uint8_t bytes[AGX3_ALU_MAX_LENGTH];
      uint8_t length = 0;
      const struct agx3_static_spill_store_desc store = {
         .tps_half = 16,
         .src = 0,
         .width = AGX3_MEM_W32,
         .slot = 1,
      };
      ASSERT_TRUE(agx3_static_spill_encode_store(enc, &store, bytes, &length));
      append(want, bytes, length);

      const struct agx3_static_spill_load_desc load = {
         .dest = 2,
         .tps_half = 16,
         .width = AGX3_MEM_W32,
         .slot = 2,
         .slot_mirror = 2,
         .wait_mask = 1,
      };
      ASSERT_TRUE(agx3_static_spill_encode_load(enc, &load, bytes, &length));
      append(want, bytes, length);

      ASSERT_TRUE(agx3_alu_compose(enc, AGX3_ALU_IADD, 4, 2, 2, 0, 0,
                                   /* wait reload slot 2 */ 2, bytes, &length));
      append(want, bytes, length);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, StaticSpillRefusesNonScalarOrNarrowShapes)
{
   {
      agx_builder *b = builder(AGX_ISA_ENCODING_AGX3B);
      agx_index h0 = agx_register(0, AGX_SIZE_16);
      agx_stack_store(b, h0, agx_immediate(0), AGX_FORMAT_I16, 1);
      agx_stop(b);
      pack(b, /*expect_ok=*/false);
   }

   {
      agx_builder *b = builder(AGX_ISA_ENCODING_AGX3C);
      agx_index w0 = agx_register(0, AGX_SIZE_32);
      w0.channels_m1 = 1;
      agx_stack_store(b, w0, agx_immediate(0), AGX_FORMAT_I32, 3);
      agx_stop(b);
      pack(b, /*expect_ok=*/false);
   }

   {
      agx_builder *b = builder(AGX_ISA_ENCODING_AGX3C);
      agx_stack_load_to(b, agx_register(2, AGX_SIZE_32), agx_immediate(2),
                        AGX_FORMAT_I32, 1);
      agx_stop(b);
      pack(b, /*expect_ok=*/false);
   }
}

TEST_F(AGX3PackBinary, FloatModifierRouting)
{
   /* A negated source must route to the long modifier form, not the compact
    * packer, and match the modifier encoder byte for byte. */
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3B);
   agx_index w0 = agx_register(0, AGX_SIZE_32);
   agx_index w2 = agx_register(2, AGX_SIZE_32);
   agx_index w4 = agx_register(4, AGX_SIZE_32);
   agx_fadd_to(b, w4, agx_neg(w0), w2);
   agx_stop(b);

   struct agx3_falu_mods m = {};
   m.src0_neg = true;
   uint8_t mod[AGX3_FALU_MOD_MAX_LENGTH];
   uint8_t mlen = 0;
   ASSERT_TRUE(
      agx3_falu_add_mod(AGX_ISA_ENCODING_AGX3B, 4, 0, 2, &m, mod, &mlen));
   struct agx3_encoded_end en;
   agx3_pack_end(AGX_ISA_ENCODING_AGX3B, &en);

   std::vector<uint8_t> want;
   append(want, mod, mlen);
   append(want, en.bytes, en.length);
   EXPECT_EQ(pack(b), want);
}

TEST_F(AGX3PackBinary, RefusesUnmappedOp)
{
   /* A texture sample has no grounded AGX3 mapping yet; the backend must fail
    * the shader, not emit a plausible-but-wrong stream. */
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3B);
   agx_index w0 = agx_register(0, AGX_SIZE_32);
   agx_index w2 = agx_register(2, AGX_SIZE_32);
   agx_iadd_to(b, w0, w0, w2, 3); /* shift != 0 -> out of grounded domain */
   agx_stop(b);
   pack(b, /*expect_ok=*/false);
}

TEST_F(AGX3PackBinary, TextureFetch2DLod0Immediate)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      for (int half = 0; half < 2; ++half) {
         agx_builder *b = builder(enc);
         agx_index dest = agx_register(0, half ? AGX_SIZE_16 : AGX_SIZE_32);
         agx_index coord = agx_register(12, AGX_SIZE_32);
         dest.channels_m1 = 3;
         coord.channels_m1 = 1;

         agx_instr *I = agx_texture_sample_to(
            b, dest, coord, agx_immediate(0), agx_immediate(0),
            agx_immediate(0), agx_immediate(0), agx_null(), AGX_DIM_2D,
            AGX_LOD_MODE_AUTO_LOD, 0, false, false, false, AGX_GATHER_NONE,
            false);
         I->op = AGX_OPCODE_TEXTURE_LOAD;
         I->mask = 15;
         agx_stop(b);

         struct agx3_tex_fetch_desc d = {};
         d.dest = 0;
         d.coord = 12;
         d.width = half ? AGX3_TEX_W16 : AGX3_TEX_W32;
         d.mask = 15;
         d.texture = 0;
         d.slot = 1;
         d.final = true;
         uint8_t tb[AGX3_TEX_MAX_LENGTH];
         uint8_t tl = 0;
         ASSERT_TRUE(agx3_tex_encode_fetch(enc, &d, tb, &tl));
         struct agx3_encoded_end end;
         ASSERT_TRUE(agx3_pack_end(enc, &end));

         std::vector<uint8_t> want;
         append(want, tb, tl);
         append(want, end.bytes, end.length);
         EXPECT_EQ(pack(b), want);
      }
   }
}

TEST_F(AGX3PackBinary, TextureFetchRefusesUngroundedAxes)
{
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3C);
   agx_index dest = agx_register(0, AGX_SIZE_32);
   agx_index coord = agx_register(12, AGX_SIZE_32);
   dest.channels_m1 = 3;
   coord.channels_m1 = 1;
   agx_instr *I = agx_texture_sample_to(
      b, dest, coord, agx_immediate(0), agx_immediate(0),
      agx_immediate(16), agx_immediate(0), agx_null(), AGX_DIM_2D,
      AGX_LOD_MODE_AUTO_LOD, 0, false, false, false, AGX_GATHER_NONE, false);
   I->op = AGX_OPCODE_TEXTURE_LOAD;
   I->mask = 15;
   agx_stop(b);
   pack(b, /*expect_ok=*/false);
}

TEST_F(AGX3PackBinary, TextureExportRetiresAtPartBoundary)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      key.no_stop = true;
      b->shader->stage = MESA_SHADER_FRAGMENT;
      agx_index dest = agx_register(8, AGX_SIZE_32);
      agx_index coord = agx_register(0, AGX_SIZE_32);
      dest.channels_m1 = 3;
      coord.channels_m1 = 1;
      agx_instr *tex = agx_texture_sample_to(
         b, dest, coord, agx_immediate(0), agx_immediate(0),
         agx_immediate(0), agx_immediate(0), agx_null(), AGX_DIM_2D,
         AGX_LOD_MODE_AUTO_LOD, 0, false, false, false, AGX_GATHER_NONE,
         false);
      tex->mask = 15;
      for (unsigned channel = 0; channel < 4; ++channel)
         agx_export(b, agx_register(8 + 2 * channel, AGX_SIZE_32),
                     8 + 2 * channel);

      std::vector<uint8_t> want;
      uint8_t bytes[AGX3_TEX_MAX_LENGTH], len = 0;
      ASSERT_TRUE(agx3_tex_stage_copy(enc, 0, 0, 1, 0, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_tex_stage_copy(enc, 2, 2, 1, 0, bytes, &len));
      append(want, bytes, len);
      struct agx3_tex_sample_desc d = {};
      d.dest = 8;
      d.coord = 0;
      d.width = AGX3_TEX_W32;
      d.mask = 15;
      d.slot = 1;
      d.wait_mask = 1;
      d.final = true;
      ASSERT_TRUE(agx3_tex_encode_sample(enc, &d, bytes, &len));
      append(want, bytes, len);

      /* Four zero-byte exports are one outstanding texture result. The
       * independently compiled epilog cannot inherit this slot implicitly. */
      uint8_t wait[AGX3_ALU_MAX_LENGTH];
      ASSERT_TRUE(agx3_alu_compose(enc, AGX3_ALU_BAND, 0, 0, 0, 0, 0,
                                  1, wait, &len));
      append(want, wait, len);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, ExportDoesNotConsumeRealReadWait)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      key.no_stop = true;
      agx_index value = agx_register(4, AGX_SIZE_32);
      agx_get_sr_to(b, value, AGX_SR_THREAD_POSITION_IN_GRID_X);
      agx_export(b, value, 4);
      agx_iadd_to(b, agx_register(6, AGX_SIZE_32), value, value, 0);
      agx_export(b, agx_register(6, AGX_SIZE_32), 6);

      std::vector<uint8_t> want;
      struct agx3_sr_desc sr = {};
      sr.dest = 4;
      sr.width = AGX3_SR_W32;
      sr.sr = 23;
      sr.slot = 1;
      uint8_t bytes[AGX3_ALU_MAX_LENGTH], len = 0;
      ASSERT_TRUE(agx3_sr_encode(enc, &sr, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_alu_compose(enc, AGX3_ALU_IADD, 6, 4, 4, 0, 0,
                                  1, bytes, &len));
      append(want, bytes, len);
      /* The real consumer already waited: no extra tail instruction. */
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, DeviceLoadExportsRetireEveryOutstandingSlot)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      key.no_stop = true;
      b->shader->stage = MESA_SHADER_COMPUTE;
      const agx_index base = agx_register(0, AGX_SIZE_64);
      const agx_index index = agx_register(4, AGX_SIZE_32);
      std::vector<uint8_t> want;
      uint8_t bytes[AGX3_MEM_MAX_LENGTH], len = 0;
      for (unsigned i = 0; i < 2; ++i) {
         const agx_index value = agx_register(6 + 2 * i, AGX_SIZE_32);
         agx_device_load_to(b, value, base, index, AGX_FORMAT_I32, 1, 0,
                             false);
         agx_export(b, value, value.value);
         struct agx3_mem_desc ld = {};
         ld.data = value.value;
         ld.width = AGX3_MEM_W32;
         ld.mask = 1;
         ld.shift = 2;
         ld.base = 0;
         ld.idx_kind = AGX3_MEM_IDX_REG;
         ld.idx_reg = 4;
         ld.slot = i + 1;
         ASSERT_TRUE(agx3_mem_encode_load(enc, &ld, bytes, &len));
         append(want, bytes, len);
      }
      for (unsigned i = 0; i < 2; ++i) {
         ASSERT_TRUE(agx3_alu_compose(enc, AGX3_ALU_BAND, 0, 0, 0, 0, 0,
                                     1u << i, bytes, &len));
         append(want, bytes, len);
      }
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, SynchronousExportPartHasNoTailDrain)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      key.no_stop = true;
      agx_mov_imm_to(b, agx_register(8, AGX_SIZE_32), 0x3f800000);
      agx_export(b, agx_register(8, AGX_SIZE_32), 8);
      struct agx3_encoded_ldimm ld;
      ASSERT_TRUE(agx3_pack_ldimm_u32(enc, 8, 0x3f800000, &ld));
      std::vector<uint8_t> want;
      append(want, ld.bytes, ld.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, TextureFetchWaitSurvivesFallthroughBlock)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      agx_block *first = agx_start_block(b->shader);
      agx_index dest = agx_register(0, AGX_SIZE_32);
      agx_index coord = agx_register(12, AGX_SIZE_32);
      dest.channels_m1 = 3;
      coord.channels_m1 = 1;

      agx_instr *I = agx_texture_sample_to(
         b, dest, coord, agx_immediate(0), agx_immediate(0), agx_immediate(0),
         agx_immediate(0), agx_null(), AGX_DIM_2D, AGX_LOD_MODE_AUTO_LOD, 0,
         false, false, false, AGX_GATHER_NONE, false);
      I->op = AGX_OPCODE_TEXTURE_LOAD;
      I->mask = 15;

      agx_block *second = agx_test_block(b->shader);
      agx_block_add_successor(first, second);
      b->cursor = agx_after_block(second);
      agx_index scalar = dest;
      scalar.channels_m1 = 0;
      agx_iadd_to(b, agx_register(8, AGX_SIZE_32), scalar, scalar, 0);
      agx_stop(b);

      struct agx3_tex_fetch_desc d = {};
      d.dest = 0;
      d.coord = 12;
      d.width = AGX3_TEX_W32;
      d.mask = 15;
      d.slot = 1;
      d.final = true;
      uint8_t tb[AGX3_TEX_MAX_LENGTH];
      uint8_t tl = 0;
      ASSERT_TRUE(agx3_tex_encode_fetch(enc, &d, tb, &tl));

      uint8_t ab[AGX3_ALU_MAX_LENGTH];
      uint8_t al = 0;
      ASSERT_TRUE(agx3_alu_compose(enc, AGX3_ALU_IADD, 8, 0, 0, 0, 0,
                                   /*wait*/ 0x1, ab, &al));
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));

      std::vector<uint8_t> want;
      append(want, tb, tl);
      append(want, ab, al);
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, TileStoreSingleSampleFormats)
{
   const struct {
      enum agx_format format;
      enum agx3_tile_width width;
   } formats[] = {
      {AGX_FORMAT_I8, AGX3_TILE_W8},
      {AGX_FORMAT_U8NORM, AGX3_TILE_W8},
      {AGX_FORMAT_S8NORM, AGX3_TILE_W8},
      {AGX_FORMAT_SRGBA8, AGX3_TILE_W8},
      {AGX_FORMAT_I16, AGX3_TILE_W16},
      {AGX_FORMAT_F16, AGX3_TILE_W16},
      {AGX_FORMAT_U16NORM, AGX3_TILE_W16},
      {AGX_FORMAT_S16NORM, AGX3_TILE_W16},
      {AGX_FORMAT_I32, AGX3_TILE_W32},
      {AGX_FORMAT_RGB10A2, AGX3_TILE_W32},
      {AGX_FORMAT_RG11B10F, AGX3_TILE_W32},
      {AGX_FORMAT_RGB9E5, AGX3_TILE_W32},
   };

   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      for (const auto &f : formats) {
         agx_builder *b = builder(enc);
         agx_index value = agx_register(0, AGX_SIZE_32);
         value.channels_m1 = 3;
         agx_st_tile(b, value, agx_immediate(AGX_ALL_SAMPLES), agx_null(),
                     f.format, 15, 24, false);
         agx_stop(b);

         uint8_t tile[AGX3_TILE_MAX_LENGTH];
         uint8_t tile_len = 0;
         ASSERT_TRUE(agx3_tile_store(enc, 0, f.width, 15, 24,
                                     AGX3_TILE_COV_1X, 0, tile, &tile_len));
         struct agx3_encoded_end end;
         ASSERT_TRUE(agx3_pack_end(enc, &end));
         std::vector<uint8_t> want;
         append(want, tile, tile_len);
         append(want, end.bytes, end.length);
         EXPECT_EQ(pack(b), want);
      }
   }
}

TEST_F(AGX3PackBinary, TileLoadStoreWaitSurvivesFallthroughBlock)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      agx_block *first = agx_start_block(b->shader);
      agx_index value = agx_register(0, AGX_SIZE_32);
      value.channels_m1 = 3;
      agx_ld_tile_to(b, value, agx_immediate(AGX_ALL_SAMPLES), agx_null(),
                     AGX_FORMAT_I32, 15, 16, false);

      agx_block *second = agx_test_block(b->shader);
      agx_block_add_successor(first, second);
      b->cursor = agx_after_block(second);
      value.discard = true; /* nonsemantic post-RA last-use hint */
      agx_st_tile(b, value, agx_immediate(AGX_ALL_SAMPLES), agx_null(),
                  AGX_FORMAT_I32, 15, 16, false);
      agx_stop(b);

      uint8_t tile[AGX3_TILE_MAX_LENGTH];
      uint8_t tile_len = 0;
      std::vector<uint8_t> want;
      ASSERT_TRUE(agx3_tile_load(enc, 0, AGX3_TILE_W32, 1, 15, 16,
                                 AGX3_TILE_COV_1X, 0, true, true, tile,
                                 &tile_len));
      append(want, tile, tile_len);
      ASSERT_TRUE(agx3_tile_store(enc, 0, AGX3_TILE_W32, 15, 16,
                                  AGX3_TILE_COV_1X, 1, tile, &tile_len));
      append(want, tile, tile_len);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, TileLoadSequenceBoundaryMarkers)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      agx_index a = agx_register(0, AGX_SIZE_32);
      agx_index c = agx_register(8, AGX_SIZE_32);
      a.channels_m1 = c.channels_m1 = 3;
      agx_ld_tile_to(b, a, agx_immediate(AGX_ALL_SAMPLES), agx_null(),
                     AGX_FORMAT_I32, 15, 0, false);
      agx_ld_tile_to(b, c, agx_immediate(AGX_ALL_SAMPLES), agx_null(),
                     AGX_FORMAT_I32, 15, 16, false);
      agx_stop(b);

      uint8_t tile[AGX3_TILE_MAX_LENGTH];
      uint8_t tile_len = 0;
      std::vector<uint8_t> want;
      ASSERT_TRUE(agx3_tile_load(enc, 0, AGX3_TILE_W32, 1, 15, 0,
                                 AGX3_TILE_COV_1X, 0, true, false, tile,
                                 &tile_len));
      append(want, tile, tile_len);
      ASSERT_TRUE(agx3_tile_load(enc, 8, AGX3_TILE_W32, 2, 15, 16,
                                 AGX3_TILE_COV_1X, 0, false, true, tile,
                                 &tile_len));
      append(want, tile, tile_len);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, TileRefusesUngroundedAxes)
{
   {
      agx_builder *b = builder(AGX_ISA_ENCODING_AGX3C);
      agx_index value = agx_register(0, AGX_SIZE_32);
      value.channels_m1 = 3;
      agx_st_tile(b, value, agx_register(8, AGX_SIZE_16), agx_null(),
                  AGX_FORMAT_I32, 15, 0, false);
      agx_stop(b);
      EXPECT_TRUE(pack(b, false).empty());
   }
   {
      agx_builder *b = builder(AGX_ISA_ENCODING_AGX3C);
      agx_ld_tile_to(b, agx_register(0, AGX_SIZE_32),
                     agx_immediate(AGX_ALL_SAMPLES), agx_null(),
                     AGX_FORMAT_I32, 1, 0, false);
      agx_stop(b);
      EXPECT_TRUE(pack(b, false).empty());
   }
   {
      agx_builder *b = builder(AGX_ISA_ENCODING_AGX3B);
      agx_index value = agx_register(16, AGX_SIZE_32);
      value.channels_m1 = 3;
      agx_st_tile(b, value, agx_immediate(AGX_ALL_SAMPLES), agx_null(),
                  AGX_FORMAT_I32, 15, 0, false);
      agx_stop(b);
      EXPECT_TRUE(pack(b, false).empty());
   }
   {
      /* A partial sample mask is per-sample traffic. AGX3's single-sample
       * group has no sample selector, so encoding it there would silently
       * widen the store to the whole pixel: refuse instead. */
      agx_builder *b = builder(AGX_ISA_ENCODING_AGX3C);
      agx_index value = agx_register(0, AGX_SIZE_32);
      value.channels_m1 = 3;
      agx_st_tile(b, value, agx_immediate(0x3), agx_null(), AGX_FORMAT_I32, 15,
                  0, false);
      agx_stop(b);
      EXPECT_TRUE(pack(b, false).empty());
   }
}

TEST_F(AGX3PackBinary, GetSrIndexedLoadAddStoreCarriesWait)
{
   /* The thread-indexed compute shape: read thread_position_in_grid.x, use it
    * as the index of a load, add, store back through the same index. The SR
    * read is an async producer: it takes scoreboard slot 1, the load waits on
    * it (for the index) while signalling slot 2, and the add waits on the
    * load. The store re-waits nothing -- both slots are already satisfied. */
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      agx_index base = agx_register(0, AGX_SIZE_64);
      agx_index gid = agx_register(4, AGX_SIZE_32);
      agx_index v6 = agx_register(6, AGX_SIZE_32);
      agx_index v8 = agx_register(8, AGX_SIZE_32);

      agx_get_sr_to(b, gid, AGX_SR_THREAD_POSITION_IN_GRID_X);
      agx_device_load_to(b, v6, base, gid, AGX_FORMAT_I32, 1, 2, false);
      agx_iadd_to(b, v8, v6, v6, 0);
      agx_device_store(b, v8, base, gid, AGX_FORMAT_I32, 1, 2, false);
      agx_stop(b);
      std::vector<uint8_t> got = pack(b);

      /* Independently build the expected stream from the encoders. */
      struct agx3_sr_desc sr = {};
      sr.dest = 4;
      sr.width = AGX3_SR_W32;
      sr.sr = 23;
      sr.slot = 1;
      sr.wait_mask = 0;
      uint8_t rb[AGX3_SR_MAX_LENGTH];
      uint8_t rl = 0;
      ASSERT_TRUE(agx3_sr_encode(enc, &sr, rb, &rl));

      struct agx3_mem_desc ld = {};
      ld.data = 6;
      ld.width = AGX3_MEM_W32;
      ld.mask = 1;
      ld.shift = 2;
      ld.base_is_uniform = false;
      ld.base = 0;
      ld.idx_kind = AGX3_MEM_IDX_REG;
      ld.idx_reg = 4;
      ld.slot = 2;
      ld.wait_mask = 0x1;
      uint8_t lb[AGX3_MEM_MAX_LENGTH];
      uint8_t ll = 0;
      ASSERT_TRUE(agx3_mem_encode_load(enc, &ld, lb, &ll));

      uint8_t ab[AGX3_ALU_MAX_LENGTH];
      uint8_t al = 0;
      ASSERT_TRUE(agx3_alu_compose(enc, AGX3_ALU_IADD, 8, 6, 6, 0, 0,
                                   /*wait*/ 0x2, ab, &al));

      struct agx3_mem_desc st = ld;
      st.data = 8;
      st.slot = 0;
      st.wait_mask = 0;
      uint8_t sb[AGX3_MEM_MAX_LENGTH];
      uint8_t sl = 0;
      ASSERT_TRUE(agx3_mem_encode_store(enc, &st, sb, &sl));

      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));

      std::vector<uint8_t> want;
      append(want, rb, rl);
      append(want, lb, ll);
      append(want, ab, al);
      append(want, sb, sl);
      append(want, end.bytes, end.length);

      EXPECT_EQ(got, want);
   }
}

TEST_F(AGX3PackBinary, RefusesUnwitnessedSr)
{
   /* samples_log2 has no corpus witness tying it to an Apple SR number; the
    * backend must fail the shader, not guess an encoding. */
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3B);
   agx_index h0 = agx_register(0, AGX_SIZE_16);
   agx_get_sr_to(b, h0, AGX_SR_SAMPLES_LOG2);
   agx_stop(b);
   pack(b, /*expect_ok=*/false);
}

TEST_F(AGX3PackBinary, LoadAddStoreCarriesWait)
{
   /* A load, an add that consumes the load result, and a store. The add must
    * carry a wait on the load's scoreboard slot; the shim must produce exactly
    * what the memory + wait-carrying-ALU encoders produce for these operands. */
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      agx_index base = agx_register(0, AGX_SIZE_64);
      agx_index idx = agx_register(4, AGX_SIZE_32);
      agx_index v6 = agx_register(6, AGX_SIZE_32);
      agx_index v8 = agx_register(8, AGX_SIZE_32);

      agx_device_load_to(b, v6, base, idx, AGX_FORMAT_I32, 1, 2, false);
      agx_iadd_to(b, v8, v6, v6, 0);
      agx_device_store(b, v8, base, idx, AGX_FORMAT_I32, 1, 2, false);
      agx_stop(b);
      std::vector<uint8_t> got = pack(b);

      /* Independently build the expected stream from the encoders. */
      struct agx3_mem_desc ld = {};
      ld.data = 6;
      ld.width = AGX3_MEM_W32;
      ld.mask = 1;
      ld.shift = 2;
      ld.base_is_uniform = false;
      ld.base = 0;
      ld.idx_kind = AGX3_MEM_IDX_REG;
      ld.idx_reg = 4;
      ld.slot = 1;
      ld.wait_mask = 0;
      uint8_t lb[AGX3_MEM_MAX_LENGTH];
      uint8_t ll = 0;
      ASSERT_TRUE(agx3_mem_encode_load(enc, &ld, lb, &ll));

      uint8_t ab[AGX3_ALU_MAX_LENGTH];
      uint8_t al = 0;
      ASSERT_TRUE(agx3_alu_compose(enc, AGX3_ALU_IADD, 8, 6, 6, 0, 0,
                                   /*wait*/ 0x1, ab, &al));

      struct agx3_mem_desc st = ld;
      st.data = 8;
      st.slot = 0;
      st.wait_mask = 0;
      uint8_t sb[AGX3_MEM_MAX_LENGTH];
      uint8_t sl = 0;
      ASSERT_TRUE(agx3_mem_encode_store(enc, &st, sb, &sl));

      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));

      std::vector<uint8_t> want;
      append(want, lb, ll);
      append(want, ab, al);
      append(want, sb, sl);
      append(want, end.bytes, end.length);

      EXPECT_EQ(got, want);
   }
}

TEST_F(AGX3PackBinary, LocalLoadAddStoreCarriesWait)
{
   /* Current Mesa shared-memory lowering folds the byte address into the
    * 16-bit base and leaves an immediate-zero index. Exercise that live path,
    * including the async ldtg slot and the dependent ALU wait. */
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      agx_index base = agx_uniform(8, AGX_SIZE_16);
      agx_index v0 = agx_register(0, AGX_SIZE_32);
      agx_index v2 = agx_register(2, AGX_SIZE_32);

      agx_local_load_to(b, v0, base, agx_zero(), AGX_FORMAT_I32, 1);
      agx_iadd_to(b, v2, v0, v0, 0);
      agx_local_store(b, v2, base, agx_zero(), AGX_FORMAT_I32, 1);
      agx_stop(b);

      struct agx3_local_desc ld = {};
      ld.data = 0;
      ld.width = AGX3_MEM_W32;
      ld.mask = 1;
      ld.shift = 2;
      ld.base_kind = AGX3_LOCAL_BASE_UNIFORM;
      ld.base = 8;
      ld.slot = 1;
      uint8_t lb[AGX3_LOCAL_TPS_MAX_LENGTH];
      uint8_t ll = 0;
      ASSERT_TRUE(agx3_local_encode_load(enc, &ld, lb, &ll));

      uint8_t ab[AGX3_ALU_MAX_LENGTH];
      uint8_t al = 0;
      ASSERT_TRUE(agx3_alu_compose(enc, AGX3_ALU_IADD, 2, 0, 0, 0, 0,
                                   /*wait*/ 1, ab, &al));

      struct agx3_local_desc st = ld;
      st.data = 2;
      st.slot = 0;
      uint8_t sb[AGX3_LOCAL_TPS_MAX_LENGTH];
      uint8_t sl = 0;
      ASSERT_TRUE(agx3_local_encode_store(enc, &st, sb, &sl));

      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      std::vector<uint8_t> want;
      append(want, lb, ll);
      append(want, ab, al);
      append(want, sb, sl);
      append(want, end.bytes, end.length);
      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, VaryingInterpolationCoefficientAndUvs)
{
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);

      agx_interp_agx3_to(b, agx_register(0, AGX_SIZE_32), agx_immediate(3),
                         agx_zero(), AGX_INTERPOLATION_CENTER, false);
      agx_interp_agx3_to(b, agx_register(2, AGX_SIZE_32), agx_immediate(4),
                         agx_zero(), AGX_INTERPOLATION_CENTROID, true);
      agx_ldcf_to(b, agx_register(4, AGX_SIZE_32), agx_immediate(5), 1);
      agx_instr *store =
         agx_st_vary(b, agx_immediate(7), agx_register(4, AGX_SIZE_32));
      store->src[1].kill = true;
      agx_stop(b);

      uint8_t bytes[MAX2(AGX3_STAGE_MAX_LENGTH, AGX3_VARY_MAX_LENGTH)];
      uint8_t len = 0;
      std::vector<uint8_t> want;
      ASSERT_TRUE(agx3_vary_interp_at(enc, 0, 3, false, AGX3_VARY_CENTER,
                                      bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_vary_interp_at(enc, 2, 4, true, AGX3_VARY_CENTROID,
                                      bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_vary_ldcf(enc, 4, 5, 1, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_stage_copy_source(enc, 0, 4, true, 1, 1, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_vary_uvsw(enc, 0, 7, 1, true, 1, bytes, &len));
      append(want, bytes, len);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);

      EXPECT_EQ(pack(b), want);
      EXPECT_EQ(b->shader->agx3_staging_halfs, 2u);
   }
}

TEST_F(AGX3PackBinary, RefusesExplicitSampleInterpolation)
{
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3C);
   agx_interp_agx3_to(b, agx_register(0, AGX_SIZE_32), agx_immediate(3),
                      agx_register(2, AGX_SIZE_16), AGX_INTERPOLATION_SAMPLE,
                      false);
   agx_stop(b);
   EXPECT_TRUE(pack(b, /*expect_ok=*/false).empty());
}

TEST_F(AGX3PackBinary, VertexStagingProducerMatchesAppleAndPreservesLiveness)
{
   static const uint8_t apple_x[] = {0x0b, 0x00, 0x29, 0xc4};
   static const uint8_t apple_y[] = {
      0x1b, 0x03, 0x2f, 0x00, 0x42, 0x00, 0x00, 0x04, 0x00, 0x00,
   };
   uint8_t bytes[AGX3_STAGE_MAX_LENGTH], len;
   ASSERT_TRUE(agx3_stage_copy_source(AGX_ISA_ENCODING_AGX3B, 0, 0, true,
                                      1, 0x20, bytes, &len));
   ASSERT_EQ(len, sizeof(apple_x));
   EXPECT_EQ(memcmp(bytes, apple_x, len), 0);
   ASSERT_TRUE(agx3_stage_copy_source(AGX_ISA_ENCODING_AGX3B, 2, 2, true,
                                      2, 0, bytes, &len));
   ASSERT_EQ(len, sizeof(apple_y));
   EXPECT_EQ(memcmp(bytes, apple_y, len), 0);

   ASSERT_TRUE(agx3_stage_copy_source(AGX_ISA_ENCODING_AGX3B, 2, 2, false,
                                      2, 0, bytes, &len));
   /* f27 is the GPR last-use flag, independent of staging f9/f5/f12. */
   EXPECT_EQ(bytes[2], apple_y[2] & ~8u);
   bytes[2] |= 8;
   EXPECT_EQ(memcmp(bytes, apple_y, len), 0);
}

TEST_F(AGX3PackBinary, VertexStagingBankAndScoreboardRoundTrip)
{
   for (unsigned slot = 1; slot <= 6; ++slot) {
      for (unsigned wait : {0u, 1u, 2u, 4u, 8u, 16u, 32u, 33u}) {
         uint8_t bytes[AGX3_STAGE_MAX_LENGTH], len;
         const unsigned staging = 2 * (slot - 1);
         ASSERT_TRUE(agx3_stage_copy_source(AGX_ISA_ENCODING_AGX3B, staging,
                                            200, false, slot, wait, bytes, &len));
         /* Decode the selected COPY destination bank and narrow indices.
          * This is the actual f9=5 form, not a GPR copy to numeric index s. */
         EXPECT_EQ(bytes[0] & 0xf, 0xbu);
         EXPECT_EQ(bytes[2] & 0xf7, len == 4 ? 0x21u : 0x27u);
         EXPECT_EQ((bytes[0] >> 4) * 2, staging);
         EXPECT_EQ((bytes[1] & 0xfe) | (len == 10 ? (bytes[5] & 1) << 7
                                                             : (bytes[3] & 16) << 3),
                   200u);
         unsigned decoded_slot, decoded_wait;
         if (len == 4) {
            decoded_slot = 1;
            const unsigned index = bytes[3] >> 5;
            decoded_wait = index ? 1u << (index - 1) : 0;
         } else {
            ASSERT_EQ(len, 10u);
            decoded_slot = 1 + ((bytes[7] >> 2) & 7);
            decoded_wait = (bytes[5] >> 5) | ((bytes[7] >> 5) << 3);
         }
         EXPECT_EQ(decoded_slot, slot);
         EXPECT_EQ(decoded_wait, wait);

         ASSERT_TRUE(agx3_vary_uvsw(AGX_ISA_ENCODING_AGX3B, staging, 0, slot,
                                    true, 1u << (slot - 1), bytes, &len));
         ASSERT_EQ(len, 8u);
         EXPECT_EQ(bytes[3], staging);
         EXPECT_EQ(1 + (bytes[6] & 7), decoded_slot);
         const unsigned consumer_wait = (bytes[1] >> 4) | ((bytes[2] & 3) << 4);
         EXPECT_EQ(consumer_wait, 1u << (decoded_slot - 1));
      }
   }
}

TEST_F(AGX3PackBinary, VertexStagingAllocationDoesNotUseGprHighWater)
{
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3B);
   b->shader->max_reg = 201;
   agx_st_vary(b, agx_immediate(0), agx_register(200, AGX_SIZE_32));
   agx_st_vary(b, agx_immediate(1), agx_register(200, AGX_SIZE_32));
   agx_stop(b);
   EXPECT_FALSE(pack(b).empty());
   EXPECT_EQ(b->shader->max_reg, 201u);
   EXPECT_EQ(b->shader->agx3_staging_halfs, 4u);
   EXPECT_TRUE(b->shader->agx3_staging_known);
}

TEST_F(AGX3PackBinary, AllGroundedComparisonRelations)
{
   const struct {
      enum agx_icond mesa;
      enum agx3_cf_icond apple;
   } integer_cases[] = {
      {AGX_ICOND_UEQ, AGX3_CF_ICOND_EQ},
      {AGX_ICOND_SEQ, AGX3_CF_ICOND_EQ},
      {AGX_ICOND_ULT, AGX3_CF_ICOND_ULT},
      {AGX_ICOND_UGT, AGX3_CF_ICOND_UGT},
      {AGX_ICOND_SLT, AGX3_CF_ICOND_SLT},
      {AGX_ICOND_SGT, AGX3_CF_ICOND_SGT},
   };
   const struct {
      enum agx_fcond mesa;
      enum agx3_cf_fcond apple;
      bool or_nan;
      bool flips_invert;
   } float_cases[] = {
      {AGX_FCOND_EQ, AGX3_CF_FCOND_EQ, false, false},
      {AGX_FCOND_LT, AGX3_CF_FCOND_LT, false, false},
      {AGX_FCOND_GT, AGX3_CF_FCOND_GT, false, false},
      {AGX_FCOND_LTN, AGX3_CF_FCOND_LT, true, false},
      {AGX_FCOND_GTN, AGX3_CF_FCOND_GT, true, false},
      {AGX_FCOND_GE, AGX3_CF_FCOND_LT, true, true},
      {AGX_FCOND_LE, AGX3_CF_FCOND_GT, true, true},
   };

   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;

      for (const auto &test : integer_cases) {
         for (int invert = 0; invert < 2; ++invert) {
            agx_builder *b = builder(enc);
            agx_if_icmp(b, agx_register(0, AGX_SIZE_32),
                        agx_register(2, AGX_SIZE_32), 1, test.mesa, invert,
                        NULL);
            agx_stop(b);

            uint8_t bytes[AGX3_CF_MAX_LENGTH];
            uint8_t len = 0;
            std::vector<uint8_t> want;
            ASSERT_TRUE(agx3_cf_icmp_reg(enc, test.apple, 0, 0, 2, bytes,
                                         &len));
            append(want, bytes, len);
            ASSERT_TRUE(agx3_cf_cndst(enc, 0, invert, 1, bytes, &len));
            append(want, bytes, len);
            struct agx3_encoded_end end;
            ASSERT_TRUE(agx3_pack_end(enc, &end));
            append(want, end.bytes, end.length);
            EXPECT_EQ(pack(b), want);
         }
      }

      for (const auto &test : float_cases) {
         for (int invert = 0; invert < 2; ++invert) {
            agx_builder *b = builder(enc);
            agx_if_fcmp(b, agx_register(0, AGX_SIZE_32),
                        agx_register(2, AGX_SIZE_32), 1, test.mesa, invert,
                        NULL);
            agx_stop(b);

            uint8_t bytes[AGX3_CF_MAX_LENGTH];
            uint8_t len = 0;
            std::vector<uint8_t> want;
            ASSERT_TRUE(agx3_cf_fcmp_reg(enc, test.apple, test.or_nan, 0, 0,
                                         2, bytes, &len));
            append(want, bytes, len);
            ASSERT_TRUE(agx3_cf_cndst(enc, 0,
                                      (bool)invert ^ test.flips_invert, 1,
                                      bytes, &len));
            append(want, bytes, len);
            struct agx3_encoded_end end;
            ASSERT_TRUE(agx3_pack_end(enc, &end));
            append(want, end.bytes, end.length);
            EXPECT_EQ(pack(b), want);
         }
      }
   }
}

TEST_F(AGX3PackBinary, StructuredIfAndForwardBranch)
{
   /* One fused AGX IR if becomes icmp + cndst. The optional exec-none jump is
    * a reserved bn patched from its own start to the target block's cndend. */
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      agx_block *pop = agx_test_block(b->shader);
      agx_index a = agx_register(0, AGX_SIZE_32);
      agx_index c = agx_register(2, AGX_SIZE_32);

      agx_begin_cf(b); /* architectural on AGX3: emits zero bytes */
      agx_if_icmp(b, a, c, 1, AGX_ICOND_ULT, false, pop);
      agx_jmp_exec_none(b, pop);

      agx_builder pb = agx_init_builder(b->shader, agx_after_block(pop));
      agx_pop_exec(&pb, 1);
      agx_stop(&pb);

      uint8_t bytes[AGX3_CF_MAX_LENGTH];
      uint8_t len = 0;
      std::vector<uint8_t> want;
      ASSERT_TRUE(
         agx3_cf_icmp_reg(enc, AGX3_CF_ICOND_ULT, 0, 0, 2, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_cf_cndst(enc, 0, false, 1, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_cf_branch(enc, true, bytes, &len));
      ASSERT_EQ(len, AGX3_CF_MAX_LENGTH);
      ASSERT_TRUE(agx3_cf_patch_branch(enc, true, bytes, 10));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_cf_cndend(enc, 1, bytes, &len));
      append(want, bytes, len);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);

      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, NestedLoopEntryBackedgeAndPop)
{
   /* Mesa's push_exec(1) + jmp_exec_any + pop_exec(1) loop skeleton maps to
    * AGX3's TRUE-slot cndst(push=2), backwards b, and cndend(pop=2). */
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      agx_index a = agx_register(0, AGX_SIZE_32);
      agx_index c = agx_register(2, AGX_SIZE_32);

      agx_push_exec(b, 1);
      agx_block *body = agx_test_block(b->shader);
      agx_builder lb = agx_init_builder(b->shader, agx_after_block(body));
      agx_while_icmp(&lb, a, c, 1, AGX_ICOND_ULT, false, NULL);
      agx_jmp_exec_any(&lb, body);
      agx_pop_exec(&lb, 1);
      agx_stop(&lb);

      uint8_t bytes[AGX3_CF_MAX_LENGTH];
      uint8_t len = 0;
      std::vector<uint8_t> want;
      ASSERT_TRUE(agx3_cf_cndst(enc, 6, false, 2, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(
         agx3_cf_icmp_reg(enc, AGX3_CF_ICOND_ULT, 0, 0, 2, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_cf_cndlt(enc, 0, false, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_cf_branch(enc, false, bytes, &len));
      ASSERT_EQ(len, AGX3_CF_MAX_LENGTH);
      ASSERT_TRUE(agx3_cf_patch_branch(enc, false, bytes, -14));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_cf_cndend(enc, 2, bytes, &len));
      append(want, bytes, len);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);

      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, FloatRelationAndCanonicalElse)
{
   /* Ordered >= is invert(less-than-or-NaN); the inversion belongs to cndst,
    * not fcmp. Mesa's fixed else_fcmp(0,0,eq) becomes cndef with no compare. */
   for (int e = 0; e < 2; ++e) {
      const enum agx_isa_encoding enc =
         e ? AGX_ISA_ENCODING_AGX3C : AGX_ISA_ENCODING_AGX3B;
      agx_builder *b = builder(enc);
      agx_index a = agx_register(0, AGX_SIZE_32);
      agx_index c = agx_register(2, AGX_SIZE_32);
      agx_if_fcmp(b, a, c, 1, AGX_FCOND_GE, false, NULL);
      agx_else_fcmp(b, agx_zero(), agx_zero(), 1, AGX_FCOND_EQ, false, NULL);
      agx_pop_exec(b, 1);
      agx_stop(b);

      uint8_t bytes[AGX3_CF_MAX_LENGTH];
      uint8_t len = 0;
      std::vector<uint8_t> want;
      ASSERT_TRUE(
         agx3_cf_fcmp_reg(enc, AGX3_CF_FCOND_LT, true, 0, 0, 2, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_cf_cndst(enc, 0, true, 1, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_cf_cndef(enc, bytes, &len));
      append(want, bytes, len);
      ASSERT_TRUE(agx3_cf_cndend(enc, 1, bytes, &len));
      append(want, bytes, len);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);

      EXPECT_EQ(pack(b), want);
   }
}

TEST_F(AGX3PackBinary, RefusesMemoryDependentControlFlowAndRollsBack)
{
   /* The compare/cnd hazard fields are not grounded yet. A load before a
    * condition therefore fails the whole pack and leaves no executable-looking
    * prefix, even when the condition happens to use a different register. */
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3B);
   agx_index base = agx_register(0, AGX_SIZE_64);
   agx_index index = agx_register(4, AGX_SIZE_32);
   agx_index loaded = agx_register(6, AGX_SIZE_32);
   agx_index cond = agx_register(8, AGX_SIZE_32);
   agx_device_load_to(b, loaded, base, index, AGX_FORMAT_I32, 1, 2, false);
   agx_if_icmp(b, cond, agx_immediate(0), 1, AGX_ICOND_UEQ, true, NULL);
   agx_stop(b);

   EXPECT_TRUE(pack(b, /*expect_ok=*/false).empty());
}

TEST_F(AGX3PackBinary, RefusesUngroundedUnconditionalBreak)
{
   agx_builder *b = builder(AGX_ISA_ENCODING_AGX3C);
   agx_break(b, 1, NULL);
   agx_stop(b);
   EXPECT_TRUE(pack(b, /*expect_ok=*/false).empty());
}
