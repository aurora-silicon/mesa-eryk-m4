# M4 timer-query probes

`m4_vulkan_timestamp_query.c` executes a reset and two timestamps without an
application shader. It requires nonzero, ordered hardware times and availability
on the first host read after the submission fence. Build with the ARM64 sysroot
and `-ldl`; run through the workspace watchdog/thermal experiment wrapper and
`/root/run-m4-gl.py`. `M4_TEST_RECORD_ONLY=1` skips execution explicitly.
`M4_TEST_SECOND_READ=1` logs a diagnostic 100ms reread without changing the first
read verdict.

`m4_gl_timer_query.c` uses surfaceless EGL/OpenGL 3.3, eight pairs of green draws,
timestamp counters and elapsed queries. Build with `-lEGL -lGL`. It checks all
131072 pixels and timestamp order; interval accuracy is not established merely
by retrieving an elapsed query. Host llvmpipe passed; M4 is not yet qualified.

2026-09-11 evidence in workspace `_provenance/experiments/`:

- 125803 timestamp availability control: first query unavailable even after
  100ms; second query valid. Both firmware jobs retired with nonzero timestamps.
- 130545 destination diagnostic: both available in this instrumented run.
- 130620 warm destination diagnostic: FAIL. Kernel mapping first16 bytes were
  zero before publishing query0; its valid timestamp was then replaced by the
  GPU reset sentinel (all FF) before query1 publication. This is evidence of
  completion/visibility ordering failure, not a missing timestamp destination.
  Two thermal samples, peak49.5C, no GPU fault, all evidence retained.

Diagnostic logging can change timing. Neither the single pass nor a delayed
reread qualifies the native query implementation. Mesa changes stay local.

The standalone probe now requests Vulkan1.2. `M4_TEST_HOST_RESET=1` enables
hostQueryReset and uses vkResetQueryPool before submission, isolating timestamp
production from GPU reset writes. The default still uses vkCmdResetQueryPool.
133318 host-reset control passed both GPU timestamps and first-read availability
(49C); 133215 GPU-reset control on the same system-barrier kernel failed(49.75C).
The stronger barrier diagnostic was rejected. The host-reset pass does not
qualify GPU reset or GL query-result copies, and is not a driver workaround.

## Local implementation checkpoint (not deployed for game qualification)

Native NIR helpers now exist for timestamp-only engine dispatch, query reset,
post-graphics timestamp copy and single-report query-result copy. They replace
unavailable precompiled libagx helpers; copies execute on the GPU. The standalone
host-reset control qualifies production in its tested kernel/Mesa combination.
The GPU-reset path still fails and GL elapsed/result-copy paths remain
unqualified. Do not claim general query support from this checkpoint.

The game performance run uses the previously qualified Vulkan library SHA
ad40ed13a5ff3adbdf1d09011aea1f07a2dbfe68e73621bfa7cf99c90d5c35bd.
The unqualified render-timestamp kernel patch is separately retained at workspace
build/0ad/native-timestamp-delivery-wip.patch; current kernel source returned to
the qualified implementation plus host phase diagnostics. No fixed sleeps or
CPU readback substitution were accepted as a synchronization fix.

## Occlusion counts (separate from timestamp ordering)

`m4_gl_occlusion_query.c` tests eight reuses of four queries:16384 full-screen
samples,77 scissored samples,zero samples, and a positive ANY_SAMPLES_PASSED
query. It also verifies131072 green pixels. Host llvmpipe passed; guarded M4 run
143947 passed all32 exact query results and pixels, peak50.25C. The tested Mesa
includes the native reset/availability/result-copy helpers. This is a bounded
basic occlusion qualification, not proof of arbitrary query ordering or timers.

Build with `-lEGL -lGL`; execute hardware only inside the existing J713 guard.

## Reset-only isolation

`M4_TEST_RESET_ONLY=1` records the GPU reset without either timestamp write.
It requires VK_NOT_READY and zero availability for both queries on the first
read after the submission fence. This distinguishes reset visibility from
publication of timestamp results. It does not qualify timestamp support.
Guarded experiment20260912T104734436215Z passed this isolation with the same
reset/read sequence, peak51.5C. Ordinary reset-plus-timestamp still failed on
that kernel; uncached query allocation did not fix it (104846,peak51.75C).

## Native timestamp-reset ownership boundary

A timestamp reset writes query storage on the GPU; the timestamp operation
publishes through firmware. End the reset CDM and preserve that boundary
through command-stream merging so the reset retires before firmware writes
its results. This applies to timestamp resets only; ordinary CDM merging
remains enabled. No sleeps, host clock values or global nomerge setting are
part of the implementation.

Guarded111731 passed with an explicit unmerged boundary. Targeted boundary
112027 passed with ordinary merging enabled (peak53.5C). These runs used
firmware user destinations and an experimental post-finalize publication;
the final kernel removes that unsuccessful sequence experiment. Final kernel
and original vkQuake capture validation are recorded separately below.
