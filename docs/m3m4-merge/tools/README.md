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


`mesa-merge-package.py --help` describes immutable ARM64 staging. It verifies
the Meson source directory, clean committed source and generated git SHA before
installing without a rebuild; it hashes every regular library in the staged
prefix. Original build products and runtime prefixes are retained.

Replay helpers now save actual process mappings and require the selected
Vulkan library. The M3 helper also requires a successful exit and saved frame;
the image still needs a separate reference comparison. No guard or thermal
policy changed. Historical M3 OpenGL replay faults remain a qualification
limitation; a successful native Vulkan replay does not qualify that path.


Versioned desktop selection always writes GBM_BACKENDS_PATH, including on M3
where the original fixed-prefix configuration omitted it. libgbm otherwise
searches its compiled /opt/asahi-m3m4 backend directory after relocation.
The desktop probes require actual candidate Vulkan, Gallium, EGL, GBM and DRI
GBM-backend mappings; current production processes were inspected to confirm
that these libraries are present on both desktops. Selection has not yet run.
`install m3-game ...` separately switches the qualified game environment with
its own rollback record, preserving all game-specific options. Run it only
after the candidate passes replay and real application qualification.
