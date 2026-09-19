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
