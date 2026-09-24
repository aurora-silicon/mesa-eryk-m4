# traces-db reciprocal and FMA fixes (2026-09-24)

Shared compiler fixes, qualified on M3 Pro G15S and M4 G16G using the separate
traces-db corpus pinned at baf65903abcdf0afc7c6737bd3c1c20e31cc9148.

- 69039a8edfc prevents reverse FMOV propagation from folding saturation into
  RCP/RSQRT/SRSQRT, whose native encodings lack saturation. Keep the explicit
  saturating move. The new saturated reciprocal NIR compile test reproduces
  the RCP rejection before the fix and passes for RCP and RSQRT afterward.
- b25c501df48 materializes one multiplicand when both FMA multiplicands are
  immediate constants. This preserves fused arithmetic and signed zeros while
  selecting an existing register/immediate encoding; no new ISA encoding or
  algebraic simplification is introduced.

Offline validation: agx3_alu_core, agx3_falu_mod_core and agx3_select_core pass.
agx3_compile_fail_closed_test --saturated-reciprocal-only passes. The unfiltered
compile-fail-closed test still stops at its existing preamble-rejection
expectation (it now observes an accepted preamble); it is not reported passing.

An original failing Heaven compiler bundle is retained in the separate local
tracie-m3m4-validation repository, under the M4 fma-capture-114 evidence.
Its input BLAKE3 is
f56fe21e8e0030726b410abc1ecb765b9b8916307bc590a5e5d6bbb31bbd40ce.
The offline agx_shader_replay rejects `ffma #0, #0, r21` before the FMA fix and
compiles the same input afterward (41,058 bytes, binary hash d17a9119).

Guarded hardware validation used identical, hash-verified library packages on
both machines and retained the ordinary desktops. Sanctuary D3D9 completed on
both with all reciprocal/pipeline rejections removed. RGB mean absolute error
against its supplied upstream PNG fell from 1.51375 to 0.19800 on M3, with the
same final 0.19800 error on M4 (8-bit channels, no rescaling). This is an
improvement, not an exact-reference pass. Remaining log diagnostics are Wine
Bluetooth-service/OpenVR messages.

Heaven D3D9 low scenes 1, 12, 3 and 6 completed on both GPUs with zero AGX
compiler rejections after the FMA fix. Eight separate guarded replays retained
images and full API logs. Their captured 8x MSAA allocations remain unsupported:
the current Vulkan device explicitly advertises only 1x, 2x and 4x samples.
Those calls and their dependent resource operations still fail, so Heaven is
not classified as a correct full replay. No sample-count downgrade or feature
advertisement change was made.

Evidence sessions: reciprocal-fix-125 on each GPU at /opt/asahi-m3m4-69039a8edfc;
fma-fix-{114,117,120,123} on each GPU at /opt/asahi-m3m4-b25c501df48.
Original application traces and derived compiler inputs stay in the separate
local validation repository; this Mesa branch contains code and validation notes.
