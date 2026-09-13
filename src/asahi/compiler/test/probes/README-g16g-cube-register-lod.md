# CTS integer cube sample with tiny explicit LOD

Random sample seed20260913 selected case10:
`dEQP-VK.pipeline.sampler.view_type.cube.format.r32_uint.mipmap.nearest.lod.select_bias_2_5`.
Its generated GLSL uses `textureLod(usamplerCube, coords, 0.000010)`.

Two compiler errors interacted: cube projection rejected integer result types,
and the optimizer inlined the raw half constant0x00a8 as an integer immediate.
The sample packer then interpreted those bits as minifloat -0.75. Preserve
nonrepresentable LODs in half registers and project integer cube coordinates.
Register LOD and cube face must be published by a single PACK_SMPIR.

Native evidence: `build/cts-fixes/native-cube-register-lod/`, generated from
`g16g-cube-register-lod.metal` by AIR-NT32023.883 on macOS25G76, targeting
applegpu_g16g. `text-750.bin`:
- offset158: `1705540600142c00f0c01100`, face half10 plus LOD half11;
  PACK_SMPIR f74=10 (array layer uses4).
- offset170: `05800cb8b0000003000010000100`, cube dimension4, register LOD4.
The exact-byte texture-core regression covers both instructions.

Hardware before: 20260913T104433960870Z-cts-cube-source, compiler refusal.
Hardware after: 20260913T104946283939Z-cts-cube-fix, CTS Pass / Result image
matches reference, same unchanged CTS binary d296ca5f77238367933e88b99babefe050e841d3c7e1fbcb6e95e4d424814b06.
Mesa library0fe03ee615c76eefa31152af076083d3a7f68874f4fab4832da9af6b79f4d54b.
60-second watchdog and six-channel thermal guard; peak58.25 C. No kernel change.
Saved failing compiler bundle c7971b7beeed1000978f6785059de425120808e7f695357b6af6e785d21d7852
also compiles offline; the fresh hardware CTS run validates projection too.
