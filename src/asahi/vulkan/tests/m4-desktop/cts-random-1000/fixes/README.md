# Corrections after the frozen 1,000-case baseline

Do not overwrite the baseline outcomes. Retests below use separately recorded
source/library/test identities and preserve each case's comparison.

## Render-pass crashes (baseline indices 0 and 848)

Both now Pass in `20260913T184532088442Z-cts-crashes-input-aspects`.
The historical CTS builds two subpasses: subpass 0 has no input attachments,
subpass 1 reads attachment 0. Its depth/stencil input-aspect records incorrectly
name subpass 0, causing Mesa's inputAttachmentIndex assertion. The existing
color correction was incomplete; the corrected variant fixes all six equivalent
depth/stencil constructions too. Images, draws and comparisons are unchanged.

`input-aspects-combined.patch` includes the earlier identified CTS corrections,
with the input-aspect correction expanded. Exact binary hash and restored
original source/executable checks are in `input-aspects-build.json`.
Remote binary: `/root/cts-1.0.2.6/deqp-vk-input-aspects`.
This fixes invalid CTS setup; no Mesa assertion was removed or bypassed.

## Indirect draw GPU reset (baseline index 6)

Fixed a stray PPP selector before consecutive indirect draw commands when no
new native bind tail was emitted. The direct draw paths already observed this
framing rule. Both indirect forms now use the same checked emitter.

`20260913T185354613522Z-cts-indirect-framing`: all eight single/multi-command,
indexed/non-indexed line/triangle cases Pass, including the original reset.
Launch traces retain the emitted bytes. Host framing/range regression passes;
maximum monitored temperature 59 C, no GPU fault or recovery.

## Integer image stores and zero-area tessellation setup

`20260913T185453340373Z-cts-integer-tess`: 12/13 Pass. Baseline indices
23, 191, 351, 432, 438, 490, 542, 545, 553 and 662 now Pass with signed and
two-channel integer storage routed through the existing native I16/I32 writes.
Index 29 now compiles but still fails image comparison and remains open.
Index 6 also passes without launch tracing.

Index 9 passes with the separately recorded CTS binary correcting its invalid
zero-sized render area to the existing 1x1 framebuffer extent. This repeats the
previously identified tessellation-control CTS correction; no driver validation
was relaxed. Combined patch and exact binary manifest are retained here.

## SNORM image stores and RG32 float framebuffer loads

`20260913T185916978817Z-cts-snorm-rg32`: baseline indices 148, 907, 462,
and 707 Pass; index 29 remains a pixel mismatch. SNORM storage takes F32 input
through the existing imgwci path and its PBE descriptor converts to SNORM.
RG32Float native framebuffer-fetch text at offset 76 confirms W32/mask=3 with
no new packet fields; source, text, compiler identity and decoded fields are
retained in `native-rg32-fetch/`. All 55 tile-core regression checks pass.

## Dynamic 3D LOD and fragment input arrays

`20260913T190648828062Z-cts-lod-input-clean`: indices 116 and 201 Pass. The 3D LOD
register form uses the native packed LOD bank, with byte-exact level/bias
compiler witnesses and 6,274 texture-core checks. The saved index-116 shader
also compiles offline. Dynamic fragment inputs are lowered through Mesa's
existing indirect-dereference control-flow pass before coefficient selection,
so no legacy ITERPROJ or mixed-width coefficient-address arithmetic remains.

## Signed normalized framebuffer readback

`20260913T191113841153Z-cts-snorm-blit`: indices 403, 649, 682 Pass after setting NORMFLT's signedness bit
for S8NORM tile reads. The previous unpack always treated the bytes as UNORM.
Native RG8Snorm text at offset 88 confirms f40=1; all 95 producer-core checks
pass, including the exact signed witness. The batch also validates the volume
blit correction documented separately below.

## Volume blits without vertex-layer output

The shared meta blitter binds one destination slice at a time when layer output
is unavailable, adjusting the source Z transform while retaining filtering and
source volume sampling. HK routes volume blits to that path rather than the
2D-only slice helper. Both original failures (161 and 260) Pass in the retained
`cts-snorm-blit` batch. The CPU records draws; sampling and pixel writes execute
on the GPU. This is not a qualification of every 3D/array/mip blit combination.

## Historical CTS midpoint reconstruction oracle

Baseline index 396 Pass in `20260913T190737203629Z-cts-pixel-mismatches` with the separately identified combined
CTS variant. It includes the previously established correction of reversed
0.25/0.75 midpoint weights, retaining hardware filtering and the complete image
comparison. This is a CTS oracle correction, not a new driver change.

## Integer and mixed render targets

`20260913T192310726640Z-cts-mrt-final`: all eight cases Pass (baseline 29, 153, 428, 911, 74, 357, 787,
975). G16G RG32 integer fragments export raw W32 lanes, as the native witness
confirms, so use the established I32 end-of-tile program rather than Neo's
format-specific conversions. Mixed end-of-tile sequences now select the same
integer/normalized/float conversions already used by single targets.

Integer byte framebuffer reads now unpack their physical byte pairs before
NIR applies signed extension. Their vector widths are reflected in validation;
sample-mask reads tolerate the optional cache hint without requiring a new
encoding. Both saved failing integer shaders compile offline (302/308 bytes).
RGB565 uses three F16 tile lanes; the retained B5G6R5 native fetch confirms
W16/mask=7 and its exact encoding is covered by the 56-check tile-core test.

## BGRA signed normalized blits

Baseline index 661 Pass in `20260913T193830753718Z-cts-bgra-snorm`.
The BGRA SNORM format uses the existing S8NORM tile conversion and descriptor
channel swizzle, but was absent from the native tile allowlist. Admit it and
use the same signed normalized export selector for mixed targets. The original
CTS blit comparison passes; peak monitored temperature 56.75 C, no GPU fault.

## Graphics shader memory visibility

Original indices 235, 543, 601, 881 and 965 Pass in `20260913T195017129647Z-cts-render-visibility`,
without a diagnostic environment override. Reader-stage controls passed with
compute/fill writers and failed with graphics writers. A native VDM end barrier
did not help; the native strong CDM cache barrier made all five pass.
Track shader memory writes independently of framebuffer exports and order the
cache barrier after renders containing those writes. The existing conservative
control-stream boundaries then publish writes before later graphics consumers
and submission completion. Framebuffer-only renders do not add this barrier.
No shader is replayed on the CPU and no stores or comparisons are removed.
This establishes the required cache operation, not optimized range tracking.

## Native LOD queries and array bias/offset sampling

Original index 763 Pass in `20260913T195252967166Z-cts-query-lod-native`:
use the native two-result LOD query opcode fields, preserving the sampler's
clamped/unclamped results. Remove only the validated zero software bias source.
Original index 994 Pass in `20260913T195753105570Z-cts-array-offset-width`:
pack array layer and register bias together, and preserve constant signed XY
offsets through register allocation and validation. The original image comparison
passes. Native compiler sources, identity and text witnesses are retained in
`native-querylod/` and `native-array-bias-offset/`; texture-core unit tests match
both native instruction sequences byte for byte (6,283 checks pass).
Both guarded tests peaked at 56.75 C without GPU faults.

## Cube-array register LOD

Original indices 160 and 181 Pass in `20260913T201332855699Z-cts-cube-array-lod`,
with full reference comparisons. The native compiler packs the cube-array layer
into an allocated temporary, then combines it with face and register LOD in one
three-source PACK_SMPIR. Keep that form paired across immediate optimization;
separate layer and LOD staging would overwrite the same bank. The native source,
identity and text are retained in `native-lodcube-dynamic/`. Texture-core tests
match the exact three-source packet and sample (6,288 checks pass).
The guarded batch peaked at 60.25 C without GPU faults.

## Vertex clip-distance exports

Original index 77 Pass in `20260913T201550665090Z-cts-clip-distances` after
admitting up to eight clip-distance UVS words on G16G and preserving their
output-select enable bits. Native position plus eight clip distances emits
12 sequential f32 UVSW writes; metadata field1.field3 is 12. Source, text and
metadata are retained in `native-clip-distance-decl/`. VDM unit checks: 2,773.
The same batch's tessellation/geometry case 368 still fails image comparison;
this commit does not claim it fixed. Peak 58.5 C, no GPU fault.

## Packed RGB10A2 integer render targets

Original index 413 Pass in `20260913T202142951573Z-cts-packed-uint-eot`.
The format's existing physical mapping is I16, with four integer lanes in the
tilebuffer. Admit the layout and use the established two-word I16 end-of-tile
program and integer MRT selector; the PBE packs the 10/10/10/2 destination.
No normalized conversion is used. Full CTS subpass/input-attachment validation
passes. The guarded test peaked at 58.5 C without GPU faults.

## Volatile and coherent storage image access

Original index 440 Pass in `20260913T202358092286Z-cts-volatile-image`.
Keep qualified image reads as memory intrinsics while loading native heap
indices; converting them to pure texture reads would lose their ordering and
volatile semantics. Stores preserve their access flags and use the native PBE
write path. The program's existing device, texture and workgroup barriers remain
in place and retire pending writes. The generic image lowering already represents
1D arrays as height-one 2D arrays; no special CPU path is involved.
Three additional coherent/volatile 1D-array/2D controls Pass in
`20260913T202453687622Z-cts-volatile-controls`. Peaks 58.5/58.25 C, no GPU faults.
These cases exercise explicit barriers, not every possible cross-invocation
communication pattern.

## Tessellation output into geometry

Original indices 368 and 636 Pass in `20260913T202809245567Z-cts-tes-invocation`.
Vertex-only, vertex+geometry and vertex+tessellation clipping controls passed;
only the combined tessellation+geometry path failed. Extra strong CDM barriers
did not fix it and that diagnostic override was removed.
When classifying native TES for resource lowering, preserve Poly's TES marker
and initialize the vertex-info union. Without the marker, software TES output
stores used the ordinary software VS preload registers, instead of compute
invocation IDs. This collapsed/corrupted the GS input buffer. Both original
image comparisons now pass without a diagnostic override, peak 58.75 C.
All original 47 Fail and two Crash cases have now passed individually; the
frozen original 1,000-case results remain unchanged. A combined retest follows.
