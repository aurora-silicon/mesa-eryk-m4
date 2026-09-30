/* SPDX-License-Identifier: MIT */
#include "hk_nir_preserve_sample_loops.h"
#include "asahi/compiler/agx_compile.h"
#include "asahi/compiler/agx_nir.h"
#include "nir_builder.h"
#include "nir_serialize.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %u: %s\n", __LINE__, #x); abort(); } } while (0)

static unsigned
textures(nir_shader *s)
{
   unsigned count = 0;
   nir_foreach_function_impl(impl, s)
      nir_foreach_block(block, impl)
         nir_foreach_instr(instr, block)
            count += instr->type == nir_instr_type_tex;
   return count;
}

static void
loop(nir_builder *b, bool sample, unsigned depth, unsigned iterations)
{
   nir_def *zero = nir_imm_int(b, 0);
   nir_def *limit = iterations ? nir_imm_int(b, iterations) :
      nir_load_uniform(b, 1, 32, zero, .range = 4);
   nir_def *one = nir_imm_int(b, 1);
   nir_loop *cf = nir_push_loop(b);
   cf->control = nir_loop_control_unroll;
   nir_block *before = nir_cf_node_as_block(nir_cf_node_prev(&cf->cf_node));
   nir_block *head = nir_loop_first_block(cf);
   nir_phi_instr *phi = nir_phi_instr_create(b->shader);
   nir_def_init(&phi->instr, &phi->def, 1, 32);
   nir_phi_instr_add_src(phi, before, zero);
   nir_break_if(b, nir_ige(b, &phi->def, limit));
   if (depth) {
      loop(b, sample, depth - 1, iterations);
   } else {
      nir_def *value = &phi->def;
      if (sample) {
         nir_def *coord = nir_vec2(b, nir_u2f32(b, value), nir_imm_float(b, .5));
         nir_tex_instr *tex = nir_tex_instr_create(b->shader, 2);
         tex->op = nir_texop_txl;
         tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
         tex->dest_type = nir_type_float32;
         tex->coord_components = 2;
         tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, coord);
         tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_float(b, 0));
         nir_def_init(&tex->instr, &tex->def, 4, 32);
         nir_builder_instr_insert(b, &tex->instr);
         value = &tex->def;
      }
      nir_store_global(b, value, nir_imm_int64(b, 0x1000), .align_mul = 4);
   }
   nir_def *next = nir_iadd(b, &phi->def, one);
   nir_phi_instr_add_src(phi, nir_cursor_current_block(b->cursor), next);
   nir_pop_loop(b, cf);
   b->cursor = nir_after_phis(head);
   nir_builder_instr_insert(b, &phi->instr);
   b->cursor = nir_after_cf_node(&cf->cf_node);
}

static void
test(bool sample, unsigned depth, bool preserve)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                                 &agx_nir_options, "sample loop cost");
   loop(&b, sample, depth, 4);
   nir_validate_shader(b.shader, "before preservation");
   if (preserve) {
      bool changed = false;
      NIR_PASS(changed, b.shader, hk_nir_preserve_sample_loops);
      CHECK(changed == sample);
      changed = false;
      NIR_PASS(changed, b.shader, hk_nir_preserve_sample_loops);
      CHECK(!changed);
   }
   bool unrolled = false;
   while (nir_opt_loop_unroll(b.shader))
      unrolled = true;
   CHECK(unrolled == (!preserve || !sample));
   CHECK(textures(b.shader) == (sample ? (preserve ? 1 : (depth ? 16 : 4)) : 0));
   nir_validate_shader(b.shader, "after unrolling");
   ralloc_free(b.shader);
}

static void
test_release(unsigned iterations, unsigned depth, bool expected)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                                 &agx_nir_options, "late short loop");
   loop(&b, true, depth, iterations);
   nir_validate_shader(b.shader, "before short loop release");
   CHECK(hk_nir_preserve_sample_loops(b.shader));
   CHECK(hk_nir_release_short_sample_loops(b.shader) == expected);
   CHECK(!hk_nir_release_short_sample_loops(b.shader));
   bool unrolled = false;
   while (nir_opt_loop_unroll(b.shader))
      unrolled = true;
   CHECK(unrolled == expected);
   CHECK(textures(b.shader) == (expected ? iterations : 1));
   nir_validate_shader(b.shader, "after short loop release");
   ralloc_free(b.shader);
}

static unsigned
preprocess(nir_shader *s, bool preserve)
{
   if (preserve) {
      bool changed = false;
      NIR_PASS(changed, s, hk_nir_preserve_sample_loops);
      CHECK(changed);
   }
   bool progress;
   NIR_PASS(_, s, nir_lower_global_vars_to_local);
   do {
      progress = false;
      NIR_PASS(progress, s, nir_lower_vars_to_ssa);
      NIR_PASS(progress, s, nir_opt_copy_prop);
      NIR_PASS(progress, s, nir_opt_dce);
      NIR_PASS(progress, s, nir_opt_constant_folding);
      NIR_PASS(progress, s, nir_opt_loop);
      NIR_PASS(progress, s, nir_opt_loop_unroll);
   } while (progress);
   agx_preprocess_nir_g17p(s);
   return textures(s);
}

int main(int argc, char **argv)
{
   glsl_type_singleton_init_or_ref();
   for (unsigned sample = 0; sample < 2; ++sample)
      for (unsigned depth = 0; depth < 2; ++depth)
         for (unsigned preserve = 0; preserve < 2; ++preserve)
            test(sample, depth, preserve);
   test_release(4, 0, true);
   test_release(16, 0, false);
   test_release(0, 0, false);
   test_release(4, 1, false);
   if (argc == 2) {
      /* Trusted developer capture from HK_NIR_CAPTURE_DIR; same Mesa ABI. */
      FILE *file = fopen(argv[1], "rb");
      CHECK(file);
      fseek(file, 0, SEEK_END);
      long size = ftell(file);
      CHECK(size > 0 && size < 32 * 1024 * 1024);
      rewind(file);
      void *data = malloc(size);
      CHECK(data && fread(data, 1, size, file) == (size_t)size);
      fclose(file);
      struct blob_reader reader;
      blob_reader_init(&reader, data, size);
      nir_shader *nir = nir_deserialize(NULL, &agx_nir_options, &reader);
      CHECK(nir && !reader.overrun && reader.current == reader.end);
      nir_shader *copy = nir_shader_clone(NULL, nir);
      unsigned before = preprocess(copy, false), after = preprocess(nir, true);
      printf("Captured shader texture instructions: early unroll=%u preserved=%u\n", before, after);
      CHECK(before > after && after > 0);
      ralloc_free(copy);
      ralloc_free(nir);
      free(data);
   }
   glsl_type_singleton_decref();
   puts("PASS: sampled and nested loops remain bounded; unrelated loops still unroll");
   return 0;
}
