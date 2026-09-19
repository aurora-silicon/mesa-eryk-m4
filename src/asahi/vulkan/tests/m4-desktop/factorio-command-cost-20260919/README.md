# Command-cost controls, September 19

The unchanged five-frame capture, full 2512x1552 resolution, native timestamps,
exact final pixels and all ten query totals were checked in each run. Kernel
754cccf0ab3b, experimental independent/compute tiling both disabled.

* No compiler register-pressure scheduling: 6.124..6.215 ms complete GPU span.
* Merge a trailing flush-only compute stream: 6.129..6.248 ms, 17 commands
  rather than 18. Keeps the ending firmware flush and all real accesses.
* Revisit the earlier immutable root-field preload after the statistics
  optimization: 6.184..6.291 ms. No meaningful improvement.

These are negative controls, not improvements or live FPS. All temporary
source changes have been removed; the flush-fold patch is retained here and
the earlier root-prefetch patch remains in the architecture evidence folder.
No full CTS claim. The 5 ms goal remains unmet.

Native counter striping also remains disabled and archived. The exact-count
oracle now passes split submissions, odd edges and CPU/GPU copies; the old
visibility failure does not reproduce with the current prolog write tracking.
But X-coordinate stripes take 6.462..6.550 ms, and draw-group stripes through
the existing uniform take 6.167..6.327 ms. Neither improves the baseline.
The draw-group variant may reuse a stripe when graphics uniforms stay clean;
that cannot change totals, but it is not a unique counter per API draw.

Compressed mip-chain atlas revisited after cache/memory/query fixes (164549):
exact pixels/counts, 5.929..6.091 ms; no meaningful gain established.
Qualified normal kernel868fa406 and full profiling restored at16:43:50 reboot.
