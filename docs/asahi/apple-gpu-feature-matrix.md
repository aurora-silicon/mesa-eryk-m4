# Apple AGX / Asahi feature matrix

This is the branch-local support ledger for Apple GPUs represented by Mesa's
`enum agx_chip`. It records implemented paths, grounded reverse engineering,
and deliberate fail-closed gates separately. It is not a hardware capability,
conformance, or release-support claim.

Status vocabulary:

- **RS — runtime-supported:** Mesa's device-open gate admits this identity and
  the named path is implemented. This is not a per-chip conformance claim.
- **IV — implemented + validated, driver-gated:** code and focused tests exist,
  but another required axis keeps device open disabled.
- **RE — RE-grounded only:** evidence is represented in tables, decoders, or
  bounded helpers; it is not a complete runtime path.
- **FC — unknown / fail-closed:** the required ABI is unknown or the code
  explicitly refuses to use it.

`G13X` and `G14X` are Mesa's aggregate multi-cluster identities. The marketing
mapping below follows the recovered Apple core-name table; `G17A` has no known
marketing name in the measured corpus.

## Hardware and encoding axes

| Mesa identity | Marketing mapping | ISA / encoding | Compiler pack / decode | Texture/PBE descriptor | Sampler / heap element | Command stream / submission |
|---|---|---|---|---|---|---|
| G13G | M1 | AGX2 / `agx2` | **RS** — full compiler and decoder | **RS** — Gen7, 24 B | **RS** — Gen4, 8 B | **RS** — AGX2 / classic |
| G13X | M1 Pro, Max, Ultra | AGX2 / `agx2` | **RS** — full compiler and decoder | **RS** — Gen7, 24 B | **RS** — Gen4, 8 B | **RS** — AGX2 / classic |
| G14G | M2 | AGX2 / `agx2` | **RS** — full compiler and decoder | **RS** — Gen8, 24 B | **RS** — Gen4, 8 B | **RS** — AGX2 / classic |
| G14X | M2 Pro, Max, Ultra | AGX2 / `agx2` | **RS** — full compiler and decoder | **RS** — Gen8, 24 B | **RS** — Gen4, 8 B | **RS** — AGX2 / classic |
| G15P | A16 Bionic | AGX2 / `agx2` | **IV/FC** — AGX2 codegen exists; device ABI does not | **FC** — unknown | **FC** — unknown, 0 B | **FC** — unknown / unknown |
| G15G | M3 | AGX3 / `agx3b` | **RE** — disassembler + tested pack subset; scheduling stalls grounded | **IV** — Gen9, 32 B | **IV** — Gen4, 32 B | **RE** — G15 / classic |
| G15S | M3 Pro | AGX3 / `agx3b` | **RE** — disassembler + tested pack subset | **IV** — Gen9, 32 B | **IV** — Gen4, 32 B | **RE** — G15 / classic |
| G15C | M3 Max | AGX3 / `agx3b` | **RE** — disassembler + tested pack subset | **IV** — Gen9, 32 B | **IV** — Gen4, 32 B | **RE** — G15 / classic |
| G15D | M3 Ultra | AGX3 / `agx3b` | **RE** — disassembler + tested pack subset | **IV** — Gen9, 32 B | **IV** — Gen4, 32 B | **RE** — G15 / classic |
| G16P | Phone part; marketing mapping ungrounded | AGX3 / `agx3b` | **RE** — disassembler + tested pack subset | **IV** — Gen9, 32 B | **IV** — Gen4 HAL200, 32 B | **RE/FC** — HAL200 / submission unknown |
| G16G | M4 | AGX3 / `agx3b` | **RE** — disassembler + tested pack subset | **IV** — Gen9, 32 B | **IV** — Gen4 HAL200, 32 B | **RE** — HAL200 / classic |
| G16S | M4 Pro | AGX3 / `agx3b` | **RE** — disassembler + tested pack subset | **IV** — Gen9, 32 B | **IV** — Gen4 HAL200, 32 B | **RE** — HAL200 / classic + SKSM |
| G16C | M4 Max | AGX3 / `agx3b` | **RE** — disassembler + tested pack subset | **IV** — Gen9, 32 B | **IV** — Gen4 HAL200, 32 B | **RE** — HAL200 / classic + SKSM |
| G17P | A18 Pro | AGX3 / `agx3b` | **RE** — disassembler + tested pack subset | **IV** — Gen9, 32 B | **IV** — Gen4 HAL200, 32 B | **RE** — HAL200 / classic + SKSM |
| G17A | A18-class, marketing unknown | AGX3 / `agx3b` | **RE** — disassembler + tested pack subset | **IV** — Gen9, 32 B | **IV** — Gen4 HAL200, 32 B | **RE/FC** — HAL200 / submission unknown |
| G17G | M5 | AGX3 / `agx3c` | **RE** — disassembler + tested pack subset | **IV** — Gen10, 32 B | **IV** — Gen5, 32 B | **RE** — HAL300 / SKSM |
| G17S | M5 Pro | AGX3 / `agx3c` | **RE** — disassembler + tested pack subset | **IV** — Gen10, 32 B | **IV** — Gen5, 32 B | **RE** — HAL300 / SKSM |
| G17C | M5 Max | AGX3 / `agx3c` | **RE** — disassembler + tested pack subset | **IV** — Gen10, 32 B | **IV** — Gen5, 32 B | **RE** — HAL300 / SKSM |

The AGX3 pack subset currently covers only individually grounded instructions:
`ldimm.u32`, canonical scheduler `nop`, workgroup barrier, shader `end`, thread
enable, the zero-argument `update_node` expansion, and 32-bit register-plus-u8
`iadd` plus the canonical three-register form (AGX3b `iadd`, AGX3c `iadd32`),
canonical three-register u32 subtraction (AGX3b `isub`, AGX3c `isub32`),
canonical three-register u32 `bit_and`, `bit_or`, and `bit_xor`, and unary u32
`bit_not` (`inv`), plus canonical three-register u32 variable shift-left
(`sll`) and i32 arithmetic variable shift-right (`sra`, Mesa `asr`). The
subset also covers u32 logical variable shift-right (`srl`, Mesa `lsr`). The
arithmetic, bitwise, and shift forms have exhaustive operand and every-bit
rejection tests plus exact G15/G16/G17 corpus replay; the separate shift-count
mask and G15G stall placement remain unsolved
scheduler rule. This is not a general shader encoder. In particular,
the generated AGX3
disassemblers and hardware-RT detection do not make AGX3 code generation or
device open supported.

## Mesa API and kernel relationship

| Mesa identity | Gallium / OpenGL | Honeykrisp / Vulkan | D3D12 AGX / WDDM UMD | DXR | Linux DRM relationship |
|---|---|---|---|---|---|
| G13G | **RS** — AGX2 path | **RS** — AGX2 path | **RS** — AGX2 execution; UMD contracts build-tested | **FC** — no hardware RT; Tier 0 | **RS** — existing `drm/asahi` path |
| G13X | **RS** — AGX2 path | **RS** — AGX2 path | **RS** — AGX2 execution; UMD contracts build-tested | **FC** — no hardware RT; Tier 0 | **RS** — existing `drm/asahi` path |
| G14G | **RS** — AGX2 path | **RS** — AGX2 path | **RS** — AGX2 execution; UMD contracts build-tested | **FC** — no hardware RT; Tier 0 | **RS** — existing `drm/asahi` path |
| G14X | **RS** — AGX2 path | **RS** — AGX2 path | **RS** — AGX2 execution; UMD contracts build-tested | **FC** — no hardware RT; Tier 0 | **RS** — existing `drm/asahi` path |
| G15P | **FC** — device axes unknown | **FC** — device axes unknown | **FC** — AGX2 shader code only; device gate closed | **FC** — no hardware RT; Tier 0 | **RE/FC** — identity routed; no runtime bring-up |
| G15G | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native Gen4 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — identity, 42-bit UAT axis, and recovery-word validator; no runtime bring-up |
| G15S | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native Gen4 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — identity and 42-bit UAT axis routed; no runtime bring-up |
| G15C | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native Gen4 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — identity and 42-bit UAT axis routed; no runtime bring-up |
| G15D | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native Gen4 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — identity and 42-bit UAT axis routed; no runtime bring-up |
| G16P | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — identity/HAL routed; submission unknown; no runtime bring-up |
| G16G | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — identity/HAL/submission routed plus recovery-word validator; no runtime bring-up |
| G16S | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — identity/HAL/dual submission routed; no runtime bring-up |
| G16C | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — identity/HAL/dual submission routed; no runtime bring-up |
| G17P | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL/dual submission, recovery validator, and opaque HAL200 PM block; no runtime bring-up |
| G17A | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL routed; submission unknown; no runtime bring-up |
| G17G | **IV/FC** — native Gen10 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen10 textures + native Gen5 sampler heaps; PBE/commands gated | **IV/FC** — Gen10 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen5 BVH helpers tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL300/SKSM, opaque HAL300 PM block, and bounded boot/recovery models; preflight closed |
| G17S | **IV/FC** — native Gen10 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen10 textures + native Gen5 sampler heaps; PBE/commands gated | **IV/FC** — Gen10 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen5 BVH helpers tested; traversal missing; Tier 0 | **RE/FC** — HAL300 PM block, named PIO prefixes, six-op pre-idle model, and Fender idle poll validated; refuses before hardware ownership |
| G17C | **IV/FC** — native Gen10 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen10 textures + native Gen5 sampler heaps; PBE/commands gated | **IV/FC** — Gen10 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen5 BVH helpers tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL300/SKSM plus bounded boot models; preflight closed |

Honeykrisp does not advertise Vulkan ray-tracing extensions for these AGX3
identities. The D3D12 driver and WDDM UMD continue to report
`D3D12_RAYTRACING_TIER_NOT_SUPPORTED`; host-side DXR validation and BVH helpers
must not be read as DXR execution support.

The D3D12 RT core recognizes the byte-exact, fixed zero-argument `update_node`
operation for AGX3b and AGX3c. This is **RE/IV** encoding validation only: it
does not provide the preceding generation-specific barrier, BVH-update
semantics, operand-bearing RT instructions, traversal, lowering, submission,
or device admission, so DXR remains Tier 0.

Honeykrisp also has an **IV** native Gen9/Gen10 hardware-PBE word packer,
including a byte-exact M5 live descriptor. Writable-image and EOT use stays
**FC** because libagx still decodes the AGX2-only software-addressing sideband;
the matrix's PBE gate refers to that complete execution contract.

Gallium and Honeykrisp's native sampled-texture helpers also represent the
recovered Gen10 2x/4x/8x sample-count field, including the exact M5 8x value in
bits 58:59. This is **RE/IV** descriptor coverage, not OpenGL/Vulkan 8x MSAA
support: the live M5 corpus proves creation, layout, and descriptor bytes but
contains no executed sampling command, while the recovered raster/PBE path
remains capped at 4x. Public 8x exposure therefore stays **FC**. D3D12 likewise
keeps 8x quality levels at zero because its resource contract has no
sampled-only multisample usage that would avoid promising an unproven 8x
render/depth path.

AIL also carries tested M5 allocation-policy primitives for the measured
2 KiB small-array compression-metadata granule, 16 KiB large-array granule,
unpadded single-layer rule, and 16x8 metadata tiles at 16 B/px. Gallium,
Honeykrisp, and D3D12 sizing/creation select this policy only for descriptor
generation 10 (G17G/S/C); 3D allocation keeps the existing policy because the
live corpus grounds 2D images. The M5 measurements are deliberately not
inferred for M3/M4 or applied to runtime-supported AGX2 devices.

Across the table, the public D3D12 command-list path implements
`DiscardResource`: it is **RS** on admitted G13/G14 devices and **IV** on the
driver-gated G15-G17 identities. Accepted calls conservatively retain resource
contents and metadata, which is valid for a discard hint; no new AGX encoding
or capability gate is implied. Legacy RT/DS/UAV state preconditions remain an
application/debug-layer contract because this command core does not track
legacy resource states.

The admitted AGX2 D3D12 path also reports Options13
`AlphaBlendFactorSupported`: blend modes 20/21 map to constant alpha and its
inverse, the fragment epilog consumes component A of `OMSetBlendFactor`, and
command-list reset now restores the specified `{1,1,1,1}` default. This is
**RS** on admitted G13/G14 and **IV** on driver-gated G15-G17; it changes no
AGX3 execution or DXR capability gate.

The D3D12 compiler side has an **IV** fail-closed bridge for canonical
three-register u32 `iadd`, `isub`, `iand`, `ior`, and `ixor`, plus unary u32
`inot`, register-register `ishl`, and signed register-register `iashr`, into the
shared AGX3b/AGX3c packer. It also maps unsigned register-register `ilshr`.
The shift bridge represents only the grounded `sll`, `sra`, and `srl`
operations and does not emit or claim the compiler-observed count mask. It
replays the exact encoding witnesses but does not admit an AGX3 device:
general instruction selection, scheduling, linking, and submission are still
missing.

Linux DRM development lives outside this Mesa repository in the separate
`linux-asahi-m3-m5-drm` worktree. Its G15-G17 tables and bounded G17 codecs are
diagnostic foundations only: initdata, RTKit transport, power sequencing,
recovery, topology, UAT handoff, and command submission remain runtime gates.
The power/recovery ledger through Linux commit `c11d6008316a` additionally
validates the exact G17S `GPCAICSWInt`,
`PushTelemetryDashboardConfigSpace`, and `FenderRegsMCW` PIO identities,
0x28-byte descriptor indexing, ordered PIO values, the six-operation pre-idle
state/synchronization model, and the Fender idle-poll predicate. It does not
perform MMIO or firmware writes: the first two types' physical mappings,
opaque helper ownership, and G17G/G17P firmware parity remain fail-closed.
It also records the exact opaque six-u64 PM blocks for pinned G17P/HAL200 and
G17G/G17S HAL300 hosts without assigning field semantics or extrapolating to
G17A/G17C. For T6050/G17S it validates the measured 14-state core/SRAM
performance tables and their two mirrored firmware properties; rail ownership,
voltage control, a third domain, and hardware programming remain unresolved.
It also requires the exact tag, UUID, and SHA-256 jointly for each pinned
G17S/G17C GFX/GFX1 nested-firmware identity. Other G17 variants, FTAB
extraction, fixups, loading, and live G17C ownership remain fail-closed.
The recovery-word grammar is now location-scoped too: G15G/G16G accept only
mapped-state offset `0x5190`, while independently pinned G17P/G17G/G17S accept
only `0x4ae0`. These are pure decoders, not firmware-state mappings or MMIO.

## Evidence and update policy

The current in-tree anchors are:

- [`agx_chip.c`](../../src/asahi/lib/agx_chip.c) and
  [`test-chip.c`](../../src/asahi/lib/tests/test-chip.c): identity, ISA,
  descriptor, sampler, command-stream, submission, RT, and device-open gates.
- [`AGX3b.xml`](../../src/asahi/isa/AGX3b.xml),
  [`AGX3c.xml`](../../src/asahi/isa/AGX3c.xml), and
  [`test-agx3-pack.c`](../../src/asahi/compiler/test/test-agx3-pack.c): AGX3
  decode tables and the bounded pack subset.
- [`d12_native_descriptor_core.h`](../../src/asahi/d3d12/d12_native_descriptor_core.h)
  and its
  [host tests](../../src/asahi/d3d12/tests/d12_native_descriptor_core_tests.c):
  generation-sized D3D12 heaps and fail-closed software-PBE rules.
- [`hk_native_descriptor.h`](../../src/asahi/vulkan/hk_native_descriptor.h),
  its [packing helper](../../src/asahi/vulkan/hk_native_descriptor_pack.c), and
  [focused test](../../src/asahi/vulkan/tests/test-hk-native-descriptor.c):
  generation-sized Honeykrisp image records, native Gen9/10 PBE hardware
  words, and the fail-closed AGX3 software-sideband policy.
- [`hk_native_sampler.h`](../../src/asahi/vulkan/hk_native_sampler.h), its
  [packing helper](../../src/asahi/vulkan/hk_native_sampler.c), and
  [focused test](../../src/asahi/vulkan/tests/test-hk-native-sampler.c): exact
  per-chip sampler words, heap strides, sparse tails, and reduction gates.
- [`agx_native_descriptor.h`](../../src/gallium/drivers/asahi/agx_native_descriptor.h),
  its [packer](../../src/gallium/drivers/asahi/agx_native_descriptor.c), and
  [focused test](../../src/gallium/drivers/asahi/tests/test-agx-native-descriptor.c):
  generation-sized Gallium texture heaps, bounded Gen10 sample-count
  representation, and deliberately closed AGX3 PBE paths.
- [`layout_core.h`](../../src/asahi/layout/layout_core.h) and
  [`test-agx3-layout.c`](../../src/asahi/layout/tests/test-agx3-layout.c): M5
  MSAA and bounded allocation-policy primitives with a generation-10-only
  selector.
- [`d12_blend_core.h`](../../src/asahi/d3d12/d12_blend_core.h) and its
  [host tests](../../src/asahi/d3d12/tests/d12_blend_core_tests.c): Options13
  alpha-factor mapping, inversion, default-state, and capability contracts.
- [`d12_agx3_isa_core.h`](../../src/asahi/d3d12/d12_agx3_isa_core.h) and its
  [host tests](../../src/asahi/d3d12/tests/d12_agx3_isa_core_tests.c): bounded
  D3D scalar `iadd`/`isub`/`iand`/`ior`/`ixor`/`inot`/`ishl`/`iashr`/`ilshr`
  mapping into exact AGX3b/AGX3c encodings.
- [`d12_raytracing_core.h`](../../src/asahi/d3d12/d12_raytracing_core.h) and its
  [host tests](../../src/asahi/d3d12/tests/d12_raytracing_core_tests.c): DXR
  host contracts, explicit Tier-0 rationale, bounded BVH helpers, and exact
  validation of the fixed AGX3b/AGX3c `update_node` opcode.
- [`d12_discard_core.h`](../../src/asahi/d3d12/d12_discard_core.h) and its
  [host tests](../../src/asahi/d3d12/tests/d12_discard_core_tests.c): public
  `DiscardResource` list/region validation and the conservative no-op contract.

Any feature commit that changes an identity, generation axis, runtime gate, or
API capability must update this matrix in the same change. Promote a status
only with a linked evidence artifact and a focused test result; record a new
unknown as **FC** rather than inferring it from a neighboring chip. Runtime
support requires compiler, descriptors, samplers, command stream, kernel UAPI,
power/boot, and submission to be ready together.
