# Software vertex uniforms and subgroup image barriers (2026-09-16)

`dEQP-VK.subgroups.basic.graphics.subgroupmemorybarrierimage` now passes.
Software vertex execution uses the vertex parameter ABI even though its final
engine is compute. Image stores populate uniform halves 0..15; the old software
vertex prolog/main shader kept draw parameters, vertex parameters and root
pointers in that same area. Native hardware vertices already reserved a prefix.

Reserve the prefix for software vertices too, in both the main shader and its
raw vertex prolog, and relocate all command-side parameter uploads together.
With zero attributes, draw parameters now start at half16, vertex parameters at
half24 and root at half28. The first two trials relocated the main shader but
missed the vertex-parameter upload and then the prolog; their failed results
are retained. No image-fence change was needed or integrated.

The diagnostic CTS binary adds stage/readback logging only. Before the fix,
logical vertex and fragment checks failed all27 widths, reading zero;
tessellation control/evaluation and geometry passed. The vertex shader in this
pipeline executes as compute, not native VDM. After the complete relocation all
five logical stages pass all27 widths. Separate image visibility probes and
Apple memory-fence compiler witnesses remain in `build/directx-subgroup/`;
they are investigative evidence, not claims of additional feature support.

Validation on J713, library SHA256
`2af12620622f557ac4500bd84199e097c862e79a863e80c556f7bac67a7425c8`:

- `20260916T113702365275Z-dx-subgroup-prefix-detail-r3`: original shader/checks,
  diagnostic stage logging, Pass; peak61C.
- `20260916T113804829050Z-dx-subgroup-prefix-basic-r4`: unmodified CTS1.4.6.2,
  all70 basic subgroup cases, 35Pass/35NotSupported/zero failures, peak61.5C.
- `20260916T113830515695Z-dx-subgroup-prefix-reg1000`: established1001-case
  regression, 815Pass/186NotSupported/zero failures, peak61.75C.
- Host `asahi-native-uniform-prefix` and `hk_vbo_raw` pass, including software
  vertex prologs and retained bindless descriptor handles.

All hardware runs use watchdog60s, six-channel0.5s thermal monitoring/cutoff95C,
fixed60s case bound; the family/regression runs use12 workers. The first family
invocation accidentally selected the runner's older default CTS binary and
stopped before results; all70Unrun are retained as `prefix-basic-r3`, superseded
by the explicit1.4.6.2 invocation, not counted as passes.

Archived summaries/results, catalog metadata, diagnostic CTS logging patch and
host log are in `subgroup-uniform-prefix/`. Full traces/launch ranges are in the
named root `_provenance/experiments/` directories. The tested library also
contains the separately qualified sampler min/max work; this fix is independent
of that lowering. This does not establish Steam application readiness.

CPU-frequency clarification: these qualification runs requested the full-clock
ceiling, but the guard preserves an already lower cap. Their effective policy
maxima were 1,512,000 / 900,000 kHz, as recorded in the retained per-run
`cpu-frequency-caps.json` files (compact copy: `effective-cpu-caps.json`). Do not
interpret their elapsed times as full-clock performance measurements. Functional
results and thermal/watchdog bounds are unchanged. The host restored the normal
maxima before strict-sparse follow-up; the active CPU policy subsequently lowered
them again, so each run's recorded effective limits remain authoritative.
