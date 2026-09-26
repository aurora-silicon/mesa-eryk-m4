# Compute root input footprint

The compute Ioto loader used the final compiler `push_count` to enlarge its
root-table read. That count includes arithmetic-preamble outputs and promoted
constants, which are produced by the appended constant program and the separate
rodata range respectively. Only the root pointer and the declared set pointers
belong to this input range. Graphics already uses this exact root footprint.

Remove the compute-only expansion. This also prevents unrelated root-table
bytes from being loaded over output and constant slots before their real
producers run. It is not a claimed frame-rate improvement.

Validation on 2026-09-26:

- Ioto program core: 7017 checks; compiler units: 306 tests.
- M3: eight CPU-exact mixed fetch/sample/divergent compute configurations pass,
  with native arithmetic and memory preambles enabled.
- M4: integer compute oracle matches all 524288 buffer words, under watchdog
  and all-channel thermal guard. Experiment114314249200.
- M3 unchanged Satisfactory DX12 gameplay replay passes its existing image
  budget (check113422040191). Three matching warm timings:3.46179,3.46030,
  3.46112 FPS, median3.46112; neutral against the prior3.47167 median.
- First cold replay113310648609 hit the37-second application timeout during
  compilation/initial replay. Cached retry completed in20.8seconds. Failure
  evidence is retained; no GPU fault was reported.

The shader binaries and normal desktop configuration are unchanged. Exact
libraries, source patch, commands, watchdog/thermal evidence, and comparisons
are retained in the local gpu-replay-bench repository under
`provenance/satisfactory-dx12-optimization-20260926`.
