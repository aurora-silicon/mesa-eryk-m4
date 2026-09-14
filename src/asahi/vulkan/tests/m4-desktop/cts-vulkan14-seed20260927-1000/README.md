# Vulkan 1.4 random sample, seed 20260927

Frozen 1,000-case broad Linux sample, no overlap with seed 20260926.
Original CTS 1.4.6.2 baseline: 481 Pass, 21 Fail, 498 NotSupported;
no crashes/timeouts. Runner 25.573 seconds, 20 workers, 50 cases per group.
Guard/evidence wall time 28.279 seconds, staging 1.566 seconds, peak 60.25 C.
Evidence: `_provenance/experiments/20260914T082941211730Z-cts14-seed27-baseline`.

## Initial fixes

Eleven of thirteen scaled-format cases pass with the retained, separately
identified CTS corrections from the previous campaign. Two linear blits remain
numerical mismatches. Original CTS outcomes are preserved, not overwritten.

R8_SINT storage images now use direct native PBE descriptor lowering and the
existing integer input path. The original 1D-array single-layer linear CTS
store passes. A dual-source blend can optimize every tile load away while
retaining separate class-8 and class-4 waits; open the colour entry bracket
before its first surviving colour wait, preserving both waits and the release.
The saved epilog compiles offline and its original CTS comparison passes.

Evidence: `_provenance/experiments/20260914T083644892182Z-cts27-small`,
peak 56.75 C, no fault. Mesa library SHA256:
`31efc01dc8ab45973965a1e4849d2e6842c765da98f0228e01cb464073325b20`.
Running total: 13/21 original failures resolved (2 Mesa, 11 corrected CTS).
Skip audit and the eight remaining failures are still in progress.

## Runtime texture offsets

Both original CTS push-constant offset cases pass on J713 (1D projected/bias
and 3D explicit LOD). Lower compact XYZ nibbles to the native auxiliary word,
insert the converted LOD/bias with PACK_SMPIR, and select SMP register offsets.
Native AIR-NT 32023.883 G16G compiler witnesses are retained beside this report;
they establish encoding, while the two numeric CTS comparisons establish GPU
execution. Evidence: `20260914T084930307418Z-cts27-offset`, peak 56.25 C.
Running total: 15/21 original failures resolved (4 Mesa, 11 corrected CTS).

## Native sample capacity

Six RGBA32F attachments need a 96-byte sample stride. Carry the native sample
capacity separately from the legacy 64-byte limit through initial allocation,
deferred MSAA and fragment epilog keys. The allocator chooses a 16x16 tile
within the existing 32 KiB budget rather than spilling attachments 4 and 5.
The exact shader-object rendering comparison passes (diagnostic CTS binary,
whose changes do not affect this test), evidence
`20260914T085507119299Z-cts27-wide-and-linear2`, peak 56.5 C. The host allocation
test checks offsets, deferred state and preservation of the legacy limit.
Running total: 16/21 original failures resolved (5 Mesa, 11 corrected CTS).

## Robust indexed indirect commands

Publish the native PPP selector and restart state before the GPU-generated
Index List, use the native dword-minus-one index span, and represent valid
zero-instance commands as empty index lists. Previously the packet builder
rejected these commands and left unwritten stream bytes, causing a GPU fault.
Both original indirect instancing cases (divisor-20 triangle fan and multiview
triangle list) now pass, evidence `20260914T091155631221Z-cts27-zero-instance`,
peak 56.5 C. The earlier fault and compute-only diagnostic are retained under
`20260914T090017418489Z-cts27-robust` and
`20260914T090919945727Z-cts27-robust-packet-diagnostic`.
Running total: 18/21 original failures resolved (7 Mesa, 11 corrected CTS).

## ShouldSupport

The compatible BC1/RG32_UINT input-attachment case passes after correcting
the CTS extended-usage feature gate (`20260914T085009788148Z-cts27-layout`).
Formatless storage texel-buffer reads were implemented but their format
feature bit was missing. Advertising it makes the sampled rgba32i null
descriptor case pass, together with its non-null len_16 counterpart; evidence
`20260914T091708835574Z-cts27-formatless`, peak 56.75 C.
