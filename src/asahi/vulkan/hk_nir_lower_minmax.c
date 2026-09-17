/* SPDX-License-Identifier: MIT */
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_builtin_builder.h"
#include "agx_nir_texture.h"
#include "hk_sampler.h"
#include "hk_shader.h"

static nir_def *
source(nir_tex_instr *tex, nir_tex_src_type type)
{
   int i = nir_tex_instr_src_index(tex, type);
   return i < 0 ? NULL : tex->src[i].src.ssa;
}

/* Project to a face, retaining the face selection for inverse projection of
 * centre taps. Sampling those directions keeps native cube edge addressing. */
static nir_def *
project_cube(nir_builder *b, nir_def *coord, nir_def **face)
{
   nir_def *x = nir_channel(b, coord, 0), *y = nir_channel(b, coord, 1);
   nir_def *z = nir_channel(b, coord, 2);
   nir_def *ax = nir_fabs(b, x), *ay = nir_fabs(b, y), *az = nir_fabs(b, z);
   nir_def *use_x = nir_iand(b, nir_fge(b, ax, ay), nir_fge(b, ax, az));
   nir_def *use_y = nir_fge(b, ay, az);
   nir_def *xp = nir_fge_imm(b, x, 0), *yp = nir_fge_imm(b, y, 0);
   nir_def *zp = nir_fge_imm(b, z, 0);
   nir_def *sc =
      nir_bcsel(b, use_x, nir_bcsel(b, xp, nir_fneg(b, z), z),
                nir_bcsel(b, use_y, x, nir_bcsel(b, zp, x, nir_fneg(b, x))));
   nir_def *tc = nir_bcsel(
      b, use_x, nir_fneg(b, y),
      nir_bcsel(b, use_y, nir_bcsel(b, yp, z, nir_fneg(b, z)), nir_fneg(b, y)));
   nir_def *scale =
      nir_fdiv(b, nir_imm_float(b, .5), nir_fmax(b, ax, nir_fmax(b, ay, az)));
   *face = nir_bcsel(
      b, use_x, nir_bcsel(b, xp, nir_imm_int(b, 0), nir_imm_int(b, 1)),
      nir_bcsel(b, use_y,
                nir_bcsel(b, yp, nir_imm_int(b, 2), nir_imm_int(b, 3)),
                nir_bcsel(b, zp, nir_imm_int(b, 4), nir_imm_int(b, 5))));
   return nir_vec2(b, nir_ffma(b, sc, scale, nir_imm_float(b, .5)),
                   nir_ffma(b, tc, scale, nir_imm_float(b, .5)));
}

static nir_def *
cube_direction(nir_builder *b, nir_def *uv, nir_def *face)
{
   nir_def *s = nir_ffma_imm12(b, nir_channel(b, uv, 0), 2, -1);
   nir_def *t = nir_ffma_imm12(b, nir_channel(b, uv, 1), 2, -1);
   nir_def *one = nir_imm_float(b, 1), *neg = nir_imm_float(b, -1);
   nir_def *dirs[] = {nir_vec3(b, one, nir_fneg(b, t), nir_fneg(b, s)),
                      nir_vec3(b, neg, nir_fneg(b, t), s),
                      nir_vec3(b, s, one, t),
                      nir_vec3(b, s, neg, nir_fneg(b, t)),
                      nir_vec3(b, s, nir_fneg(b, t), one),
                      nir_vec3(b, nir_fneg(b, s), nir_fneg(b, t), neg)};
   nir_def *dir = dirs[5];
   for (int i = 4; i >= 0; --i)
      dir = nir_bcsel(b, nir_ieq_imm(b, face, i), dirs[i], dir);
   return dir;
}

/* Derivative of projection onto the selected face, including its major-axis
 * denominator. Select a face once, so quad neighbours across a cube edge do
 * not introduce a discontinuity into the footprint. */
static nir_def *
project_cube_gradient(nir_builder *b, nir_def *coord, nir_def *delta,
                      nir_def *uv, nir_def *face)
{
   nir_def *x = nir_channel(b, delta, 0), *y = nir_channel(b, delta, 1);
   nir_def *z = nir_channel(b, delta, 2);
   nir_def *planes[] = {
      nir_vec3(b, nir_fneg(b, z), nir_fneg(b, y), x),
      nir_vec3(b, z, nir_fneg(b, y), nir_fneg(b, x)),
      nir_vec3(b, x, z, y),
      nir_vec3(b, x, nir_fneg(b, z), nir_fneg(b, y)),
      nir_vec3(b, x, nir_fneg(b, y), z),
      nir_vec3(b, nir_fneg(b, x), nir_fneg(b, y), nir_fneg(b, z)),
   };
   nir_def *plane = planes[5];
   for (int i = 4; i >= 0; --i)
      plane = nir_bcsel(b, nir_ieq_imm(b, face, i), planes[i], plane);
   nir_def *major = nir_fmax(b, nir_fabs(b, nir_channel(b, coord, 0)),
                             nir_fmax(b, nir_fabs(b, nir_channel(b, coord, 1)),
                                      nir_fabs(b, nir_channel(b, coord, 2))));
   nir_def *numerator = nir_fsub(
      b, nir_trim_vector(b, plane, 2),
      nir_fmul(b, nir_ffma_imm12(b, uv, 2, -1), nir_channel(b, plane, 2)));
   return nir_fdiv(b, numerator, nir_fmul_imm(b, major, 2));
}

static nir_def *
centre_sample(nir_builder *b, nir_tex_instr *tex, nir_def *coord, nir_def *lod,
              bool view_min)
{
   nir_tex_instr *tap =
      nir_instr_as_tex(nir_instr_clone(b->shader, &tex->instr));
   nir_builder_instr_insert(b, &tap->instr);
   for (int s = tap->num_srcs - 1; s >= 0; --s) {
      switch (tap->src[s].src_type) {
      case nir_tex_src_coord:
         nir_src_rewrite(&tap->src[s].src, coord);
         break;
      case nir_tex_src_lod:
      case nir_tex_src_bias:
      case nir_tex_src_min_lod:
      case nir_tex_src_ddx:
      case nir_tex_src_ddy:
      case nir_tex_src_offset:
         nir_tex_instr_remove_src(tap, s);
         break;
      default:
         break;
      }
   }
   if (view_min)
      tap->backend_flags |= AGX_TEXTURE_FLAG_MINMAX_TAP;
   tap->backend_flags |= AGX_TEXTURE_FLAG_CENTRE_SAMPLER;
   tap->op = nir_texop_txl;
   nir_tex_instr_add_src(tap, nir_tex_src_lod, lod);
   return &tap->def;
}

static nir_def *
reduce(nir_builder *b, nir_tex_instr *tex, nir_def *a, nir_def *c,
       nir_def *maximum, nir_def *weighted)
{
   unsigned n = tex->is_sparse ? a->num_components - 1 : a->num_components;
   nir_def *ac = nir_trim_vector(b, a, n), *cc = nir_trim_vector(b, c, n);
   nir_def *result =
      nir_bcsel(b, maximum, nir_fmax(b, ac, cc), nir_fmin(b, ac, cc));
   result = nir_bcsel(b, weighted, nir_fadd(b, ac, cc), result);
   if (tex->is_sparse) {
      nir_def *channels[5];
      for (unsigned i = 0; i < n; ++i)
         channels[i] = nir_channel(b, result, i);
      channels[n] = nir_ior(b, nir_channel(b, a, n), nir_channel(b, c, n));
      result = nir_vec(b, channels, n + 1);
   }
   return result;
}

static nir_def *
weight_sample(nir_builder *b, nir_tex_instr *tex, nir_def *value,
              nir_def *weight, nir_def *weighted)
{
   /* Integer formats only admit nearest filtering. Preserve their bits. */
   if (nir_alu_type_get_base_type(tex->dest_type) != nir_type_float)
      return value;
   unsigned n =
      tex->is_sparse ? value->num_components - 1 : value->num_components;
   nir_def *color = nir_trim_vector(b, value, n);
   nir_def *scaled = nir_fmul(b, color, nir_f2fN(b, weight, color->bit_size));
   scaled = nir_bcsel(b, nir_fgt_imm(b, weight, 0), scaled,
                      nir_imm_zero(b, n, color->bit_size));
   color = nir_bcsel(b, weighted, scaled, color);
   if (!tex->is_sparse)
      return color;
   nir_def *channels[5];
   for (unsigned i = 0; i < n; ++i)
      channels[i] = nir_channel(b, color, i);
   channels[n] = nir_bcsel(b, nir_fgt_imm(b, weight, 0),
                           nir_channel(b, value, n), nir_imm_int(b, 0));
   return nir_vec(b, channels, n + 1);
}

static bool
lower_minmax(nir_builder *b, nir_tex_instr *tex, void *data)
{
   if (tex->op != nir_texop_tex && tex->op != nir_texop_txb &&
       tex->op != nir_texop_txd && tex->op != nir_texop_txl &&
       tex->op != nir_texop_lod)
      return false;
   nir_def *sampler = source(tex, nir_tex_src_sampler_deref);
   if (!sampler)
      return false;
   b->cursor = nir_before_instr(&tex->instr);
   nir_def *state = nir_load_sampler_descriptor_agx(b, sampler);
   nir_def *lo = nir_channel(b, state, 0), *hi = nir_channel(b, state, 1);
   nir_def *mode = nir_ubitfield_extract_imm(b, hi, HK_MINMAX_MODE_SHIFT, 2);
   nir_def *weighted = nir_ieq_imm(b, mode, 0);
   nir_def *view_min =
      data ? nir_f2f32(
                b, nir_build_texture_query(b, tex, nir_texop_image_min_lod_agx,
                                           1, nir_type_float16, false, false))
           : nir_imm_float(b, 0);
   nir_def *coord = source(tex, nir_tex_src_coord);
   nir_def *needs_reduction =
      nir_ior(b, nir_inot(b, weighted), nir_fgt_imm(b, view_min, 0));
   /* Explicit LOD/gradient operations do not need quad derivatives. Keep
    * their footprint queries and arithmetic inside the reduction path: an
    * ordinary weighted sampler must not pay for software reduction setup.
    * Implicit derivatives and LOD queries must remain outside this branch,
    * since a nonuniform sampler can make it divergent within a quad. */
   const bool explicit_lod = tex->op == nir_texop_txl || tex->op == nir_texop_txd;
   if (explicit_lod)
      nir_push_if(b, needs_reduction);
   const bool cube = tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE;
   const unsigned dims = tex->sampler_dim == GLSL_SAMPLER_DIM_3D ? 3 : 2;
   nir_def *size0 = nir_build_texture_query(b, tex, nir_texop_txs, 0,
                                            nir_type_int32, false, true);
   nir_def *sizef = nir_u2f32(b, nir_trim_vector(b, size0, dims));
   nir_def *face = NULL;
   nir_def *uv =
      cube ? project_cube(b, coord, &face) : nir_trim_vector(b, coord, dims);
   nir_def *dx = NULL, *dy = NULL;
   if (tex->op == nir_texop_txd) {
      dx = source(tex, nir_tex_src_ddx);
      dy = source(tex, nir_tex_src_ddy);
   } else if (tex->op != nir_texop_txl) {
      nir_def *spatial = nir_trim_vector(b, coord, cube ? 3 : dims);
      dx = nir_ddx(b, spatial);
      dy = nir_ddy(b, spatial);
   }
   if (cube && dx) {
      dx = project_cube_gradient(b, coord, dx, uv, face);
      dy = project_cube_gradient(b, coord, dy, uv, face);
   }
   nir_def *dx_size = dx ? nir_fmul(b, sizef, dx) : NULL;
   nir_def *dy_size = dy ? nir_fmul(b, sizef, dy) : NULL;
   nir_def *dx2 = dx ? nir_fdot(b, dx_size, dx_size) : NULL;
   nir_def *dy2 = dy ? nir_fdot(b, dy_size, dy_size) : NULL;
   nir_def *lod;
   if (tex->op == nir_texop_txl) {
      lod = nir_f2f32(b, source(tex, nir_tex_src_lod));
   } else if (tex->op == nir_texop_txd) {
      lod = nir_fmul_imm(b, nir_flog2(b, nir_fmax(b, dx2, dy2)), .5);
   } else {
      /* Query outside the potentially divergent reduction branch. */
      nir_def *query = nir_build_texture_query(b, tex, nir_texop_lod, 2,
                                              nir_type_float32, true, false);
      nir_tex_instr *q = nir_instr_as_tex(nir_def_instr(query));
      /* A LOD query on a depth image does not perform comparison. */
      q->is_shadow = q->is_new_style_shadow = false;
      if (tex->is_array) {
         unsigned n = cube ? 3 : dims;
         b->cursor = nir_before_instr(&q->instr);
         nir_def *query_coord = nir_trim_vector(b, coord, n);
         nir_src_rewrite(
            &q->src[nir_tex_instr_src_index(q, nir_tex_src_coord)].src,
            query_coord);
         q->coord_components = n;
         b->cursor = nir_after_instr(&q->instr);
      }
      lod = nir_channel(b, query, 1);
      /* The original sampler can apply bias and anisotropy to its query.
       * Reconstruct the base footprint for weighted view-minimum sampling;
       * reduction uses the bias-free isotropic nearest sampler's query. */
      lod = nir_bcsel(b, weighted,
                      nir_fmul_imm(b, nir_flog2(b, nir_fmax(b, dx2, dy2)), .5),
                      lod);
   }
   if (!explicit_lod)
      nir_push_if(b, needs_reduction);
   nir_def *count = nir_imm_int(b, 1), *major_delta = NULL;
   if (dx) {
      nir_def *max_aniso = nir_u2f32(
         b, nir_ubitfield_extract_imm(b, hi, HK_MINMAX_ANISOTROPY_SHIFT, 5));
      nir_def *ratio = nir_fsqrt(
         b,
         nir_fdiv(b, nir_fmax(b, dx2, dy2),
                  nir_fmax(b, nir_fmin(b, dx2, dy2), nir_imm_float(b, 1e-20))));
      nir_def *rate = nir_fceil(
         b, nir_fmax(b, nir_imm_float(b, 1), nir_fmin(b, max_aniso, ratio)));
      count = nir_f2u32(b, rate);
      lod = nir_fsub(b, lod, nir_flog2(b, rate));
      major_delta = nir_bcsel(b, nir_fge(b, dx2, dy2), dx, dy);
   }
   nir_def *maximum = nir_ieq_imm(b, mode, 2);
   nir_def *bias = nir_f2f32(b, nir_unpack_32_2x16_split_x(b, hi));
   if (tex->op == nir_texop_txb)
      bias = nir_fadd(b, bias, nir_f2f32(b, source(tex, nir_tex_src_bias)));
   bias = nir_fmin(b, nir_fmax(b, bias, nir_imm_float(b, -15)),
                   nir_imm_float(b, 15));
   lod = nir_fadd(b, lod, bias);
   nir_def *unclamped_lod = lod;
   nir_def *min_lod = nir_f2f32(b, nir_unpack_32_2x16_split_x(b, lo));
   nir_def *max_lod = nir_f2f32(b, nir_unpack_32_2x16_split_y(b, lo));
   if (source(tex, nir_tex_src_min_lod))
      min_lod =
         nir_fmax(b, min_lod, nir_f2f32(b, source(tex, nir_tex_src_min_lod)));
   lod = nir_fmin(b, nir_fmax(b, lod, min_lod), max_lod);
   nir_def *linear =
      nir_bcsel(b, nir_fle_imm(b, lod, 0),
                nir_ine_imm(b, nir_iand_imm(b, hi, HK_MINMAX_MAG_LINEAR), 0),
                nir_ine_imm(b, nir_iand_imm(b, hi, HK_MINMAX_MIN_LINEAR), 0));
   nir_def *mip_linear =
      nir_ine_imm(b, nir_iand_imm(b, hi, HK_MINMAX_MIP_LINEAR), 0);
   nir_def *levels = nir_build_texture_query(b, tex, nir_texop_query_levels, 1,
                                             nir_type_int32, false, false);
   lod = nir_fmin(b, nir_fmax(b, lod, nir_imm_float(b, 0)),
                  nir_u2f32(b, nir_iadd_imm(b, levels, -1)));
   lod = nir_fmax(b, lod, view_min);
   nir_def *level0 =
      nir_ffloor(b, nir_fadd(b, lod,
                             nir_bcsel(b, mip_linear, nir_imm_float(b, 0),
                                       nir_imm_float(b, .5))));
   if (tex->op == nir_texop_lod) {
      nir_def *answer =
         nir_vec2(b, nir_bcsel(b, mip_linear, lod, level0), unclamped_lod);
      nir_push_else(b, NULL);
      nir_tex_instr *original =
         nir_instr_as_tex(nir_instr_clone(b->shader, &tex->instr));
      nir_builder_instr_insert(b, &original->instr);
      nir_pop_if(b, NULL);
      nir_def_replace(&tex->def, nir_if_phi(b, answer, &original->def));
      return true;
   }
   nir_def *unnormalized =
      nir_ine_imm(b, nir_iand_imm(b, hi, HK_MINMAX_UNNORMALIZED), 0);
   nir_variable *accumulator = NULL, *iteration = NULL;
   nir_def *step = NULL;
   if (dx) {
      accumulator = nir_local_variable_create(
         b->impl, glsl_vector_type(GLSL_TYPE_FLOAT, tex->def.num_components),
         "minmax_accumulator");
      iteration = nir_local_variable_create(b->impl, glsl_uint_type(),
                                            "minmax_iteration");
      nir_store_var(b, accumulator,
                    nir_imm_zero(b, tex->def.num_components, tex->def.bit_size),
                    ~0u);
      nir_store_var(b, iteration, nir_imm_int(b, 0), 1);
      nir_loop *loop = nir_push_loop(b);
      loop->control = nir_loop_control_dont_unroll;
      step = nir_load_var(b, iteration);
      nir_break_if(b, nir_uge(b, step, count));
      nir_def *position =
         nir_fadd_imm(b,
                      nir_fdiv(b, nir_fadd_imm(b, nir_u2f32(b, step), .5),
                               nir_u2f32(b, count)),
                      -.5);
      uv = nir_ffma(b, major_delta, position, uv);
   }
   nir_def *results[2];
   for (unsigned mip = 0; mip < 2; ++mip) {
      nir_def *level = nir_fmin(b, nir_fadd_imm(b, level0, mip),
                                nir_u2f32(b, nir_iadd_imm(b, levels, -1)));
      nir_def *isize = nir_imax(
         b, nir_ushr(b, nir_trim_vector(b, size0, dims), nir_f2u32(b, level)),
         nir_imm_int(b, 1));
      nir_def *size = nir_u2f32(b, isize);
      nir_def *pos = nir_bcsel(b, unnormalized, uv, nir_fmul(b, uv, size));
      pos = nir_fsub(
         b, pos,
         nir_bcsel(b, linear, nir_imm_float(b, .5), nir_imm_float(b, 0)));
      pos = nir_fmul_imm(b, nir_fround_even(b, nir_fmul_imm(b, pos, 256)),
                         1.0 / 256);
      nir_def *base = nir_ffloor(b, pos), *fraction = nir_ffract(b, pos);
      if (source(tex, nir_tex_src_offset))
         base =
            nir_fadd(b, base, nir_i2f32(b, source(tex, nir_tex_src_offset)));
      nir_def *result = NULL;
      for (unsigned tap = 0; tap < (1u << dims); ++tap) {
         nir_def *components[4], *active = nir_imm_bool(b, true);
         nir_def *weight = nir_imm_float(b, 1);
         for (unsigned d = 0; d < dims; ++d) {
            unsigned step = (tap >> d) & 1;
            nir_def *f = nir_bcsel(b, linear, nir_channel(b, fraction, d),
                                   nir_imm_float(b, 0));
            weight = nir_fmul(b, weight,
                              step ? f : nir_fsub(b, nir_imm_float(b, 1), f));
            components[d] = nir_fadd_imm(b, nir_channel(b, base, d), step + .5);
            if (step)
               active = nir_iand(
                  b, active,
                  nir_iand(b, linear,
                           nir_fgt_imm(b, nir_channel(b, fraction, d), 0)));
         }
         nir_def *center = nir_vec(b, components, dims);
         center = nir_bcsel(b, unnormalized, center, nir_fdiv(b, center, size));
         if (cube) {
            /* Nearest and NON_SEAMLESS_CUBE_MAP clamp to the selected face.
             * Only a seamless linear footprint may cross a face edge. */
            nir_def *edge = nir_fdiv(b, nir_imm_float(b, .5), size);
            nir_def *clamped = nir_fmin(b, nir_fmax(b, center, edge),
                                        nir_fsub(b, nir_imm_float(b, 1), edge));
            nir_def *seamful =
               nir_ine_imm(b, nir_iand_imm(b, hi, HK_MINMAX_SEAMFUL), 0);
            center = nir_bcsel(b, nir_ior(b, seamful, nir_inot(b, linear)),
                               clamped, center);
            center = cube_direction(b, center, face);
         }
         if (tex->is_array) {
            unsigned n = center->num_components;
            for (unsigned d = 0; d < n; ++d)
               components[d] = nir_channel(b, center, d);
            components[n] = nir_channel(b, coord, coord->num_components - 1);
            center = nir_vec(b, components, n + 1);
         }
         nir_def *value = centre_sample(b, tex, center, level, data != NULL);
         value = weight_sample(b, tex, value, weight, weighted);
         result =
            result ? nir_bcsel(b, active,
                               reduce(b, tex, result, value, maximum, weighted),
                               result)
                   : value;
      }
      results[mip] = result;
   }
   nir_def *use_second =
      nir_iand(b, mip_linear, nir_fgt_imm(b, nir_ffract(b, lod), 0));
   nir_def *mip_weight =
      nir_bcsel(b, use_second, nir_ffract(b, lod), nir_imm_float(b, 0));
   results[0] =
      weight_sample(b, tex, results[0],
                    nir_fsub(b, nir_imm_float(b, 1), mip_weight), weighted);
   results[1] = weight_sample(b, tex, results[1], mip_weight, weighted);
   nir_def *reduced = nir_bcsel(
      b, use_second, reduce(b, tex, results[0], results[1], maximum, weighted),
      results[0]);
   if (dx) {
      nir_def *previous = nir_load_var(b, accumulator);
      nir_store_var(
         b, accumulator,
         nir_bcsel(b, nir_ieq_imm(b, step, 0), reduced,
                   reduce(b, tex, previous, reduced, maximum, weighted)),
         ~0u);
      nir_store_var(b, iteration, nir_iadd_imm(b, step, 1), 1);
      nir_pop_loop(b, NULL);
      reduced = nir_load_var(b, accumulator);
      reduced = weight_sample(b, tex, reduced, nir_frcp(b, nir_u2f32(b, count)),
                              weighted);
   }
   nir_push_else(b, NULL);
   nir_tex_instr *original =
      nir_instr_as_tex(nir_instr_clone(b->shader, &tex->instr));
   /* A zero view minimum needs no later native minimum clamp. */
   if (data)
      original->backend_flags |= AGX_TEXTURE_FLAG_MINMAX_TAP;
   nir_builder_instr_insert(b, &original->instr);
   nir_pop_if(b, NULL);
   nir_def *result = nir_if_phi(b, reduced, &original->def);
   nir_def_replace(&tex->def, result);
   return true;
}

bool
hk_nir_lower_minmax(nir_shader *nir, bool view_min)
{
   return nir_shader_tex_pass(nir, lower_minmax, nir_metadata_none,
                              view_min ? &view_min : NULL);
}
