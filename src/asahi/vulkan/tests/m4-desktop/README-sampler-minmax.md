# M4 sampler min/max reduction (2026-09-16)

Honeykrisp implements sampler min/max reduction on G16G with nearest hardware
texture taps and shader footprint reduction. The sampler feature, extension,
filter-minmax properties and supported-format bits are enabled by default.
The Apple10/Gen5 native reduction encoding is not used on M4/HAL200.

The lowering includes nearest/linear spatial filtering, both contributing mip
levels, array/cube addressing, offsets, custom borders, sampler bias and LOD
clamps, image-view minima, sparse status, and up to 16 anisotropic footprint taps.
Only texels with nonzero weights participate. Sampler min/max LOD and bias are
stored as fp16 so native sampler fixed-point rounding does not remove an entire
contributing mip level. Integer nearest samples retain their bits.

Qualification also exposed a weighted-sampling view-minimum defect: applying
bias or sampler maxLod after the view minimum can select a forbidden level.
Weighted nonzero-view-minimum sampling now uses the same footprint with
interpolation. Ordinary zero-view-minimum sampling retains its native path.
Gen9 descriptors add two nearest companion sampler indices, increasing the
sampled descriptor stride 72→76; older generations retain their layout. Native
LOD queries retain both result words; generated depth queries omit comparison
metadata. Nonzero constant shadow LODs remain in registers instead of selecting
an unqualified native immediate encoding.

## Qualification

All hardware runs use watchdog 60 s, six-channel 0.5 s thermal monitoring with 95 C
cutoff, 12 CTS workers and a fixed 60 s per-case bound. Frozen catalog SHA256:
`72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.
Selected-case order and original global indices are retained in
`sampler-minmax/catalog-segments.json.gz`.

The complete 53,190-case sampler selection gives **21,670 Pass / 31,520 NotSupported,
zero failures**. The skips name exclusive compute queues, unsupported formats,
ASTC3D or other separate requirements; their exact reasons/counts are archived.
The 132 AMD trinary-minmax cases are unrelated shader arithmetic and are not
part of this sampler selection. Segment bounds below are selection positions,
not renumbered global catalog indices.

| Selection | Pass | NotSupported | Experiment | Peak C |
|---|---:|---:|---|---:|
|0–10000|4260|5740|20260916T102413701369Z-dx-minmax-final-0-10000|68.5|
|10000–20000|3650|6350|20260916T103631940645Z-dx-minmax-final-10000-20000|69|
|20000–30000|5155|4845|20260916T104738383179Z-dx-minmax-final-20000-30000|69|
|30000–40000|3835|6165|20260916T105945302536Z-dx-minmax-final-30000-40000|69.25|
|40000–50000|3525|6475|20260916T111349746424Z-dx-minmax-final-40000-50000|69.25|
|50000–53190|1245|1945|20260916T112940598353Z-dx-minmax-final-50000-53190|68|

Campaign library SHA256:
`adac8f3f62e5a37ab479e3d421aa095ccb7903361b06eba3bf9cf4ba3de956c8`.
It used the temporary experimental enable switch, removed after qualification.
The CTS 1.4.6.2 sampler admission incorrectly skips nonidentity R mappings even
when filterMinmaxImageComponentMapping is true. The retained patch conditions
that skip on the property; all result comparisons are unchanged. Binary
`deqp-vk-minmax-components` SHA256:
`c1a637217f05b42ef068ef2b777aadc8d70a3f44d20e5155ca6bbb31ad0a165f`.
It also contains the existing scaled-blit corrections. These are diagnostic
CTS results, not an unmodified conformance submission; original sources/build
were restored by the patched-build helper.

Independent CPU-reference validation: **207 configurations pass**:
64 min/max footprints, 8 constant-mip sampler/view combinations, 32 weighted
footprints, 31 depth-comparison footprints, 8 2D/cube LOD-query configurations
through Zink, and 64 fully bound sparse min/max probes checking values/status.
The sparse probes do not establish unbound-page behavior. Weighted color error
is below 0.000014 and depth error below 0.00000024. The native view-zero gradient
control has the previously observed allowed 1/256 LOD precision difference.
Evidence: `20260916T102251227386Z-dx-minmax-view-oracles-r4` (peak 61 C) and
`20260916T103508060590Z-dx-minmax-sparse-oracles-r1` (peak 62.75 C).

The default-enabled build also includes the software vertex uniform-prefix fix
in 33140dac7b6. Its library SHA256:
`41cb8e653a9d2ba31e1872890ccfcc13161d8fc8e73d3533ea3ca2145481d7d3`.
`20260916T113932527028Z-dx-default-min-lod`: 1940 view/shader minimum-LOD cases,
1,589 Pass / 351 NotSupported/zero failures, peak 64.25 C. Host Asahi suite:
103 Pass and the same 11 pre-existing failures; host and ARM64 builds pass.
The full sparse regression is unchanged: 8,507 Pass / 5,812 NotSupported /
zero failures across 14,319 cases, peak 65 C
(`20260916T114120783128Z-dx-default-sparse`). All 207 independent configurations
also pass on the default-enabled combined build, peak 62.5 C
(`20260916T115105281436Z-dx-default-oracles-r2`). The preceding aggregate runner
hit its own 25-second child timeout while checks were passing; the corrected
runner uses five separate bounded suites and eight LOD-query runs under the
same guard. That failed harness run is retained, not counted as qualification.

## Reproduction and retained evidence

`minmax-sampler-cases.py`, `view-min-lod-cases.py`, `vk-buffer-oracle.c` and
`cube-query-probe.c` are mirrored beside this report. `oracles.tar.gz` and
`sparse-oracles.tar.gz` retain generated inputs, expected bytes and runners.
The oracle mirror excludes unrelated pre-existing root-tool 16-bit-storage edits.
Run target tools only through the guarded experiment wrappers.

The CPU reference follows Vulkan's reduction across mip levels. The local
lavapipe version interpolates with lp_build_lerp across levels and lacks view
MinLod, so fractional-mip expectations are specification-derived, not changed
to match lavapipe. Relevant references are the [Vulkan texture sampling rules](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/chapters/textures.adoc)
and [Apple feature families](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf).

Summaries/results, patches/build manifest, host logs, exact skip reasons and
chronological qualification history are archived in `sampler-minmax/`. Raw
logs, shader captures and thermal/watchdog evidence remain in the named root
`_provenance/experiments/` directories. Earlier failures remain recorded,
including corrected cube-reference geometry and shadow-LOD bugs. This feature
qualification does not establish full DirectX or Steam application readiness.

The 253-case fixed-family regression also passes: 252 Pass / one unrelated
NotSupported, peak 64.25 C (`20260916T115215240222Z-dx-default-fixed-regression`).

CPU-frequency clarification: these qualification runs requested the full-clock
ceiling, but the guard preserves an already lower cap. Their effective policy
maxima were 1,512,000 / 900,000 kHz, as recorded in the retained per-run
`cpu-frequency-caps.json` files (compact copy: `effective-cpu-caps.json`). Do not
interpret their elapsed times as full-clock performance measurements. Functional
results and thermal/watchdog bounds are unchanged. The host restored the normal
maxima before strict-sparse follow-up; the active CPU policy subsequently lowered
them again, so each run's recorded effective limits remain authoritative.
