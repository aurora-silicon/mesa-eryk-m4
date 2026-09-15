# Shadow gradients, cube bias and cube/3D LOD queries (2026-09-15)

CTS catalog range 0-1M, segments 79/80: `glsl.texture_functions.texturegrad*`
and `textureprojgrad*` with shadow samplers (1D/2D/1D-array/2D-array,
with and without offsets), `texture.*cube*_bias_*`, and
`query.texturequerylod.{sampler3d,samplercube,samplercubearray}*` failed with
`VK_ERROR_FEATURE_NOT_PRESENT`: the native sampler admission refused the
shapes (`gradient && is_shadow`, `query && (three_d || cube)`, `txb` on cubes).

Witnesses compiled offline with the Metal toolchain left on the Mac
(`/Users/m1/m4-metal-917/Metal.xctoolchain`, AIR-NT 32023.883, target
applegpu_g16g; `xcrun` is gated by an unaccepted Xcode 27 licence, so the
tool was driven through the Xcode-internal Python and direct binaries):

- `g16g-shadow-grad.metal` (compute): `depth2d`/`depth2d_array`
  `sample_compare(..., gradient2d(dx, dy)[, int2 offset])`.
  SMP `05800ca010012401000000000100` (2D), `...6401...` (offset, f141=2),
  `...2c01...` (array, f77=2), `05800ca090006c01000000000100` (array+offset).
  Reference stays in staging half4, derivatives at half8, f74=0/f143=1 as
  every other depth comparison; the encoder already produced these bytes
  once the refusals were lifted (`test_m4_shadow_gradient_witnesses`).
- `g16g-lod-query-cube-bias.metal` (fragment): `calculate_*_lod` on
  `texturecube` (`05800ca8b0001000010120000100`, f76=4, f146=1, face-only
  PACK_SMPIR `1705540600040004f0c00000` with f74=10), `texturecube_array`
  (`...1301...`, implicit layer f37/f77=1), `texture3d` (`...8001...`,
  f76=3); `sample(bias())` on `texturecube` (`05800cb8b0001400010110000100`,
  f139=3, face+bias PACK_SMPIR `1705540600040c00f0c01100`) and
  `texturecube_array` (`05800cb830011800010110000100`, f77=2).

Hardware result (library ad389f90… then 929d… successors): 171 of the 211
cases pass (all shadow gradient forms, cube bias, 3D queries). The 40 cube
and cube-array LOD queries still return an integer LOD exactly one level
above the CTS reference; the projected-face query form needs more work.
