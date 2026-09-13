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

## RGB9E5 packed attachments (case 230)

Lower shared-exponent tile reads/writes to one packed 32-bit word. The format
metadata includes four channels, so preserve implicit alpha=1 on reload while
packing only RGB. Use the matching raw32 EOT instruction and PBE view to avoid
converting already packed bits a second time. Existing uncompressed native
image policy remains in force. The original blending test passes its pixel
comparison, and the small attachment probe passes all 144 exact packed words
through clear, reload, overlapping rectangles, mip 1 and layer 1.
Evidence: _provenance/experiments/20260913T212301590648Z-rgb9-raw-pbe and
20260913T212427784224Z-cts-seed20-rgb9-final (60-second watchdog, peak 57.25 C).
The older probe evidence uses ATTACHMENT_RESULT; the reusable tool now preserves
its original BGR marker and uses RGB9E5_ATTACHMENT_RESULT for the new mode.

The canonical zero-offset single-target EOT ABI is retained; compaction uses
the offset-table ABI only when needed (or for the newly packed RGB9E5 target).
Host graphics-compile diagnostics match the baseline at 4b025e10e77 after
normalizing source line numbers. Five pre-existing fixture failures remain:
f16 background narrowing, div3 leading SR, half2 varying binary, flat-colour
binary and discard acceptance. These are not being represented as a passing
host suite. The focused texture-core test and 32 optimizer unit tests pass.

## Presentation follow-up and DCP recovery

The first complete rerun has all 15 original Fail cases passing and the
userspace crash resolved: 870 Pass, 129 NotSupported, one presentation Timeout.
A 30-second-bound attempt also timed out. Both results are retained unchanged.
The subsequent investigation found that DCP had failed at21:06, independently
of shader tests; see `dcp-recovery/`. The event-pump CTS correction ensures
configure acknowledgements and pongs continue during long Vulkan loops, and
WSI preflight now refuses a latched display fault. A recovery reboot is
required before meaningful presentation validation. This does not claim the
separate hourly D114 firmware callback has been fixed.

After recovery, `deqp-vk-seed20-pump` passes all 600 Wayland presentation frames
in 18.301 seconds with the original 20-second bound. Native scanout confirms
the restored normal Omarchy panel/background and Foot. The login helper also
verified native GPU libraries. Evidence: cts-seed20-recovery-login and
_provenance/experiments/20260913T214535825840Z-cts-wayland-recovered.
The final complete campaign uses 20 workers and a recorded 30-second case
bound to leave presentation headroom; its results are retained separately.
