# M4 command-boundary GPU priorities, 2026-09-18

Implemented and deployed on J713 with the normal autologin desktop and both
outputs (eDP-1 2560x1664@60, Philips DP-1 1920x1080@60). This implements priority
selection **between hardware commands**, not interruption of an executing shader.

## Implementation

Kernel `58b6b0a9990b` corrects firmware-order priorities (0 realtime, 3 low) to
DRM scheduler priorities. The common frontend already reverses the UAPI values;
the M4 backend reversed their meaning again, letting low outrank medium.
The shared one-credit DRM scheduler now receives one job per native command,
with complete batch allocation before any publication, dependencies preserving
command order, and the final fence representing the whole userspace submission.
A render's TA and fragment stages remain paired for partial-render progress.
`e1a8a6ffbdb2` propagates the first batch failure to commands not yet started.
The existing one-credit/progress-aware-timeout working changes were preserved
and committed with the prerequisite scheduling implementation.

M4 high/realtime queue creation requires CAP_SYS_NICE in the kernel. Honeykrisp
now delegates permission enforcement to that backend; other hardware retains
its old admission boundary. Zink enables KHR_global_priority and provides the
per-application driconf `zink_queue_priority`: 0 low, 1 medium (default), 2 high,
3 realtime. Logical-device sharing and destruction account for priority.
This is a per-screen/application setting, not general EGL per-context priority
support. Hyprland's existing EGL request alone still does not select the queue.

The installed drirc at `/usr/local/share/drirc.d/99-m4-compositor-priority.conf`
selects high only for executable Hyprland. This Mesa build uses `/usr/local`
for drirc; `/usr/share/drirc.d` did not apply. The fixed-executable launcher
`/usr/local/libexec/m4-hyprland-priority`, root-owned with `cap_sys_nice=ep`,
transfers SYS_NICE as ambient only into the compositor, restoring the fixed
root-owned Mesa library path lost on file-capability entry. The session and
greeter launch through this wrapper. Hyprland reference commit `17aac38` drops
ambient SYS_NICE at main entry, also for HYPRLAND_NO_RT or CPU scheduling setup
failure. The compositor retained effective SYS_NICE; a command launched by it
had zero effective, permitted and ambient capabilities. No priority environment
override is inherited by applications. Installed hashes are retained here.

## Hardware results

Every experiment used the verified 60-second watchdog, six 0.5-second thermal
channels and 95 C cutoff. Peak temperature across the successful qualification
runs was 65 C. No GPU faults or display errors were observed.

* Same initial probe before/after: 32-command low-priority batches competing
  with 40 tiny medium-priority submissions. Foreground p95 latency improved
  from 52.137 ms to 4.612 ms; maximum from 68.453 ms to 7.053 ms. Median increased
  from 0.622 ms to 2.243 ms because more foreground work now overlapped the
  background instead of waiting until the background had finished.
* High versus normal (64-command batches): foreground p95 4.505 ms, maximum
  4.983 ms, despite background batch latency of 104–178 ms.
* Strengthened probe: each command writes its own output region, so skipped
  intermediate commands cannot be hidden by a later write. During cursor
  motion the background executed 128 batches of 32 commands (4,096 commands),
  all with checked outputs. Panel history recorded 355 consecutive intervals,
  median 16.664 ms, p95 16.683 ms, maximum 16.695 ms, zero missing sequence
  records and zero display errors. This qualifies this bounded load, not
  arbitrary shaders or an unconditional desktop frame-rate guarantee.
* Long single-command control: 500,000 dependent arithmetic iterations per
  invocation, five normal commands. High foreground maximum wait **41.002 ms**.
  This confirms the remaining lack of in-command preemption.
* Unprivileged user e: low/medium creation and execution pass; high/realtime
  return VK_ERROR_NOT_PERMITTED. Privileged high executes successfully.
* 438 atomic/shared-ordering CTS cases: **326 Pass, 112 NotSupported**, no
  failure/crash/timeout; runner 67.974 seconds with two workers.
* All 402 frozen catalog cases containing `global_priority`: **114 Pass,
  288 NotSupported**, no failure/crash/timeout; runner 6.651 seconds.
* The four original failed random-sample cases all pass again.

Frozen catalog indices, hashes, summaries and capability proof are retained
here. Full QPA/logs/source patches are in the workspace provenance directories:

- `20260918T084528565800Z-priority-before`
- `20260918T085057959414Z-priority-after-medium`
- `20260918T085116296388Z-priority-after-high`
- `20260918T085257720152Z-priority-atomic-regression`
- `20260918T085433922198Z-priority-permissions`
- `20260918T085805852754Z-priority-api-transition-v2`
- `20260918T085928617252Z-priority-desktop-caps`
- `20260918T090024749119Z-priority-desktop-motion`
- `20260918T090114564696Z-priority-long-command`
- `20260918T090149002234Z-priority-original-four`

Earlier harness attempts remain preserved: priority CTS initially lacked the
metadata provenance field; the first child-capability check used obsolete
Hyprland dispatch syntax. Neither was a driver/test-case failure. The first
desktop activation used the wrong drirc prefix; actual high-priority creation
was verified only after correcting it, by the successful Zink queue log.

## TODO / limits

* Native firmware queue isolation, priority-driven context preemption/resume,
  and bounded interruption latency for a single long render/compute command.
  Merely increasing the old firmware FIFO depth is not safe: lost followers
  and cross-context failures are documented in g16_runtime.rs.
* General Zink EGL context-priority support; current deployment is explicit
  per-executable drirc policy.
* Allocation-failure injection and cancellation stress for staged job batches.
  First-error propagation is implemented; targeted fault injection is not
  claimed by the successful workload/CTS runs.
* Broader real-game and long-shader desktop latency qualification. No macOS-like
  worst-case latency or complete preemption claim follows from these results.
