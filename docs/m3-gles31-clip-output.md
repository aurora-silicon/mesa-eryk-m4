# M3 native clip-output descriptor acceptance

The frozen GLES31 campaign stopped in segment 489 with 40 KHR-GLES3
cull-distance failures. The first isolated case was
`KHR-GLES3.cull_distance.functional_test_item_0_primitive_mode_lines_max_culldist_3`
(global index 120993, EGL config 1). It reported an unexpectedly invisible
vertex. A shader dump exposed Zink's `vkEndCommandBuffer` rejection with
`VK_ERROR_FEATURE_NOT_PRESENT`; this was not a GPU fault.

`hk_agx3_g17p_vertex_program_info` allowed up to eight clip output words only
for G16G. G15S's same-format native descriptor was rejected, despite the
compiler exporting clip distances and the draw code preserving their plane
selectors. Use the existing G15/G16 shader-ABI predicate for that bound.
The bound remains eight; other hardware families retain their old behavior.
No test expectations or advertised features are changed.

Initial qualification:

- The exact original case fails in a fresh process before the fix.
- The same case passes in all 15 selected EGL configurations after the fix.
- `hk_agx3_vdm_decode_test`: 2795 checks pass, including accepted family and
  rejected nine-distance bounds.
- Full cull-distance regression: 3270 configuration/case pairs (218 cases in
  15 configurations), 2310 Pass and 960 NotSupported, no failures. All 105
  guards completed; peak observed temperature 61.75 C. Unsupported outcomes
  are retained, not counted as passes. All 40 original failed indices passed.

Evidence is under `build/m3-gles31-20260922/triage-cull-{baseline,debug,candidate}`.
Hardware tests use the normal Zink renderer and unchanged 20-second case,
45-second experiment, 60-second watchdog, and 95 C temperature bounds.
The candidate is `/opt/asahi-m3-cts-clip-output`.

A separate noopt/nosched diagnostic did not reach the selected case: disabling
backend optimization exposed an unsupported `block_image_store` operand form
in setup, followed by an exit timeout. Its failed guard and logs are retained
in `triage-cull-noopt`; the machine was rebooted before candidate testing.
It is not evidence of a failure with normal compiler settings, nor a passing
CTS run. Candidate tests restore the normal optimization settings.

Full regression evidence: `triage-cull-neighbors/*/run`, with per-result hashes
in `triage-cull-neighbors/validation-manifest.json`.
