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

## 0 A.D. bring-up (2026-09-09 onward)

The real game reached a visible map under native M4 Hyprland in
`0ad-map-22`: terrain, units, buildings and interface are present. This is
an initial boot milestone, not rendering correctness or performance parity.
Building surfaces have conspicuous artifacts and the FPS overlay reads 1.
Normal batched submission still hangs; the boot profile separates every
draw into its own render pass using Zink's diagnostic synchronization mode:

```
M4_TEST_0AD=1 M4_0AD_UNCOMPRESSED=1 M4_0AD_NO_SHADOWS=1 \
M4_0AD_NO_POSTPROC=1 M4_0AD_SYNC_DRAWS=1 \
python3 /root/asahi-desktop/desktop-guard.py \
  --seconds 180 --output /root/asahi-evidence/UNIQUE-NAME
```

`M4_0AD_SYNC_DRAWS=1` adds `sync` to the game's `ZINK_DEBUG`; it is separate
from the older flush-thread-only diagnostic. Rendering stays on M4 through
Zink/Honeykrisp. Disabling S3TC selects CPU asset decompression, while shadows
and postprocessing avoid unsupported shader paths. No GPU clocks are raised.
The captured full scene also completed in `0ad-sync-unwritten-color` after
the depth-clear preservation fix, with a 53 C maximum monitored temperature.

`0ad-map-22` recorded 16 successful GPU/compositor health checks and a
54.5 C maximum across all six PMP channels, without a thermal trip or
recorded GPU fault. Its final process check nevertheless failed and the
guard rebooted: the engine's `EarlyInit()` calls `debug_SetThreadName("main")`,
so `pgrep -x pyrogenesis` missed the surviving game. This run remains marked
failed. The runner now identifies the mapped Wayland client and validates
its executable and UID through `/proc`; thermal-stop matching uses the
executable argv instead of the renamed comm. Final screencopy uses the same
bounded allowance as live screenshots. Successful tests stop the game and
compositor after collecting evidence, before disarming the watchdog.

The corrected repeat, `0ad-map-23`, passed its 180-second interval with
17 validated GPU/compositor health checks, 389 thermal samples, a 54.5 C
peak, no thermal trip, and no recorded GPU fault. The final process evidence
confirms comm `main`, UID 1000, and the private pyrogenesis executable; the
engine identifies M4 Zink/Honeykrisp. Live and final screencopies show the
actual map. The game and compositor were stopped and the watchdog disarmed
after evidence sync. This is bounded boot validation, not a completed match.
The kernel is `08f7797b248d`; deployed Mesa includes the runtime changes
through `2ae4d60b693` (its embedded build-version string predates that commit).
Evidence lives in workspace `_provenance/asahi-0ad-20260909/0ad-map-23/`.

### Setup and earlier failures

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

The first four configurations did not pass:

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

The next 0 A.D. vertex shader required `fma(-2, abs(x), 3)`. Mixed/minifloat
FMA now lowers F32 absolute/negate source modifiers to sign-bit operations
before register allocation, preserving a single fused operation. The native
all-register modifier path remains unchanged. `m4_vulkan_fma_abs` verifies
32 exact outputs for this expression and `fma(0.5, -abs(x), 1)`, including
both signs of zero and infinities. Build its SPIR-V header with:

```
glslangValidator -V --vn m4_vulkan_fma_abs_spv m4_vulkan_fma_abs.comp \
  -o m4_vulkan_fma_abs_spv.h
```

`0ad-fma-abs-2`, `-3`, and `-4` passed; `-4` removed the per-element diagnostic
printing before verification. The first run returned unchanged input values;
that initial failure is retained in evidence and its cause is not established.
Both host and ARM64 Mesa builds passed. `0ad-map-11` passed the former vertex
shader refusal and reached a separate fragment texture-sample refusal while
creating its shadow map. This still does not establish a rendered game map.

With shadows disabled (`M4_0AD_NO_SHADOWS=1`), `0ad-map-12` instead reached
an F16/F32 compare-select with full-width alternatives. The compiler now
widens that first comparator exactly before RA, preserving the condition
and selected bit patterns. `0ad-map-13` passed this shader and reached a
separate native draw-bind rejection; a rendered map is still unverified.

`m4_vulkan_compare_half` checks 32 half/full comparisons, including signed
zero, infinities, NaNs and half subnormals. `0ad-compare-half-2` passed all
outputs. Its first cold run returned all original inputs, resembling the
unresolved first FMA-probe behavior above; it is not qualified as a stable
cold-start compute test. Both host and ARM64 Mesa builds passed.

The isolated translucent-discard probe additionally exposed a half-register
LDIMM carrying a wait mask, although that instruction has no wait field.
The backend now emits the existing wait-retirement NOP before LDIMM. This
preserves dependencies rather than dropping the requested wait.

`0ad-map-14` localized the draw-bind refusal to pass type 3 (translucent
punch-through). The native bind path now retains that pass type, conservatively
disabling triangle merging for it. M4 B0's `updateFragmentShaderISPState` at
0x44747c..0x4474cc confirms the pass-type bits and the conditional merge-disable
bit. The exact draw-buffer predicate is not yet mapped, so this may sacrifice
merging performance; it does not remove blending or coverage rejection.
Reference arm64e slice SHA256:
`edd46e01416e595b51734428ece82aa0104868b35d585f492b2efa3ceebbce3a`.

`m4_gl_translucent_discard.c` creates a 64x64 RGBA8 target, clears blue,
then draws red at half alpha while discarding the left half. Compile with
EGL/GLESv2 and run under the mandatory bounded guard and M4 Zink environment;
its optional first argument saves raw RGBA for inspection. `0ad-translucent-3`
verified all 4096 pixels. Run 1 refused LDIMM before the wait fix; run 2 failed
its first preserved-pixel check without a pixel dump. These failures remain
in evidence. The isolated result is not a cold-start stability claim.

`M4_0AD_NO_POSTPROC=1` disables post-processing for the first-playable
profile: `0ad-map-15` otherwise rejects a blitter-style texel fetch with a
varying LOD. The runner now installs a Lua Hyprland rule before launch to
keep 0 A.D. floating at 1024x768 logical pixels (2048x1536 at the current
2x scale). Previously tiling expanded it to almost the entire panel.
Attempts 17/18 exposed launcher timing/legacy-IPC mistakes; the pre-map Lua
rule in attempt 19 fixes both. The game still stalled in its first scene
render in attempts 16/19; window size alone did not resolve it. No passing
map run has yet been established at this commit.

M4 now uses the existing retained-render transition when its 64 KiB VDM root
runs out of room. This applies the same storage-capacity check already used
on G17P; it does not impose a draw-count limit. `m4_gl_vdm_chain.c` emits 2048
draws with alternating uniform colors. `0ad-vdm-chain-3` crossed four root
boundaries and verified 256 exact green pixels, without a GPU fault. The
first split-path run completed but failed its pixel check; this remains part
of the unresolved cold-start behavior documented above. The preceding
full-alias link experiment faulted and was reverted: high address bits in
the native packet alone do not establish the command window's base semantics.

This change does not fix the whole game: `0ad-scene-replay-split` still
stalled in tiling in the first scene root, now with `linked=0`. The captured
frame is black and is not passing evidence. A 10-second host render timeout
and a 32 MiB initial parameter-buffer diagnostic also failed; both kernel
changes were reverted. Keep the normal GPU clock and thermal guards.

Native raster updates now use the immutable HAL200 bind tail even when
only depth/stencil or fragment state changes. The old fallback could emit
an AGX2 aggregate PPP packet, and the following draw lacked the selector
required for a fresh native tail. `m4_gl_depth_update.c` keeps the program,
uniforms and geometry constant while changing LESS to LEQUAL at equal depth.
`0ad-depth-update-2` verified 256 green pixels. Run 1 failed its pixel check;
the cold-start limitation remains. Host and ARM64 builds passed. Full-scene
`0ad-scene-native-ppp` still stalled in tiling, so this is not a boot claim.

Readback checkpoints allow the captured terrain and individual building
draws to complete. `0ad-scene-checkpoints` passed the prefix through call
23856; `0ad-dense-checkpoints` passed individual draws through 26623 and
the group through 27873, then stalled in a later fragment pass. These are
partial-scene diagnostics, not a rendered map or game stability result.

The VDM boundary probe now preserves the first draw on the left half of a
128x64 target while later draws affect only the right half. This checks
retained color, not just the last overwrite. `0ad-vdm-retain-tiles-2` passed
all 8192 pixels after 2048 draws. The first expanded test accidentally read
only 16x16 pixels and is invalid; that readback extent was corrected.
`0ad-vdm-retain-colors` also passed the original single-tile version.

A depth-only meta clear exposed a separate color-loss bug: the implicit
color mask (no fragment output at all) forced translucency but did not
preserve the tile value. HAL200 then stored undefined color. Extend the
existing zero-mask load/store preservation to bound, unspilled attachments
that the shader does not write. This preserves physical sRGB values too.
`0ad-depth-clear` returned black instead of red before the fix;
`0ad-depth-clear-fixed` preserved all 256 red pixels. The complete
`m4_gl_depth_clear.c` test additionally proves that the depth clear takes
effect: after clearing color to blue, a LESS-tested half-screen draw passes
against the new depth. `0ad-depth-clear-full-2` passed both checks; the first
cold run failed and remains evidence of the separate cold-start issue.
Host and ARM64 builds passed. Normal batched `0ad-scene-unwritten-color`
still stalled. The synchronized replay and actual map described above are
the subsequent boot evidence; they do not qualify ordinary batching.

Native bind state now also preserves the linker's triangle-merging restriction
(bit 26), and disables merging for points/lines as the common PPP path does.
The compiler sets this restriction for coarse derivatives, wide subgroup
operations and memory writes; it must survive independently of pass type.
`m4_gl_derivatives.c` checks derivatives across 128 triangles with discontinuous
varying values. `0ad-derivatives-merge-control-3` passed all 4096 pixels;
run 2 returned zeros, consistent with the unresolved first-readback issue.
The baseline also passed this probe, so it verifies compatibility rather than
demonstrating a fixed pixel mismatch. The first probe version used integer
division in its vertex shader and hit an unsupported IMAD form; geometry is
now supplied through an ordinary vertex buffer. Host and ARM64 builds passed.

Scene prefixes through calls 21462 and 22554 completed with ordinary batching
(`0ad-batch-prefix-*`). Adding the first normal-mapped object (a rock) at 23123 stalled in fragment
execution. An in-stream VDM barrier did not fix it and was reverted. A later
cold-boot prefix failed even at 22554, so the boundary is not deterministic
across boots. Preserving merging restrictions did not fix the batched scene
(`0ad-building-merge-control`); the synchronized normal-mapped-object replay
`0ad-building-sync-merge` completed. These tests retain the 60-second watchdog,
six-channel thermal monitoring and unchanged GPU clock. No performance or
rendering-correctness improvement is claimed from these diagnostics.


### Differential parallax diagnosis (2026-09-10)

The original captured frame through call 36525 now has matched llvmpipe and
M4 replays, including snapshots at 22554, 23123, 23931, 26623 and 36525.
Both receive identical captured inputs at 2048x1536. llvmpipe shows correct
roof tiles and tower stonework; M4's original scene still corrupts those
materials. The large discrepancy is already present at 26623, before the
later depth-only pass. The displayed FPS number belongs to the capture,
not to either replay's measured performance.

`0ad-llvmpipe-reference-2` and `0ad-m4-reference-pair` preserve the original
comparison. A separate diagnostic trace disables only the parallax branch
in fragment shaders 62, 65 and 71, preserving source-string lengths. With
that same variant on both renderers, pixels differing by more than 32/255
in any RGB channel fall from 117,490 to 13,998 at call 36525. A separate
fixed-eight-iteration variant leaves 39,102 such pixels. Neither variant
is installed in the game or offered as a rendering fix. The original
artifacts and the existing Zink synchronization requirement remain open.
These diagnostics implicate the parallax path but do not prove a specific
instruction or loop-control bug. Peak M4 temperatures remain at or below
54 C, with the 60-second watchdog and six-sensor thermal guard throughout.
Workspace evidence: `_provenance/asahi-0ad-20260909/`; the standalone
`0ad-frame-comparison.html` there provides original-frame split/difference
views without modifying the renderer outputs.

`m4_gl_parallax_loop.c` checks analytical results for fragment loop counts,
conditional entry, loop-carried coordinates/height, and final refinement.
It includes a gradient texture, explicit LOD with uniform and divergent
loop bounds, and implicit LOD with uniform bounds. All 28,672 pixel checks
pass on llvmpipe and on M4 (`0ad-parallax-loop-gradient`, peak 51.75 C).
This narrows the failure beyond these basic loop/sample combinations;
it is not a reproducer for the remaining game corruption. The test requires
M4 Zink unless M4_TEST_LLVMPIPE explicitly permits the reference renderer.
Mesa commits remain local and this working checkout has no Git remotes.

### Float-to-integer source lifetime fix (2026-09-10)

The native `flt2int` language-cast specimen discarded its floating source
unconditionally. The parallax shader converts a floating loop bound to an
integer, then reuses that float for truncation and the reciprocal step.
Discarding it produced invalid loop-carried height and final coordinates.
The runtime encoder now uses the register allocator's source/destination
cache hints and actual scoreboard waits for signed and unsigned casts.
The canonical specimen API and AGX3c path remain separate.

`m4_gl_parallax_loop.c` now accepts `M4_TEST_FLOAT_BOUND=1` or
`M4_TEST_UNSIGNED_BOUND=1` to exercise a floating bound followed by reuse.
A desktop-GL reduction failed 12,288 of 28,672 pixels before the fix and
passed all pixels after (`parallax-loop-float`, `parallax-loop-float-fixed`).
The tracked GLES test passes all 28,672 pixels for each cast variant on
both M4 and llvmpipe (`parallax-float-regression`,
`parallax-unsigned-regression`). A reduction using the game's full material
shader and generated textures previously returned black for every case;
afterward eight of nine 128x128 images match llvmpipe byte for byte, with
15 pixels exceeding 8/255 in the remaining case (`parallax-material-fixed`).

The original, unmodified game trace also improves: at call 36525, pixels
exceeding 32/255 in any RGB channel fall from 117,490 to 39,091, and mean
absolute RGB error falls from 3.8329 to 1.7875 (`0ad-f2i-lifetime-fixed`).
Roof detail is restored. Tower/pavilion material differences and batching
remain unresolved; this is not a claim of perfect rendering or higher FPS.
No game shader, effect setting, renderer fallback or clock workaround is
part of this fix. Guarded replay peaks at 53 C with the 60-second watchdog.

Host and ARM64 builds and the `agx3_alu_core`/`agx3_funop_core` tests pass.
Broader compiler suites still fail on existing half-abs, EOT, shuffle and
stale binary expectations; rebuilding the parent packer reproduces those
same failures (`build/0ad/f2i-baseline-testlog.txt` in the workspace).
Mesa remains local, with no remotes and no push.


### Retain fragment helpers through texture sampling (2026-09-10)

After the float-cast fix, mipmapped material tests still differed along the
boundary between two adjacent triangles: 256 of 16,384 pixels selected a
coarse mip even with zero parallax displacement. The SMP f156 bit copied
from native terminal samples was incorrectly treated as an implicit-LOD
marker and set on every sample. Clearing it on G16G preserves helper
coordinate updates needed by later texture operations. LOD mode is selected
independently; register-LOD samples can also precede an implicit sample.

The compiler now requests helper retention for AGX3B fragment shaders whose
NIR metadata requires quad helpers. Both implicit and register-LOD samples
honour that request. The normal colour termination protocol ends the shader;
there is no additional synchronization, fixed staging address or game shader
change. Canonical encoder callers retain their byte-exact native specimens,
and AGX3C helper retention remains unsupported. Execution evidence here is
J713/G16G only.

`m4_gl_conditional_mipmap.c` is the asset-free reproducer. Two triangles
sample generated mip chains: each base mip has a unique colour, and the
height texture uses different coordinates to prevent staging-bank reuse
from concealing the bug. The direct sample is a passing control. A uniform
branch, uniform loop and register-LOD sample each precede another implicit
sample. Before this fix those three cases fail 256 pixels each (768 total);
after it all 65,536 pixels pass, as they do on llvmpipe. Evidence:
`conditional-mipmap-four-before` and `conditional-mipmap-four-fixed`.
Build with `-lEGL -lGL`; run through the mandatory watchdog/thermal wrapper.
The reference run additionally requires `M4_TEST_LLVMPIPE=1`.

The full material shader with generated mipmaps now has only three pixels
exceeding 8/255 across nine 128x128 cases (maximum 9/255), versus roughly
256 per case before. The zero-displacement images are byte-identical to
llvmpipe (`parallax-smp156`). Serialization alone, coordinate retention,
COPY cache/discard changes and branch mask probes did not fix it. Reusing
one staging bank (0 or 32) only hid the zero-displacement failure; displaced
samples still consumed stale helper coordinates. Those probes were reverted.

The original game trace improves again at call 36525: pixels exceeding
32/255 fall from 39,091 after the float-cast fix to 4,460, versus 117,490
before both fixes. Mean absolute RGB error is now 1.0557, versus 3.8329
originally (`0ad-smp-helper-fixed`). This is approximately a 96.2% reduction
in large pixel differences, not perfect parity or a performance result.
Existing per-draw Zink synchronization remains necessary. Host/ARM64 builds,
5,622 texture-core checks and all 15 exact SMP-witness checks pass.

The final helper-retention policy also passed 180 seconds of native DCP
Hyprland with 0 A.D., 29 independent GPU/compositor health checks and 386
thermal samples. Peak temperature was 53.75 C, with the 60-second watchdog,
enabled CPU thermal policy and unchanged 338 MHz GPU clock throughout
(`0ad-retain-helpers-desktop`). The GPU renders both the game and compositor;
the existing Quickshell panel still uses its software backend. Indexed
native graphics/compute sampler compilation also passes. The broader
compiler-test failures described above remain; direct-IR fragment fixtures
also lack the NIR helper metadata that the packer expects.

During earlier float-cast desktop validation, a DCP flip failed with -5 and
the startup screenshot timed out. The guard saved evidence and rebooted;
the fresh-boot retry passed 180 seconds/30 health checks at 54 C
(`0ad-f2i-fixed-desktop-fresh`). This display failure is recorded separately
from the shader fixes, not presented as resolved by either one.


### Native fragment blend constants (2026-09-10)

The remaining large silhouette mismatch is draw 35357 (program 108): the
blue occluded horse is visible on llvmpipe but mostly absent on M4. A reduced
occluder/silhouette test reproduces this with ordinary depth/stencil state;
disabling stencil does not help, while disabling constant-alpha blending
does. The HAL200 launch path omitted the blend-constant upload performed
by the AGX2 path. Merely adding that old upload would alias red/green
constants with the native texture/sampler heap pointers in halves 0..7.

The native fragment ABI now reserves those first eight halves before the
ordinary fragment layout. Fragment main root/set loads, linked prolog root
loads and epilog blend constants agree on the shifted layout. The launch
program uploads RGBA blend constants at native halves 12..19; root and set
pointers start at half 20. Each draw uses its existing immutable root table.
No shader substitution, extra submission, wait or cache flush is introduced.
AGX2 keeps its old layout; the existing native vertex prefix is unchanged.

`m4_gl_blend_constants.c` checks eight depth/stencil/blend combinations plus
textured constant-colour/constant-alpha blending with four changing RGBA
constant vectors. The latter keeps both native texture heaps and blend
constants live, so the ABI overlap cannot be hidden by an untextured shader.
llvmpipe passes all 4,096 pixels. M4 fails 2,560 before and passes all pixels
after (`blend-constants-regression-before`, `blend-constants-regression-fixed`).
Build with `-lEGL -lGLESv2`; reference runs require M4_TEST_LLVMPIPE=1 and
hardware runs require the watchdog/thermal wrapper. The focused Ioto core
suite passes all 6,770 checks. The separate advanced-blend epilog test still
refuses its unrelated mixed-half fcmpsel shape; this change does not implement
advanced blend equations.

The original scene's horse crop drops from 701 large pixel mismatches to
2 (the crop already had 2 before the silhouette draw). At call 36525 the
full-frame count above 32/255 drops from 4,460 to 3,750 and mean absolute
RGB error from 1.0557 to 1.0396 (`0ad-blend-constants-fixed`). Remaining
scattered sampling differences are under investigation. Builds pass on
host and ARM64. Guarded regression peaks at 51 C and replay at 51.75 C,
with the 60-second watchdog and the unchanged CPU/GPU thermal policy.
Mesa changes remain local, without remotes or a push.

The commit-labelled build then passed 180 seconds of native DCP Hyprland
with 0 A.D., 29 independent GPU/compositor health checks and 390 thermal
samples, peaking at 53.25 C (`0ad-blend-constants-desktop-fresh`). Its SHA256
is `3535b9fa6e4b1971bbf25838af1f46084ec4345cdc5cb844ac588a104ad1fb6e`.
The preceding startup attempt hit the already observed DCP flip -5 and
screenshot timeout (`0ad-blend-constants-desktop`); the guard saved evidence
and rebooted. The fresh retry is successful, not evidence that this display
restart failure is fixed. Per-draw synchronization remains enabled; compression, shadows and
post-processing remain disabled in that baseline test.


### M4 shadow comparison capture and encoding (2026-09-10)

Enabling shadows in `0ad-shadows-current` rejects the terrain fragment
shader: one unsupported shadow sample prevents the whole native descriptor
lowering, so the backend first reports an ordinary sample with AGX2 operands.
The game exits; the guard records a 53.5 C maximum and reboots. This is a
compiler refusal, not a passing shadowed scene.

On macOS 25G83, 37 of 38 small Metal fragment pipeline probes compiled into native
M4 binary archives, including 2D/array/cube comparisons, fixed and indirect
resources, offsets, gradients, variable LOD and fetches. The optional color
gather probe had an invalid Metal source signature, not a hardware failure.
The 2D depth reference
uses a signalling COPY to staging half4 after x/y at halves0/2. The array
layer remains in separate auxiliary bank6. SMP uses f143=1, f74=0 and mask1;
implicit and level-zero forms are byte-witnessed for M4. Eight exact
fixed/uniform/varying-descriptor witnesses now pass the texture-core suite
(5,654 checks). The remaining captured modes are not thereby implemented.

The first implementation admits scalar 2D/array shadow samples through native
descriptor lowering, stages the reference, and retains helper coordinates when
later samples need them. The native sampler supplies the comparison function
and filtering; there is no CPU shadow calculation in the driver.
`m4_gl_shadow_compare.c` is the independent CPU-oracle test: eight comparison
functions, nearest/linear PCF, three reference depths, and edge clamping.
llvmpipe passes all 12,288 pixels. Native macOS M4 passes the equivalent Metal
test at float precision (maximum error 0.000108). Linux hardware validation is
pending at this checkpoint. Build the GLES test with EGL, GLESv2 and libm;
M4_TEST_LLVMPIPE=1 allows the reference, live J713 uses the mandatory guards.
All Mesa changes are local; the checkout has no remotes.

The Linux probe now renders its depth map with a depth-only framebuffer,
matching the game's shadow pass and testing attachment-to-sampler visibility.
CPU uploads of D32_SFLOAT images are separately unsupported in the current
native meta path. The GPU-rendered version passes llvmpipe's 12,288 pixels.
Its first M4 run exposed a kernel validation bug: zero color tilebuffer bytes
were rejected even for depth-only passes. Kernel commit `49ed361697e5` removes
that rejection while retaining the storage bound; the module builds with the
existing J713 configuration. Hardware validation follows after reboot.

After reboot with `49ed361697e5`, the GPU-rendered shadow test passes all
12,288 pixels on M4 Zink/Honeykrisp with zero byte error, matching llvmpipe.
This covers all eight comparison functions, nearest and linear PCF, three
reference depths, clamp-to-edge, and the depth-write-to-sample transition.
The guard saved normal completion without a GPU fault; the peak PMP reading
was 50.25 C, with the 60-second watchdog and thermal policy active.
Evidence: `asahi-0ad-20260910-macos/shadow-compare-depth-kernel`.

The actual shadowed scene next exposed two larger-shader boundaries. Its
shadow coordinates in r32 exceeded the ordinary SMP coordinate field even
though shadow samples copy them to bank zero. `8efc288d95c` validates those
sources against COPY's full register bank instead. Then a model vertex shader
needed 33 scalar outputs, beyond the old 32-output capture-derived limit.
Output bundling now uses the 256-half staging-file capacity (128 F32 values),
with occupancy tracking that does not overflow at 64 outputs. Host regressions
check scheduling/RA/value preservation/slot retirement through the game's
33-output case and already-terminal physical IR through 128 outputs.
Very wide live producer sets can still cause RA to insert instructions between
stores; those nonterminal shapes remain refused instead of risking GPR reuse.

The first visible shadowed scene still had filter errors and two longer runs
timed out in Fragment, both with and without API tracing. The single-tap test
did not cover the game's four-tap PCF. Extending the analytic test to that
weighted filter exposed 4,094 bad pixels of 24,576 on M4; llvmpipe had none.
Independent shadow samples reused the same coordinate staging bank before
earlier samples consumed it. Waiting for pending work removed the error.
The permanent fix adds staging ownership tokens to the existing scoreboard:
only overlapping coordinate publishers depend on the earlier texture result.
This covers fixed-bank shadow/LOD/array samples, ordinary samples and staged
fetches; per-slot vertex-fetch banks keep their existing reuse waits.

With the token fix, the expanded M4 test passes all 24,576 pixels, maximum
byte error 1 (tolerance 2). The backend regression checks both the overlapping
wait and independent nonoverlapping samples. The slot core's 60 checks and
texture core's 5,654 checks pass. The actual scene is being revalidated; the
earlier desktop timeouts are not yet claimed fixed by this pixel result.
Evidence: `asahi-0ad-20260910-macos/shadow-four-tap{,-wait,-tokens}`.

The corrected original shadowed frame (call 40898, 2048x1536) completes on
M4 and is compared with the identical llvmpipe replay. Pixels with an RGB
channel error above 32 drop from 39,134 to 3,520; mean absolute RGB error
falls from 2.464218 to 1.110645. The remaining fine filtering/rasterization
differences are comparable to the earlier non-shadow scene, not zero.
An interactive before/after/reference comparison is saved as
`asahi-0ad-20260910-macos/0ad-shadows-comparison.html`.
The tested commit build is `5ece7ba5b54`, Vulkan library SHA-256
`1182488c0aac417f09e9e6940ffc7aa2c4e6e58efdb11f770bedb09c2688cf27`.

Longer desktop testing still times out in Fragment, including after the
staging fix. An eight-frame headless replay also timed out before its first
snapshot, although the identical one-frame prefix passed independently.
Thus the shadow-pixel fix is qualified, but these results do not establish
stable sustained gameplay or blame Hyprland. Kernel render-backing reuse is
being isolated next. Watchdog recovery and saved thermal evidence remain
mandatory; the corrected single-frame run peaked at 52.25 C.


Further guarded diagnostics did not resolve sustained replay retirement:
allocating fresh render backing, allowing a 10-second kernel deadline, and
retiring each SMP immediately all still failed. The original two-second
kernel deadline and normal asynchronous texture scheduling were restored.
The serialized-SMP run completed three PNG snapshots, then timed out during
the fourth snapshot (which may be incomplete); peak temperature was 52.25 C.
Disabling only the three parallax loops also ended in watchdog recovery,
so that quality-reducing diagnostic is not a fix. These experiments do not
change the qualified shadow-pixel result above.


`m4_gl_wide_varyings.c` checks real interpolation of 28, 29 and 32 independent
user scalars (32, 33 and 36 native outputs including position), with 24
readbacks per interface. Its analytic CPU reference is independent of the
shader compiler. llvmpipe and the second M4 invocation passed all 1,179,648
pixels exactly. The first M4 invocation had one entire bad readback in the
28-scalar control; its 29/32-scalar cases passed. That first-readback anomaly
is retained as a failure, not dismissed as warmup. Evidence directories are
`shadow-wide-varyings` and `shadow-wide-varyings-repeat`; peak 52.25 C.
Build with `-lEGL -lGL -lm`, and use the same mandatory guards as other live
tests. `M4_TEST_LLVMPIPE=1` permits the software reference only.

Further isolation identifies the large model's material loop: omitting its
27 draws lets the original eight-frame scene complete, while retaining only
those draws reproduces the Fragment timeout. Disabling its parallax loop
makes that isolated model pass all eight frames. An initial explicit-LOD-zero
variant appeared to pass, but this result is INVALID: apitrace retained the
original shader-string length and truncated the longer replacement. The
model did not link, and replay continued despite API errors. The initial
explicit-gradient variant has the same defect. Neither establishes anything
about LOD selection or shader execution. Replacement strings must fit the
captured length, and successful replay validation must reject shader, link
and API errors independently of the process exit code. Uniform loop bounds,
disabled scheduling and disabled register cache hints did not fix the
original model. Valid evidence: `shadow-only-model-no-parallax` and
`shadow-only-large-model` under the same dated evidence directory.

Native shader compilation can now run on a separate macOS host. The tools
`src/asahi/compiler/test/m4-metal-loop-probes.py` and
`src/asahi/compiler/test/m4-metal-cross-compile.py` generate 18 loop variants
and compile their render pipelines explicitly for `applegpu_g16g`. Run on
the compiler host:

```sh
python3 m4-metal-loop-probes.py probes
python3 m4-metal-cross-compile.py --output compiled probes/*.metal
```

Install a missing toolchain with `xcodebuild -downloadComponent MetalToolchain`.
The compiler helper supplies a JSON render-pipeline description to
`applegpu-nt -N`, records exact commands and source/archive hashes, and uses
no GPU execution. The first corpus used Metal 32023.883 / toolchain 17F109,
macOS 26.6.1 build 25G76, SDK 26.5. All 18 variants compiled; the reusable
helper was also exercised separately on a loop/inner-if/shadow variant.
These binaries are compiler evidence, not M4 hardware success. Sources,
archives and extracted code are saved in
`asahi-0ad-20260910-macos/remote-g16g-loop-compiler`.
Apple documents the pipeline schema at
<https://developer.apple.com/documentation/metal/compiling-binary-archives-from-a-custom-configuration-script>.

The corrected, length-preserving LOD-zero shader compiles in llvmpipe but
still times out on M4. Explicit gradients compile in llvmpipe but are
rejected by the M4 backend. The original model also still fails with
coordinate bank zero, without border emulation, without anisotropy, and
with native control-flow or texture-marker diagnostics. All those encoder
changes were reverted; these failures do not isolate implicit derivatives.

Changing only the loop maximum from 20 to 4 does produce two validated
eight-frame M4 completions, peak 54 C. The four-iteration version retains
its backedge: its NIR and allocated IR differ from the failing maximum-8
variant only in the constant, hashes and diagnostic pointers. This is not
a loop-unrolling result and is not a quality-preserving fix. Replacing
only the final refinement division with multiplication also fails with
the original maximum 20. The small conditional-mipmap test's 20-iteration
loop cases pass; its first control readback still returns zeros, so the
overall test remains a failure. Evidence is saved under
`shadow-only-model-loop-max{4,4-dump,8}`, `shadow-small-loop20` and
`shadow-only-model-refinement-multiply` in the dated evidence directory.

`AGX_MESA_DEBUG=shaders` now prints each packed AGX3 binary as one hexadecimal
record with its shader stage, preamble status, encoding and byte count.
This supports comparison with the offline native archives even though the
compiler does not yet have an AGX3 instruction printer. The ARM64 build
passes, and the small loop test produced complete, length-checked records.


The conditional-mipmap regression now exercises 20 iterations, an inner
conditional sample, and loops inside uniform and varying outer conditions.
The last two cases encode their completed iteration counts in red. llvmpipe
passes all 114,688 pixels. M4 passes all loop/conditional cases, including
those counts, but its first direct-sample readback is entirely zero and the
overall test fails (16,384 bad pixels). Keep this failure visible; do not
discard the first readback as warmup. Evidence: `shadow-small-outer-loop20`,
peak 53 C.

Packed comparison confirms that the passing maximum-4 and failing maximum-8
model fragments are each 5,648 bytes and differ only in the LDIMM constant
(one byte at 0x375). The original maximum-20 fragment also runs in nine
generated two-triangle/material scenes: its machine code differs from
maximum-8 only in that same constant. Three of 147,456 pixels differ from
llvmpipe by more than 8/255 (maximum 9); zero-displacement cases match
exactly. The original game shaders are unchanged in this reduction; the
textures, geometry and uniforms are generated, including a GPU-cleared
depth texture. This is not a successful full-scene replay. Evidence:
`shadow-model-generated`, `shadow-model-generated-reference` and
`shadow-model-generated-comparison.json`.

A zero-displacement shader diagnostic in the captured scene still times
out, as does assigning the loop predicate to flag 0 and ordinary conditions
to flag 5. The latter encoder change was reverted. Neither repetition
alone nor the static shader binary now explains the scene failure; resource
and input-dependent execution remain under investigation.


The mipmap probe accepts `M4_TEST_WIDTH` (64..2048, default 128),
`M4_TEST_MODES` (a comma-separated sequence of modes 0..6, default all),
`M4_TEST_READS` (1..16 per draw, default 1), and
`M4_TEST_READ_DELAY_MS` (0..1000 before each read, default 0). Repeated
modes compile separate programs; repeated reads reuse the same draw. UV
coverage scales with resolution to preserve expected base LOD 2. Every
readback, including the first, contributes to the result; optional delays
are diagnostics only. Pixel storage is heap allocated and initialized to
0xa5 before each read.

On J713 with the baseline encoder, 1024-square modes `3,3` with two reads
each pass all 4,194,304 pixels, as does llvmpipe. The complete mode sequence
still times out in mode 3 after modes 0..2 pass. A subsequent `0,3` run
after reboot completes, but the first direct-sample read has 36,320 zero
pixels; reading the same draw again returns all pixels correctly. These
are separate observations, not proof that the timeout and readback defect
have the same cause. Evidence is in workspace `_provenance/experiments/`:
`20260910T100047738241Z-mipmap-lod-draw-repeat`,
`20260910T100105866747Z-mipmap-all-parameterized`, and
`20260910T100214401888Z-mipmap-direct-then-lod`.


`M4_TEST_FINISH=1` additionally calls `glFinish` between each draw and its
readbacks. This is a diagnostic, not a substitute for the API's implicit
readback synchronization. A printed `finished` marker alone is not success:
OpenGL can return after device loss, and the kernel fault evidence must be
checked. With per-submit waits enabled and Gallium threading disabled, the
large mode-2 loop itself times out; earlier mode-3-only attribution was
misleading. Evidence: `20260910T102058014017Z-mipmap-loop-strict-waits`.

`m4_vk_first_submit.c` isolates readback from Zink and render shaders. It
creates one Vulkan device/queue, fills 4 MiB on the GPU, submits an empty
batch carrying a fence, waits, and checks every word against a nonzero
pattern. `M4_TEST_UNCACHED=1` selects a non-cached coherent memory type;
`M4_TEST_READ_DELAY_MS` optionally delays the first read by up to 1000 ms.
`M4_TEST_SECOND_READ=1` checks the same buffer again after 100 ms without
new GPU work. Both reads count toward failure. Run only with watchdog and
thermal guards; compile with `-lvulkan`. The default device check requires
Apple M4; `M4_TEST_LLVMPIPE=1` allows a software Vulkan reference.

On a fresh boot, the Vulkan test reports 31,885 stale sentinel words out
of 1,048,576 despite a successful fence wait. Warm runs pass. Kernel and
userspace clocks require correlation: `/dev/kmsg` timestamps differ from
CLOCK_MONOTONIC, and the offset changes across boots. Paired markers show
that kernel retirement precedes the fence return in both the cold failure
and a warm success. This does not yet prove a cache or firmware cause.
Evidence: `20260910T102144732616Z-vulkan-cold-clock-correlation` and
`20260910T101952424515Z-vulkan-clock-correlation`.

A queue-initial-counter change and withholding the cached memory type were
tested and reverted. Neither is an established fix: uncached memory also
failed, delayed cached reads passed, and warm baseline GL runs passed.
Do not treat these diagnostics as a driver improvement or discard cold
first-readback failures as warmup.


The cold two-read Vulkan probe confirms that the missing data becomes
visible without another GPU submission: first read 62,496/1,048,576 stale
words, second read after 100 ms 0/1,048,576. Overall result remains FAIL.
Evidence: `20260910T102657045013Z-vulkan-cold-second-read`. This identifies
a completion/visibility problem independently of GL, textures, or game
shaders. It does not justify a fixed sleep in the driver or establish the
precise missing synchronization operation.


Additional first-submit diagnostics: `M4_TEST_DIRECT_FENCE=1` attaches the
fence directly to the GPU submission. `M4_TEST_CPU_BARRIER=1` executes
AArch64 `dsb sy` before validation; `oshld` selects `dmb oshld`.
`M4_TEST_SUBMIT_BARRIER=1` adds the selected barrier before submission.
`M4_TEST_CLEAN_BEFORE=1` cleans the initialized host buffer to PoC with
`dc cvac` and `dsb sy` (AArch64 only). These are opt-in experiments, not
changes to the driver's synchronization contract.

A cold direct-fence run also fails (45,443 stale words), excluding empty
submission forwarding as the sole cause. A full CPU barrier passed once
with a retirement-logging kernel, but failed on the baseline kernel repeat
(43,888 stale words); the weaker read barrier also failed (31,800 words).
All second reads were correct. Three subsequent warm direct-fence processes
pass without barriers. Do not promote the single full-barrier success to a
fix. Evidence: `20260910T151650754767Z-vulkan-cold-direct-fence`,
`20260910T152307130040Z-vulkan-cold-host-read-barrier`,
`20260910T152550793475Z-vulkan-cold-acquire-barrier`,
`20260910T153000854621Z-vulkan-cold-full-barrier-baseline-kernel`, and
`20260910T153119145685Z-vulkan-warm-direct-fence-repeat`.

`M4_TEST_CLEAN_BEFORE=discard` also invalidates the initial host buffer's
CPU cache lines before submission. Both clean-only and clean/invalidate
cold tests failed (69,490 and 88,784 stale words respectively), with correct
second reads. Neither CPU barrier nor pre-submit cache maintenance is an
established fix.

`m4_gl_conditional_mipmap.c` accepts `M4_TEST_ITERATIONS` (default 20,
0..128) to vary dynamic loop work without changing shader allocation.
The iteration-count color checks saturate as the RGBA8 target does.
llvmpipe passes all seven modes at 0, 8 and 40 iterations.

Optional fault evidence: set `HK_M4_FAULT_CAPTURE_DIR` to an existing output
directory and `HK_M4_FAULT_BO_BYTES` to the exact allocation size to inspect.
On the first G16G VM error in the process, Mesa snapshots matching retained
BOs directly, before returning device loss. Filenames include handles and
GPU VAs; stderr records BO labels and byte counts. `HK_M4_FAULT_REPEAT_MS`
(1..1000, omitted by default) takes a second snapshot after that diagnostic
interval. No GPU work is submitted. These are unsynchronized fault
snapshots, not valid replacement readbacks, and a delay does not fix a fence.

At 1024 square, the mode-2 twenty-iteration fault snapshot contains 270,336
expected blue words and 778,240 zero words. Forty iterations leave 168,960
blue and 879,616 zero words. Thus the output is incomplete at the timeout;
it is not a completed image with only a missing stamp. The changing amount
of completed work motivates checking execution progress and timing rather
than inferring an invalid static shader instruction from a timeout alone.
Evidence: `20260910T153829237500Z-mipmap-loop-fault-image` and
`20260910T154135004052Z-mipmap-loop40-fault-image`. Kernel baseline unchanged
for these two runs. The small 128-square twenty-iteration draw retires, but
its cold first read remains invalid (all 16,384 pixels), while its second
read passes; retain that overall failure.

The eight-iteration 1024-square probe passes all 1,048,576 pixels. Its
fragment stage retires about 23.6 ms after the tiling retirement. A repeated
twenty-iteration run leaves exactly 262,144 blue pixels; both fault snapshots
200 ms apart have identical SHA-256 and contents. No output-memory progress
was observed in that interval. The stopping point is close to the earlier
270,336-pixel result, but its mechanism is not established.
Evidence: `20260910T154551778585Z-mipmap-loop8-progress-after-fault` and
`20260910T154646492195Z-mipmap-loop20-progress-after-fault`.
