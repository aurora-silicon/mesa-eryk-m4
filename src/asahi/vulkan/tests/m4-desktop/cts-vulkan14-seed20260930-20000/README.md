# Vulkan 1.4 sample, seed 20260930

Frozen broad Linux sample: 20,000 cases, 20 workers, 50 cases per group,
60-second per-case bound. SHA256:
`8402669bf8a2cbef4a5635106f622b0f18a5bfddc1e571665de81e20f8e3881c`.
The separately identified CTS binary retains the documented seed29 reference
corrections. The four completed broad segments took 558.673 seconds of runner
time. Original outcome: 28 Fail, 2 Crash, 1 Timeout, 9,124 Pass, 10,845 NotSupported.
Skip requirements are still being reviewed; no conformance claim.

Cumulative targeted fixes resolve 15 original failures/crashes. Both original
compiler crashes now pass. There are still 15 failing cases and one timeout.
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
