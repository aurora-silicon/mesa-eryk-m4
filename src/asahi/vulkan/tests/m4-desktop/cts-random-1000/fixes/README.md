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
