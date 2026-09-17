/* SPDX-License-Identifier: MIT */
#include "hk_nir_version_samples.h"
#include "agx_nir_texture.h"
#include "hk_sampler.h"
#include "nir_builder.h"
#include "nir_control_flow.h"

/* Bound code growth and only speculate descriptor reads for one scalar,
 * statically named combined sampler. The caller additionally checks the
 * pipeline layout: partially bound and variable-count bindings must not be
 * read before their original control flow. */
nir_variable *
hk_nir_sample_version_candidate(nir_shader *nir)
{
   if (nir->info.stage != MESA_SHADER_FRAGMENT)
      return NULL;
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_variable *sampler = NULL;
   unsigned instructions = 0, samples = 0;
   bool loop = false;
   nir_foreach_block(block, impl) {
      for (nir_cf_node *n = block->cf_node.parent; n; n = n->parent)
         loop |= n->type == nir_cf_node_loop;
      nir_foreach_instr(instr, block) {
         if (++instructions > 4096)
            return NULL;
         if (instr->type != nir_instr_type_tex)
            continue;
         nir_tex_instr *tex = nir_instr_as_tex(instr);
         /* Begin with explicit-LOD 2D sampling. No derivative instructions
          * or nonuniform descriptor indices are introduced in either arm. */
         if (tex->op != nir_texop_txl || tex->sampler_dim != GLSL_SAMPLER_DIM_2D ||
             tex->is_array || tex->is_shadow || tex->is_sparse ||
             tex->dest_type != nir_type_float32 || tex->backend_flags)
            return NULL;
         nir_def *t = nir_get_tex_src(tex, nir_tex_src_texture_deref);
         nir_def *s = nir_get_tex_src(tex, nir_tex_src_sampler_deref);
         if (!t || !s)
            return NULL;
         nir_deref_instr *td = nir_def_as_deref(t), *sd = nir_def_as_deref(s);
         if (!td || !sd || td->deref_type != nir_deref_type_var ||
             sd->deref_type != nir_deref_type_var || td->var != sd->var ||
             (sampler && sampler != td->var) ||
             !glsl_type_is_sampler(td->var->type))
            return NULL;
         sampler = td->var;
         samples++;
      }
   }
   return loop && samples >= 16 ? sampler : NULL;
}

static nir_def *
query(nir_builder *b, nir_def *deref, nir_texop op, nir_alu_type type)
{
   nir_tex_instr *tex = nir_tex_instr_create(b->shader, 2);
   tex->op = op;
   tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   tex->dest_type = type;
   tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_texture_deref, deref);
   tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_sampler_deref, deref);
   nir_def_init(&tex->instr, &tex->def, 1, nir_alu_type_get_type_size(type));
   nir_builder_instr_insert(b, &tex->instr);
   return &tex->def;
}

bool
hk_nir_version_samples(nir_shader *nir, nir_variable *sampler, bool view_min)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_cf_list body;
   nir_cf_extract(&body, nir_before_impl(impl), nir_after_impl(impl));
   nir_builder b = nir_builder_at(nir_before_impl(impl));
   nir_def *deref = &nir_build_deref_var(&b, sampler)->def;
   nir_def *state = nir_load_sampler_descriptor_agx(&b, deref);
   nir_def *mode = nir_ubitfield_extract_imm(&b, nir_channel(&b, state, 1),
                                           HK_MINMAX_MODE_SHIFT, 2);
   nir_def *ordinary = nir_iand(&b, nir_ieq_imm(&b, mode, 0),
      nir_inot(&b, query(&b, deref, nir_texop_has_custom_border_color_agx,
                        nir_type_bool1)));
   if (view_min) {
      nir_def *minimum = query(&b, deref, nir_texop_image_min_lod_agx,
                                nir_type_float16);
      ordinary = nir_iand(&b, ordinary, nir_feq_imm(&b, minimum, 0));
   }
   nir_if *nif = nir_push_if(&b, ordinary);
   struct hash_table *remap = _mesa_pointer_hash_table_create(NULL);
   nir_cf_list_clone_and_reinsert(&body, &nif->cf_node, b.cursor, remap);
   _mesa_hash_table_destroy(remap, NULL);
   nir_foreach_block_in_cf_node(block, &nif->cf_node) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_tex)
            nir_instr_as_tex(instr)->backend_flags |= AGX_TEXTURE_FLAG_PLAIN_SAMPLER;
      }
   }
   nir_cf_reinsert(&body, nir_after_block(nir_if_first_else_block(nif)));
   nir_progress(true, impl, nir_metadata_none);

   /* The frontend has already made outputs single-exit. Cloning its body
    * duplicates those stores, so restore that invariant before epilog
    * lowering: AGX exports are unconditionally collected at function exit. */
   NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries, impl, nir_var_shader_out);
   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   return true;
}
