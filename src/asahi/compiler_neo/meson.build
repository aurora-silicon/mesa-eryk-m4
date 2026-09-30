# Copyright 2018 Rob Clark
# Copyright 2019 Collabora, Ltd.
# SPDX-License-Identifier: MIT

neo_libasahi_agx_files = files(
  'agx_compile.c',
  'neo_bridge.c',
  'agx_dce.c',
  'agx_liveness.c',
  'agx_insert_waits.c',
  'agx_nir_lower_address.c',
  'agx_nir_opt_select_load.c',
  'agx_nir_lower_cull_distance.c',
  'agx_nir_lower_fminmax.c',
  'agx_nir_lower_frag_sidefx.c',
  'agx_nir_lower_sample_mask.c',
  'agx_nir_lower_discard_zs_emit.c',
  'agx_nir_lower_interpolation.c',
  'agx_nir_lower_g17p_combined_sampler.c',
  'agx_nir_ray_query.c',
  'agx_nir_lower_shared_bitsize.c',
  'agx_nir_lower_subgroups.c',
  'agx_nir_lower_texture.c',
  'agx_nir_opt_preamble.c',
  'agx_lower_64bit.c',
  'agx_lower_divergent_shuffle.c',
  'agx_lower_parallel_copy.c',
  'agx_lower_pseudo.c',
  'agx_lower_spill.c',
  'agx_lower_uniform_sources.c',
  'agx_pack.c',
  'agx3_pack.c',
  'agx3_lower_uniform_program.c',
  'agx3_lower_cprog_inputs.c',
  'agx3_cprog_input.c',
  'agx3_select_core.c',
  'agx3_copy_core.c',
  'agx3_isel_core.c',
  'agx3_iadd_core.c',
  'agx3_pack_binary.c',
  'agx3_compose/agx3_compose.c',
  'agx3_compose/agx3b_tables.c',
  'agx3_compose/agx3c_tables.c',
  'agx3_binder.c',
  'agx3_atomic_core.c',
  'agx3_cf_core.c',
  'agx3_mem_core.c',
  'agx3_local_tps_core.c',
  'agx3_scs_core.c',
  'agx3_subgroup_core.c',
  'agx3_static_spill_core.c',
  'agx3_ioto_load_packer.c',
  'agx3_ioto_load_packer.h',
  'agx3_ioto_uniform_program.c',
  'agx3_ioto_uniform_program.h',
  'agx3_compute_program.c',
  'agx3_compute_program.h',
  'agx3_graphics_program.c',
  'agx3_graphics_program.h',
  'agx3_tex_core.c',
  'agx3_falu_mod_core.c',
  'agx3_uniform_alu.c',
  'agx3_uniform_integer.c',
  'agx3_vary_core.c',
  'agx3_tile_core.c',
  'agx3_pixel_core.c',
  'agx3_stage_core.c',
  'agx3_fltnorm_core.c',
  'agx3_writeout_core.c',
  'agx3_zs_core.c',
  'agx3_rt_core.c',
  'agx3_slot_core.c',
  'agx3_fixed_dependencies.c',
  'agx3_alu_core.c',
  'agx3_funop_core.c',
  'agx3_sr_core.c',
  'agx_performance.c',
  'agx_print.c',
  'agx_pressure_schedule.c',
  'agx_ir.c',
  'agx_opt_cse.c',
  'agx_opt_break_if.c',
  'agx_opt_empty_else.c',
  'agx_opt_jmp_none.c',
  'agx_opt_compact_constants.c',
  'agx_opt_promote_constants.c',
  'agx_opt_register_cache.c',
  'agx_optimizer.c',
  'agx_repair_ssa.c',
  'agx_reindex_ssa.c',
  'agx_spill.c',
  'agx_register_allocate.c',
  'agx_validate.c',
  'agx_validate_ra.c',
)

neo_agx_nir_algebraic_c = custom_target(
  input : 'agx_nir_algebraic.py',
  output : 'agx_nir_algebraic.c',
  command : [
    prog_python, '@INPUT@', '-p', dir_compiler_nir,
  ],
  capture : true,
  depend_files : nir_algebraic_depends,
)

neo_agx_opcodes_h = custom_target(
  input : ['agx_opcodes.h.py'],
  output : 'agx_opcodes.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
  depend_files : files('agx_opcodes.py'),
)

neo_idep_agx_opcodes_h = declare_dependency(
  sources : [neo_agx_opcodes_h],
  include_directories : include_directories('.'),
)

neo_agx_opcodes_c = custom_target(
  input : ['agx_opcodes.c.py'],
  output : 'agx_opcodes.c',
  command : [prog_python, '@INPUT@'],
  capture : true,
  depend_files : files('agx_opcodes.py'),
)

neo_agx_builder_h = custom_target(
  input : ['agx_builder.h.py'],
  output : 'agx_builder.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
  depend_files : files('agx_opcodes.py'),
)

neo_idep_agx_builder_h = declare_dependency(
  sources : [neo_agx_builder_h],
  include_directories : include_directories('.'),
)

neo_libasahi_compiler = static_library(
  'asahi_neo_raw',
  [neo_libasahi_agx_files, neo_agx_opcodes_c, neo_agx_nir_algebraic_c],
  dependencies: [idep_nir, neo_idep_agx_opcodes_h, neo_idep_agx_builder_h, idep_mesautil, idep_libagx],
  link_with : [libagx2_disasm],
  c_args : [no_override_init_args, '-Wno-c2x-extensions'],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

neo_compiler_archive = custom_target('asahi_neo_compiler',
  input : neo_libasahi_compiler, output : 'libasahi_neo_compiler.a',
  command : [prog_python, files('namespace_archive.py'), '@INPUT@', '@OUTPUT@'],
)
