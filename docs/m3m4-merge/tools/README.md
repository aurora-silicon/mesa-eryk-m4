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
