# Vulkan 1.4 random sample, seed20260929

Frozen broad Linux-only sample:10000 cases out of3206030 eligible entries in
CTS1.4.6.2, selected with Python random.Random(20260929).sample. Case-list SHA256
5bf35cce436384ed9cad34d0f90110b814a6e57470e6cc1d3987335b28fa2aca.
Optional skips are retained separately; requirements still need review.

Initial20-worker run stopped after a userspace SIGSEGV:355Pass,294NotSupported,
2Fail,1Crash,29Interrupted,9319Unrun. Runner time20.132s; peak62.75C, watchdog60s.
No GPU fault. Original outcomes remain in run1-summary.json and workspace QPAs.

The scaled RGBA8 swizzle case crashed inside CTS createShaderModule. Its table
compiled PROGRAM_2D_UINT but runtime getSamplerType correctly selected the
floating-point program for USCALED. The missing binary caused the crash.
The same table error affected17 scaled formats. Use FLOAT/FLOAT_BIAS programs;
retain texture formats, swizzles and pixel comparisons. The original case now
passes on M4 (guarded180058, runner0.408s, peak58.5C).

cts-scaled-swizzle.patch is cumulative with the explicitly documented seed28
CTS corrections. Source baseline:f6a29701220f34dd1407513bfe80d74ca7b392ce.
Build using tools/cts-patched-build.py with reference/vk-gl-cts-1.4.6.2 and
build/cts-1.4-arm64. The build manifest records executable/patch hashes and
restoration of original source files. This is an adapted diagnostic CTS build,
not an unmodified conformance submission. Remaining cases/fixes are in progress.

## Split depth/stencil copies and sparse queries

Continuation run2 completed another 4,140 cases before a Mesa assertion in
`depth_stencil_msaa_copy.partial.d24_unorm_s8_uint_D_2_bit_bind_offset`.
The graphics copy properties still described interleaved D32/S8 and omitted
D24/S8 entirely, although image allocation splits both into depth and stencil
planes. Use one-component raw views of each physical plane, including D24 float
storage; preserve aspect selection and partial multisample copies.

Two sparse image format queries also contradicted the advertised sparseBinding
feature. Reject sparse image flags when sparseBinding is false, including through
the shared sparse format query path. No feature advertisement was changed.

All three original cases pass with installed Mesa SHA256
`16ae1f0c91889e3e4bf4deb8c9157ae83c793f504f12045681cf48e65e0081ef`,
0.609 seconds runner time, peak 58.25 C, watchdog guard pass.
Evidence: `20260914T180951615594Z-cts29-depth-sparse-fixed`.
The full frozen sample remains incomplete; no broad regression was run.

## Native sampling, storage descriptors, and query tuples

Run3 completed the remaining 5,208 cases in 145.088 runner seconds, peak 63 C.
The cumulative ledger after the crash fixes had 4,503 Pass, 5,473 NotSupported,
24 Fail, zero Crash/Timeout/unrun. Skips still require capability review.

Three original non-array shadow tests now pass after extending register LOD,
bias and packed offsets through native descriptor lowering and the encoder.
Byte-exact texture unit fixtures match the retained Apple applegpu_g16g compiler
witnesses under `native-shadow/`. Host texture core: 6,327 checks pass.
The array-shadow vertex case now compiles but still has an image mismatch;
compilation alone is not counted as a pass.

Five transform-feedback query-copy cases pass: copy every counter in the tuple
and append availability only after the final result. Copies remain GPU commands.

Four image-store cases and one packed-integer multisample resolve pass after
propagating supported R8/RG8/R16 and RGB10A2 types through both descriptor and
backend format selection. A half-float subgroup inclusive-add case passes using
NIR active-lane shuffle reduction, preserving half arithmetic instead of issuing
a native word reduction with half operands.

Final targeted group: six Pass, 1.613 seconds, peak 60.25 C, guard pass; installed
Mesa SHA256 `db34a5b532ce7c5653e3fd86574943070e3acfcc9cc176879c7eed7c0ea5f683`.
Evidence `20260914T182220807316Z-cts29-storage-half`. An earlier storage attempt
was denied a watchdog lease by concurrent display-controller work and ran zero
cases; it is not a driver failure. No broad regression was run.

## Scaled comparison routing and RGB9E5 copies

Five scaled-destination linear blits failed in CTS's integer comparison branch.
The existing scaled reference helpers were only selected inside isFloatFormat,
which is false for scaled storage. Route scaled source/destination comparisons
to those helpers. Keep the original images, interval checks, and thresholds.
The separate cumulative CTS patch and binary manifest identify this harness fix.

One same-format RGB9E5 image copy changed values through float sample/repack.
Route this format through the existing native raw GPU copy, preserving texel
representation. The original copy and all five corrected blit comparisons pass
in 0.810 runner seconds, peak 58.25 C, guard pass. Installed Mesa SHA256
`b5727b7ff71ce8a07454a8ceb4ff7d0a6430c0e8cafce47d23b4dbfea5b18501`.
Evidence `20260914T182757124994Z-cts29-scaled-compare-rgb9`.

## Native offsets and release-fence retirement

The remaining array-shadow LOD and projected-gradient offset cases pass with
native offset operands. Coordinate arithmetic at a single mip could not preserve
these sampling semantics. Pack array layer, signed offsets and LOD into their
shared auxiliary word; gradient offsets use bank6 beside the derivative banks.
Two additional byte-exact fixtures match `offset-grad-array.metal`, target
Apple applegpu_g16g. Host texture tests: 6,335 checks pass.

The fragment write-after-read memory-model case reproduced a failure in a
compiler-capture run despite an earlier isolated pass. Register dependency waits
did not finish a payload read before its release fence when that read's value
was used later. Device fences now drain issued async slots in the dependency
planner. Subsequent consumers do not wait twice and new slot generations remain
tracked. The host slot tests cover this release shape (71 checks pass).
The saved failing shader recompiles offline; four simultaneous original CTS
instances pass under the watchdog/thermal guard. See memory-fence-repeat4 summary.
Installed Mesa SHA256 `07895e395b7ea00fbe99237e51e7caf410ec2c502841c1818a815d6228e6bea1`.

## Explicit chroma midpoint reference

The CTS 4:2:0 explicit midpoint reconstruction passed left-hand weights into a
helper that expects right-hand weights. Correct even/odd weights to 0.75/0.25
for XY reconstruction, retaining all intervals and images. This follows the
Vulkan sampling equations, not a driver-specific reference adjustment:
https://github.khronos.org/Vulkan-Site/spec/latest/chapters/textures.html#textures-chroma-reconstruction
The original YCbCr case passes, 0.408 seconds, peak 58.5 C. Lavapipe lacked
forceable explicit reconstruction for this case, so its skip is not validation.
