# CTS: 10,000 frozen cases

| Result | Count | % |
| --- | ---: | ---: |
| Pass | 9,956 | 99.560 |
| Fail | 41 | 0.410 |
| Timeout | 2 | 0.020 |
| Crash | 1 | 0.010 |

Runner execution: 559.976 seconds.
Active campaign time including guard/transfer: 577.173 seconds.
Staging: 8.377 seconds.
Wall span between first start and last result, including inspection pauses: 813.611 seconds.
Peak temperature: 62.0 C.

178 interrupted attempts restarted; no completed Pass/Fail/Crash/Timeout was retried or replaced.
The first segments used 20 workers; the final completion segment used four.
This was an interrupted and resumed campaign, not one uninterrupted throughput benchmark.
Raw runner crashes caused by coordinated SIGKILL are classified as Interrupted; true SIGSEGV and timeout results remain failures.
Exact first-attempt and segment counts are in summary.json; every attempt is retained in results.jsonl.gz.
This uses an explicitly patched historical CTS diagnostic binary and is not a Khronos conformance submission.

Seed: `20260924`; 4,043 required and 5,957 advertised cases sampled from
the 33,242 cases covered by the reviewed catalog. Unmapped cases are excluded;
this is not coverage of the entire historical CTS list.

The SIGSEGV occurred in
`dEQP-VK.tessellation.invariance.outer_edge_symmetry.triangles_equal_spacing_ccw`.
The two timeouts remain timeouts at the original 20-second bound.
All four segments used the same boot, kernel, Vulkan ICD and CTS/runner binaries.
The display manager was active and DCP reported zero failures after completion.
A reporting fix distinguishes coordinated SIGKILL cancellations from crashes
and stdout/QPA-started cases from cases that never started. Raw segment
summaries were retained, not overwritten with corrected classifications.

The first continuation was prepared before that reporting correction, and
restarted 30 cases that had begun before cancellation. The final segment
explicitly resumed the remaining interrupted cases with four workers. Across
the whole campaign, 174 distinct cases had an interrupted first attempt, and
178 interrupted attempts were restarted. No completed Pass, Fail, Crash or
Timeout outcome was rerun or overwritten.
