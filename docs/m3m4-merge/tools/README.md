# Merge validation helpers

These are source snapshots of workspace-root tools; run the corresponding
`tools/` versions from the source collection, not this archival directory.
No helper relaxes either machine's watchdog, thermal or bounded-run policy.

- `mesa-merge-probe.py`: target child of the existing guard, records actual
  candidate loading, compute/render output, profiles and kernel messages.
- `mesa-merge-cts.py`: freeze all prior repaired M3 GLES indices; run that
  exact selection on M4 using cts-bulk. M3 uses gl-cts-focused.py.
- `mesa-merge-vk-selection.py NEW_DIRECTORY`: deterministic Vulkan dossier
  selection from the pinned catalog, retaining global indices and reasons.
- `gl-cts-clone.py SOURCE NEW`: clone frozen inputs only; copies M3 options
  as well as M4 catalog/session metadata, never inherits old outcomes.
- `m3-cts-{run,batch}.py`: snapshots of tools/m3-gpu/cts-{run,batch}.py.
  Explicit --icd selects and hashes an isolated candidate; --stop-on-nonpass
  stops the host controller for ordinary failures as well as crashes.

M4 cts-bulk, gpu-experiment and gl-cts-plan accept --mesa-source to record
an integration worktree instead of incorrectly labeling the M4 original.
Their updated source snapshots remain with the existing m4-desktop tools.

- `mesa-merge-coverage.py BASELINE CANDIDATE`: compare identical frozen
  catalogs by global index, retaining warnings/timeouts and exposing capability
  regressions. It does not modify or automatically review any outcome.
- `mesa-merge-select.py`: verify all candidate library hashes, back up selected
  desktop environment files, then update only Mesa paths. Restore validates
  both fully and partially installed states and refuses unrelated user edits.
  It performs no GPU work; subsequent restart/probes require the existing guard.
- Desktop/app helper snapshots accept isolated candidate paths and retain actual
  mapped-library identities. M3 0 A.D. remains on its separately qualified
  prefix until replay and real-application validation authorize switching it.

`factorio-replay.py` forwards `--mesa-prefix` to `vulkan-trace.py`, alongside
the explicit ICD. Candidate replays therefore use a coherent EGL/Gallium/GBM
prefix instead of retaining unrelated old loader paths. The trace's memory
rebinding and offscreen swapchain adaptations remain explicit in evidence.
Coverage comparison also flags newly introduced quality/compatibility warnings.
