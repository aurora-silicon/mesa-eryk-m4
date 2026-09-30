# Rejected M3 vertex uniform-layout caches

Two CPU metadata-only cache prototypes preserved all three Minecraft replay
images, but neither earned a default implementation. Nine alternating pairs
showed a 3.10% median regression for the first full-layout cache, and a 0.28%
gain for the more restricted relocation cache. The latter is within observed
timing noise. Both prototypes are absent from the merged branch.

The companion JSON retains timings, pixel comparisons and build identities.
Source patches and command receipts are stored in the separate local benchmark
diagnostics. A local checkpoint also remains at
`experiment/m3-multidraw-vertex-ranges-20260930` (`e91ed594ee0`).
