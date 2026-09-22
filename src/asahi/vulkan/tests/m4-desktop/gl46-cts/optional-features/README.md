# Optional OpenGL features on M4 (2026-09-22)

## Shadow LOD

Zink now exposes the existing SPIR-V depth-reference sampling support for
array/cube explicit LOD and bias (`GL_EXT_texture_shadow_lod`). All 18 targeted
GL46 cases pass: 15 rendering/reference-comparison cases plus three negative
shader tests. The rendering cases exercise vertex and fragment stages,
mipmapped depth textures, LOD, bias, offsets, and cube-array comparison.
No shader special cases or backend capability overrides are used.

Evidence: `20260922T043749462400Z-gl46-shadow-lod-enable`, guarded M4,
64x64 headless EGL, CTS v1.4.6.2. Peak temperature 65.25 C.
Gallium SHA256 `bb00637159016fc57bdb9b039184c4c150fb6c01fee3cf590e37d96f8fda98c1`.
Vulkan SHA256 `06dfc67608b3fb4d1f1ba8fa7d4c6f4343c74890ce0ae8d76ac9d4a68b8273a4`.
This is targeted development validation, not a new full-suite result.

## Post-depth coverage

M4 exposes `VK_EXT_post_depth_coverage`, and Zink consequently exposes
`GL_ARB_post_depth_coverage`. Force depth/stencil tests before the main shader
and snapshot the resulting live coverage for SampleMaskIn, before shader
sample-mask writes or discards. Mark the epilog as already tested so that it
does not repeat depth/stencil/occlusion updates. The normal pre-depth input
path is unchanged.

Both GL46 cases pass (shader validation and four-sample depth/stencil image
comparisons). Vulkan early-fragment and sample-mask-with-depth regression
selection: 110 recorded, 50 Pass / 60 NotSupported, no failures. Unsupported
sample counts and other optional extensions remain skipped. Exact selections
and summaries are retained alongside this document.

Evidence: `20260922T044358633698Z-gl46-post-depth-first` and
`20260922T044424308077Z-post-depth-vk`.
Vulkan SHA256 `f3ee0369c8f87976d7b3bf21d834ab60269247995bc17e6814aa92e5e2a78256`.

## Sparse MSAA rebind regression

`gl-sparse-msaa-probe.c` checks all four channels plus residency from both
sparse texel fetches and sparse image loads. It uses two array layers,
checkerboard commitments, per-coordinate and per-sample tagged values, then
inverts the bindings. Both 2x and 4x pass after these Zink fixes:

* Extract image residency separately from the texel, avoiding an invalid
  five-component SPIR-V image-read result when all channels are used.
* Apply commitment box offsets to Vulkan image binds (including array layer),
  not only to Zink's backing-page bookkeeping. Round partial page rows up.

Before evidence: `20260922T050030884999Z-sparse-msaa-rebind` (assertion),
`20260922T050237626746Z-sparse-msaa-rebind-vector-fix` (incorrect missing page),
`20260922T050313857157Z-sparse-msaa-rebind-detail` (both reads nonresident).
Passing evidence: `20260922T050429823019Z-sparse-msaa-rebind-offset-fix`.
Gallium SHA256 `fc29451c12342e4004c6813e57d97e69b98c130e1ca944c78e8903e15ab24be9`.
Vulkan SHA256 `45973faf93836254d3e4fc29e2267f0deb4d6195a5e6596cf2f1f3f089742b7c`.
Run the probe only through the watchdog/thermal experiment guard.
