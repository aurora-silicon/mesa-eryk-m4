# Random Linux CTS sample: 1,000 cases

Selected before execution from the historical Vulkan CTS 1.0.2 must-pass list
at tag vulkan-cts-1.0.2.6, source commit 6b3bca439862e986409ffd76e857ada0cc7c9080.
Seed 20260919, Python random.Random.sample, original population order.
Only Windows-specific cases and Android WSI are excluded before selection;
optional features, defects, prior failures and previously tested cases remain.
Exact population hash, exclusions and chosen cases are in selection.json.

Use the separately identified corrected binary from ../cts-linux-100:
scaled-storage-combined.patch and scaled-storage-cts-build.json describe all
CTS source corrections. Binary SHA256:
6575f233466bfa88704e7355c288700db948dd5a0c181ecabd1443b67d27b304.
Installed Mesa SHA256:
249e310c0d691774470592804db788220135b2aa4ece7e5aa0185b3b4e7b8ad3.
Kernel commit fba91e5d9577; Mesa commit d313483e574 (d1f3a56e346 adds reports).
No driver fixes or test-oracle changes will be mixed into this measurement.

Every hardware batch uses the 60-second watchdog, host SSH heartbeat and
six-channel thermal guard, with a 40-second batch guard and 20-second per-case
bound. Timeouts are inconclusive bounded outcomes, not correctness failures.
Keep process crashes and device-loss/recovery events visible; retain every
first-attempt outcome and continue only after inspecting/recovering the target.

The result is a random-sample measurement, not full Vulkan conformance.

## Completed first-attempt results

| Outcome | Count | Rate |
| --- | ---: | ---: |
| Pass | 800 | 80.0% |
| NotSupported | 151 | 15.1% |
| Fail | 47 | 4.7% |
| Process crash | 2 | 0.2% |
| Timeout / unrun | 0 | 0.0% |

All 1,000 distinct cases completed, exactly once, with no resampling or driver
changes. Excluding the 151 unsupported cases gives **800/849 = 94.23%** passes.
Failures plus process crashes total 49/1,000 (4.9%).

One of the 47 failures (index 6, indexed indirect line drawing) caused a GPU
firmware fault and guarded reboot. The fault snapshot and first-attempt result
were retained, and the next case ran after verified recovery. Index 9 returned
DEVICE_LOST for submit EINVAL without a GPU fault; later jobs retired normally.
Both process crashes (indices 0 and 848) are render-pass input-attachment-index
assertions. These are observed failure signatures, not a claim that all failures
are driver bugs: the historical CTS can also generate invalid API use.

The 35 guarded batches used two boot IDs, with identical installed software
and guard hashes throughout. Peak recorded temperature: **59 C**. The host
heartbeat remains running, the target watchdog is idle/armed at 60 seconds,
and sddm is active and enabled following the final batch.

`results.json` retains every outcome; `summary.json` includes rates, category
counts, exact identities and all evidence paths. `failures.tsv` lists the
49 failure/crash cases for direct reproduction. `recovery-notes.json` records
the inspection decisions between interrupted segments. Every QPA/stdout and
compiler failure bundle remains in the referenced workspace evidence tree.
