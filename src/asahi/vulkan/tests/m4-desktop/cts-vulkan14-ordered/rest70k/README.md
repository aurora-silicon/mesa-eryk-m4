# Remaining deterministic batch 1 cases: [30000,100000)

In-progress checkpoint: 40,000 of the requested 70,000 cases completed.
17,093 Pass, 22,875 NotSupported, 32 Fail; no Crash or Timeout.
The remaining 30,000 are still running. This checkpoint is not a complete run.

The exact frozen catalog and global indices are preserved in selection.json,
per-case eligibility metadata and compressed results. Twenty deqp-runner workers,
50 cases per group, first-attempt execution, hard 60-second case timeout.
Every segment uses the existing 60-second watchdog and 0.5-second thermal guard.
No driver change or failure retry during this run. The earlier interrupted
[30000,40000) attempt is retained separately and is not counted here.

The CTS binary includes the previously documented reference corrections;
this is not an unmodified conformance run. Raw NotSupported does not establish
optionality. Skip auditing and the final results will follow completion.

## Why the CPU is mostly idle

A read-only process snapshot during segment [60000,70000) found 19/20 CTS
workers blocked in drm_syncobj_array_wait_timeout. The G16 backend uses a
one-credit DRM scheduler and permits one active hardware job. Twenty userspace
workers therefore do not imply twenty simultaneous GPU jobs.

A retained 16,000-line kernel-log sample covering 4,000 retirement records and
15.4695 seconds recorded 9.4553s last-command preparation, 4.8666s active interval
and 1.0793s cleanup. Active time includes publication, firmware and completion
observation; it is not pure shader execution time. These fields describe the
last command of each packet, not a general profiler for multicommand packets.
The raw sample is retained under build/cts-vulkan14-ordered/0030000-0100000/.

Compute::in_vm still creates a fresh 32 MiB private pool per dispatch; the recent
kernel optimization removed its unused firmware alias, but did not add reuse.
Render allocations already have a per-queue cache. Roughly 1,000 kernel log
lines/second also explain journald activity; this observation does not quantify
logging's causal share of elapsed time. Retirement telemetry currently supplies
the watchdog progress gate, so suppressing it requires preserving that signal.

Future optimization targets: safely reuse compute resources after retirement,
reduce routine diagnostic formatting while retaining independent progress,
and implement properly owned concurrent in-flight jobs. No such changes were
made in the middle of this run.
