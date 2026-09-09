# J713 native desktop qualification

These are opt-in hardware tests for the identified T8132/G16G firmware and
experimental common Asahi driver. They do not enable another SKU or prove
conformance. Use only with the J713 watchdog and thermal workflow.

`asahi-session.py` starts the actual Omarchy user configuration under seatd,
checks its M4 Zink renderer, compares a red test terminal in Wayland screencopy
and imported DCP scanout, then starts SuperTux's first level. It records the
application's loaded libraries, actual GL renderer, screenshots, panel layers
and KMS completions. `gl-renderer-audit.c` only logs the active context's renderer
when SuperTux queries GL_VERSION; it forwards every query unchanged.

The interactive path runs a fresh real Vulkan computation, verifying 32 output
values, and checks Hyprland IPC and DCP import/completion status every five
seconds. `desktop-guard.py` verifies a 60-second hardware watchdog before any
work, and renews it only for a fresh, sequential successful GPU/compositor
check with a live thermal monitor. It never runs an unconditional heartbeat.
During long game upload batches it also accepts newly observed, at most
three-second-old kernel GPU-job retirements. The thermal check remains
mandatory for every renewal. Compute and screencopy requests may wait up to
120 seconds behind SuperTuxKart's upload work; without continuing GPU
retirements the 60-second watchdog still expires independently.
All six PMP channels are monitored at 0.5-second intervals, with enabled CPU
thermal policy, startup below 65 C and workload termination at 75 C. No GPU
clock increase or qualified dedicated GPU sensor is assumed.

The guard defaults to a bounded 1800-second session (maximum 3600). The user
can end it with Super+Shift+Escape using the existing supervised Omarchy
configuration. Failed progress stops GPU clients and requests reboot while
leaving watchdog recovery armed. Normal completion saves evidence before
explicitly disarming. DCP remains pinned until reboot.

Deployed layout on the qualification machine:

- `/root/asahi-desktop/{desktop-guard.py,desktop-launch.py}`
- `/root/hyprland-validation/{asahi-session.py,seat-user.py}`
- `/root/g16-submit-test/{experiment_watchdog.py,experiment_thermals.py}`:
  existing guard helpers only; no lab GPU driver is loaded.
- `/root/m4-vulkan-compute-min`: the Vulkan compute test compiled with
  `TEST_DEVICE_ID=0x8132`.
- `/root/m4_dcp_rtkit-asahi.ko`: existing native DCP driver rebuilt against
  the running kernel; it supplies display scanout, separate from Asahi GPU.
- `/opt/asahi-m4-port`: isolated Mesa, explicit Vulkan/EGL JSON files, and the
  existing split-render/display Aquamarine build (`3939123` in the workspace
  `reference/aquamarine-m4` checkout).
- `/opt/m4-hyprland/omarchy.lua`: existing supervised user configuration.

Build the context logger for arm64 with `-O2 -Wall -Wextra -fPIC -shared -ldl`
and install it as `/opt/asahi-m4-port/lib/gl-renderer-audit.so`.
Start from root, choosing a new evidence directory each time:

```
systemd-run --unit=m4-asahi-desktop --collect \
  --property=RuntimeMaxSec=1900 --property=TimeoutStopSec=8 \
  python3 /root/asahi-desktop/desktop-guard.py --seconds 1800 \
  --output /root/asahi-evidence/UNIQUE-NAME
```

Do not start competing watchdog owners or reload a stopped firmware session
without a fresh reboot. The launch script retains an already-running Asahi/DCP
session. `g16_lab` must be absent. This is a supervised bring-up desktop, not an
automatic boot service or a claim of GPU recovery/thermal throttling support.

## SuperTuxKart full-race test

Set `M4_TEST_SUPERTUXKART=1` when invoking the guard to run SuperTuxKart 1.5
through Zink/Honeykrisp in an 800x450 logical Wayland window. The isolated
configuration uses Lighthouse, four AI karts, one lap, `--profile-laps=1`,
and disables dynamic lights and texture compression. This test requires the
game to exit successfully after printing nonzero frame-count and timing
statistics; mapping a loading window, reaching the deadline, or ending the
session manually does not count as completing the demo. It saves the result
as `supertuxkart-demo.json`, with game logs and intermediate screen captures.

## J713 rendering correctness probes (2026-09-09)

The standalone `m4_zink_depth_cube.c` test uses surfaceless Zink/Honeykrisp and
requires an M4 renderer. Run each invocation inside `bounded-guard.py`; it
arms the 60-second watchdog and samples all six thermal channels before the
probe can submit GPU work. The test covers ordinary depth ordering and depth
preservation across a 65,536-instance parameter-pressure draw, then samples
six solid cube faces. `--cube-grid` instead verifies every texel of a 4x4
pattern on each face against CPU-generated directions: 96 draws and 98,304
exact RGBA8 pixel comparisons. This checks orientation as well as face choice.
The `--direction`, `--face-math`, `--array` and `--array-face` modes isolate
uniform loading, major-axis arithmetic and ordinary array sampling.

Native cubemap descriptors must retain six 2D array layers, and the late cube
lowering must produce an integer layer in backend coordinates. With those
changes, the previous independent reciprocal-per-axis calculation still
failed the positive-Y direction on J713. Selecting the face before computing
one reciprocal magnitude passes all six axes and all 96 patterned samples.
This avoids evaluating reciprocals of the unused zero axes and reduces the
coordinate calculation. The precise cause of the old combined shader's
failure is not established by these tests; isolated face arithmetic passed.

Evidence under the workspace `_provenance/asahi-stk-20260909/`:

- `cube-major-axis-1`: two depth cases and six axis directions passed.
- `cube-grid-1`: two depth cases and 96 patterned face samples passed;
  peak exposed PMP temperature 51.25 C.
- `float-texture-1`: RGBA32F upload/subimage and vertex-stage `texelFetch`
  passed 4,096 exact output pixels using `m4_zink_vertex_fetch --float-texture`.
- `minmax-1`: 32 exact finite float min/max results passed using
  `m4_vulkan_minmax.c`; includes positive, negative and equal operands.

Both ARM64 and host Mesa builds passed. Host `hk_native_descriptor`,
`hk_native_sampler`, and `agx3_graphics_compile_test
--cube-sample-lowering-only` passed. These bounded checks do not establish
seamless filtering, mip selection, cube arrays, Vulkan conformance or a
correct completed SuperTuxKart race. The full game remains the acceptance
workload.

`m4_zink_perspective.c` independently checks a triangle with a 4:1 clip-W
ratio against CPU barycentric perspective equations, with a one-unit UNORM8
tolerance. `perspective-clip-1` passed 1,830 interpolation pixels and five
near/far/negative-W clipping cases. `m4_zink_stk_vertex_layout.c` uses STK's
48-byte skinned vertex and 44-byte instance layouts, four instances and an
offset 16-bit index buffer. `stk-vertex-layout-5` passed all twelve tested
attributes (12,288 pixels), including packed signed normals, half floats,
signed bone indices, instance divisors and normalized signed texture offsets.
The probe exposed two compiler refusals: instance-ID reads incorrectly vetoed
terminal vertex output bundling, and FCMP pseudo-ops escaped the nonzero
float-comparator constant lowering already used for FCMPSEL. Both now use the
existing qualified encoders; four focused vertex-output host tests passed.

Full race `stk-demo-19` still showed stretched geometry and damaged kart
surfaces. It was stopped for further isolation, not accepted as a completed
race. Opt-in `M4_STK_APITRACE=1` records the game's EGL/GL commands using the
isolated `/opt/m4-apitrace` wrapper. The trace path is recorded in
`graphics-trace.json`; each trace has a unique name under the desktop user's
`.local/state/m4-asahi-traces/`. The usual watchdog and thermal requirements
also apply during recording and GPU replay.

`bounded-guard.py --seconds N` permits a bounded command longer than the
normal 40-second no-renewal probe. For N > 40 it records a new kernel log
stream and renews only for newly observed, monotonically increasing Asahi
job retirements at most three seconds old, at most once every five seconds.
The thermal monitor must remain healthy; an exited kernel monitor stops the
command and leaves the watchdog armed. A deadline or loss of GPU progress
cannot be hidden by an unconditional heartbeat. This mode is used for the
resource uploads needed to replay a captured game frame. Maximum duration is
1,800 seconds; it is a test deadline, not a hardware limit.

`stk-demo-20` captured 392 EGL frames (about 144 MiB compressed). A local
llvmpipe replay produced intact sky and kart geometry from those same calls,
providing a software reference rather than an M4 success claim. Additional
J713 probes `culling-1`, `float-texture-tall-1`, and `float-texture-multi-1`
passed winding/cull combinations, 72 samples spanning the 4x2049 skinning
texture, and eight groups of sixteen vertex fetches respectively. The X11
replay setup exposed a separate loader bug: explicit GALLIUM_DRIVER=zink
with a software presentation winsys was given an empty driver option table.
The loader now supplies Zink's option descriptions for that explicit choice.

### Live unary operands and the stretched grass regression

The ten draw checkpoints in `stk-replay-5` isolate the large sky/grass streaks
between calls 23332 and 23360, where program 47 executes `sp_grass_pass.vert`.
Its wind uniform is 190.6785 and its vertex displacement evaluates sin/cos.
`trig-1` failed 10/32 numeric comparisons, with huge negative-input outputs;
`round-1` showed correct floor results but fract(x) equal to -floor(x).
The rounding encoder retained the canonical specimen's source-discard bit,
although the allocator needed x again after floor(x). Runtime unary encoding
now carries actual source/destination cache hints and scoreboard waits.

- `round-3`: all 32 floor/fract results passed bit-for-bit.
- `trig-2`: all 32 sin/cos results passed absolute tolerance 0.002, including
  the captured wind input. This qualifies the existing approximation for
  these inputs; it is not a general trigonometric accuracy claim.
- `stk-replay-6`: the full captured first scene replayed successfully with
  the huge stretched polygons gone. At call 23360, pixels differing from the
  llvmpipe reference by more than three UNORM8 values fell from 639,708 to
  58,031. Differences in the karts, shading and sky remain under investigation.
- `unary-1` exposed the same destructive lifetime assumption in reciprocal
  and inverse square root: rsqrt(x) discarded x before rcp(x), producing
  infinity and dropping x from the final addition.
- `unary-2`: after preserving actual reciprocal/rsqrt lifetimes and their
  distinct wait-field layouts, all 32 live-source reciprocal/rsqrt/exp2/log2
  results passed absolute tolerance 0.002.

The canonical funop byte-replay test still passes all 55 checks. ARM64 and
host builds pass, as does the targeted cube-lowering test. The broad graphics
compiler suite still has unrelated stale exact-binary/refusal expectations
and unsupported AGX3c cases; it is not reported as a passing suite. These
results do not yet qualify a correct full SuperTuxKart race.

### Skinning texture coordinates (2026-09-09)

`m4_zink_skinning.c` combines packed 48-byte vertices, 44-byte instances,
sixteen vertex texture fetches, four weighted bone matrices, quaternion
rotation and the associated normals/tangents. Each of four input vertices
and four instances writes seven vec4 results. The fragment shader encodes
one float's bits at a time into RGBA8, avoiding any dependence on float
render-target support. All 448 values are compared with independent CPU math
at absolute tolerance 0.0002. `--reference` explicitly selects a local
llvmpipe reference; normal mode requires Zink and an M4 renderer.
`--matrices`, `--identity-bones` and `--identity-rotation` isolate the data
while keeping the shader's calculations present.

The test reproduced the game's bad positions and normals. `skinning-3`
failed geometry while color/UV passed; both identity diagnostics also failed.
Individual three-channel texture reads passed (`fetch-xyz-1`). Two independent
encoder mistakes were involved:

- Coordinate staging masked the source half-register index with 0x7f,
  aliasing high registers with unrelated low registers. Unlike PACK_SMPIR,
  this COPY path did not subsequently restore the high bit.
- Independent vertex texture loads overwrote the same staging coordinates
  while earlier fetches could still read them. Vertex fetches now use one
  eight-half coordinate bank per scoreboard slot and retire that slot before
  reusing its bank. Other texture/image protocols keep their existing layout.

With just these changes, `skinning-11` passed all 448 values. The earlier
plain-ALU destination and register-MOV experiments did not fix the reproducer
and are not part of this change; their patch is saved in the workspace
provenance. The host texture-core suite passes 5,571 checks, including high
coordinate source registers with all cache hints and six dependency slots.
The complete correct race remains the acceptance target.

The skinning oracle now also covers 40-byte vertices, divergent bone indices,
indexed drawing from a nonzero offset, and the view/projection uniform block
(512 float results). `--fixture DIR` accepts four external vertex records,
four instance records, a bone atlas and a matrices block; captured application
assets are kept outside this MIT source tree. `skinning-camera-1` passed all
512 results with the captured first kart's data. This is a numeric shader test,
not a claim about the complete game's appearance.

`stk-coverage-1` replayed the unchanged kart shaders with clear alpha zero and
saved alpha as well as RGB. The first kart draw (254 covered pixels) and all
kart draws (1,751 covered pixels) matched the llvmpipe reference's coverage
exactly. RGB remained incorrect, directing further investigation to shading
and texture sampling. The guarded replay completed with no thermal failure;
the hottest exposed sensor reached 53 C.

### Mipmap selection and helper pixels (2026-09-09)

The captured kart's coverage, UVs and vertex colors matched the reference,
and linear filtering without mipmaps differed by at most one UNORM8 step.
With mipmaps enabled, 226 of its 254 pixels differed by more than three steps.
A fixture replay with each mip filled with a distinct constant color showed
that levels 1–10 selected correctly, while explicit level zero incorrectly
used implicit derivatives. Keep AGX3B `txl(..., 0)` explicit in every stage,
not just compute (`stk-modes-3`).

Small procedural triangles then reproduced invalid derivatives at primitive
edges. Pixel-position derivatives were correct, while texture-coordinate
helper values were wrong. The f63 marker previously emitted on the first
interpolation suppressed those helper values. With that marker clear,
`stk-marker-1` matched the small-triangle derivative reference exactly and its
mipmap reference within one UNORM8 step. The isolated captured kart dropped
from 226 to eight pixels above the three-step threshold (maximum nine steps);
this remaining difference is not claimed to be exact rendering.

Use NIR's coarse/full-quad helper requirements to retain helper values during
interpolation and coefficient loads. Plain varying shaders keep the existing
native marker. The ordinary color writeout protocol remains responsible for
completion. Primary/secondary triangle-merging bit experiments did not fix
this error and are not included.

Derivative instructions also need their own source-lifetime handling: their
composer root requires f29=0, so optional last-use discard must not be encoded.
Retire pending input producers with the existing slot-wait sequence rather
than requiring unqualified inline derivative waits.

`m4_zink_mipmaps.c` is a self-contained regression with generated mip colors,
explicit levels 0–8, automatic selection and XY derivatives on full-size and
small triangles. It verifies both covered-pixel counts and RGB values against
an analytic oracle (tolerance two UNORM8 steps). `--explicit-only` isolates
explicit selection. `--reference` requires llvmpipe; normal mode requires an M4
Zink renderer. Build like the other EGL/GLES3 tests and run inside the guards.
`mipmaps-2` passed all 46,048 pixel checks with the conditional helper fix;
the host oracle passed as well. The host funop, texture and varying suites
passed 59, 5,571 and 977 checks respectively, and both Mesa builds passed.
Full-scene and complete-race validation are separate acceptance checks.

### Array mip levels and texture growth (2026-09-09)

The mipmap test now accepts `--array` (three independently colored layers)
and `--immutable` (allocate all levels before uploading). It additionally
checks implicit sampling clamped to level zero and integer `texelFetch`.
Exactly representable constant explicit LODs use the immediate encoding,
leaving the auxiliary staging bank available for the array layer. Dynamic
register LOD combined with an array layer remains unsupported.

Mutable texture growth initially corrupted level zero of layers one and two.
Allocating immutable storage isolated this to integer array reads: filtered
sampling was correct, but `texelFetch` selected layer zero. The fetch staging
layout places the layer at half-register offset six, after the reserved LOD
pair, rather than immediately after XY at offset four. Reserve the full eight
half-register bank as well.

With this correction, `mipmaps-immutable-2` and `mipmaps-array-4` each passed
162,720 pixel checks on J713, including texture growth/copying. Both guards
verified a 60-second watchdog and healthy thermal monitoring; peak exposed
temperatures were 51.5 C and 51.25 C respectively. The host llvmpipe oracle
also passed. Host texture, funop and varying suites passed 5,571, 59 and 977
checks. These tests do not establish complete Vulkan conformance.

### Complete Lighthouse race (2026-09-09)

`stk-replay-11` with Mesa `d693fcdee6d` completed the captured first frame.
Against llvmpipe, pixels whose largest RGB error exceeds three UNORM8 steps
fell from 136,605 in replay 10 to 2,978 in replay 11. The kart group has 55
such pixels; the opaque track checkpoint has 125. Thin cube-face seams remain
visible in the sky, and foliage has smaller residual differences. This is
substantial progress, not a pixel-perfect rendering claim.

`stk-demo-21` then completed the normal Lighthouse race under the actual
Omarchy Hyprland session: four AI karts, one lap, `--profile-laps=1`, exit 0,
7,321 frames in 175.244003 seconds, reported average 41.776035 FPS. The path
was Asahi kernel, Honeykrisp Vulkan, Zink and native DCP scanout. The guard
verified 38 compute/compositor/scanout health checks and a 60-second watchdog.
All six exposed thermal channels remained healthy; the peak was 54.75 C.
GPU clocks remained at the boot setting and CPU thermal policy stayed enabled.
This qualifies this bounded race on J713, not general long-term stability.

### Native M4 cubemap filtering (2026-09-09)

The earlier cube-to-array lowering selected the right faces but discarded
cube-aware edge filtering and derivative handling. The captured sky had
1,809 pixels more than three UNORM8 steps from the reference, including
visible dashed seams. M4's native SMP cube selector is f76=4. It takes
projected UV and a packed integer face, rather than the original XYZ ray.
Use the existing face projection, coordinate bank zero and PACK_SMPIR bank
six, and retain a cube image descriptor with a cube count. This M4 selection
does not change the unqualified G17P cube path. Cube arrays and explicit cube
LOD remain outside this qualified subset.

`cube-native-grid-6` passed all 96 face/texel cases (98,304 exact pixels).
`sky-native-normal-1` ran the isolated captured sky shader with its original
textures and mip generation: all 1,440,000 pixels were within one UNORM8 step
of llvmpipe and the seams disappeared. `cube-native-seams-3` passed all twelve
edges at tolerance one and all eight constant-color corners exactly, using
the self-contained `m4_zink_depth_cube.c --cube-seams` test. Host reference
mode is available with `--reference` as the final argument. These guarded
runs peaked at 51.75 C, with no thermal failures.

An initial corner oracle required the arithmetic average of three differently
colored faces. M4 differed from it by up to three color steps. That is not a
portable conformance requirement: the [Vulkan sampling rules](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/chapters/textures.adoc)
allow different corner reconstruction, while requiring constant preservation
when the incident texels agree. The committed corner oracle checks that
requirement; the differing-color measurements remain in the evidence.

The compiler test now compiles both native cube projection and the retained
cube-to-array path. Host texture, funop and varying suites still pass 5,571,
59 and 977 checks. Host and ARM64 builds pass. Combined texture-coordinate
and descriptor dependencies retire through the existing slot-wait sequence
before staging, instead of failing when more than one producer is outstanding.

`stk-replay-12` confirmed the change in the complete captured scene. The sky
adds no pixels above the three-step comparison threshold; the final frame has
2,267 such pixels, largely in foliage. The replay peaked at 53.75 C.
`stk-demo-22` then completed another normal Lighthouse/four-kart/one-lap race
under Omarchy Hyprland, with exit 0, 7,182 frames in 169.127014 seconds and
reported average 42.465126 FPS. All 36 compute/compositor/scanout health checks
passed and the hottest exposed sensor reached 54.75 C. Both completed races
used the same boot without a GPU reset. Moving screenshots show the sky seams
removed. These are visual and bounded stability results, not pixel identity
with llvmpipe or a full conformance claim.

### Foliage filtering oracle (2026-09-09)

Removing alpha discard from the whole scene changes depth occlusion, so its
grayscale difference image is not a reliable isolated sampler test. An external
fixture for the first foliage draw instead uses the original 127 packed
24-byte vertices, 261 indices, fourteen 44-byte instances, camera block,
wind uniform and texture mip levels. Application shaders/assets remain outside
this MIT source tree. `grass-modes-1` reproduced the residual differences.
With discard removed, both renderers covered exactly the same 77,231 pixels.

`grass-uv-1` exported the interpolated U and V float32 bit patterns through
RGBA8, separately from the texture reads. The median absolute U/V differences
from llvmpipe were 0.00000730 and 0.00000441; some overlapping/thin geometry
has larger differences. Feed each renderer's own measured UVs into independent
nearest and bilinear repeat-wrap equations. On M4 all 77,231 nearest alpha
samples match exactly, and every linear alpha sample is within one UNORM8
step (maximum 0.971135). The texture reads are consistent with their UV inputs;
the remaining cross-renderer differences originate earlier than those reads.
This does not establish exact interpolation or depth-tie behavior.

`texture-filter-oracle.py` implements that independent check using NumPy. Its
inputs are a raw RGBA8 texture, separate U/V bit-pattern images, and nearest/
linear grayscale samples with alpha 255 for coverage and zero for the clear.
All inputs use the same row order; width/height describe the texture. It checks
base-level repeat filtering only and leaves interpolation validation separate.

## 0 A.D. bring-up attempt (2026-09-09, not passing)

`M4_TEST_0AD=1` selects 0 A.D. instead of either SuperTux test. The runner
starts release 28 on `skirmishes/acropolis_bay_2p`, Athens versus a Sparta
Petra AI, deterministic seeds, a 1024x768 requested Wayland window, the GL
backend through Zink, no sound, and the FPS overlay. The installed private
Debian ARM64 package is `0.28.0-3+b2`, with data `0.28.0-1`, under
`/opt/m4-0ad`; `/usr/share/games/0ad` points to its data directory. Its missing
versioned libraries are private too. Package URLs and SHA256 checks against
the Debian package index are in workspace
`_provenance/asahi-0ad-20260909/packages.json`.

The game gets isolated configuration, cache and state directories, Honeykrisp
error logging, and the same watchdog/thermal guards as the desktop tests.
The guard explicitly stops `pyrogenesis` on failure and copies its engine
logs before final evidence sync. The bounded result checks process/window
survival and the engine-reported renderer; screenshots still need inspection
to establish actual map rendering. It is not a full-match completion test.

```
M4_TEST_0AD=1 python3 /root/asahi-desktop/desktop-guard.py \
  --seconds 180 --output /root/asahi-evidence/UNIQUE-NAME
```

`M4_0AD_UNCOMPRESSED=1` additionally hides S3TC from this game's GL context,
selecting its existing texture decompression fallback. It changes asset
upload, not GPU rendering, and is an explicitly experimental diagnostic.

No tested configuration passed:

- `0ad-map-1`: compositor screencopy timed out before the game launched.
- `0ad-map-2` and `0ad-map-3`: the engine identified M4 Zink/Honeykrisp,
  but command recording rejected buffer-to-image uploads. Detailed logging
  localized `VK_ERROR_FEATURE_NOT_PRESENT` to `hk_cmd_meta.c:1850` at that
  source revision. The native upload path covers only selected compressed
  formats; the game's DXT family includes additional formats. Exact rejected
  upload parameters were not captured, so this remains a likely cause.
- `0ad-map-4`: disabling S3TC passed that initial rejection, then GPU job 89
  failed retirement with EIO and Zink reported device loss. The guard saved
  firmware trace/MMU diagnostics and requested reboot. The captured early
  desktop frame contains only the wallpaper; no rendered map was verified.

All four tests verified a 60-second watchdog. Peak of all monitored PMP
channels across the attempts was 54 C, with no thermal trip. GPU clocks were
not changed. Evidence is under `_provenance/asahi-0ad-20260909/` in the
workspace. Next work is capturing exact rejected upload parameters and
isolating the first uncompressed submission that loses the device; no
kernel or Mesa rendering implementation was changed for these attempts.
