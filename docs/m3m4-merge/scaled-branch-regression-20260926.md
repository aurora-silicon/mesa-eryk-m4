# Restore scaled-format selects after a full-suite regression

The 30-frame Lethal Company DX11 gameplay capture exposed an image regression
from enabling `HK_NATIVE_BRANCH_SCALED` by default. On the current M4 kernel,
Mesa a08e6488f94 and e6a74082f1d reproduce every reference RGB pixel. Mesa
2d45450614d changes 279 pixels in each checked 3840x2160 frame (1, 16, 31),
with maximum channel error 2/255. Disabling mixed-width FMA legalization does
not change the discrepancy. Setting only `HK_NATIVE_BRANCH_SCALED=false`
restores exact reference images on that same latest build.

Restore the select implementation as the default, with the matching compiler
cache discriminator. Keep the explicit branch override for investigation.
This removes a newly enabled optimization whose complete correctness has not
been established; it does not claim that the underlying compiler interaction
has been isolated or that all suite failures have the same cause. The prior
Satisfactory-specific 1.2–1.3% gain is not sufficient qualification for a global
default. No screenshot tolerance is relaxed.

Evidence in the private replay repository:
- old binary: 20260926T150554756986Z (a08), 20260926T150711987143Z (e6a)
- mixed-FMA control: 20260926T151652714194Z (same discrepancy)
- select override: 20260926T151846705640Z (all three images exact)
- rebuilt default: 20260926T152153107859Z (M4, all three images exact)
- M3 DX12 gameplay: 20260926T152155456043Z (maximum channel error 1/255;
  2, 1 and 8 changed pixels in frames 6, 20 and 35, within its unchanged
  previously reviewed 4/255, 32-pixel policy)

All hardware jobs used the 60-second watchdog and all-channel thermal guard.
Normal desktop Mesa installations were unchanged. Host compiler tests: 306 pass;
existing sparse-access tests pass. Other full-suite regressions remain under
investigation, including Frostpunk gameplay and the experimental no-GCM profile.
