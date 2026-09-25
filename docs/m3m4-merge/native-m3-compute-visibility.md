# Native M3 compute visibility (2026-09-26)

Enable the native wide CDM visibility operation on G15S, and retain it at
compute stream closure. This allows the existing merge pass to join complete
meta destination ledgers across memory dependencies. Incomplete ledgers,
attachment limits, query resets, timestamp boundaries, and firmware completion
publication remain intact. The kernel wide-visibility capability is not enabled.

## Independent native witness

The arm64e slice of AGXMetalG15X_M1, which includes AGXG15SDevice, has SHA256
`5e6c069a49fa76b2aa97091e100ceaac3510ce2dc3a65cf5401c6b1e44fbfc89`.
Its G15 CommandEncodingNext ComputeContext::encodeBarrier starts at 0x2aa5f0.
For a nonzero visibility enum, 0x2aa7a8..0x2aa7bc constructs 0x600215fe and
0x600201fe, selected by the context byte at +0x902. These are the same two
words independently observed in G16G. The ordinary visibility path selects
0x60001560/0x60000160. This change qualifies G15S only, not all G15 identities.
The host codec test checks exact native words and rejects mutated encodings.
The Apple binary remains outside the source repository.

## Validation

Candidate library SHA256:
`b060ff750e1b86f258031a2ec5782f37d420282a3326503e9296a3017fbba57f`.
M3 module SHA256:
`4402c97997564589654344b0217acf1aadb36fb18d7c671e2bdc1dc30d985f94`.
Boot `437f5fe8-f3f7-41bd-8147-f8083ffd21a7`, normal timing logging disabled.

- 4,027 host CDM codec checks pass.
- 40 sparse-buffer and 3 alias-buffer oracle variants pass on M3.
- All 13 query reset/copy/timestamp boundary modes pass.
- Eight pinned Vulkan CTS 1.4.6.2 synchronization2 cases pass: compute SSBO
  producers to SSBO/UBO/transfer consumers, image compute producers/readers,
  and fill/copy producers to SSBO consumers. Pinned catalog SHA256
  `72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`;
  global indices 2911361, 2911363, 2911389, 2910494, 2910488, 2909529,
  2910243, 2911353. No cases were replaced after execution.
- Satisfactory check `20260925T222206618328Z` matches all three reference RGB
  images exactly. All 30 measured frames remain at the captured resolution
  and settings. Repeats: 7.361538664, 7.340003505, 7.444210242 seconds;
  median 4.07523 saved-replay FPS. Previous signed-integer build on this boot:
  7.606888642 seconds / 30 frames = 3.94379 FPS (3.33% improvement).

The separate compute-start timestamp default experiment also passed query and
image checks, but had no measurable performance benefit; it remains disabled.
Hardware tests used the 60-second watchdog and thermal guard. Full game assets,
images, QPA logs and telemetry are retained in the private local benchmark repo.
These targeted results are not a claim of Vulkan conformance.
