# Local Radeon trim-command-pool comparison

Native unmodified CTS1.4.6.2 (`f6a29701220f34dd1407513bfe80d74ca7b392ce`),
Release `-O3 -DNDEBUG`, x86_64 workstation `mini`, Ryzen9 8945HS / Radeon780M
RADV Mesa26.2.1. Exact case `dEQP-VK.api.command_buffers.trim_command_pool`.
Single isolated first attempt: Pass, CTS case225327us, process wall0.314490s.
The binary hash, command, renderer identity and raw QPA are retained here.
No M4 workload or desktop state was changed for this comparison.

M4 measured isolated case5.132689s in
`build/g16-completion-generation/copy-trim-zero/` is about22.78x slower.
That measurement used matching notifier generation, zero settle and idle-off0;
the final driver has direct completion and idle-off2ms. The final driver's
first10k contended run measured41.027435s for this case with19 other workers;
that contention time is not a like-for-like isolated comparison. M4 guarded
CPU caps were1.8/1.86GHz; this workstation used its normal CPU policy.
These are whole-stack timings on different systems, not GPU-only speed ratios.

The test submits330 batches containing6000 command buffers, each setting an
event. It trims the pool300 times and waits for each submission's fence. Our
M4 path produces6000 individual GPU jobs for those buffers. No expensive
application shader is involved. Batching and event/submit overhead remain
concrete optimization leads; the timing alone does not attribute every cost.

Reproduce using workspace `tools/cts-host-case.py`; see `tools/README-cts.md`.
