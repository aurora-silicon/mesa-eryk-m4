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
