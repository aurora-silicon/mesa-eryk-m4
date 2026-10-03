/* Copyright 2026 Aurora Silicon; SPDX-License-Identifier: MIT */
#include "asahi/lib/agx_nir_lower_vbo.h"
#include "asahi/lib/agx_linker.h"
#include "asahi/compiler/agx_compile.h"
#include "asahi/compiler/agx_nir.h"
#include "asahi/lib/agx_abi.h"
#include "compiler/nir/nir_builder.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"VBO FAIL line%d: %s\n",__LINE__,#x); abort(); } } while (0)
static void
check_sparse_vertex_locations(void)
{
   struct agx_velem_key api[32] = {0}, compact[AGX_MAX_ATTRIBS];
   const unsigned locations[] = {0, 3, 4, 9, 14, 15, 16, 31};
   uint32_t read = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(locations); ++i) {
      api[locations[i]] = (struct agx_velem_key){
         .format = PIPE_FORMAT_R32_UINT, .stride = 4 * i,
         .instanced = i == 6, .divisor = 1,
      };
      read |= 1u << locations[i];
   }
   /* An unused declaration must not overwrite its following live slot. */
   api[30] = (struct agx_velem_key){.format=PIPE_FORMAT_R8_UNORM,.stride=999};
   CHECK(agx_compact_velem_keys(compact, ARRAY_SIZE(compact), api, 32, read));
   for (unsigned i = 0; i < ARRAY_SIZE(locations); ++i) {
      CHECK(compact[i].format == PIPE_FORMAT_R32_UINT);
      CHECK(compact[i].stride == 4 * i);
      CHECK(compact[i].instanced == (i == 6));
   }
   CHECK(compact[8].format == PIPE_FORMAT_NONE);
   CHECK(!agx_compact_velem_keys(compact, 7, api, 32, read));
   CHECK(!agx_compact_velem_keys(compact, ARRAY_SIZE(compact), api, 16, read));
   CHECK(!agx_compact_velem_keys(compact, ARRAY_SIZE(compact), api, 32, 1u << 2));
}

static const nir_shader_compiler_options options = {0};
static void check_format(enum pipe_format format, unsigned stride, unsigned count,
                         unsigned words, const uint32_t raw[2], const float expected[4])
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, &options,
                                                   "native raw VBO conversion");
   nir_def *input = nir_load_input(&b, 4, 32, nir_imm_int(&b, 0), .base=0,
                                    .dest_type=nir_type_float32);
   nir_store_output(&b, input, nir_imm_int(&b, 0), .base=0, .write_mask=15,
                      .src_type=nir_type_float32);
   const struct agx_velem_key attrs[] = {{.format=format,.stride=stride}};
   CHECK(agx_nir_lower_vbo_raw(b.shader, attrs,
                               (struct agx_robustness){.soft_fault=true},false));
   unsigned loads = 0;
   nir_intrinsic_instr *output = NULL;
   nir_foreach_function_impl(impl,b.shader) {
      nir_foreach_block(block,impl) {
         nir_foreach_instr_safe(instr,block) {
            if(instr->type!=nir_instr_type_intrinsic)continue;
            nir_intrinsic_instr *intr=nir_instr_as_intrinsic(instr);
            if(intr->intrinsic==nir_intrinsic_store_output)output=intr;
            if(intr->intrinsic!=nir_intrinsic_load_constant_agx)continue;
            loads++;
            CHECK(intr->def.num_components==words && intr->def.bit_size==32);
            CHECK((nir_intrinsic_base(intr)>>AGX_NIR_MEMORY_PACKED_COUNT_SHIFT)==count);
            b.cursor=nir_before_instr(instr);
            nir_def *packed=nir_imm_int(&b,raw[0]);
            if(words==2)packed=nir_vec2(&b,packed,nir_imm_int(&b,raw[1]));
            nir_def_rewrite_uses(&intr->def,packed);
         }
      }
   }
   CHECK(loads==1 && output);
   bool changed;
   do {changed=false;NIR_PASS(changed,b.shader,nir_opt_constant_folding);NIR_PASS(changed,b.shader,nir_opt_copy_prop);}while(changed);
   CHECK(nir_src_is_const(output->src[0]));
   const nir_const_value *value=nir_src_as_const_value(output->src[0]);
   for(unsigned c=0;c<4;c++)CHECK(fabsf(value[c].f32-expected[c])<0.000001f);
   ralloc_free(b.shader);
}
static void check_prolog(bool native_raw, bool hardware)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, &options,
                                                 "packed vertex prolog");
   struct agx_vs_prolog_key key = {.hw = hardware};
   key.attribs[0] = (struct agx_velem_key){.format = PIPE_FORMAT_R8G8B8A8_UNORM,
                                         .stride = 5};
   for (unsigned c = 0; c < 4; ++c) BITSET_SET(key.component_mask, c);
   if (native_raw) agx_nir_vs_prolog_raw(&b, &key);
   else agx_nir_vs_prolog(&b, &key);
   unsigned loads = 0;
   nir_foreach_function_impl(impl, b.shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic) continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            /* Image writes populate uniform halves 0..15. Neither the
             * hardware nor software vertex prolog may read parameters there. */
            if (native_raw && intr->intrinsic == nir_intrinsic_load_preamble)
               CHECK(nir_intrinsic_base(intr) >= AGX_ABI_VUNI_NATIVE_PREFIX);
            if (intr->intrinsic != nir_intrinsic_load_constant_agx) continue;
            ++loads;
            CHECK(nir_intrinsic_format(intr) ==
                  (native_raw ? PIPE_FORMAT_R8_UINT : PIPE_FORMAT_R8_UNORM));
            CHECK(intr->def.num_components == (native_raw ? 1 : 4));
            if (native_raw)
               CHECK((nir_intrinsic_base(intr) >> AGX_NIR_MEMORY_PACKED_COUNT_SHIFT) == 4);
         }
      }
   }
   CHECK(loads == 1);
   ralloc_free(b.shader);
}

static void
check_norm8_swizzle(enum pipe_format format, unsigned count,
                    const float decoded[4], const float expected[4], bool oob)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, &options, "native norm8 VBO swizzle and robustness");
   nir_def *input = nir_load_input(&b, 4, 32, nir_imm_int(&b, 0), .base = 0,
                                   .dest_type = nir_type_float32);
   nir_store_output(&b, input, nir_imm_int(&b, 0), .base = 0,
                    .write_mask = 15, .src_type = nir_type_float32);
   const struct agx_velem_key attrs[] = {{.format = format, .stride = 5}};
   CHECK(agx_nir_lower_vbo_raw_norm8(
      b.shader, attrs,
      (struct agx_robustness){.level = AGX_ROBUSTNESS_D3D}, false));
   unsigned loads = 0;
   nir_intrinsic_instr *output = NULL;
   nir_foreach_function_impl(impl, b.shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic) continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            nir_def *replacement = NULL;
            b.cursor = nir_before_instr(instr);
            switch (intr->intrinsic) {
            case nir_intrinsic_load_constant_agx: {
               ++loads;
               CHECK(intr->def.num_components == count && intr->def.bit_size == 32);
               CHECK((nir_intrinsic_base(intr) >> AGX_NIR_MEMORY_PACKED_COUNT_SHIFT) == count);
               CHECK(nir_intrinsic_format(intr) ==
                     (format == PIPE_FORMAT_R8G8B8A8_SNORM ? PIPE_FORMAT_R8_SNORM
                                                         : PIPE_FORMAT_R8_UNORM));
               nir_def *values[4];
               for (unsigned i = 0; i < count; ++i)
                  values[i] = nir_imm_float(&b, decoded[i]);
               replacement = nir_vec(&b, values, count);
               break;
            }
            case nir_intrinsic_load_vertex_id:
               replacement = nir_imm_int(&b, oob ? 2 : 0);
               break;
            case nir_intrinsic_load_attrib_clamp_agx:
               replacement = nir_imm_int(&b, 1);
               break;
            case nir_intrinsic_store_output:
               output = intr;
               break;
            default:
               break;
            }
            if (replacement) nir_def_rewrite_uses(&intr->def, replacement);
         }
      }
   }
   CHECK(loads == 1 && output);
   bool changed;
   do {
      changed = false;
      NIR_PASS(changed, b.shader, nir_opt_constant_folding);
      NIR_PASS(changed, b.shader, nir_opt_copy_prop);
   } while (changed);
   CHECK(nir_src_is_const(output->src[0]));
   const nir_const_value *value = nir_src_as_const_value(output->src[0]);
   for (unsigned c = 0; c < 4; ++c)
      CHECK(value[c].f32 == expected[c]);
   ralloc_free(b.shader);
}

static void
check_instance_divisor_lowering(uint32_t divisor, uint32_t instance,
                                uint32_t base_instance,
                                uint32_t expected_index)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, &options, "G17P instance divisor lowering");
   nir_def *input = nir_load_input(&b, 1, 32, nir_imm_int(&b, 0), .base = 0,
                                   .dest_type = nir_type_uint32);
   nir_store_output(&b, input, nir_imm_int(&b, 0), .base = 0, .write_mask = 1,
                    .src_type = nir_type_uint32);

   const struct agx_velem_key attrs[] = {{
      .divisor = divisor,
      .stride = 12,
      .format = PIPE_FORMAT_R32_UINT,
      .instanced = true,
   }};
   CHECK(agx_nir_lower_vbo_raw(
      b.shader, attrs, (struct agx_robustness){.soft_fault = true}, false));

   nir_foreach_function_impl(impl, b.shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_load_instance_id &&
                intr->intrinsic != nir_intrinsic_load_base_instance)
               continue;

            b.cursor = nir_before_instr(instr);
            nir_def *replacement =
               nir_imm_int(&b, intr->intrinsic == nir_intrinsic_load_instance_id
                                  ? instance
                                  : base_instance);
            nir_def_rewrite_uses(&intr->def, replacement);
         }
      }
   }

   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, b.shader, nir_opt_constant_folding);
      NIR_PASS(progress, b.shader, nir_opt_copy_prop);
      NIR_PASS(progress, b.shader, nir_opt_dce);
   } while (progress);

   unsigned loads = 0;
   nir_foreach_function_impl(impl, b.shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_load_constant_agx)
               continue;

            ++loads;
            CHECK(nir_src_is_const(intr->src[1]));
            CHECK(nir_src_as_uint(intr->src[1]) == expected_index);
         }
      }
   }

   CHECK(loads == 1);
   ralloc_free(b.shader);
}

static void
check_instance_divisor_compiles(uint32_t divisor)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, &agx_nir_options, "G17P instance divisor prolog");
   struct agx_vs_prolog_key prolog = {
      .hw = true,
      .robustness = {.soft_fault = true},
   };
   prolog.attribs[0] = (struct agx_velem_key){
      .divisor = divisor,
      .stride = 12,
      .format = PIPE_FORMAT_R32_UINT,
      .instanced = true,
   };
   BITSET_SET(prolog.component_mask, 0);
   agx_nir_vs_prolog_raw(&b, &prolog);

   struct agx_shader_key key = {
      .dev.encoding = AGX_ISA_ENCODING_AGX3B,
      .secondary = true,
      .no_stop = true,
   };
   struct agx_shader_part part = {0};
   bool compiled = agx_compile_shader_nir(b.shader, &key, &part);
   if (!compiled)
      fprintf(stderr, "divisor %u failed G17P prolog compilation\n", divisor);
   CHECK(compiled);
   CHECK(part.binary != NULL && part.info.binary_size > 4);

   bool found_instance_id = false;
   const uint8_t *binary = part.binary;
   for (unsigned i = 0; i + 4 <= part.info.binary_size; ++i) {
      if ((binary[i] & 0x7) == 0x4 && binary[i + 1] == 0xd8 &&
          binary[i + 2] == 0x10 && (binary[i + 3] & 0x1f) == 0x06) {
         found_instance_id = true;
         break;
      }
   }
   CHECK(found_instance_id);

   free(part.binary);
   ralloc_free(b.shader);
}
int main(void)
{
   glsl_type_singleton_init_or_ref();
   check_prolog(true, true);
   check_prolog(true, false);
   check_prolog(false, true);
   check_prolog(false, false);
   for (unsigned v = 0; v < 256; ++v) {
      const float u[] = {v / 255.f, 64 / 255.f, 128 / 255.f, 1};
      const float swapped[] = {u[2], u[1], u[0], u[3]};
      const float s[] = {MAX2(-1.f, (int8_t)v / 127.f), -1 / 127.f, 0, 1};
      check_norm8_swizzle(PIPE_FORMAT_R8G8B8A8_UNORM, 4, u, u, false);
      check_norm8_swizzle(PIPE_FORMAT_B8G8R8A8_UNORM, 4, u, swapped, false);
      check_norm8_swizzle(PIPE_FORMAT_R8G8B8A8_SNORM, 4, s, s, false);
   }
   for (bool oob = false;; oob = true) {
      const float u[] = {.25f, .5f, .75f, 1};
      check_norm8_swizzle(PIPE_FORMAT_R8_UNORM, 1, u,
                         (float[]){oob ? 0 : .25f, 0, 0, 1}, oob);
      check_norm8_swizzle(PIPE_FORMAT_R8G8_UNORM, 2, u,
                         (float[]){oob ? 0 : .25f, oob ? 0 : .5f, 0, 1}, oob);
      check_norm8_swizzle(PIPE_FORMAT_R8G8B8_UNORM, 3, u,
                         (float[]){oob ? 0 : .25f, oob ? 0 : .5f,
                                   oob ? 0 : .75f, 1}, oob);
      if (oob) break;
   }
   check_format(PIPE_FORMAT_R8G8B8A8_UNORM,5,4,1,(uint32_t[]){0xff804020,0},(float[]){32.f/255,64.f/255,128.f/255,1});
   check_format(PIPE_FORMAT_B8G8R8A8_UNORM,5,4,1,(uint32_t[]){0xff804020,0},(float[]){128.f/255,64.f/255,32.f/255,1});
   check_format(PIPE_FORMAT_R8G8B8A8_SNORM,5,4,1,(uint32_t[]){0x7f00ff80,0},(float[]){-1,-1.f/127,0,1});
   check_format(PIPE_FORMAT_R16G16B16A16_UNORM,10,4,2,(uint32_t[]){0x80000000,0xffff4000},(float[]){0,32768.f/65535,16384.f/65535,1});
   check_format(PIPE_FORMAT_R16G16_FLOAT,6,2,1,(uint32_t[]){0x40003c00,0},(float[]){1,2,0,1});
   check_format(PIPE_FORMAT_R10G10B10A2_UNORM,4,1,1,(uint32_t[]){0xc00803ff,0},(float[]){1,512.f/1023,0,1});
   check_instance_divisor_lowering(0, UINT32_MAX, 5, 15);
   check_instance_divisor_lowering(1, 10, 5, 45);
   check_instance_divisor_lowering(2, 7, 5, 24);
   check_instance_divisor_lowering(3, 2, 5, 15);
   check_instance_divisor_lowering(3, 3, 5, 18);
   check_instance_divisor_lowering(3, 10, 5, 24);
   check_instance_divisor_lowering(UINT32_MAX, UINT32_MAX - 1, 0, 0);
   check_instance_divisor_lowering(UINT32_MAX, UINT32_MAX, 0, 3);
   check_instance_divisor_compiles(0);
   check_instance_divisor_compiles(1);
   check_instance_divisor_compiles(2);
   check_instance_divisor_compiles(3);
   check_instance_divisor_compiles(UINT32_MAX);
   check_sparse_vertex_locations();
   glsl_type_singleton_decref();
   puts("Native packed VBO: formats, norm8 swizzles/bounds and instance divisors PASS");
   return 0;
}
