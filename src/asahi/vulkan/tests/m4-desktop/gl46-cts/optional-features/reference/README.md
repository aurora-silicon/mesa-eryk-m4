# CTS sparse-depth setup correction

## Committed fix

`reference/vk-gl-cts-1.4.6.2` commit `de63a4f` (`m4-sparse-clamp-fixes`)
contains the final correction, exported as `cts-sparse-depth-setup-final.patch`.
The previous `cts-sparse-depth-setup.patch` is retained as the diagnostic
version described below; use the final patch for new builds.

All changes now live in the clamp test itself. Ordinary textures use valid
64-pixel dimensions when the sparse-page query has no results. Sparse
allocation is attempted only for supported formats. Depth levels are populated
with ordinary TexSubImage uploads; neither the invalid R16 image binding nor a
new ARB_clear_texture dependency is introduced. Sampled values and reference
comparisons are unchanged. Successful function/level comparisons are logged.

All six affected cases pass with the final binary. The logs show 57 actual
function/mip-level checks across 1D, 2D, arrays, cube and cube-array targets,
covering levels 0, 1 and 2. See `final-depth6-coverage.json` and the original
QPA logs in `20260922T053924212171Z-gl46-clamp-depth-upload`. No Mesa changes.
`final-binary.json` identifies the corrected binary and its checksum/commit.
The unmodified `/root/gl-cts-1.4.6.2/glcts` and original failure records remain
available; results from the corrected binary are identified separately.

Full regression selection with the committed CTS fix: **1,914 Pass**, zero
Fail/Crash/Timeout/NotSupported, in 109.26 seconds. Evidence:
`20260922T054015845409Z-gl46-optional-cts-fixed`, peak 73.5 C, mandatory 60 s
watchdog and thermal guard. Selection hash is unchanged from the earlier
1,908 Pass / 6 Fail run. Mesa and its advertised features are unchanged.

The latest main-catalog tracking including corrected-CTS outcomes is
18,106 Pass / 1,607 NotSupported / 1 CompatibilityWarning; no unresolved
Fail/Crash/Timeout. This combines previous runs with the corrected 1,914-case
selection, not a fresh 19,714-case conformance run. The original upstream-CTS
snapshot still records its six failures, and is deliberately not overwritten.

Reproduce using the normal guard with an explicit corrected CTS binary:

```sh
python3 tools/gl33-cts-run.py --api gl46 \
  --cases build/gl46-optional-20260922/all-four.txt \
  --output build/NEW_OUTPUT_DIRECTORY --name gl46-cts-fixed-recheck \
  --seconds 300 --jobs 2 \
  --binary /root/gl-cts-1.4.6.2/glcts-m4-sparse-clamp-0193725ec88f \
  --environment _provenance/experiments/20260922T003548369477Z-gl46-zink-plan/remote/renderer-environment.json
```

## Historical diagnostic


The unmodified v1.4.6.2 clamp-color tests fail six optional depth-format cases
before rendering. The parent sparse lookup loop checks NUM_VIRTUAL_PAGE_SIZES,
but this subclass does not. It also sizes non-sparse textures from sparse page
sizes (zero for unsupported sparse formats), and writes DEPTH_COMPONENT16 using
an R16 storage-image binding, which is not a legal image format for that texture.

The patch retains ordinary depth-clamp coverage: skip only unsupported sparse
function/format combinations; choose nonzero dimensions for ordinary textures;
fill depth mip levels with glClearTexImage. It does not change driver limits,
expected sampled values or shader comparisons. Upstream result files remain
unchanged. Patched-binary results must be reported separately.

Spec: https://registry.khronos.org/OpenGL/extensions/ARB/ARB_sparse_texture.txt
Only the listed color formats require sparse allocation; D16 is optional.

All six affected cases pass on M4 with the diagnostic binary, including the
ordinary shadow LOD-clamp comparisons. Evidence:
`20260922T050841594484Z-gl46-clamp-depth-diagnostic`.
The driver is unchanged from the unmodified six-failure run. This validates
the test correction; it is not six passes on the original CTS binary.
