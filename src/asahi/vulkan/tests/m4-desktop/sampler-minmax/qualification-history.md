CPU-cap correction: the "full-clock CPU cap" below was the requested ceiling, not the effective limit. See README-sampler-minmax.md and effective-cpu-caps.json.

# M4 sampler min/max reduction (qualification in progress, 2026-09-16)

M4 HAL200 has no qualified native min/max sampler reduction. The Gen5 path
belongs to Apple10 and must not be enabled on M4. Honeykrisp now lowers
reduction to nearest centre taps, retaining a software sampler state in the
existing eight-byte descriptor field. Native sampler indices still select the
nearest hardware entries; descriptor stride and custom-border storage stay
unchanged. Min/max combines only nonzero-weight texels, including both mip
levels for fractional mip interpolation. Sampling wraps, cube projection,
array layers, sparse status and custom borders compose with the lowering.

The feature remains gated by `HK_EXPERIMENTAL_MINMAX=1` until qualification
finishes. Both filter-minmax properties and format bits use that gate.

Anisotropic reduction preserves gradients (including cube gradients before
the normal cube-LOD lowering), computes the major/minor axis ratio and uses a
bounded loop of up to 16 footprint samples. The sampler's min/max LOD and bias
are retained as fp16 instead of HAL200 fixed point: quantizing 1.01 to 1.0
would exclude an entire contributing mip level from a reduction.

LOD queries now retain their full native two-word destination. Generated
array queries omit the array index from their coordinates. These fix compiler
refusals/validation crashes in the first experimental run.

## Evidence so far

Every hardware run uses the 60-second watchdog, six-channel 0.5-second thermal
monitor, 95 C cutoff and full-clock CPU cap. CTS uses 12 workers and the fixed
60-second case bound. Frozen catalog SHA256:
`72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.

- `20260916T093054074674Z-dx-minmax-r1`: compiler validation errors; coordinated
  stops are not independent GPU faults.
- `20260916T093227623410Z-dx-minmax-r2`: 42/42 shape/filter probes Pass.
- `20260916T093302889570Z-dx-minmax-first-r1`: first 10,000 selected cases,
  2,556 Pass / 7,444 NS / zero failures (482 s, peak 70.5 C).
- `20260916T094125539628Z-dx-minmax-oracles-r1`: 38/38 exact independent probes
  Pass, including fractional mip reduction, sampler/view minima and bias.
- `20260916T094407576610Z-dx-minmax-first-r2`: same 10,000 selected cases with
  corrected CTS component-mapping admission: 4,260 Pass / 5,740 NS / zero
  failures (582 s, peak 68.5 C).
- `20260916T095716619391Z-dx-minmax-oracles-r2`: 48/48 exact probes Pass after
  fp16 state and anisotropic-loop implementation (isotropic sampler settings;
  nontrivial anisotropy probes were qualified in the next run).

- `20260916T095850709796Z-dx-minmax-r3`: 42/42 shape/filter probes Pass.
- `20260916T100222609534Z-dx-minmax-oracles-r3`: 64/64 exact probes Pass,
  including 2/4/8/16 anisotropy, two major-axis directions and view minima.
  Peak 60 C; library SHA256
  `07a19bed2e4b4996d2aba61382bc8dd011b23ac8bb462b181fbca852346b7542`.

The CTS version unconditionally skipped nonidentity R mappings even when
`filterMinmaxImageComponentMapping` is true. The diagnostic patch makes that
skip conditional on the actual property. It changes admission only and keeps
the full result comparison (which already checks the property separately).
New binary `deqp-vk-minmax-components`, SHA256
`c1a637217f05b42ef068ef2b777aadc8d70a3f44d20e5155ca6bbb31ad0a165f`, includes the
existing cumulative scaled-blit fixes. Build manifest and exact patch are in
`build/directx-sparse/cts-minmax-build/`; original source/binary were restored.
These are diagnostic CTS results, not an unmodified conformance submission.

Independent probes: `tools/minmax-sampler-cases.py`, using
`tools/vk-buffer-oracle.c`. Their CPU reference follows Vulkan's reduction
across mip levels. Local lavapipe incorrectly uses `lp_build_lerp` across mip
levels; single-level probes pass there, fractional-mip probes deliberately
retain the specification-derived expectations. Lavapipe also lacks view MinLod.

Sources: [Vulkan sampling](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/chapters/textures.adoc),
[Apple feature families](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf).

## Separate issue found during qualification

`20260916T095404898193Z-dx-view-min-combinations` exposes ordinary weighted
sampling view-minimum bugs outside the previously passing CTS group: explicit
LOD applies the view minimum before sampler bias, and sampler maxLod can
suppress a greater view minimum. These must still be fixed. The new min/max
fallback already applies the view minimum after sampler bias and clamps.
An exact-byte gradient expectation also differs by 1/256 LOD, which is allowed
precision; add a numeric bound for that probe rather than treating it as a
sampling defect. The generator is `tools/view-min-lod-cases.py`.

Full 53,190-case reduction selection, final default advertisement, broad
regression, sampler-combination fixes and Steam readiness remain pending.

Weighted view-minimum repair is being qualified. It reuses footprint filtering
with interpolation, adds nearest sampler companions and four bytes to the Gen9
software descriptor tail (stride 76), and retains the native path for zero
view minima. LOD queries reconstruct the advertised sampler state as well.
The preceding runs used the unchanged 72-byte descriptor; do not infer that
they qualify the subsequent ABI or weighted filtering change.

## Weighted view-minimum qualification

- `20260916T101322825675Z-dx-minmax-view-oracles-r1`: all 64 min/max,
  eight constant-mip view combinations and 32 weighted footprint probes Pass.
  The combined verdict failed because the new cube-query reference incorrectly
  assumed one cube face while its directions crossed face edges.
- `20260916T101535018394Z-dx-minmax-view-oracles-r2`: the same sampling suites
  Pass. Cube-query geometry still touched the face tie at its largest gradient.
- `20260916T101856779571Z-dx-minmax-view-oracles-r3`: cube size 128 and direction
  gradients scaled by 1/64 keep all quad lanes strictly on +Z; all eight 2D/cube
  min/max, linear/nearest LOD-query configurations Pass through Zink. The 104
  sampling probes Pass. Additional shadow footprint probes expose unqualified
  nonzero immediate shadow LOD selection; only eight of 31 compile/pass.
- `20260916T101631676709Z-dx-minmax-view-reg-r1`: 1,555 Pass / 351 NS / 34 Fail.
  Generated depth LOD queries retained shadow metadata despite having no
  comparator, preventing native indexed resource lowering. Clearing those flags
  fixes the refusal.
- `20260916T102017594469Z-dx-minmax-view-reg-r2`: 1,589 Pass / 351 NS / zero Fail,
  79 s, peak 63.75 C. Library SHA256
  `e20d729e7f6d8c5ec365c9c906d4ba372e7793c5e0192740e4a02f75d33c31e4`.

The current compiler repair preserves nonzero constant shadow LODs in registers
both during NIR instruction selection and immediate propagation. It does not
widen the native encoder's unsupported immediate forms. Hardware qualification
is recorded below.

- `20260916T102251227386Z-dx-minmax-view-oracles-r4`: all **143 independent
  configurations Pass**: 64 exact min/max, eight constant-mip view combinations,
  32 weighted footprints, 31 depth-comparison footprints, eight 2D/cube LOD
  queries through Zink. Peak 61 C. Library SHA256
  `adac8f3f62e5a37ab479e3d421aa095ccb7903361b06eba3bf9cf4ba3de956c8`.
  Weighted color worst error < 0.000014; depth < 0.00000024. The view-zero
  native-gradient control differs by 1/256 LOD as before; all nonzero view
  combinations match exactly. The whole 53,190-case selection is now rerunning
  on this build in bounded 10,000-case segments, with catalog metadata attached.

The complete selection includes all current catalog cases grouped by min/max
feature, format support and component-mapping reasons: no sampler-feature case
is omitted. `VK_AMD_shader_trinary_minmax` is a separate shader-arithmetic
extension and is not part of this sampler qualification.

The independent tools are mirrored beside this report. The mirror of
vk-buffer-oracle.c deliberately excludes unrelated pre-existing root-tool
shaderInt16/16-bit-storage edits. `oracles.tar.gz` retains generated inputs,
expected bytes, shaders and runners. Raw experiment evidence remains at the
named `_provenance/experiments/` directories. Archived intermediate failures
are retained and are not replaced by the final results.

- `20260916T103508060590Z-dx-minmax-sparse-oracles-r1`: all **64 fully bound
  sparse min/max probes Pass**, checking both sampled values and resident status.
  This adds gradient, offset, array, min-LOD and anisotropy combinations. It
  does not establish unbound-page behavior. Same adac8f3f... library, peak62.75C.
- Final selection `[0,10000)`: **4,260 Pass / 5,740 NS / zero Fail**, peak68.5C,
  `20260916T102413701369Z-dx-minmax-final-0-10000`.
- Final selection `[10000,20000)`: **3,650 Pass / 6,350 NS / zero Fail**,
  peak69C, `20260916T103631940645Z-dx-minmax-final-10000-20000`.
  Indices here are positions in the selected 53,190 cases; attached metadata
  retains their original global indices and frozen catalog SHA256. Later
  segments are still running; these are not full-feature qualification yet.
- Final selection `[20000,30000)`: **5,155 Pass / 4,845 NS / zero Fail**,
  peak69C, `20260916T104738383179Z-dx-minmax-final-20000-30000`.
- Final selection `[30000,40000)`: **3,835 Pass / 6,165 NS / zero Fail**,
  peak69.25C, `20260916T105945302536Z-dx-minmax-final-30000-40000`.
