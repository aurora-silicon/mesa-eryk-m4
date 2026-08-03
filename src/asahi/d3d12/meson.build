# Copyright 2026 NTASP contributors
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
  'd12_queue.c',
  'd12_resource.c',
)

libd3d12agx = shared_library(
  'd3d12agx',
  libd3d12agx_files,
  include_directories : [
    inc_include,
    inc_src,
    inc_asahi,
    inc_virtio_gpu,
    inc_virtio_vdrm,
  ],
  dependencies : [
    dep_dxheaders_d12,
    dep_libdrm,
    dep_valgrind,
    idep_nir,
    idep_mesautil,
    idep_agx_pack,
    idep_libagx,
    idep_libagx_shaders_h,
  ],
  link_with : [libasahi_lib, libasahi_layout, libasahi_compiler],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  install : false,
)

d3d12agx_g1_smoke = executable(
  'd3d12agx_g1_smoke',
  files('tests/g1_smoke.c'),
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

# DXIL frontend (G4): standalone of the COM driver for now
libd12_dxil_files = files(
  'dxil/d12_dxil_bitstream.c',
  'dxil/d12_dxil_module.c',
)

d3d12agx_dxil_dump = executable(
  'd3d12agx_dxil_dump',
  [libd12_dxil_files, files('dxil/d12_dxil_dump.c')],
  include_directories : [inc_include, inc_src],
  dependencies : [idep_mesautil],
)
