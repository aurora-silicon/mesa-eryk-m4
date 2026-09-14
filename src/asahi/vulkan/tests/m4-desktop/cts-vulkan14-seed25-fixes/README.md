# Vulkan 1.4 seed 20260925 fixes

## Mutable descriptor crash

The original mutable combined-image-sampler/storage-buffer case aborted at
`hk_descriptor_stride_align_for_type` because rounding the 72-byte sampled
image payload to the buffer descriptor's 16-byte alignment produces an 80-byte
slot. HK_MAX_DESCRIPTOR_SIZE represented only the unpadded payload. Derive the
maximum slot size from the native payload plus alignment, and use that maximum
alignment for mutable pools with an omitted or empty type list.

The original unmodified CTS 1.4.6.2 case passes on M4 in **0.413 seconds**.
The mandatory guard passed, temperature peaked at 59.25 C, and no reboot or
GPU fault occurred. The new library was installed atomically; the existing
SDDM compositor retained its old mapping while the fresh CTS process tested
the updated library. Exact library and CTS hashes are retained in the evidence.
No broad regression campaign was run; the original 1,000-case report remains
unchanged. One of the two original crashes is now repaired.

## Scaled-format swizzle CTS crash

The second original crash dereferenced ProgramBinary address 0xf inside CTS
`vk::createShaderModule`, before calling the driver. SwizzleTests prepared a
UINT program for USCALED (and INT_BIAS for SSCALED), while `getSamplerType`
correctly chose FLOAT. The binary collection lookup used the missing float
program name. Correct the 17 scaled-format declarations to FLOAT/FLOAT_BIAS,
matching the existing runtime selection and scaled-format sampling semantics.

A separately named `deqp-vk-scaled-swizzle` containing this test correction
passes the original swizzle case in **0.311 seconds**, peak 59.25 C, guard pass.
It uses the same Mesa library as the mutable-descriptor retest. No Mesa change
was needed for this crash. The original CTS binary and source were restored
and checksum-verified by cts-patched-build.py; original results remain Crash.
The core backtrace warns about a subsequently changed Mesa shared library, but
the faulting code/registers are in the exact unchanged CTS executable.

Both original crash cases now have passing targeted results: one driver fix
and one separately identified CTS correction. The 91 original Fail outcomes
and eight mandatory-feature skip reasons remain outstanding at this point.

## Image copies across dimensions and block extents

The native GPU block-copy path required equal image types and block dimensions,
rejecting valid size-compatible copies and 1D/2D/3D transfers. Normalize source
and destination offsets using their respective block extents; interpret the
copy extent in source texels, and compare destination bounds in blocks. Map
array layers and 3D depth slices independently while retaining layout, aspect,
block-size and bounds checks. The existing GPU compute copy performs the work.

All **21 original image-to-image failures pass** with unmodified CTS 1.4.6.2,
four workers and the mandatory watchdog/thermal guard. Peak temperature was
59.75 C. No broad regression was run. Vulkan library SHA256:
`6a939606881aa091a2d7c48f920fe451b147c6437e64564ab38778eb0c1e8c1e`.
Raw QPA, per-case outcomes, exact CTS identity and guard evidence are archived.

Running total: both crash cases resolved (one CTS correction), **21/91 Fail
cases repaired**, 70 Fail cases remaining. Original sample outcomes are retained.

## Preserve pipeline-library vertex outputs

The no-fragment output-removal optimization tested LIBRARY_BIT in
vk_graphics_pipeline_state::pipeline_flags, but the runtime stores only
mergeable rendering flags there. Pre-raster libraries consequently lost their
user outputs before a later fragment-library link. Restrict that optimization
to statically enabled rasterizer discard, preserving XFB and fixed-function
outputs. Missing fragment stages no longer imply unused vertex outputs.

Of the 21 original fast-linked-library failures, **17 now pass** on unmodified
CTS. Four dual-source cases advance to a separate compiler rejection for mixed
dynamic user outputs and immediate point-size export; they remain Fail.
The guard recorded the expected API errors, no crash/reboot, peak 59.75 C.
Library SHA256:
`8f7abdcbb5de0e4bef15c745b9ce1553a147274ae17b6d222ba91575c3d6dbd2`.
Per-case results and raw diagnostics are archived. No broad regression run.

Running total: both crashes resolved, **38/91 Fail cases repaired**, 53 remain.

## Scaled sampled-type declarations in CTS

ImageTests and ImageViewTests classified the integer backing representation as
the GLSL sampled type, generating usampler/isampler for USCALED/SSCALED images.
Use the existing glu::TextureTestUtil::getSamplerType helper, which correctly
chooses float for scaled formats. Data generation, reference values, lookup
scale/bias and comparison tolerances remain unchanged. This is the 1.4 source
counterpart of the separately recorded CTS 1.0 scaled-sampling correction.
Vulkan format type rules: https://docs.vulkan.org/spec/latest/chapters/formats.html

Six of seven affected original cases pass with separately identified
`deqp-vk-scaled-sampler-types`; packed A2B10G10R10_SSCALED still mismatches and
remains outstanding. Mesa is unchanged from the library-output fix.
Peak 60.0 C, watchdog/thermal guard healthy, no reboot. Original CTS source and
executable were restored and checked by cts-patched-build.py. Binary hashes and
full per-case results are retained; these are corrected-test results, not
passes of the unmodified CTS release.

Running total: **44/91 Fail cases resolved** (38 Mesa fixes, 6 CTS corrections),
47 remaining. Both original crashes have separately recorded passing retests.

## Half-width integer legalization

Use native 32-bit complement and shifts with exact low-half truncation for
16-bit results. Arithmetic shifts explicitly sign-extend half operands, also
when the shift count is a register or the destination is 32 bits. This repairs
four saved compiler bundles without inventing instruction encodings.

The original i16 arithmetic-shift tessellation case now **Passes** on M4.
The two i8 bit-field cases advance from compilation refusal to numerical
mismatches (including zero odd byte components); they remain Fail. The fragment
NOT bundle also compiles, but original multisample rendering cases hit later
layout/layer restrictions, so no additional pass is claimed for that change.

New boot d6085557-9b5d-48eb-b2af-d2d9d5365e2e, watchdog armed at 60 seconds,
peak 57.25 C in the three-case shift run. No GPU fault or reboot in that run.
Vulkan library SHA256:
`da8c68f1663da21d113351521cfa88673956259726ff9f7041d9518b5eee3ba0`.
Running total: **45/91 failures resolved** (39 Mesa fixes, 6 CTS corrections),
46 remaining. No broad regression campaign.

## Byte-vector memory packing

Native I8 vector loads/stores transfer densely packed bytes. NIR byte values
occupy individual 16-bit compiler lanes. Unpack vector loads and repack stores
at instruction selection, retaining addresses, masks and coherence. Register
accounting now tracks the packed store operand instead of four scalar halves.
The independent storage-buffer probe changed from five bad words to 0/7,
including preserved surrounding words and separate constant-output validation.
Both original i8 bit-field cases now Pass on unmodified CTS.

The fixture in byte-memory/ uses M4_ORACLE_INT8=1 with vk-buffer-oracle; compile
shader.comp with glslangValidator -V --target-env vulkan1.1. Run one workgroup
through the mandatory watchdog/thermal guard. Initial implementation's explicit
compiler rejection and corrected operand-width retest are retained in evidence.
Final library SHA256:
`7d1e5e85134809ae1d3db00895a42f3eeca0129ffe25618e329857466f70505d`.
Peak 58.0 C; no GPU fault/reboot. No broad regression campaign.
Running total: **47/91 failures resolved** (41 Mesa fixes, 6 CTS corrections),
44 remaining; both original crash cases have passing targeted retests.

## Inactive color-attachment slots

BG/EOT already compact active logical attachments into consecutive resource
bindings while retaining original tile offsets. Remove the obsolete rejection
of gaps in logical indices; format, allocation, overlap and spill bounds stay
intact. Six of nine original shader-object rendering cases now Pass. The other
three reach block_image_store compilation refusals and remain Fail.

The host mixed-layout test checks sparse slots and overlapping storage; it
completes before the larger existing test stops at its stale line-1866 assertion
that R8_UINT multisampling is unsupported. No full host-suite pass is claimed.
Nine-case M4 test peak 58.0 C, no GPU fault/reboot, expected API refusals retained.
Library SHA256:
`e15f165751eff41eb4731710f21e221435011f4c28fb83966bada9d05557acc5`.
Running total: **53/91 failures resolved** (47 Mesa fixes, 6 CTS corrections),
38 remaining. Original crash retests still recorded separately.

## MRT EOT logical-format aliases

Select the existing F16 tile conversion for packed 565/5551/4444 UNORM
attachments and the I8 conversion for BGRA integer attachments, consistent
with layout/formats.c. The PBE handles final packing and component order.
Unknown logical/physical pairs remain rejected and now report both format IDs
under shader diagnostics. No new machine encoding is introduced.

All three remaining shader-object rendering cases now Pass on unmodified CTS;
all three saved EOT compiler bundles compile offline. Peak 57.75 C, no GPU
fault/reboot. Library SHA256:
`7acbca26a9247e690fafa17a0eb474afdde00f334214b0ef02970fa5ceca6c66`.
Running total: **56/91 failures resolved** (50 Mesa fixes, 6 CTS corrections),
35 remaining. No broad regression campaign.

## Threadgroup byte stores

The local-memory translator incorrectly requested the load-only narrow_extend
field for byte stores. STTG rejects that field; the byte width already defines
the stored amount. Set narrow_extend only for loads. The saved shader compiles,
and the original explicit-layout alias test (four byte stores, one i32 load)
now Passes on M4. Peak 57.75 C, no fault/reboot. Library SHA256:
`39d0a1ff68e72c74fd3172797bfadc0f738b0c94d1416d63c5d8f61cb64cbf97`.
GDB field isolation and an offline Apple G16G byte-memory compilation are saved;
the Apple output was not GPU-executed or fully decoded in this experiment.
Running total: **57/91 failures resolved** (51 Mesa fixes, 6 CTS corrections),
34 remaining. No broad regression campaign.

## Native narrow shuffles and bitwise scans

Native shuffles move full words: widen half operands and truncate after lane
selection, preserving raw bit patterns. AGX3 bitwise scans now use NIR's
active-lane-aware lowering. Iterate common subgroup lowering to process scans
introduced by vector scalarization before converting native inclusive scans.

The original nonconstant bvec4 broadcast and both bitwise scan cases now Pass.
The i64 shuffledown graphics case still numerically fails and remains open.
Its failure is not included among resolved cases. One saved narrow-shuffle
bundle compiles offline; old bitwise-scan bundles were captured after the
changed preprocessing pass and still reject unchanged, as expected.

Peak 59.5 C, watchdog armed at 60 seconds, boot
0ca1f3f9-acb4-4b79-8a3c-15498f475016. The preceding attempt ran no CTS: a
concurrent m4_dcpext_rtkit_probe timed out and latched watchdog recovery. Its
separate journal evidence is retained; the four-case retry had no GPU reset.
Library SHA256:
`30309d0ad663f90ac7d7b7b37d36e6d93a41435cc8685913555cfb22c2c8b582`.
Running total: **60/91 failures resolved** (54 Mesa fixes, 6 CTS corrections),
31 remaining. No broad regression campaign.

## Integer saturation and ALU wait semantics

The wait-carrying generic ALU path could silently discard saturation, unlike
the ordinary select core which refuses unsupported saturation. Preserve that
boundary for saturation and shifts, and refuse saturated address-add fast paths.
AGX3 now uses standard NIR lowering for signed and unsigned saturating addition
(and signed subtraction via the same option), retaining arithmetic clamp semantics.

The captured signed/unsigned dot-product test previously emitted plain addition
for a signed saturating accumulator when a memory wait was present. Its exact
original CTS case now Passes; the saved compiler bundle also compiles with
explicit bounds comparisons. Peak 57.75 C, no GPU fault/reboot. Library SHA256:
`a8887cb8469682d6ba6c7ab4155c5bc7f26050d71729693ee477659b8fa7fc27`.
Running total: **61/91 failures resolved** (55 Mesa fixes, 6 CTS corrections),
30 remaining. No broad regression campaign.

## Single-layer linear BG/EOT descriptors

BG/EOT uses a 2D descriptor for one layer, independently of the API array view.
Do not attach the linear-array stride extension to that 2D descriptor. Actual
array descriptors and multiple layers retain their existing handling.
Both original linear-destination blits now Pass: RGB16_UNORM to RGBA8_SNORM,
and packed RGBA4 to RG8_UNORM. Peak 58.0 C, no GPU fault/reboot. Library SHA256:
`f41d0dc63cd8942e312d294001b22668f77c60fee7a925b5e645b3e15df5ffde`.
Running total: **63/91 failures resolved** (57 Mesa fixes, 6 CTS corrections),
28 remaining. No broad regression campaign.

## Fragment reads of clip distances

Accept the ordinary UVS copies of CLIP_DIST0/1 in fragment varying linkage,
in addition to their separate clipper outputs. The vertex lowering already
emits these interpolated copies. Perspective and component bounds checks
remain unchanged. The original dynamic-index clip/cull fragment-read case
now Passes on unmodified CTS. Peak 57.75 C, no GPU fault/reboot. Library SHA256:
`d875e72527e15001f474d8ac76e2051ae64de0c123712fbc140bdeb56c537f9d`.
Running total: **64/91 failures resolved** (58 Mesa fixes, 6 CTS corrections),
27 remaining. Both original crashes have passing targeted retests.
No broad regression campaign.

## 3D integer mip-level fetches

Apple G16G compiler 32023.883 independently emits the existing 3D fetch
encoding combined with register LOD (native TEXT offset 0xda). Enable that
combination in indexed-handle lowering and the encoder; preserve the qualified
coordinate bank and array restrictions. Track all XYZ staging halves until
the texture result retires, including level-zero 3D loads. The byte witness
is part of the texture-core checks (6295 checks passed). The new offline
`native-fetch-decode.c` helper reports exact re-encoding candidates.

The original ETC2/RG32_UINT 3D texel-view case now Passes. Peak 57.75 C, no GPU
fault/reboot. An intermediate attempt still rejected the earlier indexed-handle
shape check and is retained. The old saved compiler input precedes neither
changed sampler preprocessing nor that check, so its rejection is retained
and is not claimed as a passing offline replay. Native Apple compilation is
an encoding witness, not an independently GPU-executed probe.
Library SHA256:
`4671d6811e03e20283b71326f51fdecfcf6a362cf45406bfcb386bd4ca708703`.
Running total: **65/91 failures resolved** (59 Mesa fixes, 6 CTS corrections),
26 remaining. No broad regression campaign.

## CTS scaled blit classification

Five original blits were rejected by the CTS oracle before pixel comparison:
it treated the integer storage of scaled formats as integer-only blit semantics.
Vulkan instead converts scaled values to floating point for filtering:
https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBlitImage.html

Exclude scaled formats from the oracle's integer-class compatibility check.
For nearest conversion involving scaled formats, build the destination-format
reference texels first and use zero color tolerance in the lookup verifier.
This avoids a one-integer tolerance hiding errors in normalized [0,1] inputs.
The four linear comparisons retain their existing reference and thresholds.

All five now Pass with the separately patched CTS binary
`adeddeb606346eb230e1a1366772afa6da921a248eae9608014d0e139e3340b6`.
Original CTS source/binary restored by the build helper. No Mesa change or
unmodified-CTS pass is claimed. Peak 58.25 C, no fault/reboot.
Running total: **70/91 failures resolved** (59 Mesa fixes, 11 CTS corrections),
21 remaining. No broad regression campaign.

## CTS signed packed scaled reference values

`tcu::ConstPixelBufferAccess::getPixel` built an unsigned UVec4 from the
sign-extended SSCALED 10:10:10:2 components, then converted it to float.
Negative values became approximately 2^32 instead of negative floats. Use
IVec4, matching the adjacent signed-integer format implementation.

Combined with the previously recorded GLSL sampler-type correction, the
remaining A2B10G10R10_SSCALED array-sampling case now Passes. This is a CTS
reference fix, not a Mesa fix. Separate binary SHA256:
`9f58b84ad67ca2759d5ef66ec506dfbff1d38233b08de81206e922fbd604223f`.
Original source and binary restored. Peak 58.25 C, no fault/reboot.
Running total: **71/91 failures resolved** (59 Mesa fixes, 12 CTS corrections),
20 remaining. No broad regression campaign.

## Register-indexed SIMD broadcast lifetimes

The G16G register-indexed broadcast emitter hard-coded the destination hint,
source retention, and final-use lane-index flag. NIR implements divergent
shuffles with repeated reads of each source quad; discarding a still-live
lane index corrupts later reads. Honor the allocated operand hints instead.
Independent Apple G16G compilation supplies exact repeated/final lane-index
byte witnesses, now checked in the subgroup-core test (Pass). The new offline
shuffle decoder derives register masks and requires exact candidate re-encoding.

A one-workgroup raw-buffer probe compares 64-bit values and both individual
32-bit halves. It improved from 96/192 mismatched words to 0/192. Its runner
explicitly enables shaderSubgroupExtendedTypes. The original i64vec3 graphics
shuffledown case now Passes. The reconverge11 case still fails and remains open.
Peak 58.0 C, no fault/reboot. Library SHA256:
`03ca149b0ec7c2bd5c76281b6a628ca65650cd52fdacdf661160dfb3eebbe4f9`.
Running total: **72/91 failures resolved** (60 Mesa fixes, 12 CTS corrections),
19 remaining. No broad regression campaign.

## Unsigned min/max source retention

Native SIMD unsigned min/max reductions marked their input as consumed even
when the original value remained live. This broke subgroup election: the
minimum lane ID is compared with that same original lane ID after reduction.
Honor source liveness for these operations. Independent Apple G16G min/max
byte witnesses exercise a retained source, including a slot-6 wait, in the
passing subgroup-core test.

The minimal election probe improves from 31/96 mismatched words to 0/96.
The original reconverge11 CTS case now Passes, including its setup/election
check and the subsequent control-flow comparison. Peak 57.5 C, no fault/reboot.
Library SHA256:
`a95fda90594873acbc8e5ad1e3c1eff5e6226a4861326bdf2fae4d56f4ad2a72`.
Running total: **73/91 failures resolved** (61 Mesa fixes, 12 CTS corrections),
18 remaining. No broad regression campaign.

## Runtime UVS destination indices

Separate shader linkage loads varying destinations at draw time. The native
UVSW table supports register/uniform half-width destination operands (f23
kind 2/3, f16/f17 index), but the emitter only accepted immediate indices.
Add these forms and retain index liveness. For dynamic exports, retire each
write before reusing its index register or staging bank; these destinations
may alias, unlike the existing statically distinct terminal export batches.
Value/index producer dependencies and operand bounds remain checked.

Both original saved vertex bundles compile offline. Five original CTS cases
now Pass: four dual-source blending cases and transform-feedback fuzz case28.
Two further blend cases reach a new fragment FLTNORM source-negation refusal
and remain open. No native Metal byte witness is claimed for the indexed
UVSW form: this implementation is table-derived and validated by M4 execution.
The final source also checks operand count before reading the index. Both
host and ARM builds passed after that arity-check cleanup.
Peak 58.25 C, no fault/reboot. Tested library SHA256:
`27d9ead157a430931ed2569395d29130e9cdc17d9decd848409aea4be330a833`.
Running total: **78/91 failures resolved** (66 Mesa fixes, 12 CTS corrections),
13 remaining. No broad regression campaign.
