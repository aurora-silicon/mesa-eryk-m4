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

#include <gtest/gtest.h>
#include <vector>

#include "agx3_pack.h"
#include "agx3_falu_mod_core.h"
#include "agx3_mem_core.h"
#include "agx3_alu_core.h"
#include "agx3_sr_core.h"

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
   ~AGX3PackBinary() { ralloc_free(mem_ctx); }

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
   ASSERT_TRUE(agx3_falu_add_mod(AGX_ISA_ENCODING_AGX3B, 4, 0, 2, &m, mod,
                                 &mlen));
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
      sr.dest = 4; sr.width = AGX3_SR_W32; sr.sr = 23;
      sr.slot = 1; sr.wait_mask = 0;
      uint8_t rb[AGX3_SR_MAX_LENGTH]; uint8_t rl = 0;
      ASSERT_TRUE(agx3_sr_encode(enc, &sr, rb, &rl));

      struct agx3_mem_desc ld = {};
      ld.data = 6; ld.width = AGX3_MEM_W32; ld.mask = 1; ld.shift = 2;
      ld.base_is_uniform = false; ld.base = 0;
      ld.idx_kind = AGX3_MEM_IDX_REG; ld.idx_reg = 4;
      ld.slot = 2; ld.wait_mask = 0x1;
      uint8_t lb[AGX3_MEM_MAX_LENGTH]; uint8_t ll = 0;
      ASSERT_TRUE(agx3_mem_encode_load(enc, &ld, lb, &ll));

      uint8_t ab[AGX3_ALU_MAX_LENGTH]; uint8_t al = 0;
      ASSERT_TRUE(agx3_alu_compose(enc, AGX3_ALU_IADD, 8, 6, 6, 0, 0,
                                   /*wait*/ 0x2, ab, &al));

      struct agx3_mem_desc st = ld;
      st.data = 8; st.slot = 0; st.wait_mask = 0;
      uint8_t sb[AGX3_MEM_MAX_LENGTH]; uint8_t sl = 0;
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
      ld.data = 6; ld.width = AGX3_MEM_W32; ld.mask = 1; ld.shift = 2;
      ld.base_is_uniform = false; ld.base = 0;
      ld.idx_kind = AGX3_MEM_IDX_REG; ld.idx_reg = 4;
      ld.slot = 1; ld.wait_mask = 0;
      uint8_t lb[AGX3_MEM_MAX_LENGTH]; uint8_t ll = 0;
      ASSERT_TRUE(agx3_mem_encode_load(enc, &ld, lb, &ll));

      uint8_t ab[AGX3_ALU_MAX_LENGTH]; uint8_t al = 0;
      ASSERT_TRUE(agx3_alu_compose(enc, AGX3_ALU_IADD, 8, 6, 6, 0, 0,
                                   /*wait*/ 0x1, ab, &al));

      struct agx3_mem_desc st = ld;
      st.data = 8; st.slot = 0; st.wait_mask = 0;
      uint8_t sb[AGX3_MEM_MAX_LENGTH]; uint8_t sl = 0;
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

