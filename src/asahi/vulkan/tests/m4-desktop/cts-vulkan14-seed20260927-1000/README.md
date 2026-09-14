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
