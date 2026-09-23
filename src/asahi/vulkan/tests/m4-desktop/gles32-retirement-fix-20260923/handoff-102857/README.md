# Repaired-kernel ES 3.2 handoff: 102,857 pairs

The ordinary repaired kernel module completed guarded segments 0–41 of the
original frozen, ordered 326,602-pair catalog. Outcomes: 100,477 Pass,
2,348 NotSupported, 15 QualityWarning, 17 CompatibilityWarning, and zero
Fail, Timeout, or Crash. Segment 41 completed 2,500/2,500 Pass in 821 seconds
with a 100.5 C peak; its guard returned the 60-second watchdog to idle.
No segment 42 workload started.

The exact indexed comparison with the full original baseline shows seven
original Fail cases now Pass, one original CompatibilityWarning now Pass,
and no worse outcome among measured pairs. The remaining original failure
and timeout lie beyond this checkpoint; full sustained validation is pending.
The five targeted high-stamp trial segments passed separately as described
in the parent README.

The compressed `results.json`, `summary.json`, and `comparison.json` are
the checkpoint. `catalog.json` and `clone.json` pin the selected coverage.
`controller.json` records the clean pause and next segment. The three Python
scripts here preserve the local supervisor/clone/compare versions; the full
workspace tools, segment inputs, and all thermal/watchdog evidence remain
in `/home/council/dev/dirtyroom_m4`. Resume from segment 42 only after checking installed hashes, idle watchdog,
available evidence space, and the normal desktop. Use
`python3 -u build/gles32-retirement-fixed-20260923/run-campaign.py --start 42`
from the workspace root. The script validates the completed prefix and retains
the original 60-second case and 900-second segment bounds. The controller
copy here still expects the workspace-root `tools/gl-cts-plan.py` and existing
campaign.
