# September 18 Vulkan CTS random-sample fixes

Original sample: 10,000 frozen Vulkan 1.4.6.2 cases, seed 2026091801,
catalog SHA256 72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf.
Original results and every interrupted attempt remain in
`build/cts-20260918-reboot-10k/`; this follow-up does not replace those outcomes.

## Descriptor pool padding

`pipeline.monolithic.descriptor_limits.compute_shader.samplers_1024` failed
allocation with VK_ERROR_OUT_OF_POOL_MEMORY. Gen9 sampled descriptors occupy
76 bytes, but distinct bindings require 8-byte alignment, yielding 80-byte
spacing. Pool sizing reserved only 76 bytes per descriptor. Round each pool
entry stride up to the pool's maximum binding alignment, with 64-bit arithmetic,
to cover arbitrary layouts without changing the descriptor ABI.

## Sparse binding format queries

`api.info.image_format_properties.1d.optimal.r8g8b8_srgb` rejected required
SPARSE_BINDING combinations because the format has a non-power-of-two block
size. The restriction belongs to sparse residency, which uses texel tiles;
opaque sparse binding uses byte/page ranges and does not require residency
queries. Restrict the rejection to SPARSE_RESIDENCY, preserving all residency
and host-copy restrictions.

## Validation on M4 J713

Both original cases pass under the watchdog and thermal guard:
`20260918T071131028000Z-cts-fixes-api`.
All 414 selected monolithic descriptor-limit and RGB image-format query cases
pass: `20260918T071255781618Z-cts-fixes-api-neighbors`, peak 61.5 C,
16.092 seconds of runner execution. Lists, indexed metadata, raw QPA and
library identities are retained under `build/cts-fixes-20260918/` and the
corresponding `_provenance/experiments/` directories.

The memory-model device loss remains outstanding at this checkpoint.

## Register allocation at phi nodes

`ssbo.phys.layout.random.8bit.vector_types.21` aborted in RA validation.
The allocator moved phi destinations one copy at a time. A later move could
then match an already moved phi, losing the original parallel-copy mapping.
Match all copies against each original phi before inserting live-in phis;
handle both SSA and register-only destinations, and skip unallocated phis.

The captured compiler input is retained in `ra-shuffle-input/`. From the
workspace root, build `build/mesa-m4-asahi-host/src/asahi/compiler/agx_shader_replay`
and use `tools/compiler-replay.py <this-directory>/ra-shuffle-input --output NEW`.
The baseline aborts with invalid RA sources; the fixed replay exits zero.
The original hardware case passes, as do all 25 physical 8-bit random vector
SSBO cases on the final library: `20260918T071557271496Z-cts-fixes-ra-final`.
Peak 60.75 C; 2.827 seconds runner time. The 22 LowerParallelCopy, LowerSpill,
and RepairSSA unit tests pass, along with the static spill core test.

A broader `agx_tests` invocation encountered stale expected shuffle registers
and an assertion while printing an intentionally invalid packer operand;
those tests do not exercise this RA path. Their output is retained in
`build/cts-fixes-20260918/compiler-unit-tests.log`; no broad-unit-suite pass
is claimed.
