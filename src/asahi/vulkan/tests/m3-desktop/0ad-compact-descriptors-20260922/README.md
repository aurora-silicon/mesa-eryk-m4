# Compact native descriptor prefetch, 2026-09-22

The optional `HK_NATIVE_COMPACT_DESCRIPTOR_PREFETCH=true` path, used together
with `HK_M4_PREFETCH_DESCRIPTORS=true`, gathers at most 32 disjoint word-aligned
spans. It merges overlaps and adjacency without filling gaps, keeps set identity
and halfword offsets, and leaves loads unchanged when the span or uniform budget
is exhausted. The recorder checks each span against the bound descriptor set,
including push sets. Compiler cache bit 43 separates the option. Both prefetch
modes remain disabled by default.

The captured hot 0 A.D. shader reads a few fields from widely separated image
and sampler descriptors. Copying whole bounding intervals consumes uniforms
for unused bytes. Compact spans avoid that transfer without specializing
runtime descriptor values, removing bounds checks, or changing image quality.
The ARM64 build and host NIR tests pass, including disjoint sets, halfwords,
large gaps, dynamic-index fallback and exhausted allocation budgets.

All 32 pinned graphics descriptor CTS cases Pass: immutable/mutable samplers,
primary/secondary command buffers, copies, multiple and discontiguous sets,
update templates, push bindings, and dynamic-offset layout reuse. The 200-loop
native game replay preserves the exact saved image hash in results.json.
Live unchanged 3024x1964 Vulkan is 94.125 FPS versus 92.417 with wide prefetch
in the preceding sample. This small difference does not establish a sustained
speedup or complete the 120 FPS goal. Normal game/desktop Mesa is unchanged.

All hardware experiments completed the M3 60-second watchdog and five-channel
thermal guard (0.5-second sampling, 95 C stop). Raw traces, binaries, logs and
screenshots remain private under `build/m3-0ad-120/0ad-120-{compact-descriptors-replay,compact-descriptor-cts,vk-compact-descriptors,vk-descriptor-prefetch}`.
