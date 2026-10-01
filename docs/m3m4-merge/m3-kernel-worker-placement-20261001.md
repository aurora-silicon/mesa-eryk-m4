# M3 kernel submission worker placement

A bounded system-wide perf capture, filtered to the completed 30-frame
Minecraft interval, identifies the DRM scheduler's native GPU worker on CPU 5
(capacity 576). All 35 of its non-idle samples are on that efficiency core;
the recording/submission workers are on performance cores. Stacks include
`m3_runtime::execute_inner`, DRM scheduler callbacks and short polling delays.
System-wide idle samples are not a GPU utilization measurement. This profile
alone cannot attribute frame latency to CPU placement.

A reversible diagnostic tested that lead by restricting Linux's global
unbound-workqueue mask to the highest-capacity allowed CPUs. Each guarded
segment recorded and restored both requested and effective masks. Other
kernel workers also moved, so the test is not an isolated GPU-driver policy.
The application mask, Mesa, kernel, capture, calls, settings and GPU clocks
were unchanged. All three image comparisons are exact.

Nine alternating pairs show a **0.34% median regression**, within timing
variation. Thus this experiment does not justify adding a production GPU
workqueue affinity policy. The normal mask remains `7ff`, matching the original.
No GPU fault or reboot occurred; the existing watchdog and thermal guards
covered every segment. The companion JSON and local benchmark diagnostics
retain all samples, profile bounds, actual worker stacks and mask receipts.
