# Seeded random CTS selection

`cts-ranges.py random --catalog CATALOG --seed SEED --count COUNT --output NEW_DIR`
samples frozen catalog indices uniformly without replacement. It verifies the
full catalog hash/count/order, preserves each global index, and never filters
on capabilities or previous results. `cases.txt`, `cases.meta.json` and
`selection.json` retain reproducible input membership and provenance.

Feed bounded partitions to the normal guarded CTS runner. Keep presentation
cases on a private headless compositor, one worker per compositor. Report
skips separately from passes, and retain failures and incomplete attempts.
Worker-count changes or recovery reboots must be included in timing reports.

Offline verification: repeated seed gives identical bytes, all selected
indices are unique and map exactly to names, and a changed catalog is rejected
without publishing an output directory. The recorded 202609172000 selection
contains exactly 10000 unique cases from catalog SHA256
72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf.
