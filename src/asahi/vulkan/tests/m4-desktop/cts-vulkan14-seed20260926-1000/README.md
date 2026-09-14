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
