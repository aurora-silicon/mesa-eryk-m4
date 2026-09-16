# M4 sparse alias visibility (2026-09-16)

G16G now advertises sparseResidencyAliased and uses the read-only sparse
buffer alias for reads. The former wrong-data failures came from missing
cache publication, not incorrect physical page mappings.

A standalone oracle binds one allocation at its ordinary VA and a second
sparse VA. CPU initialization reads correctly through both; a GPU fill
followed by an aliased read returned the old CPU bytes. Preserving separate
firmware commands makes that probe pass. This isolates the distinction:
CDM's post-dispatch barrier alone does not publish writes to physical aliases;
the firmware completion flush does.

- Preserve G16G command boundaries for memory dependencies with source writes.
  Execution-only dependencies may still merge. The existing pipeline-barrier
  path already ends compute and graphics streams, but EndCommandBuffer had
  merged the compute streams back together.
- Record buffer-copy destinations in firmware flush attachments, as fills
  already do. A separate-copy submission without these ranges left aliased
  reads stale after its fence.
- Retain both streams' compute attachment ranges during merging. Coalesce
  their union and refuse merging if it exceeds the kernel's attachment limit;
  a rejected merge preserves both original lists.
- Restore reads through the read-only alias, which supplies the zero page for
  unbound buffer reads, and enable sparseResidencyAliased on qualified G16G.

The boundaries are conservative and can increase command count. An in-stream
flush with equivalent alias visibility remains an optimization opportunity.
The strict residency property and shaderResourceResidency remain disabled;
this change does not claim complete tiled-resource support or Steam readiness.

## Validation

All target work used the 60-second watchdog and six-channel thermal guard,
95 C cutoff, 12 CTS workers, and unchanged 60-second per-case bound.

- All 32 retained alias failures now Pass with default driver settings.
- All 14,319 non-device-group sparse-resource cases were selected from the
  frozen catalog: 6,501 Pass, 7,818 NotSupported, no failures. The skips name
  remaining features/formats. Catalog SHA256:
  `72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.
- Regression: 815 Pass / 186 NotSupported across the unchanged 1,001 cases.
- Host and ARM64 builds pass. Host Asahi suite: 103 Pass and the same 11
  known failures. The attachment test covers coalescing, hardware-limit
  overflow, incomplete coverage, and preserving lists on failed merges.
- Independent alias oracles exercise CPU, transfer and ordinary SSBO shader
  producers. Every word of the allocation is compared against independent
  expected bytes, including the original data and readback halves.

Compact records are in `sparse-aliasing/`. Full evidence:

- `20260916T073618479266Z-dx-sparse-cpu-alias` (baseline Pass)
- `20260916T073638271391Z-dx-sparse-gpu-alias` (baseline stale read)
- `20260916T073843783540Z-dx-sparse-gpu-alias-split` (split commands Pass)
- `20260916T074350231003Z-dx-sparse-shader-alias` (shader producer Pass)
- `20260916T074400842944Z-dx-sparse-full-r1` (peak 66.75 C)
- `20260916T075033682779Z-dx-sparse-reg1000`
- `20260916T075151200714Z-dx-alias-final`

Paths are under workspace `_provenance/experiments/`. Earlier diagnostic
`dx-sparse-gpu-alias-nomerge` used the wrong environment variable and still
merged; only `ASAHI_MESA_DEBUG=nomerge` selects the driver switch.

## Reproduction

`tools/sparse-alias-cases.py NEW_DIRECTORY` creates three shaders, input and
expected bytes and a bounded child runner. Compile the `.comp` files with
`glslangValidator -V`, and build `tools/vk-buffer-oracle.c` for ARM64 with the
existing sysroot and `-ldl`. Run generated `run.py OUTPUT ORACLE` only through
`tools/gpu-experiment.py`, with `/root/run-m4-gl.py` providing the private ICD
environment. `M4_ORACLE_SPARSE_ALIAS`, `M4_ORACLE_SPARSE_FILL` and
`M4_ORACLE_SPARSE_SHADER` select the oracle modes; they require two bindings
and the advertised sparse alias feature. The library has no diagnostic knob
or case-specific behavior for these tests.
