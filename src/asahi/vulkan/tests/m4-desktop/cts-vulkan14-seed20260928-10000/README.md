# Vulkan 1.4 random 10,000, seed 20260928

Broad frozen sample from CTS 1.4.6.2, four cases overlap seed 20260927.
Uses the separately identified seed27 precision-corrected CTS binary,
SHA256 `25e691e613ff9f3a386fd282dd48f0658b86126f56e2ff16c2e95378825d12fc`.
Original CTS changes and provenance are retained in the preceding sample.
This campaign is still in progress; no complete 10k result is claimed.

First segment: 220 Pass, 170 NotSupported, 5 Fail, 33 Interrupted,
9572 Unrun. A GPU fault ended the run; 63.461 seconds includes recovery,
not a complete 10k benchmark. Evidence: `20260914T124651368249Z-cts14-seed28-10k`.
One worker's device-loss case passed alone and with its preceding sequence.
Single-worker isolation then reproduced the fault on an advanced graphics
render timestamp test (`20260914T125056696558Z-cts28-interrupted-isolate`).

The kernel rejected render query destinations despite already emitting native
firmware timestamp operations. Resolve and retain validated timestamp mappings,
then connect their pointer pairs to those existing operations. Internal
retirement timestamps remain separate. The original case passes without
synchronization diagnostics: `20260914T125945901211Z-cts28-render-timestamp-normal`.
Earlier diagnostic run: `20260914T125910939877Z-cts28-render-timestamp`.

The maint9 signed 64-bit bitfield extraction case used a folded 16-bit EXTR
source. Native EXTR reads full words; explicitly zero-extend data operands
before RA. Its captured shader now compiles offline and the original numerical
CTS comparison passes: `20260914T125934545830Z-cts28-extr`.
Library SHA256 `545e45870fbf4e117b77693e24f63a363f163b6bb7c568b485098f25a96cfa58`;
kernel module SHA256 `b415d4f8660f5d74130fd74a9d06a7345e49c807f4161830f79bfdf7aaa49f12`.
Both guarded targeted comparisons used a 60-second watchdog and thermal monitor;
maximum temperature 57.75 C including the diagnostic run.

## Geometry output crash

The next segment completed 928 Pass, 1084 NotSupported and 13 Fail before a
SIGABRT in `poly_nir_lower_gs`. Its 100 Missing CSV entries are canceled groups
without a CTS start marker, now reported as Unrun by the collector; raw results
remain retained. 41 other cases were interrupted. Peak temperature 61 C.
Evidence: `20260914T130056156246Z-cts28-cont1`.

The core points to inserting a 16-bit GS output into a 32-bit temporary vector.
Preserve the raw bits in word temporaries and restore the original width per
component on rasterizer export. The original interface-matching comparison
passes on J713: `20260914T130412322900Z-cts28-gs-half`, peak 57 C.
