# Canonical ordinary buffer descriptors

Current default changed after full-suite regression testing: see
[buffer-address-regression-20260926.md](buffer-address-regression-20260926.md).
The measurements below are historical and do not qualify the current default.

Native ordinary UBO/SSBO descriptors now name their allocation's immutable
backing address with a private bit-63 tag. Memory lowering strips that tag and
skips the software alias-table walk. Sparse descriptors, raw device addresses
and descriptor-buffer addresses retain the full translation path. Descriptor
sizes, robustness checks and sparse residency checks are unchanged. The private
tag is never exposed through the buffer-device-address API; the optional native
prefetch path strips it before issuing memory operations.

Validation for the runtime-tag checkpoint:

- Host alias/residency tests pass, including tagged and untagged loads, stores
  and atomics with an alias table mapping the same numeric address elsewhere.
- M4 strict sparse oracle: 40/40 configurations pass; CPU, transfer and shader
  alias producers also pass. Evidence: `20260925T173617931431Z-native-direct-buffer-sparse`
  and `20260925T173704335446Z-native-direct-buffer-alias`.
- Frozen Satisfactory native Vulkan gameplay completes on both GPUs, with
  exact RGB agreement at frames 1, 16 and 31 against the retained reference.
  M4: `20260925T172903955985Z-satisfactory-native-direct-buffer`.
  M3: `20260925T173857540904Z-m3-satisfactory-vulkan-gameplay-check`.
- Three uninstrumented M3 timing runs are retained in the private benchmark
  repository under `20260925T1742*` and `20260925T1743*`. Approximately 22.1 s
  per 30 completed frames versus 25.73 s before this optimization series.

This is targeted regression evidence, not a full Vulkan conformance claim.
Further compile-time descriptor provenance optimization is a separate experiment.
