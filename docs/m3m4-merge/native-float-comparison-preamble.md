# Native F32 comparison preambles

Allow uniform `feq`, `fneu`, `flt`, and `fge` with 32-bit inputs in the native
preamble. Their Boolean outputs use the qualified 32-bit exports. Arithmetic,
conversions, rounding, narrow loads, and wide exports retain their existing
restrictions. Native/memory preambles still require their explicit opt-in flags.

Validation on 2026-09-26:

- All 299 host backend tests pass. The added float-comparison export regression
  fails on 939d8bb2b34 and passes with this change.
- Twelve hot compute and twelve hot fragment inputs compile successfully.
- Six pinned highp vec4 fragment comparison CTS cases pass on both chips:
  catalog indices 782814, 782826, 782838, 782850, 782862, 782874 in the existing
  SHA256 72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf catalog.
- A 32-update UBO comparison oracle passes all 32,768 U32 values on both chips.
- The stronger `tests/m3m4-preamble/float-comparison.c` checks all pairs of
  twelve special values, including zeros, subnormals, normals, infinities,
  quiet/signaling NaNs, over 144 UBO updates and 147,456 U32 outputs. Both
  chips match the explicit `--flush-subnormals` CPU model with zero mismatches.
  M3's previous driver also passes that model. The strict IEEE model has
  13,824 mismatches on both candidates and on the previous M3 driver; these
  are retained failures of that model, not relabeled passes. The driver
  advertises F32 flush-to-zero and does not advertise F32 denorm preservation.
  This does not qualify a preserve-denorm execution mode.
- The M4 FTZ wrapper reported missing success evidence because its `--expect`
  regular expression did not escape parentheses. The actual command output
  says PASS with zero mismatches and its guard completed; the original wrapper
  summary is preserved unchanged.

The unchanged 30-frame Satisfactory Vulkan gameplay replay matches RGB frames
1/16/31 exactly on both chips, with the same 4K output, 2880x1620 internal size,
capture, replayer, and adaptations. First M3 repeats: 4.98288, 5.00795, 5.01415
FPS; confirmation: 4.99045, 4.99254, 5.01255. This straddles 5 FPS and is not a
comfortable sustained margin. M4 repeats: 3.57257, 3.57725, 3.57537 FPS (median
3.57537). These are replay timings, not live-game performance. All hardware
runs retained the watchdog and thermal guards; desktop Mesa is unchanged.
