# Vulkan 1.4 sample, seed 20260930

Frozen broad Linux sample: 20,000 cases, 20 workers, 50 cases per group,
60-second per-case bound. SHA256:
`8402669bf8a2cbef4a5635106f622b0f18a5bfddc1e571665de81e20f8e3881c`.
The separately identified CTS binary retains the documented seed29 reference
corrections. The four completed broad segments took 558.673 seconds of runner
time. Original outcome: 28 Fail, 2 Crash, 1 Timeout, 9,124 Pass, 10,845 NotSupported.
Skip requirements are still being reviewed; no conformance claim.

Cumulative targeted fixes resolve 26 original failures/crashes. Both original
compiler crashes now pass. There are still 4 failing cases and one timeout.
The timeout remains unresolved despite an isolated pass: hitting its 60-second
bound under contention counts as failure.

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
