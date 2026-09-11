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
