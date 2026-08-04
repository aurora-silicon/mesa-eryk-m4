/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Probe-shader generator: NIR in, DXIL out.
 *
 * `dxc` does not run on Asahi/aarch64, so the only DXIL this project had
 * for tests was the vkd3d-proton corpus -- LGPL-derived, and therefore
 * usable as a reference/oracle but never vendored into this MIT tree.
 * That left GPU suites either depending on a corpus the repository cannot
 * carry, or asserting nothing about shaders it could not author.
 *
 * Mesa already contains the other direction: src/microsoft/compiler is
 * NIR -> DXIL, MIT, and in tree. Building probe shaders with nir_builder
 * and running them through it gives clean-room, committable DXIL with no
 * dxc dependency at all.
 *
 * This is a BUILD-TIME tool. Nothing in libd3d12agx links it, and no
 * application shader passes through it.
 *
 * The generated .dxil are committed next to the sources that produced
 * them, so replicating the suites needs neither this tool nor dxc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nir.h"
#include "nir_builder.h"
#include "nir_to_dxil.h"
#include "util/blob.h"

/*
 * gs_input_vs: a vertex shader whose only input is the vertex ID and
 * whose VAR0 is a full float4.
 *
 * This exists because the geometry test needs one and nothing else
 * provides it: of the corpus vertex shaders that write VAR0, the ones
 * with an xyzw write read vertex ATTRIBUTES (which need an
 * attribute-fetch prolog this driver does not have), and the
 * attribute-free ones write only .x. A geometry shader that copies
 * VAR0.xyzw to POSITION therefore had an undefined position, and a test
 * cannot assert on undefined behaviour.
 *
 * Four vertex IDs give four points, one per quadrant, at +-0.5:
 *
 *    id 0 -> (-0.5, -0.5)      id 1 -> (+0.5, -0.5)
 *    id 2 -> (-0.5, +0.5)      id 3 -> (+0.5, +0.5)
 *
 * w is 1.0 so the position is usable as-is after the geometry shader
 * copies it. POSITION is written too, so the same shader is meaningful
 * with and without a geometry shader bound -- which is what makes the
 * control comparison in g6_geometry mean something.
 */
/* One vec4 slot per variable, which is what the DXIL signature expects. */
static int
probe_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static nir_shader *
build_gs_input_vs(const nir_shader_compiler_options *opts)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, opts,
                                                  "gs_input_vs");

   /* nir_to_dxil creates the SV_VertexID input variable itself, but only
    * when system_values_read says the shader wants it, and it only
    * lowers the ZERO_BASE form. Miss either and it asserts on a null
    * variable deep inside dxil_nir.
    */
   BITSET_SET(b.shader->info.system_values_read,
              SYSTEM_VALUE_VERTEX_ID_ZERO_BASE);
   nir_def *id = nir_load_vertex_id_zero_base(&b);

   /* x = (id & 1) ? +0.5 : -0.5 ; y = (id & 2) ? +0.5 : -0.5 */
   nir_def *half = nir_imm_float(&b, 0.5f);
   nir_def *neg_half = nir_imm_float(&b, -0.5f);

   nir_def *x = nir_bcsel(
      &b, nir_ine_imm(&b, nir_iand_imm(&b, id, 1), 0), half, neg_half);
   nir_def *y = nir_bcsel(
      &b, nir_ine_imm(&b, nir_iand_imm(&b, id, 2), 0), half, neg_half);

   nir_def *pos =
      nir_vec4(&b, x, y, nir_imm_float(&b, 0.0f), nir_imm_float(&b, 1.0f));

   nir_variable *var0 = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "var0");
   var0->data.location = VARYING_SLOT_VAR0;
   var0->data.driver_location = 0;
   nir_store_var(&b, var0, pos, 0xf);

   nir_variable *posv = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "gl_Position");
   posv->data.location = VARYING_SLOT_POS;
   posv->data.driver_location = 1;
   nir_store_var(&b, posv, pos, 0xf);

   b.shader->info.outputs_written =
      BITFIELD64_BIT(VARYING_SLOT_VAR0) | BITFIELD64_BIT(VARYING_SLOT_POS);
   return b.shader;
}

/*
 * gs_point_passthrough: points in, points out, VAR0 copied to POSITION.
 *
 * Deliberately the cheapest geometry shader that is still a geometry
 * shader. Static topology and no side effects, so the lowering's main
 * body compiles to almost nothing and the shape is
 * POLY_GS_SHAPE_STATIC_PER_INSTANCE -- the one the runtime dispatches
 * with a plain non-indexed draw and no output index buffer.
 */
static nir_shader *
build_gs_point_passthrough(const nir_shader_compiler_options *opts)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_GEOMETRY, opts,
                                                  "gs_point_passthrough");

   b.shader->info.gs.input_primitive = MESA_PRIM_POINTS;
   b.shader->info.gs.output_primitive = MESA_PRIM_POINTS;
   b.shader->info.gs.vertices_in = 1;
   b.shader->info.gs.vertices_out = 1;
   b.shader->info.gs.invocations = 1;
   b.shader->info.gs.active_stream_mask = 1;

   nir_variable *in = nir_variable_create(
      b.shader, nir_var_shader_in,
      glsl_array_type(glsl_vec4_type(), 1, 0), "in_var0");
   in->data.location = VARYING_SLOT_VAR0;
   in->data.driver_location = 0;

   nir_variable *out = nir_variable_create(b.shader, nir_var_shader_out,
                                           glsl_vec4_type(), "out_pos");
   out->data.location = VARYING_SLOT_POS;
   out->data.driver_location = 0;

   nir_deref_instr *elem =
      nir_build_deref_array_imm(&b, nir_build_deref_var(&b, in), 0);
   nir_store_var(&b, out, nir_load_deref(&b, elem), 0xf);

   nir_emit_vertex(&b, .stream_id = 0);
   nir_end_primitive(&b, .stream_id = 0);

   b.shader->info.inputs_read = BITFIELD64_BIT(VARYING_SLOT_VAR0);
   b.shader->info.outputs_written = BITFIELD64_BIT(VARYING_SLOT_POS);
   return b.shader;
}

int
main(int argc, char **argv)
{
   if (argc < 3) {
      fprintf(stderr, "usage: %s <gs_input_vs|gs_point> out.dxil\n", argv[0]);
      return 2;
   }

   glsl_type_singleton_init_or_ref();

   nir_shader_compiler_options options;
   dxil_get_nir_compiler_options(&options, SHADER_MODEL_6_2, 1 << 5,
                                 1 << 5);

   bool want_gs = !strcmp(argv[1], "gs_point");
   nir_shader *nir = want_gs ? build_gs_point_passthrough(&options)
                             : build_gs_input_vs(&options);

   NIR_PASS(_, nir, nir_lower_variable_initializers, nir_var_all);
   NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries,
            nir_shader_get_entrypoint(nir), nir_var_shader_out);
   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_lower_io,
            nir_var_shader_out | (want_gs ? nir_var_shader_in : 0),
            probe_type_size, 0);
   NIR_PASS(_, nir, nir_opt_dce);

   struct nir_to_dxil_options opts = {
      .environment = DXIL_ENVIRONMENT_VULKAN,
      .shader_model_max = SHADER_MODEL_6_2,
      .validator_version_max = DXIL_VALIDATOR_1_6,
   };

   struct blob blob;
   blob_init(&blob);
   if (!nir_to_dxil(nir, &opts, NULL, &blob)) {
      fprintf(stderr, "nir_to_dxil failed\n");
      return 1;
   }

   FILE *f = fopen(argv[2], "wb");
   if (!f) {
      perror(argv[2]);
      return 1;
   }
   fwrite(blob.data, 1, blob.size, f);
   fclose(f);

   printf("wrote %s, %zu bytes\n", argv[2], blob.size);
   blob_finish(&blob);
   glsl_type_singleton_decref();
   return 0;
}
