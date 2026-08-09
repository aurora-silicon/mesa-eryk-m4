# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT
#
# d3d12agx: experimental native Direct3D 12 driver on the AGX substrate.
# Campaign front door: ntasp repo, docs/d3d12agx-campaign.md.

dep_dxheaders_d12 = dependency('directx-headers', required : false)
if not dep_dxheaders_d12.found()
  dep_dxheaders_d12 = dependency(
    'DirectX-Headers',
    version : '>= 1.619.1',
    fallback : ['DirectX-Headers', 'dep_dxheaders'],
    default_options : ['build-test=false'],
    required : true,
  )
endif

libd3d12agx_files = files(
  'd12_cmd.c',
  'd12_descriptor.c',
  'd12_device.c',
  'd12_fence.c',
  'd12_format.c',
  'd12_pipeline.c',
  'd12_query.c',
  'd12_queue.c',
  'd12_resource.c',
  'd12_root_sig.c',
  'dxil/d12_dxil_bitstream.c',
  'dxil/d12_dxil_module.c',
  'dxil/d12_dxbc.c',
  'dxil/d12_dxil_nir.c',
)

d12_is_windows = host_machine.system() == 'windows'
if d12_is_windows
  libd3d12agx_files += files('d12_platform_windows.c')
  d12_platform_includes = []
  d12_platform_deps = []
else
  libd3d12agx_files += files('d12_platform_linux.c')
  d12_platform_includes = [inc_virtio_gpu, inc_virtio_vdrm]
  d12_platform_deps = [dep_libdrm]
endif

libd3d12agx = shared_library(
  'd3d12agx',
  libd3d12agx_files,
  include_directories : [
    inc_include,
    inc_src,
    inc_asahi,
    d12_platform_includes,
    include_directories('../isa'),
  ],
  dependencies : [
    dep_dxheaders_d12,
    d12_platform_deps,
    dep_valgrind,
    idep_nir,
    idep_mesautil,
    idep_agx_pack,
    idep_libagx,
    idep_libagx_shaders_h,
    idep_libpoly,
  ],
  link_with : [
    libasahi_lib, libasahi_layout, libasahi_compiler, libagx2_disasm,
    libpoly_nir,
  ],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  vs_module_defs : files('d3d12agx.def')[0],
  name_prefix : d12_is_windows ? '' : [],
  install : false,
)

d3d12agx_g1_smoke = executable(
  'd3d12agx_g1_smoke',
  files('tests/g1_smoke.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_shared_interop = executable(
  'd3d12agx_shared_interop',
  files('tests/shared_interop.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g2_smoke = executable(
  'd3d12agx_g2_smoke',
  files('tests/g2_smoke.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g3_smoke = executable(
  'd3d12agx_g3_smoke',
  files('tests/g3_smoke.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_render_ppm = executable(
  'd3d12agx_render_ppm',
  files('tests/render_ppm.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g4_triangle = executable(
  'd3d12agx_g4_triangle',
  files('tests/g4_triangle.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g4_smoke = executable(
  'd3d12agx_g4_smoke',
  files('tests/g4_smoke.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g5_wave = executable(
  'd3d12agx_g5_wave',
  files('tests/g5_wave.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g5_control_flow = executable(
  'd3d12agx_g5_control_flow',
  files('tests/g5_control_flow.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g5_atomics = executable(
  'd3d12agx_g5_atomics',
  files('tests/g5_atomics.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g5_descriptors = executable(
  'd3d12agx_g5_descriptors',
  files('tests/g5_descriptors.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g5_texture = executable(
  'd3d12agx_g5_texture',
  files('tests/g5_texture.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

# Build-time only: generates committable probe DXIL so the suites do not
# need dxc (absent on aarch64) or the LGPL corpus. Nothing links it.
d3d12agx_make_probe = executable(
  'd3d12agx_make_probe',
  files('tests/make_probe.c'),
  include_directories : [inc_include, inc_src, inc_compiler, inc_gallium],
  dependencies : [idep_nir, idep_mesautil, idep_libdxil_compiler],
)

d3d12agx_g11_indexed_instanced = executable(
  'd3d12agx_g11_indexed_instanced',
  files('tests/g11_indexed_instanced.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g10_vertex_buffers = executable(
  'd3d12agx_g10_vertex_buffers',
  files('tests/g10_vertex_buffers.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g9_amplification = executable(
  'd3d12agx_g9_amplification',
  files('tests/g9_amplification.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g8_mesh = executable(
  'd3d12agx_g8_mesh',
  files('tests/g8_mesh.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g7_tessellation = executable(
  'd3d12agx_g7_tessellation',
  files('tests/g7_tessellation.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g6_geometry = executable(
  'd3d12agx_g6_geometry',
  files('tests/g6_geometry.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

d3d12agx_g5_depth = executable(
  'd3d12agx_g5_depth',
  files('tests/g5_depth.c'),
  dependencies : [dep_dxheaders_d12],
  link_with : [libd3d12agx],
)

# DXIL frontend (G4): standalone of the COM driver for now
libd12_dxil_files = files(
  'dxil/d12_dxil_bitstream.c',
  'dxil/d12_dxil_module.c',
)

d3d12agx_dxil_dump = executable(
  'd3d12agx_dxil_dump',
  [libd12_dxil_files, files('dxil/d12_dxil_dump.c')],
  include_directories : [inc_include, inc_src, inc_compiler],
  dependencies : [idep_mesautil],
)

# DXIL -> NIR -> AGX, the "no SPIR-V" path end to end
d3d12agx_dxil_compile = executable(
  'd3d12agx_dxil_compile',
  [libd12_dxil_files, files('dxil/d12_dxil_nir.c', 'dxil/d12_dxbc.c',
                            'dxil/d12_dxil_compile.c')],
  include_directories : [
    inc_include, inc_src, inc_asahi, inc_gallium, inc_gallium_aux,
    include_directories('../isa'),
  ],
  dependencies : [idep_nir, idep_mesautil, idep_agx_pack, idep_libagx],
  link_with : [
    libasahi_compiler, libasahi_lib, libasahi_layout, libagx2_disasm,
  ],
)
