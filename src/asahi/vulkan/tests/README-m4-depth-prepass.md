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

### Address-lifetime isolation (2026-09-11 16:03 UTC)

`AGX3_VERTEX_EXPLICIT_WAITS=all|fma|load` moves existing waits to explicit
bit-preserving instructions. `AGX3_VERTEX_SOURCE_WAITS=1` additionally retires
loads before a physical GPR address input is overwritten;
`AGX3_VERTEX_REUSE_WAITS=1` instead retires only outstanding slot reuse. These
are disabled-by-default diagnostic controls, not qualified production policy.

The original material-vertex reproducer has 94753 fog pixels differing by more
than one channel level. Explicit waits alone leave58405; slot-reuse waits alone
leave31148. Address-source waits reduce this to one pixel off by2, matching
full serialization and the simple reference rasterization floor. Evidence IDs
155913,160119,160254 under the workspace experiment collection; all completed,
three thermal samples each, peak50.5C. This supports an asynchronous source
lifetime bug; the complete original game prefix remains to be qualified.

### Normal load source dependencies

The slot core now tracks all outstanding GPR readers for AGX3B device loads,
derives write-after-read dependencies, and releases readers when their slot
retires. Interpolation/LDCF forms without an inline wait field use explicit
waits before overwriting such an address. Seventy host slot checks pass.
The original material probe160518 matches the simple rasterization floor
without debug flags. The self-contained `m4_gl_load_lifetime.c` exercises
matrix/plane uniform loads and tangent-frame arithmetic across a48:1 W range,
checking393216 pixels on24 alternating-uniform draws against independent CPU
math. Host llvmpipe and M4 experiment161132 pass exactly, including cold first
draw. Build with `-lEGL -lGL -lm`; use the mandatory M4 guard.

The complete original prefix160833 still has fog patches (87497pixels>8),
whereas full vertex serialization161033 removes them (1349pixels>8,mean.21558).
Thus the address dependency fix is partial; it is not a complete game result.
Peak temperature across these runs51.5C. Remaining vertex timing issue under
investigation. The synthetic test has not yet been run on the pre-fix compiler.

The synthetic oracle also passes on preserved pre-fix binary369e72f027b6
(experiment161624,51C). It is a broad arithmetic regression control, not a
reproducer of the address-lifetime failure; the captured material test remains
the reproducer. Combined address+slot-reuse retirement161224 leaves13179
pixels>8; address+explicit existing waits161326 leaves53101. Neither removes
all game patches. `AGX3_VERTEX_SERIALIZE_SCOPE=main|internal` now narrows the
existing serialization diagnostic by NIR shader identity; optional
`AGX3_VERTEX_SERIALIZE_BEFORE=load|alu|output` narrows the instruction boundary.
Both require AGX3_VERTEX_SERIALIZE=1 and have no default effect.
