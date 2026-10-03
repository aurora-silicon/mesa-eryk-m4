# Native compute timestamp boundaries, 2026-10-03

A TOP/BOTTOM query pair around a compute dispatch reported approximately
2.75 microseconds for work whose fenced completion took 1.5–2 milliseconds.
The first query used the end timestamp of an open native CDM stream; the
subsequent dispatch was appended to that stream. Both queries therefore
measured after the dispatch. Vulkan permits a timestamp at a logically later
stage, so this observation is a profiling limitation, not by itself evidence
of a specification violation. The existing Minecraft completed-frame scores
do not use these queries and remain valid.

Close a native compute stream after placing its timestamp, preserving the
requested boundary against subsequent dispatches. Adjacent timestamp fanout
and the opt-in M4 start/tail replacements reuse closed streams, respecting
the existing reset, dependency, feature and capacity checks. Graphics
render-pass timestamp batching is unchanged.

On M3, all 262 frozen monolithic timestamp cases completed: 236 Pass,
26 NotSupported, no failure or crash. The case-list SHA-256 is
`86297dfce05731372fb82e962ef0a3add496ab33ba4a5583505d7875cc1d3a97`;
the records retain catalog hashes and original global indices. A validated
128 MiB copy now measures 1.964 ms GPU time; a validated FP32 workload measures
1.530 ms. Full Minecraft first/middle/last checks are pixel-identical, and a
23-second real Vulkan gameplay check completed successfully at 3024×1837.

On M4, both small compute workloads passed, including an additional copy
run with the existing start/tail/fanout options enabled. The larger CTS run
stopped at `two_cmd_buffers_primary_with_availability_bit`: the live kernel
returned ENOSPC during public preparation before decoding any commands
(`phase=decode commands=0 bytes=480 error=-28`). Its preserved record contains
220 Pass, 24 NotSupported, one Crash, three Interrupted and 14 Unrun. This is
not a full M4 CTS qualification. The kernel was subsequently replaced and
rebooted outside this experiment; a control must pin that kernel identity
before attributing the preparation failure to this change.

Private tested library SHA-256:
`8ad5f713b61325e7f438d0ac540d69c129ccf19f1bcb3b08e17970b1cfb2e97e`.
Evidence is retained outside Git in
`gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261003/throughput/`
and the M4 `_provenance/experiments/*timestamp-boundary*` directories.
Linux experiments used a bounded workload, the 60-second hardware watchdog
and all-channel thermal monitoring. Desktop libraries were not replaced.
