/*
 * Copyright 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "agx_builder.h"
#include "agx_compiler.h"
#include "agx_test.h"

#include <gtest/gtest.h>

#define CASE(instr, expected, size)                                            \
   INSTRUCTION_CASE(                                                           \
      {                                                                        \
         UNUSED agx_index out = agx_temp(b->shader, AGX_SIZE_##size);          \
         instr;                                                                \
         agx_unit_test(b, out);                                                \
      },                                                                       \
      {                                                                        \
         UNUSED agx_index out = agx_temp(b->shader, AGX_SIZE_##size);          \
         expected;                                                             \
         agx_unit_test(b, out);                                                \
      },                                                                       \
      agx_opt_compact_constants)

#define NEGCASE(instr, size)    CASE(instr, instr, size)
#define CASE32(instr, expected) CASE(instr, expected, 32)
#define NEGCASE32(instr)        NEGCASE(instr, 32)

class CompactConstants : public testing::Test {
 protected:
   CompactConstants()
   {
      mem_ctx = ralloc_context(NULL);

      wx = agx_register(0, AGX_SIZE_32);
   }

   ~CompactConstants()
   {
      ralloc_free(mem_ctx);
   }

   void *mem_ctx;
   agx_index wx;
};

TEST_F(CompactConstants, FP32)
{
   CASE32(agx_fadd_to(b, out, wx, agx_mov_imm(b, 32, fui(32768.0))),
          agx_fadd_to(b, out, wx, agx_mov_imm(b, 16, 0x7800)));
}

TEST_F(CompactConstants, InexactFP32)
{
   NEGCASE32(agx_fadd_to(b, out, wx, agx_mov_imm(b, 32, fui(32769.0))));
}

TEST_F(CompactConstants, NativeHalfConstantOwnsAlignedUploadWord)
{
   for (unsigned initial : {0u, 1u, 254u, 510u}) {
      agx_builder *b = agx_test_builder(mem_ctx);
      agx_shader_key key = {};
      key.dev.encoding = AGX_ISA_ENCODING_AGX3B;
      agx_shader_info info = {};
      info.push_count = initial;
      b->shader->key = &key;
      b->shader->out = &info;
      agx_index lod = agx_mov_imm(b, 16, 3);
      agx_index coord = agx_register(0, AGX_SIZE_32);
      coord.channels_m1 = 1;
      agx_index dest = agx_register(8, AGX_SIZE_32);
      dest.channels_m1 = 3;
      agx_instr *tex = agx_texture_sample_to(
         b, dest, coord, lod, agx_immediate(0), agx_immediate(0),
         agx_immediate(0), agx_null(), AGX_DIM_2D, AGX_LOD_MODE_LOD_MIN,
         0, false, false, false, AGX_GATHER_NONE, false);
      tex->op = AGX_OPCODE_TEXTURE_LOAD;
      tex->mask = 15;
      agx_opt_promote_constants(b->shader);
      if (initial >= 256) {
         // Texture operands cannot read the high uniform bank.
         EXPECT_EQ(tex->src[1].type, AGX_INDEX_NORMAL);
         EXPECT_EQ(info.rodata.size_16, 0u);
         EXPECT_EQ(info.push_count, initial);
         continue;
      }
      EXPECT_EQ(tex->src[1].type, AGX_INDEX_UNIFORM);
      EXPECT_EQ(info.rodata.base_uniform, (initial+1)&~1u);
      EXPECT_EQ(info.rodata.size_16, 2u);
      EXPECT_EQ(info.push_count, info.rodata.base_uniform+2u);
      EXPECT_EQ(b->shader->rodata[0], 3u);
      EXPECT_EQ(b->shader->rodata[1], 0u);
   }
}
