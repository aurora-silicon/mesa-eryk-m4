# Vulkan 1.4 random 10,000, seed 20260928

Broad frozen sample from CTS 1.4.6.2, four cases overlap seed 20260927.
Uses the separately identified seed27 precision-corrected CTS binary,
SHA256 `25e691e613ff9f3a386fd282dd48f0658b86126f56e2ff16c2e95378825d12fc`.
Original CTS changes and provenance are retained in the preceding sample.
All 10,000 cases have initial terminal results across interrupted segments.
Fixes and skip review remain in progress; this is not a same-build full pass.

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

## Integer scans, quad writes, and additional storage formats

`20260914T134408615911Z-cts28-compiler-next` establishes eight new numerical
passes: three integer scan cases, a 64-bit tessellation-control broadcast,
three RG16_SNORM storage reinterpretations, and a gradient-offset texture case.
Min/max scans use NIR's existing active-lane lowering. Undefined out-of-range
broadcast indices use the hardware lane field's low bits. RG16_SNORM uses the
same formatted PBE conversion as its scalar/vector siblings. Gradient offsets
use standard coordinate lowering.

`20260914T134656718933Z-cts28-quad-bias` adds passes for boolean tessellation
quad broadcast and integer 3D sampling with bias. The qualified quad destination
uses ordinary GPR write hints. The floating 3D projected-bias case and the
16-bit geometry quad broadcast still fail numerically and remain unresolved.
Peak for these comparisons 57.75 C. Latest installed library:
`667274915c5b080d573891ae8aa173f71a557d767e0ab3da117c6bc8e0264683`.

All three original presentation failures pass in
`20260914T134241765993Z-cts28-wsi-greeter-fixed` after the greeter compiler fix:
Xlib present fences, Wayland multi-swapchain fences, and XCB incremental mailbox
presentation. Peak 57.5 C. Separate missing-display skips were rechecked in
`20260914T134439006110Z-cts28-wsi-setup-fixed`; preserve their revised capability
reasons rather than treating absence of the greeter as unsupported Vulkan.

## Null index buffers and compressed-copy diagnosis

`20260914T135242005531Z-cts28-copy-index` passes the original maintenance6
null-index draw. Native indexed commands require a real bounded index span;
direct null bindings now allocate zero-backed indices before native validation,
retaining vertex offset, instance state and shader execution. Allocation failure
is reported normally. Peak temperature 57 C; installed library
`2d267523a8ad0543a40f2aff3563ccd811fc88d62fb4c42e16181c4a27155125`.

The two 1D compressed-view cases still fail. CTS rounds the reference image
width to complete blocks but uses the original partial width for its 1D upload
(vktImageCompressionTranscodingSupport.cpp:2486,2583). The upload does not end
at the image edge. A separate CTS correction is being built; no successful
result is claimed yet and invalid-region validation remains enabled.

## Active sample tile reads and scoreboard reuse

Lower a variable one-hot sample selection into guarded constant sample reads,
using the native G16 LLDCOL form. Retire producer waits separately when the
scoreboard allocator reuses a slot; LLDCOL has no qualified incoming-wait form.
Read the native coverage register at its 16-bit width and widen explicitly.
Native four-sample Metal reference: `build/cts28-native/tile-sample`, target
`applegpu_g16g`; constant masks 1, 2, 4, 8 are independently compiled witnesses.
The captured random85 shader now compiles offline. The original random85 and
extended dynamic sample-count tests pass in
`20260914T140847534246Z-cts28-tile-wait`, maximum 58.5 C. The static
alpha-to-coverage/sample-mask comparison still fails numerically. Random73
also passes in `20260914T135918331855Z-cts28-sample-tile-and-copy`.
Library SHA256: `21973a7938376d3f87f63e9b15e614430b0ae4cfcdd30badb3610f664f93cf37`.

## CTS compressed 1D reference uploads

The attached full CTS correction includes the preceding seed27 precision fixes
and a 1D block-edge correction. The reference compressed image is allocated at
a block-rounded width, so its upload must cover that width: copying a partial
block at the original logical width does not reach the allocated image edge.
The original ASTC12x12 read and BC1 write cases pass with the corrected binary
in `20260914T135918331855Z-cts28-sample-tile-and-copy`. The unmodified CTS
failures are retained; this is a test correction, not a driver relaxation.
Patch SHA256: `ac11a26760c4a46b1b6eee5daa9ceb89a79bdf3e136d5be4fda71a717a1f7250`.
Corrected binary SHA256:
`abf9a339413e68f93d731fd8a683971c3b949c8fc13f57a22ad3bb33cdbf8c41`.
Target path: `/root/cts-1.4.6.2/deqp-vk-seed28-corrected`.

## Register-lane quad broadcast lifetimes

The register-lane quad broadcast encoder marked both operands as final reads,
even when another broadcast or comparison reused them. Use the actual source
lifetimes, as for SIMD broadcast. Independent native G16G Metal capture
`build/cts28-native/quad-live2` shows a live quad source in field25 at byte136.
The original u16vec2 geometry quad-broadcast case now passes:
`20260914T141855171884Z-cts28-quad-live`, maximum 58.25 C. Library SHA256
`57831b9a66cfe0b3fee6e8a80de2949adb2a014673fe0369e7d41027e9337e8b`.
The twelve scaled-format blit cases were also tried with lavapipe; all twelve
were NotSupported (`20260914T141708938415Z-cts28-blit-lavapipe`). Those
software outcomes are excluded from the hardware result ledger.

## Indirect dispatch workgroup units

Native fixed-local indirect CDM accepts workgroup counts. Converting Vulkan's
indirect arguments to global thread counts dispatched the shader six times
too often for a 3x1x2 workgroup. Remove that conversion; direct CDM still
uses global thread counts. The original GPU-produced five-dispatch test passes
with its exact atomic counts: `20260914T142215157428Z-cts28-indirect-groups`,
peak 58.25 C. Library SHA256
`894fe461fca27a0d463a3675d07679c422aa10dc63d7caf5221123b3275cac7f`.

## Cumulative checkpoint and capability audit

All 10,000 cases have terminal observations: 4,619 Pass, 39 Fail and 5,342
NotSupported. Forty original Fail/Crash/Timeout outcomes now pass. This ledger
combines different builds and targeted retests, not one uninterrupted timing
or conformance run. All 5,342 current skips were reviewed against the pinned
registry, CTS gates and saved device profile; none is currently classified as
NotSupportedButShouldBeSupported. The compressed per-case audit retains each
exact reason and rationale. Optional means outside this advertised capability
set, not that support cannot be implemented later.

The corrected CTS direct-DRM test no longer enables the device extension
VK_EXT_physical_device_drm on its instance. It reaches the real platform check:
No DRM primary device (`20260914T142634588886Z-cts28-query-and-drm`). The
corrected binary and patch hashes are in `cts-drm-build.json`; the preceding
binaries and raw failures remain retained.

GPU job979's concurrent fault remains unresolved even though all interrupted
cases passed serially. A zero Crash count in the cumulative case ledger does
not qualify that concurrency/recovery path. No full regression run is claimed.

## Integer reduction lifetimes and array LOD queries

Two further original comparisons pass in
`20260914T143340068946Z-cts28-subgroup-and-array`, peak 58.25 C:
signed 64-bit vertex subgroup OR and 1D-array textureQueryLod with a nonzero
base level. OR/XOR native witnesses preserve reused inputs with field25;
ballot preserves a live input by leaving field27 clear. Native references:
`build/cts28-native/subgroup-live4` and `subgroup-live5`. Boolean XOR and the
geometry ballot comparison still fail and are not counted as fixed.

LOD queries have no layer coordinate, but array descriptors still require
array query mode (field77=1, field37=1). Normalizing to a non-array instruction
returned NaNs. Use the independently compiled G16G array-query form and retain
the descriptor and mip range. The new native golden vector passes with the
texture-core unit checks (6,310 checks). Native reference:
`build/cts28-native/array-query`, fragment text-1220 byte126.
Library SHA256: `1091bfb8a1f78d738f23e3b6fc28284029230f49ac0030df4e2140050da881af`.

## Scaled blit reference precision

The twelve remaining scaled blits pass with a separately retained corrected CTS
binary in `20260914T143937662614Z-cts28-scaled-precision`, peak 58.75 C.
These are CTS comparison corrections, not twelve Mesa fixes. The original
failures, pinned baseline and diagnostic binaries remain intact. Full patch:
`cts-scaled-precision.patch`; build/source/binary hashes: `cts-scaled-build.json`.

Linear scaled destinations preserve the unquantized filtered reference, then
compare the result against the rounded/clamped existing precision interval.
Compressed nearest scaled conversions retain decompression uncertainty plus
the half-unit conversion bound. Packed 11/11/10 float destination precision
uses each channel's actual exponent/mantissa quantum rather than an absolute
near-one tolerance at values as large as 65535. Missing channels remain masked.
No format support or shader path was disabled to obtain these outcomes.

Latest cumulative checkpoint: 4,633 Pass, 25 Fail, 5,342 NotSupported;
54 original Fail/Crash/Timeout cases now pass across fixes and explicit CTS
corrections. The concurrent job979 fault remains unresolved.

## Native ballot equality operand mode

G16G integer ballots compare against zero with fields38/39=1, including
equality. The inherited G17P equality form used fields38/39=2 and field42=1;
on M4 the minimal shader stopped writing outputs after that instruction.
Native G16G witness `subgroup-live4` byte98 establishes the correct encoding.
The independent raw-buffer fixture in `ballot-oracle/` verifies 288 words:
true/false/data ballots, reduction XOR, popcounts and per-lane identity.
It failed before the change and passes in `20260914T144703535482Z-cts28-ballot-zero`.
Both original CTS comparisons, compute subgroupxor_bvec4 and geometry ballot,
pass in `20260914T144743930559Z-cts28-ballot-cts`, peak 58.25 C.
The subgroup-core unit checks include the byte-exact native witness and pass.
Library SHA256: `ead0084c88fa7a0b89c8aa0717274645a4f48bebcecc11b3f34dcd43eee35462`.

## Narrow subtraction semantics

The halfword bitfield test now passes in `20260914T145337326657Z-cts28-half-subtract`,
peak 58.5 C. IADD uses abs for zero/sign extension selection and neg independently
for subtraction. The mixed-width legalizer skipped negated half sources; the
immediate encoder then treated neg as extension and performed addition.
Extend the half operand first, preserving its subtraction modifier. This also
removes the unsupported register-minus-half shape from the wide diagnostic.

`half-subtract-oracle/` retains the original assembly and repeated failing input.
Assemble with SPIR-V 1.0 as in CTS; a 1.5 assembly without updated interface
operands is invalid and its rejected diagnostic is retained separately.
`vk-buffer-oracle.c` can expose four aligned descriptor ranges and enable
explicit 16-bit storage. The data/offset/count load-only diagnostics passed;
original arithmetic failed before the fix. The complete original CTS case
passes after it. No other five cases in the focused group passed yet.
Library SHA256: `26c0684a0a4d34a9d47e51b4fcaf664e6ba678d7fb0c6f7068e0ea80d827632d`.

The concurrent job979 prefix was rerun with 20 workers:
`20260914T144845630690Z-cts28-fault979-prefix`, 100 Pass / 76 NotSupported,
runner 5.642 seconds, guard/staging-excluded wall 9.878 seconds, peak 60.75 C.
It did not reproduce the fault; this is not proof of its underlying cause.

## I16 framebuffer reads

Native LLDCOL loads packed halfword channels, whereas AGX2's I16 form can
widen to W32 per channel. Explicitly load W16 lanes and then widen in NIR.
This fixes exact integer NAND and OR_INVERTED image comparisons:
`20260914T145647026333Z-cts28-tile-half-load`, both Pass, peak 58.25 C.
Saved failing epilog: `f06454af2696228e093f7e151be97c25b3b0855bac4e78cef947d581f872e3af`
in the preceding narrow-case capture. Library SHA256:
`eaafcf06e828f0086598cddaece208d04e7d324f4efd6c751cd26834d4d62f35`.

## Packed shared bytes

Byte-vector stores copied half-register padding into packed shared memory:
`5,9,3,9` became `5,0,9,0` in the reduced runtime-input struct. Scalarize 8-bit
shared vectors, preserving byte alignment, access flags and store masks.
Use G16G's byte-load extension selector zero, already established by the
native local/stack corpus; the G17S selector remains separate.

The reduced 24-word probe passes in `20260914T150247878319Z-cts28-shared-byte-small`;
the original nested_structs.4 passes in `20260914T150311030546Z-cts28-shared-byte-cts`.
Both peaked at 58.25 C. The fixture and independent native Metal source are
in `shared-byte-oracle/`; native archive is `build/cts28-native/shared-byte`.
The larger raw-field diagnostic also exposed separate large-shader behavior
and is not counted as passing. Library SHA256:
`ceba8a0e493a3ebecc40b938ee06973d8037df041a47188842ed3e06b600ce1d`.

## Cosited 4:2:2 CTS reference correction

The original explicit-linear/cosited test selected chroma sample floor(i/2)+1
at even luma i: its left-tap weight was zero. Vulkan's explicit reconstruction
formula selects floor(i/2) exactly there. Set the left weight to one for even
cosited positions; leave odd and midpoint weights intact. Pinned specification:
`external/vulkan-docs/src/chapters/textures.adoc`, explicit reconstruction,
also https://docs.vulkan.org/spec/latest/chapters/textures.html#textures-explicit-reconstruction.

The corrected test passes in `20260914T150825416650Z-cts28-cosited-reference`,
peak 60.5 C, with unchanged Mesa. Full cumulative patch and exact build hashes
are in `cts-scaled-and-cosited.patch` and `cts-cosited-build.json`.
The original fail remains retained. The grow_shrink.33 renderpass case also
passes on the halfword tile-read fix (`20260914T150419124001Z-cts28-next-numeric`).

## Vertex image fetch and terminal exports

The volatile robust 3D storage-image vertex shader had two compiler gaps.
Earlier terminating paths incorrectly disabled bundling of exports that all
belong to the final block. Preserve those paths and only reorder exports
locally in that block. Integer-mip fetches also hardcoded PACK_SMPIR to staging
half6 even when the coordinates occupied the separate image bank at half16.
Pack mip (and array layer where applicable) at coordinate-base + 6.

Native G16G 3D-read capture `build/cts14-crash-fixes/texture3d-lod-native`,
text-750.bin offset172, independently encodes mip at half14 with coordinates
at half8; the byte-exact texture-core test checks that relocation.
The saved vertex compiler replay now compiles to 6442 bytes. Original CTS case
passes in `20260914T151835879088Z-cts28-vertex-lod-bank`, peak57.75 C,
installed library SHA256
`8d539236b78721b6b2d2f9e3e235515f14939311467142067f5df4c123d5aff6`.

Cumulative results are 4642 Pass / 16 Fail / 5342 NotSupported, 63 originally
failing/crashing/timing-out cases resolved. These are latest per-case results
across builds, including the separately documented CTS reference corrections;
this is not an uninterrupted same-build run. The concurrency fault discussed
above is still not proven fixed.

## Multiple viewport state

Native draw upload now retains every viewport transform, region clip and
matching scissor, with the draw selecting the first scissor of its owned range.
The varying record follows the actual variable-sized viewport record.

Two prior assumptions caused the initial guarded stalls:
- OUTPUT word2 low bits are vertex amplification count minus one, not viewport
  count. M4 B0 setVertexAmplificationCount at 45b408..45b418 independently
  establishes this; ordinary draws leave it zero.
- Bind-tail controls are lengths for the following PPP address. The initial
  0x500 describes the first 20-byte varying record, then 0x700 the 28-byte FS
  record, and so on. pairs[4].control must be viewport_record.length/4 << 8,
  replacing the fixed one-viewport value 0xa00 with 0x1200 for two viewports.

Failed experiments `20260914T153421984883Z-cts28-two-viewports` and
`20260914T153955838318Z-cts28-viewports-no-amplification` retain firmware
snapshots and watchdog recovery. Final original case Pass:
`20260914T154215266863Z-cts28-viewport-packet-length`, peak59.25 C.
Library SHA256 `a0b6f802d2b071c2f6597eba845f12762b7e156f06a9a1621a00997281d999b8`.
Host upload test verifies relocated pointers, packet length and allocation
bounds. Cumulative: 4643 Pass / 15 Fail / 5342 NotSupported, 64 resolved.

## Packed signed sampler in the corrected CTS

Our earlier scaled-format CTS correction used glu::getSamplerType to separate
scaled formats from integer images. That GL helper omitted the packed signed
10:10:10:2 format, producing a float sampler for a SINT image. Add the missing
signed case to the helper. The original Vulkan image test now passes without
changing the driver texture format or reference pixels:
`20260914T154833620026Z-cts28-signed-and-centroid`, case0, peak58 C.
This resolves a defect in our CTS correction, not a new Mesa format fix.
The cumulative patch also logs every multiple-interpolation reference before
an early comparison failure; that diagnostic changes no pass thresholds.
Manifest and full patch are retained, binary SHA256:
`fbd7690324772743d9587e816ce4325be947c479362bc36507e451c19310218a`.
Cumulative: 4644 Pass / 14 Fail / 5342 NotSupported, 65 resolved.

## Centroid interpolation coordinates

Centroid interpolation used an uninitialized register selector. Read the
original SR39 coverage word and emit native itrcoords into an SSA coordinate
value; every RHW/channel interpolant keeps that operand live through register
allocation. Native projective RHW f54 also applies to centroid interpolation.
The ordinary and RHW codecs independently reproduce the native bytes, while
the AGX3C witness remains intact.

Independent G16G AIR-NT32023.883 reference: build/cts28-native/centroid-1x,
text-1200.bin main64: SR39 load at64, itrcoords at72, RHW at80, channel at100.
The source is retained here. Host varying codec tests and the saved fragment
compiler replay pass. Original hardware CTS Pass:
`20260914T155317619890Z-cts28-centroid-coordinates`, peak60.25 C,
Mesa library SHA256 `2dcb398ce6d6f61f6a16476e3b931d09f8369c0f05e584037ad6fbc54eab12a3`.
Earlier RHW-only attempt still failed; retained all-reference images showed
centroid had only12 colors versus4442 for the smooth reference. No thresholds
were relaxed. Cumulative: 4645 Pass / 13 Fail / 5342 NotSupported, 66 resolved.

## Integer interfaces before interstage optimization

`20260914T160943434956Z-cts28-integer-io-fresh` passes the original
`16bit_storage.input_output_int_32_to_16.vector_uint0_tesse` case.
The varying optimizer can retype flat integer interfaces as floating point;
widening/narrowing them later as half floats corrupts integer values whose
bits encode NaNs. Native compilation now widens 16-bit integer interfaces
before that optimization and narrows inside the consumer. XFB retains a
separate store of the original width. No CTS expectation changes.

Installed library SHA256:
`0ef4a4f9d552626ca9fb66ecf6c0d9c8a504a6fd59b14f874bbbf14c56e39158`.
Guarded M4 run peaked at 59.25 C. The two preceding attempts did not execute
the test (external watchdog lease, then a case-list file lost during reboot).
Cumulative ledger13: **4,646 Pass / 12 Fail / 5,342 optional NotSupported**;
**67 originally failing/crashing/timing-out cases resolved**. These are
observations across builds, not an uninterrupted full regression run.
