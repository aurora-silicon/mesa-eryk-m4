# Seed 20260920 corrections and retests

## Nested SPIR-V blocks (case 36)

The historical compute generator decorates both outer_struct and input_buffer
as Block, although input_buffer contains outer_struct. Add a single-member
block wrapper for the separate inputA/inputB buffers and leave outer_struct
undecorated. Both original input layouts, binding numbers, pointer selections
and output comparisons are preserved. No Mesa validation is relaxed.

The separately named deqp-vk-seed20-block includes this correction and the three
previously documented historical CTS corrections. Build hashes and cumulative
patch are retained. Case 36 passes on native M4 with the unchanged baseline
Mesa, producing the expected output. Guard evidence:
_provenance/experiments/20260913T205424440238Z-cts-seed20-block-fix
(60-second watchdog, peak 57.75 C, unchanged boot). The original crash remains
in the first-attempt baseline; this adapted CTS pass is recorded separately.

## RGBA16 normalized storage images (cases 690 and 860)

Enable UNORM and SNORM RGBA16 in native bindless storage lowering and select
the existing formatted F32-lane write ABI; the PBE descriptor controls signed
or unsigned normalized conversion. Both original CTS cases pass with expected
output on M4. Evidence: _provenance/experiments/
20260913T205635734916Z-cts-seed20-normalized-store (60-second watchdog,
peak 57.5 C). Original Vulkan commands and output checks are unchanged.

## Optimizer operand legality (cases 766 and 933)

Fold immediate fmov conversions before allocation, preserving negative zero;
retain explicit zero-extension of half-width ballot predicates and only fuse
comparisons with native operand shapes. Saved compiler bundles replay
successfully offline, and both original CTS pixel comparisons pass on M4.
Evidence: _provenance/experiments/20260913T205911233931Z-cts-seed20-compiler2
(60-second watchdog, peak 57.5 C).

## EOT compaction and cull-distance linkage (200, 359, 532, 987)

Use the existing native offset-table EOT ABI when compaction leaves one
attachment at a nonzero tile offset. B10G10R10A2 uses the same typed store as
R10G10B10A2, with channel order supplied by the PBE descriptor. Recognize the
CULL_PRIMITIVE UVS slots used by Asahi cull-distance lowering while retaining
component, interpolation and slot-bound checks. All four cases, plus the two
optimizer regressions, pass their original CTS comparisons. Evidence: _provenance/experiments/20260913T210227253218Z-cts-seed20-eot-cull

## Wayland platform compatibility (cases 759 and 976)

Backport the stable xdg-shell handshake to the historical CTS platform while
retaining wl_shell fallback. Build protocol bindings from installed
wayland-protocols XML with wayland-scanner. Vulkan test bodies are unchanged.
The basic swapchain rendering case passes on the actual compositor (17.758 s).
The display-timing case now reaches its capability check and returns
NotSupported for VK_GOOGLE_display_timing, which this driver does not expose.
This resolves both setup errors; it is one rendering pass and one optional
feature skip, not two passes. Both runs verified the 60-second watchdog and
thermal guard, peak 57.75 C. Evidence: experiments ending in
cts-seed20-wayland and cts-seed20-wayland-timing.

## Invalid 3D framebuffer preclear (case 719)

The historical clear-color test creates a VK_IMAGE_VIEW_TYPE_3D view and uses
it directly as a framebuffer attachment for optional initialization. This is
not a valid framebuffer view type. For 3D images use the existing transfer
path: retain all tested vkCmdClearColorImage ranges, both clear values, full
volume/mip readbacks and pixel checks; omit the invalid attachment preclear.
The selected whole-volume case overwrites every tested texel. A separately
identified CTS executable deqp-vk-seed20-final passes that test on unchanged
Mesa; the original failure remains recorded separately. Evidence: _provenance/experiments/20260913T211057136561Z-cts-seed20-clear3d

## Cube comparisons and dynamic gathers (100, 287, 358, 818, 861)

Compose native cube-face, shadow-reference and gather controls while preserving
four-component comparison-gather results. Lower dynamic gather offsets through
NIR texture-size/coordinate arithmetic, retaining the array layer. All five
original CTS cases pass their original output checks on M4; no test changes.
Evidence: _provenance/experiments/20260913T210614576841Z-cts-seed20-textures
(60-second watchdog, peak 57.5 C). The host agx3_tex_core test also passes.
