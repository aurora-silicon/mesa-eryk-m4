# Seed 20260922 fixes

Original reports remain frozen in `../cts-seed20260922-1000`.

## X8_D24 rendering crash (697)

Render state now validates the physical depth plane (Z16 or Z32), consistent
with attachment addressing and clear encoding. X8_D24 uses the existing
Z32 emulation, with logical-format depth bias preserved. The original crashing
compare-operations case passes on J713; peak 59.25 C, 60-second watchdog,
no GPU fault. Exact source patch/binary identity in the retained experiment
`20260913T235918400096Z-cts-seed22-depth-fix`.

## Attachment and tessellation exports (58, 342, 424)

All three original cases pass with the existing CTS binary in guarded experiment
`20260914T000310583679Z-cts-seed22-exports-v1` (peak 57.75 C). The fourth
case in that run is the still-unfixed cube gather and remains Fail.

G16G signed RGBA16 MRTs select the same I16 export as UINT, matching the
previously qualified single-target builtin; G17P retains its distinct path.
The vertex export bundler now accepts side effects before the first export,
without moving exports across side effects or control flow. The original
point-mode compiler capture now compiles offline and the CTS execution passes.
No-output rasterizer-discard TES uses its reserved position-only UVS layout;
its SSBO writes remain executed and checked by CTS.

## Cube gather (412)

The original unsigned cube-gather case passes, as do matching no-corner signed
and UNORM controls. Projection now admits gather and preserves native cube
face operands; the SMP cube and gather fields are composed independently.
A broader 20-worker run of long full-corner cases timed out all 20 active cases
at 20 seconds, with GPU work retiring and no fault; remaining 47 were not run.
Those timeouts are retained and are not counted as passes or a stability claim.
The short controls pass in `20260914T000843327303Z-cts-seed22-cube-small`.

## Invalid CTS fragment shader for USCALED (970)

The original CTS binary generates usampler2D/uimage2D with rgba16ui for
RGBA16_USCALED. Vulkan requires floating-point shader values for scaled
formats. Extend the previously documented compute shader correction to the
fragment texture-read/write path, using sampler2D/image2D and a formatless
store (the feature is advertised). The case passes with the separately
identified corrected binary in `20260914T000743709250Z-cts-seed22-fragment-scaled-reference`.
No Mesa rendering change was made for this case. The original invalid-shader
Fail remains in the frozen report. Lavapipe skips this and the four scaled
blits because the requisite optional image formats are unsupported.

## Scaled linear blits (116, 388, 704, 977)

The UNORM-alias sample followed by multiplication loses fractional precision.
Numeric texel fetches followed by float32 GPU bilinear interpolation make
116, 388 and 977 pass the original CTS binary, with its original tolerances.
Eight previously passing scaled-source/destination blits also pass unchanged.
The new path uses mip-view dimensions, clamps neighbours to the image edges,
and retains the array layer. It applies to 2D scaled linear blits; ordinary
formats, nearest blits, resolves and 3D blits retain their existing paths.
This is GPU shader filtering, with no CPU pixel processing.

The first development build misused NIR builder variadic arguments and aborted
in shader construction. Its four SIGABRT results are retained as
`linear-development-abort-*`; no GPU fault or reboot occurred. Explicit `.lod`
arguments fix that construction error in the qualified v2 build.

704's maximum error falls from 1.59961 to 0.0136719, but the historical test's
fixed absolute 0.001 epsilon still rejects it. Source values are 16-bit scaled
integers and the float destination exceeds the normalized range. The existing
TCU lookup interval verifier, using the advertised 8 subtexel precision bits,
23 coordinate bits and destination float quantization, accepts its pixels.
Three of the four old blits already passed that diagnostic before the driver
change, but 388 failed it; all four now pass. This distinguishes actual
filtering error from the original comparison's overly strict tolerance.

`precision-only.patch` records the separate CTS reference adaptation. It
converts source reference texels to exact numeric float values, checks sampled
pixels with `isLevel2DLookupResultValid`, and checks untouched pixels against
the reference. Every deliberately corrupted sampled pixel (red +65536) is
rejected: 1413/1413 per case. No tolerance is derived from observed GPU errors.
The combined patch also includes the earlier CTS corrections. This locally
adapted CTS is **not an official conformance result**. Frozen failures remain
unchanged; 704 is not reported as a pass of the original comparator.

The precision rule is documented in Vulkan's
[subTexelPrecisionBits definition](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceLimits.html).
An old [CTS discussion](https://github.com/KhronosGroup/VK-GL-CTS/issues/174)
and its comment-only fix discuss scaled/integer blit classes, but current
[validation checks](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/layers/core_checks/cc_copy_blit_resolve.cpp)
distinguish UINT/SINT from scaled formats. We did not use that conflicting
historical discussion to mark these cases unsupported or to remove formats.

## Final result and limits

The ten original nonpassing cases all pass the explicitly corrected CTS build
in `20260914T001820238959Z-cts-seed22-all-fixed`: 10 Pass, 0 Fail, 0 Crash,
peak 58.25 C, no reboot or kernel/GPU fault. The subsequent eight unchanged
blit controls pass, also peaking at 58.25 C. Every run used the exclusive
60-second watchdog lease and six-channel 0.5-second thermal monitor.

Breakdown versus the frozen sample: **seven Fail and one Crash are fixed by
Mesa changes**; **two Fail require corrected CTS shader/reference code**
(970 and 704). The full 1000 was not rerun. The original report remains
914 Pass / 76 NotSupported / 9 Fail / 1 Crash. These targeted qualifications
resolve its ten nonpassing cases, but are not a fresh 1000-case run or a
formal Vulkan conformance claim. No kernel source or installed module changed.
