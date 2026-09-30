# M3 multi-draw UBO metadata reuse and DrawID initialization

Implementation commits: `5d9e712aa4f` and `b2e6898b172`.

The driver repeatedly read write-combined UBO/sampler descriptor metadata to
select its ordinary resource variant for each indexed indirect draw. Those
bindings and dynamic offsets cannot change inside one multi-draw API call.
G15S now proves eligibility once per stage, shader and variant in that call.
Internal meta entry invalidates both proofs before replacing any descriptors.
Mutable/update-after-bind/partially-bound UBO bindings and flagged sampler
bindings are already excluded by the existing compiler admission rules.
Sparse/shadow addresses keep their existing general fallback. This caches a
boolean decision, never uniform values; each shader execution still loads its
data on the GPU. `HK_M3_MULTIDRAW_UBO_PROOF=false` disables this optimization
independently of fragment-program reuse.

The vertex path also uploaded only two DrawID bytes while its native uniform
range loads four. It now initializes the complete 32-bit value. Existing
advertised draw-count limits are unchanged. The pixel probe consumes all four
DrawID bytes plus baseVertex and firstInstance, as well as vertex/fragment UBOs.

## Timings and diagnostics

Same 30 completed 3024x1837 Minecraft GL gameplay frames through unchanged Zink.
Nine alternating pairs, same Vulkan binary, cache option false versus true:

| Option | Replay FPS samples | Median FPS |
|---|---|---:|
| disabled | 68.1552, 78.7366, 72.4836, 71.9179, 73.4647, 73.7785, 72.0709, 73.6424, 72.1572 | 72.4836 |
| enabled | 75.7905, 76.5568, 81.2271, 75.8895, 81.1318, 74.9574, 69.4501, 74.3294, 74.1876 | 75.7905 |

Median gain: **4.56%**; first/middle/last snapshots are pixel-identical. These
are short replay intervals, not live Java FPS or a new M1/M4 measurement.
An earlier prototype compared entire root tables for each proof. Its initial
three-pair median regressed **4.63%** and was rejected; its exact images,
samples and patch remain in the workspace evidence.

The metadata walk disappears from the enabled CPU sample profile. Total CPU
time did not consistently improve: an earlier diagnostic comparison was
84.986 versus 75.082 ms, while the final same-binary instrumented comparison
was **80.103 versus 103.914 ms**. The latter profiles have only 40/53 samples;
they are attribution diagnostics, not performance qualification. Both results
are retained. Vertex uniform-range rebuilding remains prominent.

Timed library SHA256:
`ab28287b25d7f998e0941cf69df4c80688f30fb2f12d888a43a322a7f3d9145e`.
Prefix: `/opt/asahi-m3-multidraw-ubo-scope-ab28287b25d7`.
The subsequent DrawID fix is separately image/CTS qualified, not separately
timed here. Its library SHA256:
`bae133dad83fce62c2c91b1a2a074a45cf1065fca5e269d916d016dce5828aa2`.
Prefix: `/opt/asahi-m3-multidraw-drawid-bae133dad83f`.

## Qualification

- UBO proof: frozen 2,404-case indexed-draw/resource-binding union:
  **1,434 Pass, 970 NotSupported, zero failures**, all 51 guards complete.
- DrawID fix: the full frozen `shader_draw_parameters` family, including
  capability API cases: **78 Pass, zero failures**, both guards complete.
- Resource pixel oracle: **48 configurations for each on/off variant**,
  switching ordinary/sparse fragment UBOs, vertex UBO values, bound ranges,
  edge/custom-border samplers and 2/129/257 indirect records. The extended
  DrawID/baseVertex/firstInstance oracle also passes all 48 for each variant.
  Each image checks every pixel against its CPU color calculation, within
  one RGB level. These synthetic configurations are separate from CTS.
- Both staged libraries pass the six retained game image checks: Minecraft
  GL/Vulkan, Satisfactory Vulkan gameplay and 0 A.D. Vulkan are exact;
  Factorio Wayland and SuperTuxKart Zink retain the accepted one-level policy.
- ARM64 builds and existing host sparse/root/descriptor-prefetch tests pass.

All hardware runs use the 60-second watchdog and 0.5-second all-channel PMP
monitor, with existing kernel thermal policy. UBO CTS peak: 79 C; DrawID CTS:
68 C; game checks: 89.75/89.5 C. Boot remains
`4b361b98-fdd1-4b11-ae68-508522fecbd9`; no GPU fault/recovery/reboot was needed.
The normal display manager is active. These are isolated Vulkan candidates
with qualified unchanged GL libraries, not a desktop installation or M4
qualification.

Workspace evidence: `build/m3-multidraw-vertex/`; benchmark diagnostics:
`m3-multidraw-ubo-{proof,scope}-toggle-20260930`; image-check sessions:
`m3-multidraw-{ubo-scope,drawid}-20260930`. The JSON companions retain hashes,
all timing samples, guard receipts and every CTS catalog index/status/reason.

The reusable host wrapper is mirrored at
`src/asahi/vulkan/tests/m4-desktop/run-m3-multidraw-resources.py`:

```sh
python3 tools/m3-multidraw-resource-probe.py \
  --source mesa/m3m4-merged/src/asahi/vulkan/tests/m4-desktop/gl-multidraw-resources-probe.c \
  --prefix /opt/asahi-m3-multidraw-drawid-bae133dad83f \
  --sha256 bae133dad83fce62c2c91b1a2a074a45cf1065fca5e269d916d016dce5828aa2 \
  --output build/NEW-resource-witness
```

It compiles the small probe natively before guarded GPU execution, records
source/binary/library hashes and stops after any failed oracle or guard.
