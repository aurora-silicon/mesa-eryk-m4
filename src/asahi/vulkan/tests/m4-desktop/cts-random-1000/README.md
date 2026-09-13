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
