# Vulkan 1.4 random 10,000, seed 20260928

Broad frozen sample from CTS 1.4.6.2, four cases overlap seed 20260927.
Uses the separately identified seed27 precision-corrected CTS binary,
SHA256 `25e691e613ff9f3a386fd282dd48f0658b86126f56e2ff16c2e95378825d12fc`.
Original CTS changes and provenance are retained in the preceding sample.
This campaign is still in progress; no complete 10k result is claimed.

First segment: 220 Pass, 170 NotSupported, 5 Fail, 33 Interrupted,
9572 Unrun. A GPU fault ended the run; 63.461 seconds includes recovery,
not a complete 10k benchmark. Evidence: `20260914T124651368249Z-cts14-seed28-10k`.
One worker's device-loss case passed alone and with its preceding sequence.
Single-worker isolation then reproduced the fault on an advanced graphics
render timestamp test (`20260914T125056696558Z-cts28-interrupted-isolate`).

The kernel rejected render query destinations despite already emitting native
firmware timestamp operations. Resolve and retain validated timestamp mappings,
then connect their pointer pairs to those existing operations. Internal
retirement timestamps remain separate. The original case passes without
synchronization diagnostics: `20260914T125945901211Z-cts28-render-timestamp-normal`.
Earlier diagnostic run: `20260914T125910939877Z-cts28-render-timestamp`.

The maint9 signed 64-bit bitfield extraction case used a folded 16-bit EXTR
source. Native EXTR reads full words; explicitly zero-extend data operands
before RA. Its captured shader now compiles offline and the original numerical
CTS comparison passes: `20260914T125934545830Z-cts28-extr`.
Library SHA256 `545e45870fbf4e117b77693e24f63a363f163b6bb7c568b485098f25a96cfa58`;
kernel module SHA256 `b415d4f8660f5d74130fd74a9d06a7345e49c807f4161830f79bfdf7aaa49f12`.
Both guarded targeted comparisons used a 60-second watchdog and thermal monitor;
maximum temperature 57.75 C including the diagnostic run.

## Geometry output crash

The next segment completed 928 Pass, 1084 NotSupported and 13 Fail before a
SIGABRT in `poly_nir_lower_gs`. Its 100 Missing CSV entries are canceled groups
without a CTS start marker, now reported as Unrun by the collector; raw results
remain retained. 41 other cases were interrupted. Peak temperature 61 C.
Evidence: `20260914T130056156246Z-cts28-cont1`.

The core points to inserting a 16-bit GS output into a 32-bit temporary vector.
Preserve the raw bits in word temporaries and restore the original width per
component on rasterizer export. The original interface-matching comparison
passes on J713: `20260914T130412322900Z-cts28-gs-half`, peak 57 C.

## Half operations and storage formats

Four further original comparisons pass in `20260914T131020684884Z-cts28-small`:
half shuffle (its boolean comparison), FP16 clamp with NaN input, clustered
16-bit integer addition, and RG8_SNORM image reinterpretation/storage writes.
General half comparisons widen exactly; native tied half min/max retains its
encoding and retires otherwise unencodable producer waits first. Integer
reductions use words with appropriate extension/truncation. RG8_SNORM uses the
same formatted floating PBE path as its scalar and four-channel forms.
Peak temperature 60.5 C; library SHA256
`0e23502ee62ee2579c11fc4a80f1dac5bef5537a19bc0bf4110280925b73fd01`.

Narrow SRGB end-of-tile programs also compile after selecting the normalized
export encoding. The grow/shrink attachment case still fails numerically and
is explicitly unresolved; compilation success is not counted as a CTS pass.

## Sample interpolation and texture offset lowering

Seven original comparisons pass in `20260914T132740745232Z-cts28-sample-offset-fix2`:
four 2x sample-interpolation shader-object variants and three 3D texture-offset
cases. Sample positions and sample count come from dynamic draw descriptors;
late-created sample barycentrics use the existing coefficient interpolation.
Unsupported offset shapes use standard NIR coordinate lowering. The array
texel-fetch compiler abort is removed but its register-LOD encoding remains
unresolved, so that case is still Fail. Peak temperature 57 C. Library SHA256
`60c1610342d4e5346a87932d2c34b57b5f75a418195f51496fc5c4aff16143a7`.
Earlier `20260914T132049350904Z-cts28-offset-sample` also establishes passes for
RG8_SINT and R16_SNORM storage reinterpretation and 2D-array bias offsets.

The continuation `20260914T132821665770Z-cts28-cont4b` completed another 85 Pass
and 74 NotSupported, then encountered GPU job 979 retirement failure. Its
firmware snapshot and all 17 interrupted cases are retained for isolation;
3654 other cases remain unrun in that segment. Peak temperature 59.75 C.

## Array texel fetches

The original `texelfetchoffset.repeat.isampler1darray_fragment` now passes
numerically in `20260914T133501967450Z-cts28-array-coverage` (57 C peak), library
`02b6e9a3bd042df3c33d69cce24628405bcf239c3daafff74d2e10aa54f05c1d`.
Apple offline compiler 32023.883 targeting applegpu_g16g establishes the fused
integer layer/mip PACK_SMPIR and corresponding SMP form. Native sources and
archives remain under `build/cts28-native/array-fetch-{reg,ulod}`; independent
instruction bytes are checked by `agx3_tex_core_test`. The descriptor lowering
also accepts this newly implemented array LOD form. The separate four-target
RGBA32F MSAA case remains rejected at tile-layout validation, not passed.

## Greeter compiler failure

Saved greeter capture `9daa06b88ed5a72af8cd1e5eb072f8e221a33ca90809328b7d83290970e59fbf`
uses a plain 2D sample whose coordinates are allocated above the direct SMP
coordinate range. Use the existing staged-coordinate path for those registers.
The unchanged captured shader compiles offline to 6798 bytes. Guarded greeter
restart `20260914T134150736013Z-cts28-greeter-coordinate-fix` keeps Hyprland alive
through its ten-second check, peak 57.5 C, installed library
`79f4b084b7b7e9a199c4db26db5d70c88d98149c197c78c500ff104b471544f3`.
Presentation failures still need rechecking; a missing display is a setup error,
not evidence of optional Vulkan support.
