# M4 depth and masked-visibility probes

Build `m4_gl_occlusion_color_mask.c` and `m4_gl_depth_prepass.c` as standalone
C programs with `-lEGL -lGL`. The workspace cross toolchain targets ARM64 Linux.
Run hardware binaries only via the mandatory watchdog/thermal guard and
`/root/run-m4-gl.py`; each program verifies the M4 Zink renderer. For host
reference checks, use software EGL with `M4_TEST_LLVMPIPE=1`.

The masked-query probe checks eight rounds of full, 11x7 scissored, empty and
boolean visibility queries, then draws a red overlay with color writes disabled.
The expected image stays green. `M4_TEST_DEPTH_MASK=1` additionally places the
masked draw nearer than the stored depth while disabling depth writes, then
checks that a later intermediate-depth green draw still passes. Both modes
check all 131072 pixels and exact query results.

The depth-prepass probe renders a sloped triangle with nonconstant clip W and
color writes disabled, flushes, clears a different framebuffer, then redraws the
same surface with LEQUAL against the reloaded D32F depth attachment. All 131072
pixels must be green. This uses the same vertex program in both passes; it does
not establish cross-program position invariance or complete clipping behavior.

Host llvmpipe passed all three cases. Guarded J713 experiments on Vulkan
c83cf477661de94365ae498d34084dc0bf6c633ef10edd0c7c87c70769100317:

- 20260911T144811236714Z-sauerbraten-masked-visibility-oracle: pass, 50.25 C.
- 20260911T144932643479Z-sauerbraten-masked-depth-oracle: pass, 50.25 C.
- 20260911T145334086031Z-sauerbraten-depth-prepass-oracle: pass, 49.75 C.

These controls did not reproduce Sauerbraten's visible geometry gaps. They are
bounded regression evidence, not proof that the game or all query ordering is
correct. The original game trace still differs visibly from llvmpipe.

`m4_gl_wide_varyings.c` now also checks 8/11-scalar interfaces and optionally
uses clip W spanning16..752 with `M4_TEST_PERSPECTIVE=1`. The reference uses
screen barycentrics divided by each original vertex W, independently of the
GPU interpolation path. Host llvmpipe and guarded J713151431 passed all1966080
pixels across8/11/28/29/32 scalar varyings, peak50.25C. This does not reproduce
the game artifact or establish behavior across clipped negative-W triangles.

The later original-material vertex reproducer isolates bad fog without texture
shading (workspace tools/gl-mesh-replay.c, original assets outside Mesa). Baseline
154343 has94753 bad fog pixels; disabling promotion154538 still has61135 and
noopt154741 is rejected as an unsupported pipeline. Optional AGX3 vertex controls
are disabled by default:

- `AGX3_VERTEX_SERIALIZE=1`: wait for the preceding native producer before each
  instruction. 155048 reduces the fog difference to one pixel by2 channel levels,
  matching the small-geometry control. This is causal evidence for dependencies,
  not a production serialization fix.
- `AGX3_VERTEX_NO_CACHE_HINTS=1`: skip only vertex register-cache hint assignment.
  155123 still has106168 bad fog pixels. This does not fix the failure.

Both controls preserve all shaders' mathematical operations. Installed diagnostic
Vulkan d97577c1aed310d891daef63079f11764bec45f118315e0da41eb52ce778f78a;
all tests guarded, peak50.5C. Need identify and fix the specific dependency.
