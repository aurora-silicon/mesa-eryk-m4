# Vulkan 1.4 sample, seed 20260930

Frozen broad Linux sample: 20,000 cases, 20 workers, 50 cases per group,
60-second per-case bound. SHA256:
`8402669bf8a2cbef4a5635106f622b0f18a5bfddc1e571665de81e20f8e3881c`.
The separately identified CTS binary retains the documented seed29 reference
corrections. The four completed broad segments took 558.673 seconds of runner
time. Original outcome: 28 Fail, 2 Crash, 1 Timeout, 9,124 Pass, 10,845 NotSupported.
Skip requirements are still being reviewed; no conformance claim.

Cumulative targeted fixes resolve all 31 original failures/crashes. Both original
compiler crashes now pass. No original Fail, Crash or Timeout remains open. There are 9,155 cumulative
Pass and 10,845 NotSupported; the skip audit is still incomplete.
The original 60-second timeout is retained in the initial report. Its fix is
validated under contention below; an isolated pass alone was not accepted.

The first fixes covered narrow absolute value, vertex array texel fetch, and
spilled vertex exports. The next fixes cover quad ballots, rematerialization
register reservations, sRGB storage writes, empty draws after geometry lowering,
RGB565 image writes, multisample depth copies, modified RGB10A2 packing,
half clustered minimum, image-load destination register limits, byte-to-float
conversion, narrow bit reversal and register-addressed returned shared atomics.

The latter atomic has an independent G16G offline compiler witness from the
included Metal source. Its instruction at main+0x1c is
`670354000200000012000024`, with address register half 4, result/value half 0,
address last-use, and scoreboard slot 1. The exact-encoding host check and the
original Vulkan memory-model case pass. Native source/artifacts and compiler
versions remain under build/cts-vulkan14-seed20260930-20000/native-local-returned-atomic-byte.

Most recent targeted run: 12 original cases, nine Pass and three Fail,
4.228 seconds of runner time, peak 59.75 C. The two sRGB eight-attachment
resolve cases advance to shader compilation failures; the sample-location
case advances to an image mismatch. Those are still open.

A separate compute-allocation optimization is not accepted: cold 20-worker
baseline passed in 43.68–45.59 seconds per case. Reuse exposed crashes or
wrong image values. The working kernel was restored; diagnostic patches and
all failed runs remain in the local investigation directory. No timing-only
result is counted as a correctness fix. No broad regression was rerun.

The two eight-attachment sRGB resolve cases now pass (0.609 seconds, peak
56.75 C). Spilled tilebuffer outputs need the same explicit sRGB conversion
as tile-resident outputs, and their native typed stores use linear UNORM
descriptors. Both RGBA and BGRA attachment orders were exercised.

Half-precision geometry cosine now passes after f32 angle reduction and
polynomial evaluation, with rounding to f16 only at the result. The six-case
texture/trigonometry check took 0.810 seconds (one Pass, five still Fail,
peak 59.5 C). The five texture failures remain open.

All three implicit projected-offset fragment cases now pass, including
shadow and integer sampling (0.609 seconds, peak 56.75 C, cache disabled).
Register offsets use indexed native descriptors and a zero hardware bias.
Select that bias after NIR folding, which removes a semantic txb bias of zero.
The compiler replay and each original CTS case pass.

Both projected 3D vertex offset cases and the shadow-array gather offset
case now pass (1.010 seconds, peak 56.75 C, cache disabled). Preserve runtime
3D offsets for the native operand bank rather than adding them before
projection. Lower expanded shadow gather offsets after the early gather pass.

Two scaled-image compute failures were invalid CTS shaders, independently
reported by validation as VUID-vkCmdDispatch-format-07753: unsigned integer
image types were paired with USCALED views, which require floating-point types.
The separately identified CTS correction extends the prior graphics correction
to compute sampling/texel-fetch and requires formatless storage writes. Both
cases now pass (0.609 seconds, peak 57 C). This is a CTS correction, not a driver
fix or an unmodified conformance result. The cumulative source patch and build
manifest are included; the original binary and failed validation run are retained.
Corrected binary SHA256: f445539c9b216b86820ebe0135e67f3938787197d9c77044849c4674ea9a8296.
Evidence: 20260914T204741923371Z-cts30-scaled-compute-fixed1.

The narrow geometry shuffle case also passes with the existing widened-lane
fixes and the shader cache disabled (1.134 seconds). No speculative shuffle
encoding change was installed or counted. The high-volume f16 TCS output test
now passes after using a word per component for both stores and loads, matching
the four-byte component addresses and the existing geometry interface layout.
The two-case tessellation/primitive-ID run took 1.211 seconds, peak 56.75 C;
primitive-ID verification still fails and remains open.
Evidence: 20260914T204918017170Z-cts30-remaining-four-capture and
20260914T205230239205Z-cts30-tess-primitive2.

The maintenance6 null indexed indirect draw now passes (0.607 seconds, peak
56.75 C). Native validation must follow index robustness lowering, whose input
may have an empty span. For fully out-of-bounds index streams, the GPU allocates
and clears byte indices using the actual indirect count and the existing heap;
there is no fixed draw-count cap or CPU readback of indirect arguments. This
also replaces the separate CPU allocation for direct null-index draws.
Evidence: 20260914T205745556036Z-cts30-null-index2.

Kernel commit 6254d3bf99da removes redundant clearing of fresh zero-filled GEM
shmem backing and leaves CPU mappings lazy. Every allocation and firmware owner
is still fresh. Twenty concurrent cold-cache repetitions all pass in
35.06–36.67 seconds each, compared with the prior 43.68–45.59 second baseline.
Peak 59 C. The optimized module SHA256 is
96097493b0ccfd52939e5c9f2f8c4ebcc32ff779ee832f7ef9a77232d73d7a5b.
A targeted contention check then used the original timed-out worker's 50 cases
plus the 19 slowest other passing cases from the initial run: 47 Pass and 22
NotSupported, no errors/timeouts, 12.468 seconds with 20 workers and cache off.
The original timed-out case passed in 11.604 seconds. This is a focused workload
check across fresh processes, not a replay of the original scheduling order or
a full 20k regression. Both runs retain the 60-second limit; no bound was raised.
Evidence: 20260914T210137647967Z-cts30-timeout-cold20-no-doublezero and
20260914T210412754200Z-cts30-timeout-context69.

The sample-location verification case now passes with a complete primitive-ID
path: VDM producer flag 6 and its extra output slot, plus rasterizer Cull-2 bit
12. Producer state alone left all IDs zero. Removing both producer fields while
retaining the consumer caused a GPU timeout; watchdog recovery succeeded and
that failed diagnostic is retained, not counted as success. Restoring the full
configuration passes again (0.809 seconds, peak 59.25 C) after recovery.
M4 evidence: 20260914T210607187586Z-cts30-primitive4 (Pass),
20260914T210709586586Z-cts30-primitive5 (diagnostic GPU timeout),
20260914T210912579294Z-cts30-primitive-restored (Pass).
This evidence is for J713/G16G, not a qualification claim for other GPUs.

Current skip audit: 9,161 reviewed optional/environmental skips and 1,684
NotSupportedNeedsReview, with no unconditional core-feature gap identified.
Do not report the unaudited skips as optional or claim full Vulkan conformance.
The current rules, pending cases and cumulative ledger/audit summaries are
retained alongside this report. Default graphical login is restored; watchdog
broker remains armed and idle, and normal CPU frequency limits are restored.
