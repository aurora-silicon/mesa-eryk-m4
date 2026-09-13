# Vulkan 1.0 random baseline — 2026-09-13

100 cases selected before execution from233795 must-pass entries with Python
Random(20260913). No exclusions/redraws. Original outcomes:35 Pass,19 Fail,
41 NotSupported,5 userspace crashes,0 timeouts. Results and exact names are in
results.json/cases.txt; original logs remain at the listed workspace evidence
paths and on the target. Unsupported results are not passes. This is a sample,
not conformance. Driver:Apple M4(G16G B0), native Asahi Vulkan, library
19afd1a56957886e18ecbaf88f63b473700c69a5cbb3011df0227dbc783369bb.
The four additional info tests passed, independently of the100 sample cases.

All batches completed with a60-second watchdog lease and six-channel thermal
monitoring. Highest sample temperature58.75 C. No GPU hang/watchdog reset.
The existing DCP idle fault occurred at uptime3610 before CTS(3999); the render
GPU continued to retire jobs. A guarded reboot subsequently recovered display
state; default SDDM remained enabled/active.

Observed failures include compiler refusals(imad, texture_sample, texture_load,
image_write, ld_tile), multi-plane image handling,16-bit shader inputs and one
texel-view pixel mismatch. These are observations, not a blanket kernel blame.
Case45's render-pass abort also crashes under lavapipe(control
20260913T104404951360Z-cts-1-0-old-renderpass-control). Pinned CTS returns the
input-aspect vector by value then passes .data() from that temporary after its
lifetime ends(vktRenderPassTests.cpp:680,1190). Keep this historical test bug
separate from driver defects; the original baseline test code is unchanged.

First fix:seed20260913 drawn again from the19 Fail cases selects index10,
integer cube sampling with explicit tiny LOD. Fresh hardware run
20260913T104946283939Z-cts-cube-fix passes against CTS's reference image.
See compiler/test/probes/README-g16g-cube-register-lod.md for native evidence.
Unit checks:6262 texture-core checks,33 focused optimizer/staging tests and
cube-lowering-only pass. The broader historical graphics suite has unrelated
stale expectations. ExplicitLodStagingAndLifetime fails both with this change
and at baseline HEAD3b3a3df7ce7; its baseline log is retained in
build/cts-fixes/baseline-explicit-lod-test.log. Do not claim the full unit suite
passes.

Second fix:continuing the same RNG without replacement selects index29,
`dEQP-VK.binding_model.shader_access.secondary_cmd_buf.uniform_texel_buffer.vertex.single_descriptor.offset_nonzero`.
Its vertex shader contains register multiply minus immediate1. Generalize the
existing immediate-multiplier lowering to register multipliers, emitting the
native pure multiply and subtract-immediate with modulo-2^32 semantics.
Original compiler bundle6a7a8a01caba15aa0ddfeb33673d8a69ecc0ae7c8bb4d892fd927970a2d20c70
replays successfully after the fix. End-to-end NIR regression:
`agx3_graphics_compile_test --imad-negative-only` passes. Native CTS run
20260913T105408152345Z-cts-negative-imad-fix passes, peak58 C, full guards,
libraryb1d2fbf56b331f4e9f3518648d5fdb3d533a5ca19621dc3b3cb379746dc04752.
Normal login and Foot restored after the earlier idle-display recovery:
/root/asahi-evidence/cts-restore-login-20260913T1054. Kernel unchanged.

Third selection:index42, geometry uniform texel buffers. The arithmetic fix
cleared its compiler rejection, exposing a Mesa command-recording refusal:
logical GEOMETRY was rejected despite the raster variant executing as hardware
VERTEX. The runtime also reconstructed root_uniform from logical stage alone,
losing the texture-root reservation. Retain the final compiler root address in
shader metadata and use the physical stage for indexed-sampler acceptance.
Bump shader binary version9->10; reject the preceding version in the cache test.

20260913T105823659292Z-cts-geometry-uniform-fix:all4 cases pass(indices42,1,10,29
in the original sample), peak57.5 C, full guards, no kernel changes.
Librarybc1d7cfc2b6e5d39e613cedfa42086c7379abf613469da23dfa37b72f33612e9
contains the root/stage fix before the cache-version-only bump. The version10
build was subsequently installed;2769 VDM decode/cache checks pass.

Fourth selection:index55 (vertex/fragment push-template uniform buffers) now
passes with the existing arithmetic fix; no separate source change was needed.
Evidence:20260913T105941993579Z-cts-push-template-check.

Fifth selection:index37, mutable RGBA8_UNORM/RGBA8_UINT draw/load. Add integer
RGBA8 to native bindless storage lowering and accept its U32 input lanes in
the indexed IMGWCI packer; the PBE supplies destination width. This reuses the
existing G16G integer-write witness, with no new instruction encoding.
`agx3_graphics_compile_test --integer-storage-only` passes. The historical
compiler bundle remains rejected because it was captured after the missing
NIR lowering; the fresh full-pipeline CTS run covers that transformation.
20260913T110524038553Z-cts-rgba8ui-native-store:Pass, peak57.5 C, full guards,
library1f144b9cc221f2e523edcb123b72ffd328ff16a6f3fd07ef101de44e2b031818.
Kernel unchanged.

Sixth selection:index43, three-plane 10-bit 444 to 420 optimal/linear copy.
The native GPU raw-copy path rejected plane aspect bits and linear endpoints.
Use the selected plane's format, address, dimensions and tiling/stride in
buffer/image and image/image transfers. Preserve compression and packed
depth/stencil exclusions. No CPU copy or kernel change.
20260913T110821361548Z-cts-plane-copy:original case43 Pass, peak57.75 C.
Library8ce7c7b0eebdf8664ccd0ccaaf681f2600c4f63e542a4ca45b30db94cd9afc98.

20260913T110850190124Z-cts-plane-copy-regression:original case66 also Pass,
plus linear/linear Pass. Three optimal-destination regressions exposed another
historical CTS bug:downloadImage transitions to TRANSFER_SRC_OPTIMAL but passes
TRANSFER_DST_OPTIMAL to CmdCopyImageToBuffer (vktYCbCrUtil.cpp:619). The driver
correctly rejects that invalid layout. Preserve the baseline binary and counts.
A separate diagnostic binary applies only ycbcr-read-layout.patch, SHA256
6a82de1157dfe922ab967cfa5b1404cddc0749d6b935d08321940919cbd1726d.
20260913T111025012776Z-cts-plane-copy-corrected-readback:all5 regressions Pass,
including linear/linear, linear/optimal, optimal/optimal, disjoint planes and
case66. This is corrected-test evidence, not five original-CTS passes.
Semantics checked against https://docs.vulkan.org/spec/latest/chapters/copies.html.

Seventh selection:index23, ETC2 punchthrough blocks viewed as RGBA16_SINT
attachments. Original output was uniformly black; the retained QPA/reference
and compact statistics are in 20260913T111116577746Z-cts-block-view-pixels and
build/cts-fixes/block-view-images. Diagnostic readback confirmed the upload
was byte-exact but the attachment output was zero before decompression
(20260913T111820243687Z-cts-block-view-source-readback). Successful shader bundles are retained at
20260913T111540483190Z-cts-block-view-compiler-inputs.
RGBA16_UINT and RGBA16_UNORM controls passed, isolating format-dependent EOT.

Apple G16G AIR-NT32023.917 builtin format8/components4 compiles to the ordinary
68-byte I16 EOT (selector0x15), unlike inherited G17P's84-byte special program.
Restrict the special program to G17P. Native input/archive/tool hashes:
build/cts-fixes/native-rgba16-sint-eot; tracked reproducible input and witness
are in compiler/test/probes/README-g16g-sint16-eot.md.
20260913T112057102686Z-cts-m4-sint16-eot:original case23 Pass, peak58 C,
library44d03aabcb5f82a67bffc58eb71f2cd514874355523c40a0d88804a7339db80e.
No CTS assertion, application shader, kernel or software-rendering fallback
changed. The original CTS source and binary have been restored after diagnostics.

Eighth selection:index4, RG8 sRGB blending. The backend refused two-channel
W8 tile loads despite already packing/unpacking their two bytes correctly.
Fresh Apple G16G RG8Unorm framebuffer-fetch compilation witnesses mask3/W8;
see compiler/test/probes/README-g16g-rg8-fetch.md. Admit that exact form.
All54 tile-core checks pass; original failed compiler bundle101de234... replays
successfully (412bytes). 20260913T112410307569Z-cts-rg8-tile-load:original CTS
case4 Pass (result image matches reference), peak58.5 C, full guards.
Libraryb898fc1ea3acae2dc693383e185319f4cfc63030573efc3cbf0c7f65b1880257.

Ninth selection:index48, integer-array gather with multiple negative offsets
and swizzles. Enable the witnessed array/offset combination in native resource
lowering, immediate translation, IR width validation and instruction packing.
Native evidence:compiler/test/probes/README-g16g-array-gather.md. All6264
texture-core checks and end-to-end --array-gather-only compile test pass.
The first attempt stopped in userspace IR validation (20260913T112854797486Z-
cts-array-gather-offset); it was not a GPU crash. After fixing immediate width,
20260913T113102546620Z-cts-array-gather-validated:original case48 Pass,
peak58.75 C, full guards. Library
73b5d0efd21c9db27d01c6de4ef7fe89bc0b3ced85a84c7eb38ae6609249197f.

Tenth selection:index66 already passed with the plane-copy fix, using the
original test (20260913T110850190124Z-cts-plane-copy-regression, local index4).
Continue to original index19; no extra source change needed for66.

Eleventh selection:index19, extended compressed-block texel views with a
runtime compute mip level and signed RGBA16 store. Allow the existing native
integer-LOD fetch in compute, lower signed RGBA16 storage descriptors and emit
integer U32 input lanes with the PBE's signed16 destination format. Native
witness:compiler/test/probes/README-g16g-integer-copy-lod.md.
--compute-lod-store-only passes through the full NIR/IR/native compiler path.
20260913T113435091246Z-cts-compute-lod-sint16-store:original case19 Pass,
peak58.25 C, full guards, libraryc99775a2887dc8720b8d3a920f5f53daadc44c8f0d9a4527547534345ddcf060. Kernel unchanged.
