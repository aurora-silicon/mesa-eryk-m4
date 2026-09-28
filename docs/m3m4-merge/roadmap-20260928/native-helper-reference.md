# Native Vulkan Satisfactory helper-reference review

The current preamble-disabled M3 and M4 builds produce identical RGB output
at frames 1, 16 and 31. They differ from the pre-helper M4 reference by up to
94/94/96 channel levels, in 44,904 / 448,145 / 612,376 pixels. These differences
are not covered by the separately accepted sparse Frostpunk rounding policy.

A diagnostic candidate reverses only the BACKFACING original-coverage fix in
`dd452c4dd66`. Its three M3 images exactly reproduce the earlier remat-r2 M3
images. The current image also exactly matches dd452, ruling out the newer
preamble-composition change (preambles are disabled in these controls). This
establishes that the changed reference is caused by the helper correction.

The correction itself has an independent failing-before/passing-after fragment
derivative oracle on both GPUs, the expanded mixed-facing triangle probe,
software normal readback in Frostpunk, and the 2,058-case regression selection.
See `frostpunk-preamble-investigation.md`. Reverting that correction just to
recover the old image would reintroduce a demonstrated compiler bug.

Reviewed the three current M4 images: the moving animal, terrain, rocks,
foliage and HUD are present. This output is the new exact regression baseline
for opt-in optimization checks. Its existing capture adaptations (including
depth-bounds AO and distance/height-field shadow limitations) remain recorded.
This is a reviewed image regression reference, not a software-renderer oracle
for the entire game or proof of unmodified application support.

The private benchmark repository retains all images, binary hashes and the
single-change reversal patch in `provenance/roadmap-20260928-preamble-publication`.
`native-helper-control-comparison.json` records each original run and metric.
The new M4 reference is `20260928T214033106395Z`; the previous reference
`20260928T104955386569Z` and its failed comparison are preserved. Neither the
reference review nor the compiler experiment enables preambles by default.
