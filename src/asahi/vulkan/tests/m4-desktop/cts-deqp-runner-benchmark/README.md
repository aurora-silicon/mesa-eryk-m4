# M4 CTS grouped execution benchmark — 2026-09-14

Completed the same 1,000-case random list used by the earlier 4/20-worker runs.
Case SHA256: `618a12fca57caf3c0be31c0b6edf9cd1e4004efb74b1026296b5c336533b30e8`.
This historical list is unfiltered; its labels remain unclassified. It is used
for comparison, not presented as the new required/advertised sample.

20 workers, 50 cases/process: **39.610 s runner**, **43.326 s guard/transport**,
plus **1.446 s staging**. **913 Pass, 84 NotSupported, 3 Fail**, no crash.
Peak temperature **60.75 C**, guard Pass, display manager active after completion.
Earlier: 4 workers 129.124 s (90.447 s batch execution); 20 workers 86.531 s
(58.332 s batch execution). Driver/CTS capabilities and caches changed between
those runs, so these are observed timings, not an isolated causal speedup.

The three failures are retained in baseline-results.json: two compressed texel
view cases using R16G16B16A16_USCALED and an RGB8 blend pipeline rejection.
No expected failures, skips, retries, source changes, or baseline rewriting.

Evidence:
`_provenance/experiments/20260914T004204253423Z-cts-deqp-runner-baseline1000`.
Contains exact binaries/driver hashes, QPA, logs, watchdog and thermal records.

Before that comparison, a fresh filtered random sample (seed 20260923, 421
required / 579 advertised) triggered a GPU retirement failure. Evidence:
`_provenance/experiments/20260914T003845653966Z-cts-deqp-runner-random1000`.
The guard saved a firmware fault snapshot and rebooted; boot recovered with SDDM.
Peak 60.25 C. The 93.946 s elapsed includes recovery; **not a completed benchmark**.
Offline recovery found **935 complete Pass, 56 complete Fail, 9 without a complete
QPA record**. A further Fail XML node lacked its enclosing end-of-case marker
and is not counted as a complete result. Many failures contain device loss,
not independent correctness defects. The zero recorded count in the original
summary reflects interruption before aggregation; recovered-results retains
actual evidence separately rather than overwriting that original summary.

Both attempts are retained. The new sample was not redrawn to suppress failures.
The filtered catalog currently maps only 33,242 eligible cases; unmapped cases
are explicitly excluded and are not asserted to be optional or unsupported.
