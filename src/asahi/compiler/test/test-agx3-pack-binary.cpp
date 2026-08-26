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
#include "agx3_cf_core.h"
#include "agx3_falu_mod_core.h"
#include "agx3_local_tps_core.h"
#include "agx3_mem_core.h"
#include "agx3_pack.h"
#include "agx3_sr_core.h"
#include "agx3_static_spill_core.h"
#include "agx3_tex_core.h"
#include "agx3_tile_core.h"
#include "agx3_vary_core.h"

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
         agx_st_tile(b, value, agx_immediate(0), agx_null(), f.format, 15,
                     24, false);
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
      agx_ld_tile_to(b, value, agx_immediate(0), agx_null(), AGX_FORMAT_I32,
                     15, 16, false);

      agx_block *second = agx_test_block(b->shader);
      agx_block_add_successor(first, second);
      b->cursor = agx_after_block(second);
      value.discard = true; /* nonsemantic post-RA last-use hint */
      agx_st_tile(b, value, agx_immediate(0), agx_null(), AGX_FORMAT_I32, 15,
                  16, false);
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
      agx_ld_tile_to(b, a, agx_immediate(0), agx_null(), AGX_FORMAT_I32, 15,
                     0, false);
      agx_ld_tile_to(b, c, agx_immediate(0), agx_null(), AGX_FORMAT_I32, 15,
                     16, false);
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
      agx_ld_tile_to(b, agx_register(0, AGX_SIZE_32), agx_immediate(0),
                     agx_null(), AGX_FORMAT_I32, 1, 0, false);
      agx_stop(b);
      EXPECT_TRUE(pack(b, false).empty());
   }
   {
      agx_builder *b = builder(AGX_ISA_ENCODING_AGX3B);
      agx_index value = agx_register(16, AGX_SIZE_32);
      value.channels_m1 = 3;
      agx_st_tile(b, value, agx_immediate(0), agx_null(), AGX_FORMAT_I32, 15,
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

      uint8_t bytes[AGX3_VARY_MAX_LENGTH];
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
      ASSERT_TRUE(agx3_vary_uvsw(enc, 4, 7, 1, true, 1, bytes, &len));
      append(want, bytes, len);
      struct agx3_encoded_end end;
      ASSERT_TRUE(agx3_pack_end(enc, &end));
      append(want, end.bytes, end.length);

      EXPECT_EQ(pack(b), want);
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
