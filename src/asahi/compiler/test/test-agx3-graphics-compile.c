/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

/*
 * End-to-end compile coverage for the AGX3 graphics stages.
 *
 * The AGX3 compute paths are covered by test-agx3-compile-fail-closed.c. This
 * file is the vertex/fragment counterpart, starting from a flat-shaded
 * triangle -- the smallest program that is still a real draw.
 *
 * The NIR here is written at the *post-lowering* level, i.e. what the backend
 * actually sees. agx_nir_lower_uvs() and agx_nir_lower_tilebuffer() live in
 * src/asahi/lib, which is built after the compiler and therefore cannot be
 * linked from a compiler test; store_uvs_agx / store_local_pixel_agx below are
 * exactly the intrinsics those two passes emit (agx_nir_lower_uvs.c:108,
 * agx_nir_lower_tilebuffer.c:75).
 *
 * Cases marked "expect reject" are gaps that remain fail-closed in
 * agx3_pack_binary(). They are asserted as rejections so that grounding one
 * later is a deliberate change to this table, not a silent behaviour drift.
 */

#include "agx_compile.h"
#include "agx_nir.h"
#include "nir_builder.h"
#include "util/format/u_formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t agx3_end[] = {0x0e, 0x00, 0x00, 0x00};

/* AIL_ISA_FORMAT_U8NORM: the tilebuffer format for an RGBA8_UNORM target. */
#define AGX3_TEST_TIB_FORMAT PIPE_FORMAT_R8_UNORM

static const char *
enc_name(enum agx_isa_encoding enc)
{
   return enc == AGX_ISA_ENCODING_AGX3B ? "AGX3b" : "AGX3c";
}

/*
 * Minimal vertex shader: gl_Position = vec4(float(gl_VertexID), 0, 0, 1),
 * written to the UVS the way agx_nir_lower_uvs lowers a store_output of
 * VARYING_SLOT_POS with a single-varying layout (base index 0).
 */
static nir_shader *
build_vs_flat(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, &agx_nir_options, "AGX3 flat triangle VS");

   nir_def *id = nir_u2f32(&b, nir_load_vertex_id(&b));
   nir_def *pos[4] = {
      id,
      nir_imm_float(&b, 0.0f),
      nir_imm_float(&b, 0.0f),
      nir_imm_float(&b, 1.0f),
   };

   for (unsigned c = 0; c < 4; ++c)
      nir_store_uvs_agx(&b, pos[c], nir_imm_intN_t(&b, c, 16));

   b.shader->info.outputs_written = VARYING_BIT_POS;
   agx_preprocess_nir(b.shader);
   return b.shader;
}

/*
 * Vertex shader with a real attribute fetch: agx_nir_lower_vbo turns
 * load_input into the load_agx used here, so this is the shape a DRAW with a
 * vertex buffer actually reaches the backend with.
 */
static nir_shader *
build_vs_attribute(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, &agx_nir_options, "AGX3 attribute VS");

   nir_def *base = nir_imm_int64(&b, 0x1000);
   nir_def *index = nir_load_vertex_id(&b);
   nir_def *attr = nir_load_agx(&b, 4, 32, base, index, .access = 0, .base = 0,
                                .format = PIPE_FORMAT_R32_UINT,
                                .sign_extend = false);

   for (unsigned c = 0; c < 4; ++c) {
      nir_store_uvs_agx(&b, nir_channel(&b, attr, c),
                        nir_imm_intN_t(&b, c, 16));
   }

   b.shader->info.outputs_written = VARYING_BIT_POS;
   agx_preprocess_nir(b.shader);
   return b.shader;
}

/*
 * The FS lowering passes introduce driver-owned system values that every
 * driver resolves itself (d12_dxil_lower_root_mask / hk_lower_uvs_index and
 * friends). Resolve them to the monolithic-draw constants so the test
 * exercises the backend rather than a driver ABI.
 */
static bool
lower_driver_sysvals(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *_)
{
   uint16_t value;

   switch (intr->intrinsic) {
   case nir_intrinsic_load_api_sample_mask_agx:
      value = 0xFF;
      break;
   case nir_intrinsic_load_shader_part_tests_zs_agx:
      value = 0;
      break;
   case nir_intrinsic_load_active_samples_agx:
      value = 0xFF;
      break;
   default:
      return false;
   }

   b->cursor = nir_before_instr(&intr->instr);
   nir_def_replace(&intr->def, nir_imm_intN_t(b, value, 16));
   return true;
}

static void
fs_lower(nir_shader *s)
{
   NIR_PASS(_, s, agx_nir_lower_discard_zs_emit);
   NIR_PASS(_, s, agx_nir_lower_sample_mask);
   NIR_PASS(_, s, agx_nir_lower_interpolation);
   NIR_PASS(_, s, nir_shader_intrinsics_pass, lower_driver_sysvals,
            nir_metadata_control_flow, NULL);
}

static void
fs_store_colour(nir_builder *b, nir_def *colour)
{
   nir_store_local_pixel_agx(b, colour, nir_imm_intN_t(b, 0xFF, 16),
                             nir_undef(b, 2, 16), .base = 0, .write_mask = 0xf,
                             .format = AGX3_TEST_TIB_FORMAT);
}

static nir_shader *
fs_finish(nir_builder *b)
{
   b->shader->info.outputs_written = BITFIELD64_BIT(FRAG_RESULT_DATA0);
   agx_preprocess_nir(b->shader);

   fs_lower(b->shader);
   return b->shader;
}

/*
 * Minimal fragment shader: colour = vec4(0.25, 0.5, 0.75, 1.0) stored to
 * render target 0, as agx_nir_lower_tilebuffer lowers a store_output of
 * FRAG_RESULT_DATA0 into a single sample-replicating store_local_pixel_agx.
 */
static nir_shader *
build_fs_flat(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "AGX3 flat triangle FS");

   fs_store_colour(&b, nir_vec4(&b, nir_imm_float(&b, 0.25f),
                                nir_imm_float(&b, 0.5f),
                                nir_imm_float(&b, 0.75f),
                                nir_imm_float(&b, 1.0f)));
   return fs_finish(&b);
}

/* Fragment shader reading a perspective-interpolated vec4 varying. */
static nir_shader *
build_fs_interpolated(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "AGX3 interpolated FS");

   nir_def *bary = nir_load_barycentric_pixel(
      &b, 32, .interp_mode = INTERP_MODE_SMOOTH);
   nir_def *colour = nir_load_interpolated_input(
      &b, 4, 32, bary, nir_imm_int(&b, 0), .base = 0, .component = 0,
      .io_semantics.location = VARYING_SLOT_VAR0, .io_semantics.num_slots = 1);

   fs_store_colour(&b, colour);
   b.shader->info.inputs_read = BITFIELD64_BIT(VARYING_SLOT_VAR0);
   return fs_finish(&b);
}

/* Fragment shader with a discard: lowered to a sample_mask write. */
static nir_shader *
build_fs_discard(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "AGX3 discard FS");

   nir_demote(&b);
   b.shader->info.fs.uses_discard = true;
   fs_store_colour(&b, nir_vec4(&b, nir_imm_float(&b, 0.25f),
                                nir_imm_float(&b, 0.5f),
                                nir_imm_float(&b, 0.75f),
                                nir_imm_float(&b, 1.0f)));
   return fs_finish(&b);
}

/* Fragment shader writing gl_FragDepth: lowered to zs_emit. */
static nir_shader *
build_fs_depth(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "AGX3 depth-write FS");

   fs_store_colour(&b, nir_vec4(&b, nir_imm_float(&b, 0.25f),
                                nir_imm_float(&b, 0.5f),
                                nir_imm_float(&b, 0.75f),
                                nir_imm_float(&b, 1.0f)));
   nir_store_output(&b, nir_imm_float(&b, 0.5f), nir_imm_int(&b, 0),
                    .base = 0, .write_mask = 0x1, .src_type = nir_type_float32,
                    .io_semantics.location = FRAG_RESULT_DEPTH,
                    .io_semantics.num_slots = 1);

   b.shader->info.outputs_written = BITFIELD64_BIT(FRAG_RESULT_DATA0) |
                                    BITFIELD64_BIT(FRAG_RESULT_DEPTH);
   agx_preprocess_nir(b.shader);
   fs_lower(b.shader);
   return b.shader;
}

/*
 * Fragment shader that reads the tilebuffer before writing it -- the shape a
 * blend or a framebuffer fetch takes, and the shape Apple's own `w_pass8`
 * probe uses to pair a tile load's scoreboard slot with the store that
 * consumes it.
 */
static nir_shader *
build_fs_tilebuffer_load(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "AGX3 tilebuffer-load FS");

   nir_def *old = nir_load_local_pixel_agx(
      &b, 4, 32, nir_imm_intN_t(&b, 0xFF, 16), .base = 0,
      .format = AGX3_TEST_TIB_FORMAT);
   fs_store_colour(&b, old);
   return fs_finish(&b);
}

/* Fragment shader sampling a bindless 2D texture. */
static nir_shader *
build_fs_texture(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "AGX3 textured FS");

   nir_def *bary = nir_load_barycentric_pixel(
      &b, 32, .interp_mode = INTERP_MODE_SMOOTH);
   nir_def *uv = nir_load_interpolated_input(
      &b, 2, 32, bary, nir_imm_int(&b, 0), .base = 0, .component = 0,
      .io_semantics.location = VARYING_SLOT_VAR0, .io_semantics.num_slots = 1);

   nir_def *timage = nir_bindless_image_agx(&b, nir_imm_int(&b, 0),
                                            .desc_set = 0);
   nir_def *tsampler = nir_bindless_sampler_agx(&b, nir_imm_int(&b, 0),
                                                nir_imm_intN_t(&b, 0, 16),
                                                .desc_set = 1);

   nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
   tex->op = nir_texop_tex;
   tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   tex->dest_type = nir_type_float32;
   tex->coord_components = 2;
   tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, uv);
   tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_texture_handle, timage);
   tex->src[2] = nir_tex_src_for_ssa(nir_tex_src_sampler_handle, tsampler);
   nir_def_init(&tex->instr, &tex->def, 4, 32);
   nir_builder_instr_insert(&b, &tex->instr);

   fs_store_colour(&b, &tex->def);

   b.shader->info.inputs_read = BITFIELD64_BIT(VARYING_SLOT_VAR0);
   b.shader->info.outputs_written = BITFIELD64_BIT(FRAG_RESULT_DATA0);

   NIR_PASS(_, b.shader, agx_nir_lower_texture_early, false);
   NIR_PASS(_, b.shader, agx_nir_lower_texture);
   agx_preprocess_nir(b.shader);
   fs_lower(b.shader);
   NIR_PASS(_, b.shader, agx_nir_lower_texture);
   return b.shader;
}

/*
 * Fragment shader sampling texture 0 through sampler 0 by immediate index
 * with implicit LOD -- a textured triangle, the exact shape the smp sampler
 * form is grounded for (fragment auto-LOD witnesses ts_auto / fs_bufcoord).
 */
static nir_shader *
build_fs_texture_imm(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "AGX3 textured-triangle FS");

   nir_def *bary = nir_load_barycentric_pixel(
      &b, 32, .interp_mode = INTERP_MODE_SMOOTH);
   nir_def *uv = nir_load_interpolated_input(
      &b, 2, 32, bary, nir_imm_int(&b, 0), .base = 0, .component = 0,
      .io_semantics.location = VARYING_SLOT_VAR0, .io_semantics.num_slots = 1);

   nir_tex_instr *tex = nir_tex_instr_create(b.shader, 1);
   tex->op = nir_texop_tex;
   tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   tex->dest_type = nir_type_float32;
   tex->coord_components = 2;
   tex->texture_index = 0;
   tex->sampler_index = 0;
   tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_coord, uv);
   nir_def_init(&tex->instr, &tex->def, 4, 32);
   nir_builder_instr_insert(&b, &tex->instr);

   fs_store_colour(&b, &tex->def);

   b.shader->info.inputs_read = BITFIELD64_BIT(VARYING_SLOT_VAR0);
   b.shader->info.outputs_written = BITFIELD64_BIT(FRAG_RESULT_DATA0);

   NIR_PASS(_, b.shader, agx_nir_lower_texture_early, false);
   NIR_PASS(_, b.shader, agx_nir_lower_texture);
   agx_preprocess_nir(b.shader);
   fs_lower(b.shader);
   NIR_PASS(_, b.shader, agx_nir_lower_texture);
   return b.shader;
}

/*
 * Fragment shader exercising the funop family: floor/exp2/log2 (f32rint and
 * friends) plus the fragment-only gradient dfdx.
 */
static nir_shader *
build_fs_funops(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "AGX3 funop FS");

   nir_def *bary = nir_load_barycentric_pixel(
      &b, 32, .interp_mode = INTERP_MODE_SMOOTH);
   nir_def *v = nir_load_interpolated_input(
      &b, 2, 32, bary, nir_imm_int(&b, 0), .base = 0, .component = 0,
      .io_semantics.location = VARYING_SLOT_VAR0, .io_semantics.num_slots = 1);

   nir_def *x = nir_channel(&b, v, 0);
   nir_def *y = nir_channel(&b, v, 1);

   fs_store_colour(&b, nir_vec4(&b, nir_ffloor(&b, x), nir_fexp2(&b, y),
                                nir_flog2(&b, nir_fabs(&b, x)),
                                nir_ddx(&b, y)));
   b.shader->info.inputs_read = BITFIELD64_BIT(VARYING_SLOT_VAR0);
   return fs_finish(&b);
}

struct agx3_graphics_case {
   const char *name;
   nir_shader *(*build)(void);
   bool expect_ok;
};

static const struct agx3_graphics_case cases[] = {
   {"vs-flat-triangle", build_vs_flat, true},
   {"vs-attribute-fetch", build_vs_attribute, true},
   {"fs-flat-colour", build_fs_flat, true},
   {"fs-interpolated-varying", build_fs_interpolated, true},
   {"fs-tilebuffer-load", build_fs_tilebuffer_load, true},
   {"fs-textured-triangle", build_fs_texture_imm, true},
   {"fs-transcendentals", build_fs_funops, true},

   /*
    * Deliberate refusals. Each one names a specific missing witness, not a
    * missing line of routing:
    *
    * - discard lowers to sample_mask. Apple's discard and [[sample_mask]]
    *   shaders both go through movmsk under the class-1 pixwait/pixrel
    *   bracket with a value staged by a copy (never cmsk, which only
    *   alpha-to-coverage emits), and movmsk's operand roles plus the staging
    *   ABI are not isolated. Mesa's mask+value operand split has no
    *   witnessed encoding either way.
    *
    * - a depth write lowers to zs_emit, whose counterpart is depthf. depthf's
    *   enable bits are grounded, but it carries no value register: every
    *   witness stages the value through a copy whose destination field is 0,
    *   behind an un-decomposed iadd/fsel cluster that may be a required
    *   format transform, so the staging ABI is a correlation rather than a
    *   traced contract (see agx3_pack_binary.c).
    *
    * - the *bindless* texture sample stays refused: the sampler form is
    *   grounded for immediate texture/sampler state only (fs-textured-triangle
    *   above), the bindless heap operand selects a different encoding branch
    *   with no in-domain witness, and this shader also reaches an ALU op with
    *   a uniform source, which the AGX3 selection core refuses independently.
    */
   {"fs-discard", build_fs_discard, false},
   {"fs-depth-write", build_fs_depth, false},
   {"fs-texture-sample-bindless", build_fs_texture, false},
};

static bool
run_case(const struct agx3_graphics_case *c, enum agx_isa_encoding enc)
{
   nir_shader *nir = c->build();

   struct agx_shader_key key = {0};
   key.dev.encoding = enc;

   struct agx_shader_part part;
   memset(&part, 0xa5, sizeof(part));

   const bool ok = agx_compile_shader_nir(nir, &key, &part);
   bool result;

   if (!ok) {
      const uint8_t zero[sizeof(part)] = {0};
      if (memcmp(&part, zero, sizeof(part)) != 0) {
         fprintf(stderr, "%s %s: rejection did not clear compiler output\n",
                 enc_name(enc), c->name);
         ralloc_free(nir);
         return false;
      }

      result = !c->expect_ok;
      if (!result)
         fprintf(stderr, "%s %s: REJECTED by agx3_pack_binary\n", enc_name(enc),
                 c->name);
   } else {
      result = c->expect_ok && part.binary != NULL &&
               part.info.binary_size > sizeof(agx3_end) &&
               part.info.main_offset == 0 &&
               memcmp((const uint8_t *)part.binary + part.info.binary_size -
                         sizeof(agx3_end),
                      agx3_end, sizeof(agx3_end)) == 0;

      if (!result) {
         fprintf(stderr, "%s %s: %s\n", enc_name(enc), c->name,
                 c->expect_ok ? "accepted but binary is malformed"
                              : "unexpectedly accepted");
      } else if (getenv("AGX3_DUMP_BINARY")) {
         fprintf(stderr, "%s %s: %u bytes:", enc_name(enc), c->name,
                 part.info.binary_size);
         for (unsigned i = 0; i < part.info.binary_size; ++i)
            fprintf(stderr, " %02x", ((const uint8_t *)part.binary)[i]);
         fprintf(stderr, "\n");
      }

      free(part.binary);
   }

   ralloc_free(nir);
   return result;
}

int
main(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };
   bool all_ok = true;

   for (unsigned e = 0; e < ARRAY_SIZE(encodings); ++e) {
      for (unsigned i = 0; i < ARRAY_SIZE(cases); ++i) {
         if (!run_case(&cases[i], encodings[e]))
            all_ok = false;
      }
   }

   if (!all_ok)
      return 1;

   puts("agx3_graphics: vertex and fragment shaders compile");
   return 0;
}
