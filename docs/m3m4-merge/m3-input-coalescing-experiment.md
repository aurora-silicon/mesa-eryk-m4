# M3 input scheduling experiment

Opt-in G15S `HK_M3_COALESCE_UBOS=true` orders admitted UBO scalar inputs by
buffer/offset and updates their NIR uniform addresses. The range builder keeps
every scalar bounds check, groups contiguous source/destination ranges and
uploads eligibility words separately. Ordinary variants omit unused flags.
`HK_M3_VERTEX_PLAIN_SAMPLERS=true` also specializes ordinary vertex textures
with the existing immutable descriptor predicates and general fallback.

Candidate caa43523cdf979a13f1456bc2b65a0995396016c0b3bf45aaab77f358cf141ca:
three retained Minecraft frames are byte-identical to the reference PNGs.
The pinned 1069-case input selection completed 716 Pass / 353 NotSupported;
128 additional evenly spaced vertex sampled-descriptor cases completed
74 Pass / 54 NotSupported. No test failures or guard errors.
These are targeted regressions, not a full conformance run.

Same-boot alternating timings in benchmark diagnostics/m3-next-inputs-paired:
installed 95.6225 FPS, vertex ordinary textures 97.6464 FPS, combined coalescing
97.0227 FPS (three samples each). The compiler input-layout change has not
shown an additional throughput gain; keep it opt-in pending further work.

Per-pass profiling on a separate instrumented boot:
production world TA 2.94775 ms, vertex preamble 3.1385 ms, coalescing 2.904166 ms,
vertex ordinary textures 2.819583 ms, combined compact inputs 2.823 ms.
World fragment time stays around 2.66 ms. These are firmware stage wall spans,
not isolated instruction costs or scored frame rates.

Evidence: workspace build/minecraft-inputs-next and benchmark diagnostics/
m3-next-inputs-paired. The local benchmark repository retains its own commits;
no captures were pushed. The desktop still uses f7b0d4dce0a.
