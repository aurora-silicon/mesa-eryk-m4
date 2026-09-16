# M4 shader and image-view minimum LOD (2026-09-16)

G16G implements shaderResourceMinLod using the native SMP minimum-LOD selector
(field 140=2). It composes with implicit/bias sampling, explicit gradients,
array and cube coordinates, depth comparisons, sparse results and offsets.

Apple's offline compiler witnesses establish the operand layouts. A minimum
and bias share operand bank 6 for ordinary samples; array+bias uses bank 4 for
the minimum. Gradients combine the minimum with the array layer or packed
offsets in bank 6. A cube face shares the packed base of the minimum+bias word;
cube arrays retain the separate minimum and three-source face/bias/layer pack.
3D bias+offset and array depth+bias use an additional packed scalar GPR minimum
in SMP. This scalar requires a full-word allocation: a half allocation caused
integer 3D samples to read the wrong minimum. The native staging source for a
packed offset has a seven-bit selector, accounted for during allocation.

For array gradients with MinLod, retain the native offset instead of the old
NIR offset lowering. The latter calculated its coordinate displacement without
including the minimum LOD and failed at border texels. Implicit sampling with
zero bias also retains its minimum instead of reaching the old unsupported
compiler assertion. None of these paths special-case a CTS name or shader.

## Qualification

All J713 runs retain the 60-second watchdog, six-channel 0.5-second thermal
monitor, 95 C cutoff, 12 workers and 60-second per-case bound. Frozen catalog:
`72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.

- Initial 842-case feature run: 509 Pass, 210 Fail, 123 NotSupported.
- Corrected operand packing: 100 of those failures Pass; all compiler refusals
  eliminated. Gradient+offset fixes resolved another 80 cases.
- Full-word minimum allocation and native array gradient offsets resolved
  the final 30 failures.
- Complete feature and regression run, default feature advertisement:
  1,534 Pass / 309 NotSupported / zero failures in 1,843 cases. The feature
  itself is 719 Pass / 123 separate format or sparse-3D feature skips; the
  unchanged regression selection is 815 Pass / 186 NotSupported.
- Host and ARM64 builds pass. Asahi host suite retains 103 passes and the
  same 11 pre-existing failures.

Compact results and exact native compiler witnesses are in `min-lod/`.
Full evidence under workspace `_provenance/experiments/`:

- `20260916T085536157745Z-dx-min-lod-r1`
- `20260916T090439172611Z-dx-min-lod-r2`
- `20260916T090604578096Z-dx-min-lod-r3`
- `20260916T090758210162Z-dx-min-lod-r4`
- `20260916T090847814693Z-dx-min-lod-final` (59 s, peak 64 C)

`VK_EXT_image_view_min_lod` and its feature are also enabled by default on
G16G. Its descriptor integer threshold is converted to the floating-point NIR
source type before fetch lowering; reinterpreting the bits caused all 16
integer-coordinate cases to fail. Array-gradient offsets are preserved when
a dynamic view minimum will be added later in lowering. The feature is part
of the shader cache key and disabled devices avoid the extra descriptor loads.

Image-view qualification: 55 Pass / 42 unrelated exclusive-compute-queue skips.
The combined default-advertisement run covers 1,940 indexed cases: **1,589 Pass /
351 other-feature skips / zero failures**. This includes 719 shader minima,
55 view minima and 815 regression passes. Native packing has 6,413 passing host
checks; the full host suite retains its 103 Pass / same 11 known failures.

- `20260916T091002910665Z-dx-view-min-lod-r1`: implicit-zero-bias assertion;
  coordinated worker stops are not independent GPU faults.
- `20260916T091247696316Z-dx-view-min-lod-r2`: 16 fetch threshold failures.
- `20260916T091427985574Z-dx-view-min-lod-r3`: 55 Pass / 42 NS.
- `20260916T091554260497Z-dx-min-lod-both-final`: 1,589 Pass / 351 NS.
- `20260916T092208187666Z-dx-min-lod-offset-final`: same result after preserving
  view-minimum gradient offsets (68 s, peak 63.75 C).

Final tested library SHA256:
`8e922dc8a194d3ee7c59b2e980b35950fa457e298dc402c6a757f54235eeb8af`.
Strict sparse behavior and other missing DirectX capabilities remain open;
these results do not establish Steam readiness.
