# Ordered CTS batch 1: completed prefix [0, 30000)

The user stopped the planned [0, 100000) run after 30k. Resume deterministic
coverage at index 30000. The frozen catalog and original 100k selection are
unchanged. Interrupted segment4 evidence is retained locally and is excluded
from this report.

Original 30k outcomes: **14580 Pass, 15395 NotSupported, 24 Fail, 1 Timeout**.
After targeted fixes: **14605 Pass, 15395 NotSupported; no unresolved original
failures, crashes or timeouts**. These are cumulative results, not a same-build
30k regression run. Unsupported cases are not passes or blanket optionality
claims. CTS contains the previously documented reference corrections.

## Multisampled stencil copies

All 24 failures covered stencil copies on split D16S8/D24S8 images at 2x/4x,
across whole/partial/array variants. The common meta copy helper replaced the
requested stencil aspect with COLOR for its internal R8 view, selecting the
depth plane. The new depth_stencil_aspect_views property preserves aspect
selection for drivers with separate backing planes; Honeykrisp enables it for
split depth/stencil images. All 24 original cases pass in 1.212s runner time.
Mesa fix: 263d691c651. Evidence and exact cases: stencil-fixed-results.json.

## Command-pool timeout

`dEQP-VK.api.command_buffers.trim_command_pool` originally timed out at
60.056313s during the 20-worker broad run. Isolated before optimization it
passed in 48.7837s, performing 6000 GPU event-write jobs and fresh 32MiB private
allocations. Measured preparation totaled33.4601s, active execution4.7523s and
cleanup9.9686s.

The kernel no longer creates an unused firmware alias of compute private
storage. Only client GPU addresses to that storage enter the command's page
lists. Fresh allocations, size, zero initialization, GPU work and retirement
ownership are retained; this is not buffer reuse or CPU event emulation.
Kernel fix: 3dfe2229c473. Installed module SHA256:
597b9033e8277880abdac4f6c0737887e0a77f074ebfc0a2488bf4d0be08d4fd.

Isolated after optimization: **43.7790s**, about10.3% faster. Preparation fell
to28.4505s; active execution4.7774s and cleanup9.9077s were essentially unchanged.
The failing worker's original50-case group also completed with20 workers and
one case per group:16Pass/34NotSupported, no failures. The timeout case passed
in **46.335518s**. The60s limit was unchanged. This focused context check is
not the original10k concurrent workload or a broad regression run.

All GPU tests used the60s watchdog and six-channel thermal guard. Peak during
the targeted fixes was60.25 C. The optimization retains the larger allocation
cost; it removes5s of unnecessary preparation rather than claiming that the
entire overhead is gone.

`final-results.json.gz` retains every completed and interrupted attempt;
`final-summary.json` hashes the inputs. The original timeout remains visible.
The19 apparent peer crashes from the runner's cancellation were recorded as
Interrupted, then only unfinished cases were resumed. No driver crash occurred
in the accepted30k run. The later deliberate runner interruption abandoned its
watchdog lease and triggered recovery; it is not a CTS crash in these30k cases.
