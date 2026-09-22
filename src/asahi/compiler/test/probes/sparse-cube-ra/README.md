# Sparse cube sampling register-allocation regression

Captured by the GL46 sparse_texture2 run on M4:
`20260922T044835101838Z-gl46-sparse-offset`, CTS v1.4.6.2.
The sparse texture result needs 16 register halves. Allocating it shuffled its
own cube-array coordinate vector above half 127, violating the fused
PACK_SMPIR layer operand constraint although its initial allocation was legal.

Decompress input.bin.gz to a temporary directory and run:

    agx_shader_replay /tmp/sparse-cube/input.bin

The capture embeds the compiler key. Serialized NIR is tied to this Mesa
revision; retain the GL CTS case as the portable reproducer:
`KHR-GL46.sparse_texture2_tests.UncommittedRegionsAccess_texture_cube_map_r32i`.

Before: backend rejects texture_sample after cube-array LOD staging fails.
After: PASS, 24,642 bytes, hash 64c3b40d; 159 GPR halves, no spills/fills.
All 292 host compiler tests also pass. Hardware feature qualification is
recorded separately in the GL46 optional-features report.
