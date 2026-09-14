# Vulkan 1.4 random sample, seed 20260926

Frozen broad Linux sample: 1,000 cases, zero overlap with seed 20260925.
Original CTS 1.4.6.2: 405 Pass, 47 Fail, 548 NotSupported, no crashes/timeouts.
Runner: 21.720 seconds; guarded command/evidence transfer: 24.204 seconds;
staging: 1.490 seconds. Peak temperature: 59.75 C.

Baseline evidence: `_provenance/experiments/20260914T074325312596Z-cts14-seed26-baseline`.
The original outcomes are frozen; skips still require a support audit.

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

Nine additional scaled blits Pass with the previously retained CTS
`scaled-blit.patch` correction (separate binary, original CTS unchanged),
peak 56.75 C. Evidence:
`_provenance/experiments/20260914T080012963601Z-cts26-scaled-blit-oracle`.
Thus 42/47 are resolved: 33 Mesa fixes and nine identified CTS corrections.
Five remain; skip audit is still pending.

## CTS scaled clear and sampler declarations

Four remaining scaled cases Pass with `cts-scaled-types.patch`, using the
separately built and hashed CTS binary. Correct integer sampler declarations
for floating sampled types, and convert the clear union to float32 for scaled
formats at the API boundary. Preserve the original integer reference values,
input generation and comparison thresholds. Includes the already qualified
scaled blit/image sampler and signed packed-value reference corrections.

Hardware evidence: `_provenance/experiments/20260914T080404534872Z-cts26-scaled-types`.
Peak 56.75 C; original CTS source and binary restored after building.
Resolved: **46/47** (33 Mesa fixes, 13 CTS corrections). The mixed MRT MSAA
case remains a real driver mismatch. These are not 46 unmodified-CTS passes.

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
**All 47 original failures resolved: 34 Mesa fixes and 13 CTS corrections.**
The separate unmodified `dEQP-VK.info.device_mandatory_features` check also
Passes (56.25 C); format/limit skip review remains separate.
