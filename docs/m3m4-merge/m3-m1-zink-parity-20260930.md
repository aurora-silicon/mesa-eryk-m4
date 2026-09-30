# Diagnosing remaining Minecraft Zink parity with M1

The previous deferred-UBO work is committed and pushed as `f182d8d9401`.
Its M3 replay median 51.83 FPS and refreshed M1 51.52 FPS establish parity,
not a hardware performance ratio or a live-game score.

## Newly reproduced pending-push-descriptor bug

Recording-time selection checks UBO descriptors before
`hk_cmd_buffer_flush_push_descriptors` fills the cached `set_sizes` array.
`hk_prefetch_buffer_address` already reads the CPU push storage, but uses
that not-yet-valid cached size. First draws can therefore reject an available
push descriptor and select the general fragment executable.

A bounded opt-in descriptor-rejection log identifies Minecraft's fragment
`4088ae4d` rejecting set 0, offset 64. The selected shader has 7156 instructions,
192 register slots, 48 bytes of scratch and three loops. It appears 30 times
inside the 30-frame interval. This is selection frequency, not pixel coverage
or attribution of total frame time to this shader.

Use the pending push storage's actual capacity when no regular set is bound,
consistent with the sampler predicate. Preserve the regular-set/root paths,
bounds checking, alias qualification and GPU-time content reads. Tests cover
initial zero size, stale previous size, last valid offset, one-past/wrapping
offsets, no storage and direct-bit removal. The sparse-access host suite passes.

Candidate `/opt/asahi-m3-push-prefetch-9e6c63c25cfc` (SHA256
`9e6c63c25cfc6ae302df142732bbbfbb443c32c12200644a437116553f95f38e`)
passes all three Minecraft GL images exactly against the prior optimized build.
Evidence: benchmark run `20260930T150705862467Z-m3-minecraft-demo-zink-check`.
Paired timings and broader checks are pending. The desktop is unchanged.

## Other measured leads and exclusions

A diagnostic replayer records per-call CPU elapsed times and engine counters
only across the completed/predecoded measured interval. Most main replay-thread
time is in swap/fence waits. This does not rule out CPU work on Zink's worker
threads. The M3 counter deltas include about 80 ms tiling, 158 ms fragment and
115 ms compute spans over 577 ms wall time. Global counters include other
clients and overlapping engines; their sum is not utilization or a complete
partition of frame time.

Although the wrapper requests `EGL_PLATFORM=surfaceless`, this binary explicitly
selects EGL's X11 platform and creates a hidden window in Xvfb. A separate
completion-before-presentation diagnostic leaves M3 at 51.92 FPS and M1 at
50.87 FPS for this one instrumented run. Thus presentation is not established
as the cause of parity. These are diagnostic timings, not replacement benchmark
scores. Existing statements that the binary actually renders through surfaceless
EGL must be corrected.

The measured interval records 40860 small compute dispatches with a 32-thread
group and 5450-byte programs, matching the indirect-index robustness path.
The ordinary GL trace has 90 multi-draw-indirect calls in this interval. This
is another optimization lead; no robustness requirement was disabled and no
per-shader GPU cost has yet been established.

Evidence is under workspace `build/m3-m1-parity/` and benchmark diagnostics
`m3-zink-bottleneck-20260930/parity-*-host.log`. The replayer source patch and
binary identity are retained; all hardware runs use existing watchdog/thermal
guards. M4 is untouched.
