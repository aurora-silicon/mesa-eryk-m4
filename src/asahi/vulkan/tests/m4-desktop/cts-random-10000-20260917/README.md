# Random 10,000-case Vulkan 1.4 timing run

2026-09-17, J713 / M4. All 10,000 selected cases have a terminal result.
No driver or CTS fixes were made during the run. Seed: **202609172000**.
Selection is uniform without replacement from the pinned 3,206,030-case
catalog, with no capability or prior-result filter. Original global indices
and exact selection membership are retained.

| Outcome | Cases |
| --- | ---: |
| Pass | 4,724 |
| Fail | 139 |
| Crash (SIGSEGV) | 11 |
| ResourceError | 1 |
| NotSupported | 5,125 |
| Timeout | 0 |
| Unrun | 0 |

## Measured time

- Sum of active test-runner wall times: **357.975 seconds (5m58s)**.
- Sum of guarded segment wall times, including evidence handling: 462.794 s.
- Additional staging: 31.974 s.
- First runner start to last runner finish: **1062.858 seconds (17m43s)**.
  This includes recovery, inspection and pauses between segments, rather than
  continuous testing. Final evidence collection/commit time is outside it.
- 21 bounded segments, two boot IDs, one recovery reboot; peak temperature
  69.25 C. Every hardware segment retained the 60-second watchdog, six-channel
  temperature monitor and existing CPU thermal policy.

The first two segments used 20 workers. The second exhausted memory and
triggered a kernel warning in dma_fence_set_error via g16_submit::Packet::finish;
the guard rebooted. Remaining non-WSI work used four workers. Eleven process
SIGSEGVs interrupted scheduling; after evidence inspection, only unfinished
cases resumed. Failed/crashed terminal cases were not rerun or replaced.
All incomplete attempts remain in results.json.gz alongside their terminal
outcomes. The installed Vulkan, kernel module and CTS hashes stayed unchanged
across both boots (summary.json).

The guard requested a 4.104 GHz CPU ceiling but never raises an existing lower
limit. Actual caps varied with inherited settings/thermal policy; the first
batch used 1.26/0.9 GHz policy maxima. Exact per-segment caps are retained in
result.json. These results are not a maximum-clock throughput qualification or
an uninterrupted same-boot benchmark. Do not extrapolate a precise full-suite
runtime from this interrupted sample.

The 91 WSI cases ran with one worker and one case per process on a private
1024x1024@1000 Hyprland compositor. Outcomes: one Pass, 89 NotSupported, and
one ResourceError: `wsi.wayland.incremental_present.scale_none.fifo_latest_ready.rotate_180.inherit.reference`
failed to bind the compositor. Keep that environment/setup error separate
from confirmed GPU-driver failures. The normal compositor was restored/left
running; no CTS processes or private compositor remain.

`failures.tsv` lists all 151 Fail/Crash/ResourceError cases with catalog index
and reason. `not-supported.tsv` retains capability/platform skips. These are
raw results from the original pinned CTS binary, including previously known
scaled-format reference issues; no baseline comparison established that all
139 Fail cases are regressions. No failed cases were fixed during this task.

## Reproduce selection

```sh
python3 tools/cts-ranges.py random \
  --catalog build/cts-vulkan14-ordered/catalog \
  --seed 202609172000 --count 10000 \
  --output NEW_DIRECTORY
```

Frozen catalog SHA256:
`72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.
Selection SHA256:
`e16b78dc041b1a095b36590dc7dfa97fd43654e8f02568fba60420b12a013b19`.

Use existing cts-bulk.py guards in bounded segments, with WSI on a private
headless compositor. See segments.json and the individual source/binary/guard
identities. Complete QPA and runtime evidence remains in workspace
`_provenance/experiments/*-random10k-20260917-*`.

TODO: diagnose/fix the SSBO-layout and scaled-swizzle SIGSEGVs; fix the
out-of-memory fence-completion warning; investigate the WSI compositor bind
error. A clean timing rerun and a full Vulkan 1.4 campaign remain separate work.
