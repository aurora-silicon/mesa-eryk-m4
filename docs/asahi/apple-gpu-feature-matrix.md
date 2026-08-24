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

The AGX3 pack subset currently covers only individually grounded instructions;
it is not a general shader encoder. In particular, the generated AGX3
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
| G17P | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL/dual submission plus recovery-word validator; no runtime bring-up |
| G17A | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL routed; submission unknown; no runtime bring-up |
| G17G | **IV/FC** — native Gen10 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen10 textures + native Gen5 sampler heaps; PBE/commands gated | **IV/FC** — Gen10 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen5 BVH helpers tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL300/SKSM plus bounded boot and recovery models; preflight closed |
| G17S | **IV/FC** — native Gen10 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen10 textures + native Gen5 sampler heaps; PBE/commands gated | **IV/FC** — Gen10 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen5 BVH helpers tested; traversal missing; Tier 0 | **RE/FC** — diagnostic preflight plus bounded GFX1 power/recovery validators; refuses before hardware ownership |
| G17C | **IV/FC** — native Gen10 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen10 textures + native Gen5 sampler heaps; PBE/commands gated | **IV/FC** — Gen10 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen5 BVH helpers tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL300/SKSM plus bounded boot models; preflight closed |

Honeykrisp does not advertise Vulkan ray-tracing extensions for these AGX3
identities. The D3D12 driver and WDDM UMD continue to report
`D3D12_RAYTRACING_TIER_NOT_SUPPORTED`; host-side DXR validation and BVH helpers
must not be read as DXR execution support.

Linux DRM development lives outside this Mesa repository in the separate
`linux-asahi-m3-m5-drm` worktree. Its G15-G17 tables and bounded G17 codecs are
diagnostic foundations only: initdata, RTKit transport, power sequencing,
recovery, topology, UAT handoff, and command submission remain runtime gates.
The power/recovery ledger reflects Linux commit `362b3ab60ab0`; it validates
only evidenced state transitions and does not perform MMIO or firmware writes.

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
  generation-sized Honeykrisp image records and fail-closed AGX3 policy.
- [`hk_native_sampler.h`](../../src/asahi/vulkan/hk_native_sampler.h), its
  [packing helper](../../src/asahi/vulkan/hk_native_sampler.c), and
  [focused test](../../src/asahi/vulkan/tests/test-hk-native-sampler.c): exact
  per-chip sampler words, heap strides, sparse tails, and reduction gates.
- [`agx_native_descriptor.h`](../../src/gallium/drivers/asahi/agx_native_descriptor.h),
  its [packer](../../src/gallium/drivers/asahi/agx_native_descriptor.c), and
  [focused test](../../src/gallium/drivers/asahi/tests/test-agx-native-descriptor.c):
  generation-sized Gallium texture heaps and deliberately closed AGX3 PBE
  paths.
- [`d12_raytracing_core.h`](../../src/asahi/d3d12/d12_raytracing_core.h) and its
  [host tests](../../src/asahi/d3d12/tests/d12_raytracing_core_tests.c): DXR
  host contracts, explicit Tier-0 rationale, and bounded BVH helpers.

Any feature commit that changes an identity, generation axis, runtime gate, or
API capability must update this matrix in the same change. Promote a status
only with a linked evidence artifact and a focused test result; record a new
unknown as **FC** rather than inferring it from a neighboring chip. Runtime
support requires compiler, descriptors, samplers, command stream, kernel UAPI,
power/boot, and submission to be ready together.
