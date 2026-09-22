# M3 fragment storage qualification (2026-09-23)

Six KHR-GLES31 shader-image-size cases failed shader linking with
`Too many fragment shader storage blocks (1/0)`. Honeykrisp disabled
`fragmentStoresAndAtomics` on G15S through a retained qualification gate.
Zink therefore advertised zero fragment SSBOs, while the image-size tests
used a fragment SSBO to return their results. A fresh singleton reproduced
the original failure.

The candidate enables only fragment storage on the shared G15/G16 path.
Vertex storage, geometry, tessellation, sparse features and ES 3.2 remain at
their existing qualification boundaries. Native stores, atomics, helper
suppression and serial render retirement already have implementations; this
change requires hardware qualification rather than assuming similar ISA is
sufficient. CTS expectations and shader code are unchanged.

Evidence under `build/m3-gles31-20260922`:

- `campaign/segments/0499/run`: six original link failures.
- `triage-fragment-ssbo-baseline`: fresh original failure.
- `triage-fragment-storage-candidate`: exact case Pass.
- `triage-fragment-storage-khr`: 155 Pass, 57 NotSupported across image
  load/store, image-size, SSBO and atomic-counter tests. Includes fragment
  format stores, image atomics, early fragment tests, image-access visibility,
  fragment copies and storage-to-vertex-array visibility.
- `triage-fragment-storage-helper`: 124 Pass, 20 NotSupported for helper
  invocations, early fragment tests and SSBO atomics.
- `triage-fragment-storage-imagesize`: 285 Pass (19 cases × 15 configurations).
- `triage-fragment-storage-prior-skips`: follow-ups for old capability skips;
  these never replace first-attempt statuses in the frozen campaign.

All experiments retain the 45s guard, 60s watchdog, 0.5s five-channel PMP
telemetry and 95°C cutoff. This is focused M3 qualification, not a claim of
complete ES 3.1 conformance or vertex-stage storage support.
