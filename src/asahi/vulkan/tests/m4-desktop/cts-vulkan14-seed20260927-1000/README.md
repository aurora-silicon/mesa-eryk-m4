# Vulkan 1.4 random sample, seed 20260927

Frozen 1,000-case broad Linux sample, no overlap with seed 20260926.
Original CTS 1.4.6.2 baseline: 481 Pass, 21 Fail, 498 NotSupported;
no crashes/timeouts. Runner 25.573 seconds, 20 workers, 50 cases per group.
Guard/evidence wall time 28.279 seconds, staging 1.566 seconds, peak 60.25 C.
Evidence: `_provenance/experiments/20260914T082941211730Z-cts14-seed27-baseline`.

## Completed result

All 21 original failures and both reviewed ShouldSupport cases now have
passing targeted comparisons. Eight original failures needed Mesa fixes;
thirteen needed separately identified CTS corrections. Of the two skips,
one needed a Mesa buffer-format feature bit and one a CTS compatible-view gate.

The reconciled ledger contains **504 Pass, 496 optional NotSupported, zero
remaining Fail/Crash/Timeout/ShouldSupport**. This combines the baseline and
exact targeted reruns; it is not a second full regression run. Fourteen passes
use corrected CTS checks and are explicitly tagged `CorrectedCTSPass`.
One driver fix was checked with a diagnostic CTS binary whose changes do not
affect that case. Neither is silently presented as an unmodified CTS pass.

`reconciled-results.json.gz` retains every baseline status, skip rationale and
successful rerun evidence. `skip-audit-summary.json` pins the CTS vk.xml and
profile hashes; all 498 skips were reviewed, with no unreviewed remainder.
The unconditional core-feature inventory finds no gap; it is not a complete
Vulkan conformance audit. Optional skips include non-Linux WSI extensions.

Final Mesa SHA256: `7c832608f1b37ccb44a4fb7d6bb2a433b879d5ac6bd5b4ae27e7becb37bf141b`.
The original layered-MSAA test passes with temporary diagnostics removed
(`20260914T093104504932Z-cts27-final-clear`, peak 56.75 C).
SDDM is active/enabled, default and current boot are graphical, and the
60-second watchdog broker remains idle under the SSH heartbeat. No CTS
workload remains running. One diagnostic iteration faulted during robust
indirect bring-up; its packet cause was fixed and both original cases pass.

The following sections retain the intermediate findings and evidence.

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

## Strict tile capacity

Remove the native override that admitted an exactly 32 KiB tile. Preserve the
shared allocator's strict less-than-32-KiB bound, separately from the native
sample-stride and raster sample limits. RGBA32 at 2x now chooses 32x16 tiles.
The original layered UINT case and its float counterpart both pass, along with
their single-sample controls: `20260914T092725045112Z-cts27-strict-tile`,
peak 56.75 C. Host boundary tests cover initial and deferred sample counts.

Diagnostics had localized the first mismatch to layer 4, pixel (160,0).
The Apple type8 EOT binary matched ours byte-for-byte, and earlier pixels
contained all four channels; this was not an export-format conversion defect.
The temporary post-completion diagnostic patch is retained for reproducibility
and is absent from the final driver.
Running total: 19/21 original failures resolved (8 Mesa, 11 corrected CTS).

## Scaled linear-blit reference precision

Both remaining blits pass with `cts-precision-corrections.patch`, applied to
pinned CTS f6a29701220f34dd1407513bfe80d74ca7b392ce. The independently built
binary SHA256 is `25e691e613ff9f3a386fd282dd48f0658b86126f56e2ff16c2e95378825d12fc`.
Original CTS failures and exact-value diagnostics are retained. This patch
includes the earlier scaled type/clear/reference and capability-gate fixes;
it is not a Mesa rendering workaround and is not an upstream CTS release.

The existing `calculateFloatConversionError` computes 1/(2^bits-1), a
normalized channel quantum. Scaled values occupy their integer value range,
so convert that source threshold back to value units. RGB9E5 additionally
needs its representable output quantum, 2^(shared_exponent-24), per pixel;
a fixed 0.05 tolerance is smaller than one representable step at these values.
The original reference at (28,0) is (512,920,102), versus (510,920,102),
one shared-exponent quantum. RGB16_USCALED -> RGBA32F differs by roughly
0.004–0.016 near values in the tens of thousands, exceeding the old 0.001
absolute threshold while remaining far below a scaled source quantum.

The comparison still checks every pixel/channel and retains the existing
clamped/unclamped alternatives. It changes no GPU commands or image values.
The diagnostic printouts are under
`20260914T092213241533Z-cts27-raw-and-precision`; both corrected comparisons
pass in `20260914T092910413164Z-cts27-precision`, peak 56.75 C.

This agrees with Vulkan's [blit precision rules](https://docs.vulkan.org/spec/latest/chapters/copies.html)
and [floating-point format conversion rules](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html),
which allow implementation-dependent rounding. The retained patch is a local
reference correction, not Khronos certification evidence.
