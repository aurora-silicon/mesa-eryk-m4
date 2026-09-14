# Vulkan 1.4 random sample, seed 20260926

Frozen broad Linux sample: 1,000 cases, zero overlap with seed 20260925.
Original CTS 1.4.6.2: 405 Pass, 47 Fail, 548 NotSupported, no crashes/timeouts.
Runner: 21.720 seconds; guarded command/evidence transfer: 24.204 seconds;
staging: 1.490 seconds. Peak temperature: 59.75 C.

Baseline evidence: `_provenance/experiments/20260914T074325312596Z-cts14-seed26-baseline`.
The original outcomes are frozen. The completed support audit and reconciled
results are below; intermediate counts describe each fix, not the final state.

## Sparse vertex exports

Constant position components leave holes in the UVS export indices. Default
layer exports can be scheduled out of order with the remaining positions.
The pre-RA sink pass rejected those sparse sets, leaving an export order the
native sparse packer cannot consume. Sort the live SSA stores while retaining
their actual indices, control-flow boundaries and side-effect ordering.

Offline replay: 11 formerly refused bundles now compile. A twelfth vertex
bundle progresses to a separate quad-shuffle refusal. The targeted host test
checks sorted sparse indices and the corresponding UVSW staging operands.

Hardware evidence: `_provenance/experiments/20260914T075044414121Z-cts26-sparse-vertex`.
Of 23 selected original failures, 17 now pass and six remain compiler failures.
Peak temperature: 58.25 C. Original CTS binary, no test adaptations.
This is a partial fix ledger, not a claim that the full sample passes.

## Operand legalization and G16 native forms

Preserve at least one register operand in constant integer comparisons and
branches; retain structured execution-mask semantics. Fetch aligned uniform
words and split the selected half before RA for narrow uniform inputs.
G16G LDTG has no G17S narrow-extension selector; use the native byte-load
form. Enable the four quad-broadcast lanes verified against Apple G16G
compiler output. Native sources, binary text and hashes are retained here.

All 17 original refused compiler inputs now compile offline. Native byte-load
and quad-broadcast encoding checks pass. Hardware evidence:
`_provenance/experiments/20260914T075645878847Z-cts26-operands`.
Of the 30 remaining original failures, 12 now pass, 18 remain. Two multi-draw
cases progress to command-buffer rejection. Peak 58.25 C; no crash or timeout.
Total resolved so far: **29/47 original failures**, all on unmodified CTS.

## DrawID and three-dimensional blits

Upload the full 32-bit DrawID ABI slot (two uniform halves). This fixes both
graphics uniform loading and the tessellation compute path. Select padded RGB
and scaled integer destination views before choosing the 2D/3D blit path,
reusing the existing common 3D raster blit and conversion shader.

All four affected original cases Pass, peak 56.5 C. Evidence:
`_provenance/experiments/20260914T080148805379Z-cts26-meta`.
Total driver fixes: **33/47 original failures**.

Seven of nine additional scaled blits Pass with the previously retained CTS
`scaled-blit.patch` correction (separate binary, original CTS unchanged),
peak 56.75 C. Evidence:
`_provenance/experiments/20260914T080012963601Z-cts26-scaled-blit-oracle`.
Thus 40/47 are resolved: 33 Mesa fixes and seven identified CTS corrections.
Seven remain; skip audit is still pending.

## CTS scaled clear and sampler declarations

Four remaining scaled cases Pass with `cts-scaled-types.patch`, using the
separately built and hashed CTS binary. Correct integer sampler declarations
for floating sampled types, and convert the clear union to float32 for scaled
formats at the API boundary. Preserve the original integer reference values,
input generation and comparison thresholds. Includes the already qualified
scaled blit/image sampler and signed packed-value reference corrections.

Hardware evidence: `_provenance/experiments/20260914T080404534872Z-cts26-scaled-types`.
Peak 56.75 C; original CTS source and binary restored after building.
Resolved: **44/47** (33 Mesa fixes, 11 CTS corrections). The mixed MRT MSAA
case and two scaled-blit oracle cases remain. These are not 44 unmodified-CTS passes.

## Multisample MRT export offset ABI

The MRT EOT used the single-sample 2D staging word for packed imageblock
offsets. G16G multisample IMGWBLK consumes that offset in the fourth staging
word, as independently witnessed by the native single-target MS builtin.
Use that slot for every multisample target, preserving selectors and sample
addressing. This repairs nonzero tile offsets; the zero-offset integer target
was already correct. The host check retains the native PACK_IMGWIR bytes.

The original mixed RGBA8/RGBA16F/RGBA32U, 2x, depth/stencil resolve case now
Passes all attachment comparisons. Evidence:
`_provenance/experiments/20260914T080712096128Z-cts26-ms-offset`, peak 56.5 C.
**45/47 original failures resolved at this stage: 34 Mesa fixes and 11 CTS corrections.**
The earlier prose tally mistakenly counted the two failed scaled-blit results
as passes; the final case-by-case reconciliation caught and corrected that error.
The separate unmodified `dEQP-VK.info.device_mandatory_features` check also
Passes (56.25 C); format/limit skip review remains separate.

## Storage texel buffers and extended-usage views

Review of all 548 skips found 546 optional capabilities/configurations and two
cases incorrectly blocked by CTS checks. The review is specific to this frozen
sample and device profile; it is not a general allow-list for future skips.
`skip-review.json` retains each exact case group and rationale, and
`skip-audit-results.json.gz` preserves the classifications and original messages.
The registry inventory covers unconditional requirements; the separate original
CTS mandatory-features test also checks conditional requirements and passes.

The R8_SNORM storage texel-buffer test checks buffer capabilities correctly but
then checks irrelevant image tiling/array support. `cts-buffer-gate.patch`
returns after the buffer checks. With that gate corrected, the test exposed a
real Mesa refusal: R8_SNORM was missing from both direct descriptor lowering
and typed storage-write selection. Route it through the existing floating-input
native PBE write; the descriptor supplies signed-normalized conversion.
The corrected test now passes, including its output comparison, in 0.260 seconds.
Evidence: `_provenance/experiments/20260914T081842339785Z-cts26-r8snorm`,
peak 56.25 C. Installed Mesa SHA256:
`d24c249ba9ca62d731db73acd4393c91d67d5b9d6598568d4d5cd0b6476a155a`.

The ETC2/RGBA32_UINT compatible-view test already validates its complete
EXTENDED_USAGE image query, but later incorrectly requires STORAGE_IMAGE on
the compressed base format. `cts-extended-usage-gate.patch` checks the compatible
view format for that usage. This corrected test passes without another Mesa
change. Evidence: `_provenance/experiments/20260914T081352364101Z-cts26-extended-gate`,
peak 56.75 C. Both separate CTS build manifests retain source and binary hashes;
the original CTS source and binary were restored. Test workloads and output
comparisons were retained. These are corrected-CTS passes, not passes by the
unchanged release binary.

## Compressed and packed scaled blit references

The two final mismatches were additional reference conversion omissions.
For BC6H-to-SSCALED the separate compressed path compared decompressed fractional
source values directly against quantized destination integers. Exact diagnostics
showed values such as -0.940918 correctly becoming -1, but failing that oracle.
For R16F-to-packed-USCALED the reference's packed setPixel path truncated floats,
unlike the ordinary scaled reference path, which rounds to nearest even.

`cts-scaled-blit-complete.patch` includes the previous scaled classification fix,
converts compressed-source reference texels to the scaled destination format,
and explicitly clamps/rounds scaled reference values before packed storage.
Input image generation, blit commands and nearest-coordinate verification remain
unchanged. The quantized lookup comparisons use zero color tolerance; no broad
integer tolerance hides the 0-versus-1 mismatches. The filtering/conversion rules
are at https://docs.vulkan.org/spec/latest/chapters/copies.html#copies-blits.

Both cases now Pass with the separately identified corrected CTS binary:
`803061eda7c2a5c2e4710900249978606ba30adb806eababfb2e7811935e4c86`.
Evidence: `_provenance/experiments/20260914T082716896989Z-cts26-scaled-blit-complete`,
peak 56.5 C. Original CTS source and binary restored. Lavapipe was tried for
comparison but skipped both destination formats; it is not passing-reference
evidence. The exact diagnostic values and corrected test patch are retained.

## Completed sample ledger

`python3 reconcile.py` regenerates the case-by-case ledger and final counts from
the retained baseline, skip audit and ordered targeted test results. It does
not execute GPU tests. Original case indices are matched by name because each
targeted runner numbers its own subset from zero.

- 454 qualified passes: **439 with unmodified CTS, 15 with documented CTS fixes**.
- 546 reviewed optional skips; no unresolved failures or ShouldSupport cases.
- All 47 original failures resolved: 34 Mesa fixes and 13 CTS corrections.
- Both additional ShouldSupport cases pass after gate corrections; one also
  required the R8_SNORM Mesa fix described above.
- No crash or timeout in this sample or the targeted qualification runs.

These are reconciled results across the baseline and targeted retests, not a
new complete run on the final library and not a full conformance claim. The
baseline remains 21.720 seconds with 20 workers; no final full-sample regression
run was performed. Every hardware run retained the watchdog and thermal guards.
