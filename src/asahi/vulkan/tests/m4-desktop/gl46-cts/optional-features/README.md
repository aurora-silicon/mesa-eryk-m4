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

## Sparse multisampling and LOD clamp

M4 now exposes 2x/4x sparse residency. Image-format queries honor the exposed
sample counts; 2x blocks are flagged nonstandard. Sparse tile offsets and page
size assertions now include all samples. Native sparse MSAA reads return
hardware residency instead of the old always-resident fallback.

The final unmodified 1,806-case GL46 sparse_texture2/clamp selection records
**1,800 Pass / 6 Fail**, no crashes/timeouts. The six depth-clamp cases fail in
CTS setup before rendering: see `reference/cts-sparse-depth-setup.patch`.
Do not relabel their upstream outcomes as Pass. All 74 MSAA lookup failures
from the intermediate run pass with native status; all earlier crashes have
completed passing rechecks. Historical results remain in the compressed files.

Vulkan sparse block-shape and multisampled-binding selection: 158 Pass /
360 NotSupported (518 recorded). The additional checkerboard probe covers
actual missing pages, 2x/4x samples, array layers, and rebinding; see above.
All 23 host layout tests and 292 host compiler tests pass.
Evidence: `20260922T050521873125Z-gl46-sparse-final` (peak 85.5 C),
`20260922T045832168156Z-gl46-sparse-native-ms`,
`20260922T044614155393Z-sparse-msaa-vk`.


## Matched Mesa deployment

The new native-coherent blend capability changes the internal `pipe_caps`
layout. Rebuild Gallium and its EGL/GLX/GBM consumers together, including
`src/gbm/backends/dri/dri_gbm.so`. Updating only libgallium leaves the old GBM
backend reading the wrong capability fields and rejecting all output buffers.
The failed `20260922T051605541996Z-gl46-blend-native` and
`20260922T052210560213Z-gl46-blend-native-loaders` attempts never started CTS;
they are setup failures, not test passes. Updating the GBM backend restored
the private headless output in `20260922T052329355178Z-gl46-blend-native-matched`.
That run then recorded 56 Pass / 32 Fail, exposing double advanced blending
in the GLSL shader fallback plus native Vulkan blend state.

`install-mesa-library.py` now accepts `gbm`, `gbm-backend`, `egl`, and `glx`
in addition to Vulkan/Gallium, retaining checksum-verified backups. Use it
between guarded runs. Stage loaders with installed RUNPATHs (`$ORIGIN` for
lib/, `$ORIGIN/..` for lib/gbm/) rather than their build-tree RUNPATHs.
Do not treat a Gallium internal-ABI change as an independent library update.


## Native advanced blending through Zink

Zink uses Vulkan advanced blend operations and their dynamic state instead of
GLSL framebuffer-fetch emulation when supported. A distinct Gallium coherent
advanced-blend capability avoids claiming coherent framebuffer fetch. Skip
GLSL blend lowering when the screen supports native advanced blend state;
otherwise the equation is applied twice. Both pipeline and dynamic-state
paths retain the standard GL premultiplied, uncorrelated overlap semantics.

All 88 GL46 advanced-blend cases pass after the native path and lowering fix,
including coherent enable/disable and mixed/multiply coherency sequences.
Evidence: `20260922T052745981819Z-gl46-blend-native-validation`, peak 77 C.
Despite the experiment name, its inherited VK_LAYER_PATH was /var/empty;
this is an image/CTS pass, **not** evidence of loaded Vulkan validation.
Gallium SHA256: 6c38fc9b7949a23bf8b1bc699a62b7350d0a1e8d5d85d926a02525aa75311786.
See `advanced-blend-vulkan.md` for the separate 32,564-case Vulkan record.


Validation-layer recheck: `20260922T053122671809Z-gl46-blend-vvl-loaded`
explicitly uses `/usr/share/vulkan/explicit_layer.d`. Both worker logs confirm
that libVkLayer_khronos_validation.so was loaded as an instance/device layer.
All 88 cases pass, with no VUID/validation errors (peak 72 C). Worker logs
are retained in `blend-vvl-worker-logs.txt.gz`.

Final combined unmodified GL46 selection: **1,908 Pass / 6 Fail**, 1,914
recorded, no NotSupported, crashes or timeouts. The failure set exactly equals
`clamp-depth.txt`: the CTS setup defects described above. Evidence:
`20260922T052852393960Z-gl46-all-four-optional`, peak 85.25 C, 127.19 seconds.
This run uses the final Gallium/Vulkan pair listed above and the matching
EGL/GLX/GBM loaders. The six original Fail outcomes remain unchanged.
`vulkan-global-indices.json.gz` maps every Vulkan case used in this feature
work to its original frozen global index and catalog SHA256; case indices
in individual bare-list runner records are selection-local.


The final-driver diagnostic recheck of all six CTS setup corrections also
passes: `20260922T053227982821Z-gl46-clamp-depth-final-diagnostic`, peak 68.5 C.
Only `/root/gl-cts-1.4.6.2/glcts-sparse-depth-diagnostic` contains the test
patch; the pinned production CTS binary remains unchanged. Latest mixed-build
main-catalog tracking after these features is 18,100 Pass / 1,607 NotSupported /
6 CTS-setup Fail / 1 CompatibilityWarning (19,714 total). Diagnostic passes are
not substituted into that record. Original global catalog indices are retained
in `latest-catalog-after-features.json.gz`; prior snapshots are not overwritten.
