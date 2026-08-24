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
enable, the zero-argument `update_node` expansion, the G17S-only fixed
`update_bvh` witness, the fixed first-site hardware `continue_ray` witness, and
the canonical no-kill, scheduler-neutral i16-register `yield`, plus 32-bit
register-plus-u8 `iadd` and the canonical three-register form (AGX3b
`iadd`, AGX3c `iadd32`),
canonical three-register u32 subtraction (AGX3b `isub`, AGX3c `isub32`),
canonical three-register u32 `bit_and`, `bit_or`, and `bit_xor`, and unary u32
`bit_not` (`inv`), plus canonical three-register u32 variable shift-left
(`sll`) and i32 arithmetic variable shift-right (`sra`, Mesa `asr`). The
subset also covers u32 logical variable shift-right (`srl`, Mesa `lsr`) and
the low-u32 register multiply selected by an implicit-zero addend in AGX3b
`imadd` and AGX3c `imadd32`, plus the byte-distinct four-register low-u32
multiply-add selection with a real register addend. It also packs the exact
six-byte canonical f32 `add` emitted by the native compiler, restricted to
even half-register indices 0--254 on the nine witnessed AGX3b/AGX3c targets.
The arithmetic, bitwise, shift, multiply, multiply-add, and float-add forms
have exhaustive operand and every-bit rejection tests plus exact G15/G16/G17
corpus replay; the separate shift-count mask and G15G stall placement remain
an unsolved scheduler rule. This is not a general shader encoder. In
particular, the generated AGX3 disassemblers and hardware-RT detection do not
make AGX3 code generation or device open supported.

The `yield` form is grounded by controlled source ladders and independent
runtime-value probes on G15G/G15S/G15D, G16P/G16G/G16S, G17P, and G17G/G17S.
It accepts AGX3b half-registers 0--191 and AGX3c half-registers 0--255. Source
last-use/kill state, surrounding scheduler context, immediate/alternate source
forms, AGX3c's unprobed ninth register bit, and every unknown field remain
closed. G15P fails native instruction selection, while G15C/G16C/G17A/G17C
remain unwitnessed; the packer therefore establishes an exact instruction
encoding only, not lowering, scheduling, submission, or device admission.

The shared packer also models the exact terminal `end` carrier for literal
`llvm.agx3.wait.fence` dependencies. Consecutive waits form an idempotent,
order-independent bit set: AGX3b carries slots 0--5 in `end` bits 10--15,
while AGX3c additionally carries slots 6--7 in bits 17--18. Exhaustive native
compiler replay covers all 64 AGX3b and 256 AGX3c masks, with independent
Apple-table differential agreement; the AGX3b bits corresponding to slots 6
and 7 are table gaps and compiler-rejected. The proved target set is the same
seven AGX3b and two AGX3c identities as canonical `yield`. A wait placed before
a store or `yield` is consumed by that nonterminal carrier instead, so this
helper deliberately exposes only `end`-attached masks and does not claim a
standalone wait instruction, arbitrary scheduling mutation, memory ordering,
lowering, execution, submission, or device admission.

The D3D12 ISA core now maps that terminal wait mask through the same shared
packer on exactly the compiler-witnessed targets. It accepts masks 0--0x3f on
the seven AGX3b identities and 0--0xff on G17G/G17S, rejects every unsupported
identity and distinguishable wrong form, and clears rejected outputs. The
low-six-slot bytes are genuinely shared between AGX3b and AGX3c, so their
generation cannot be inferred from those four bytes alone. This remains an
**IV/RE** host encoding bridge, not D3D/DXIL lowering, scheduling, memory-order
policy, execution, device admission, or a capability claim.

## Mesa API and kernel relationship

| Mesa identity | Gallium / OpenGL | Honeykrisp / Vulkan | D3D12 AGX / WDDM UMD | DXR | Linux DRM relationship |
|---|---|---|---|---|---|
| G13G | **RS** — AGX2 path | **RS** — AGX2 path | **RS** — AGX2 execution; UMD contracts build-tested | **FC** — no hardware RT; Tier 0 | **RS** — existing `drm/asahi` path |
| G13X | **RS** — AGX2 path | **RS** — AGX2 path | **RS** — AGX2 execution; UMD contracts build-tested | **FC** — no hardware RT; Tier 0 | **RS** — existing `drm/asahi` path |
| G14G | **RS** — AGX2 path | **RS** — AGX2 path | **RS** — AGX2 execution; UMD contracts build-tested | **FC** — no hardware RT; Tier 0 | **RS** — existing `drm/asahi` path |
| G14X | **RS** — AGX2 path | **RS** — AGX2 path | **RS** — AGX2 execution; UMD contracts build-tested | **FC** — no hardware RT; Tier 0 | **RS** — existing `drm/asahi` path |
| G15P | **FC** — device axes unknown | **FC** — device axes unknown | **FC** — AGX2 shader code only; device gate closed | **FC** — no hardware RT; Tier 0 | **RE/FC** — identity routed; no runtime bring-up |
| G15G | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native Gen4 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity, 42-bit UAT axis, and recovery-word validator; no runtime bring-up |
| G15S | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native Gen4 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity, 42-bit UAT axis, and recovery-word validator; no runtime bring-up |
| G15C | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native Gen4 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity, 42-bit UAT axis, and recovery-word validator; no runtime bring-up |
| G15D | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native Gen4 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity, 42-bit UAT axis, and recovery-word validator; no runtime bring-up |
| G16P | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity/HAL routed; submission unknown; no runtime bring-up |
| G16G | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity/HAL/submission routed plus recovery-word validator; no runtime bring-up |
| G16S | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity/HAL/dual submission and recovery-word validator; no runtime bring-up |
| G16C | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity/HAL/dual submission and recovery-word validator; no runtime bring-up |
| G17P | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL/dual submission, recovery/transition validators, HAL200 PM/CAS helper and physical PIO map; no runtime bring-up |
| G17A | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL routed; submission unknown; no runtime bring-up |
| G17G | **IV/FC** — native Gen10 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen10 textures + native Gen5 sampler heaps; PBE/commands gated | **IV/FC** — Gen10 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB + Gen5 BVH helpers tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL300/SKSM, HAL300 PM/CAS helper, transition/poll and physical PIO models, bounded boot/recovery; preflight closed |
| G17S | **IV/FC** — native Gen10 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen10 textures + native Gen5 sampler heaps; PBE/commands gated | **IV/FC** — Gen10 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB + Gen5 BVH helpers tested; traversal missing; Tier 0 | **RE/FC** — HAL300 PM block, direction-correct PIO prefixes, CAS/six-op pre-idle model, Fender idle poll, and physical PIO map validated; refuses before hardware ownership |
| G17C | **IV/FC** — native Gen10 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen10 textures + native Gen5 sampler heaps; PBE/commands gated | **IV/FC** — Gen10 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB + Gen5 BVH helpers tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL300/SKSM plus bounded boot models; preflight closed |

Honeykrisp does not advertise Vulkan ray-tracing extensions for these AGX3
identities. The D3D12 driver and WDDM UMD continue to report
`D3D12_RAYTRACING_TIER_NOT_SUPPORTED`; host-side DXR validation and BVH helpers
must not be read as DXR execution support.

The shared RT library and D3D12 host core now decode the RIA Gen1 64-byte node
header used by BVH versions 1--4 on the M3/M4 and A18 Pro paths. The D3D12
wrapper dispatches by the blob's version rather than GPU identity because an
M5 Pro public-builder capture also emitted version 3. It covers the special
all-inner and all-leaf BVH8 control endpoints, inner-child and leaf-box counts,
indexed versus extent leaf modes, the 4+2-bit leaf-lane ends, rounded leaf-storage
extent, child-space and inner-child offsets, leaf type, and all six raw 7-bit
quantized-bound components for each of eight children. The bounds extractor
matches Apple's `RIANodeHeaderGen1::getQuantizedChildBounds` over 8,000,000
randomized headers / 64,000,000 child vectors with zero mismatches; the other
fields are checked against an independent instruction-order model of Apple's
Gen1 extraction loop. A separate exact decoder covers the 64-byte Gen1
hardware triangle leaf: marker `0x50`, twelve SoA f32 vertex coordinates, and
the raw plus low-30-bit geometry/primitive ID words. The pinned M5 Pro
version-3 capture walks nine nodes to 64 such leaves, recovers primitive IDs
0--63 exactly once, and reproduces every source vertex. A second exact M5 Pro
decoder covers the fixed 48-byte Gen1 hardware AABB leaf. Controlled public-
Metal captures prove its low-byte `0x18` marker, upper-24-bit intersection-
function-table offset, min/max XYZ f32 bounds, low-30-bit geometry ID,
high-30-bit primitive ID, opaque flag, and the high-word bit-30 gate for a
signed relative primitive-data value at byte 40. The complete packed ID/flag
word and both still-unassigned tail words remain available losslessly. One-
and four-AABB vectors are tested directly; the four records are byte-identical
across default, extended, fast-build, and refit-capable build options, and
controlled two-geometry and opaque probes independently correlate the named
fields. This decoder does not generalize to unobserved curve leaves. Geometric
quantized-component names, node origin/scale semantics, other leaf formats,
traversal lowering, building, submission, Vulkan RT extensions, and DXR
execution remain **FC**. The existing RIA Gen5 decoder continues to cover the
distinct M5 node-header child layout.

The D3D12 RT core generates and validates the byte-exact, fixed zero-argument
`update_node` expansion for AGX3b and AGX3c through Mesa's shared packer,
including the generation-specific six-byte barrier and trailing operation.
This is **RE/IV** encoding validation only: it does not establish barrier or
BVH-update semantics, operand-bearing RT instructions, traversal, lowering,
submission, or device admission, so DXR remains Tier 0.

The shared compiler and D3D12 RT core also generate and validate the exact
G17S-only 14-byte `update_bvh` fixed witness. It is not an operand encoder:
the intrinsic's pointer/index arguments are not correlated to machine fields,
so the bytes are deliberately disconnected from D3D12 acceleration-structure
build execution. G17G, G17C, every AGX3b target, traversal/update semantics,
submission, and DXR capability exposure remain **FC**.

The cross-target `continue_ray` corpus also disproves a one-instruction
interpretation of the compiler's stable per-call deltas. Every source call
lowers to two hardware `continue_ray` instructions plus barriers, LDS traffic,
compares, conditionals, and branches. G17G/G17S use 174-byte non-final and
166-byte final call blocks; G15S/G15D/G16P/G16G/G16S/G17P use 164/154 bytes,
while G15G additionally inserts per-call scheduler `nop` runs and uses 188/178
bytes. A 140-translation matrix varying each of the intrinsic's seven
arguments produces 280/280 exact Apple-table round trips but no corresponding
hardware-instruction bit change: the operands are carried by the surrounding
lowering. G15P fails native selection, while G15C, G16C, G17A, and G17C remain
unwitnessed. The shared packer therefore represents only the exact first-site
AGX3b/AGX3c hardware specimens and rejects every second-site or corrupted
prefix; it exposes neither intrinsic operands nor compiler allocation/finality
fields. No traversal lowering rule, device admission, or API capability is
claimed.

The D3D12 RT core consumes that same fixed witness only on the compiler-proven
target set: AGX3b G15G/G15S/G15D/G16P/G16G/G16S/G17P and AGX3c G17G/G17S.
It rejects both encoding forms on every other identity, including the
same-encoding G15C/G16C/G17A/G17C cases, and still exposes no traversal,
acceleration-structure execution, device admission, or DXR tier.

Honeykrisp also has an **IV** native Gen9/Gen10 hardware-PBE word packer,
including a byte-exact M5 live descriptor. Writable-image and EOT use stays
**FC** because libagx still decodes the AGX2-only software-addressing sideband.
Native width/level, Gen9 address, and sample-selector fields are grounded, but
no native equivalents are identified for aligned-MSAA width, mip offset, tile
dimensions, layer stride, or sub-16-byte buffer offset; Gen10 address bits
also overlap conditional image/page fields, and live YUV descriptors use the
otherwise-unexplained fourth word. The retained corpus submits no texture
atomic or multisampled-store workload, so the matrix's PBE gate refers to that
complete execution contract rather than descriptor construction alone.

Apple's recovered texture-atomic emulation archives establish a different
software ABI, not a native-PBE overlay: G15/G16 share one 288-module LLVM
bitcode payload, G17 uses a distinct payload, and G17P deliberately selects the
G16/HAL200 family. Every texture-heap slot has same-index 32-byte read and PBE
descriptors plus one 8-byte side entry. The compiler loads that entry as i64,
uses its low 32 bits as `agc.texture_heap.texture_buffer_length`, and treats the
high 32 bits as a companion/auxiliary texture-heap slot. Buffer textures put
their texel width in the low word. For eligible Gen9 atomic images the low word
encodes the exact level/plane offset formula, including the split 16-bit 3D or
array form, `0xae99` sentinel branch, and 16 KiB page-aligned branch; the G17
runtime decoder independently consumes those same forms. Gen10 removes
`getExtraTextureMetadata`: all ordinary and atomic image payloads were zero in
a broad 963-line M5 Pro capture, while a 4,096-texel buffer produced low word
`0x1000`. Mode 2's direct i64 at texture+0x288, construction of a nonzero
companion slot, and executed G15/G16 nonzero atomic payloads remain **FC**. A
shared, tested 8-byte side-entry helper now packs and decodes the proven
subset: Gen9/Gen10 buffers, zero-valued ordinary Gen10 images, and the bounded
Gen9 atomic formula. It rejects mode 2, unknown cases, misalignment, and
overflow, so Honeykrisp still has no safe total packer and does not expose
writes or atomics.

Gallium and Honeykrisp's native sampled-texture helpers also represent the
recovered Gen10 2x/4x/8x sample-count field, including the exact M5 8x value in
bits 58:59. This is **RE/IV** descriptor coverage, not OpenGL/Vulkan 8x MSAA
support: the live M5 corpus proves creation, layout, and descriptor bytes but
contains no executed sampling command, while the recovered raster/PBE path
remains capped at 4x. Public 8x exposure therefore stays **FC**. D3D12 likewise
keeps 8x quality levels at zero because its resource contract has no
sampled-only multisample usage that would avoid promising an unproven 8x
render/depth path.

The independently extracted Gen9 sampled-texture selector for G15/G16/G17P is
named centrally in word-0 bits 56:57. Four separate G15/HAL200 image-read
encoders map numeric 2x/4x/8x counts to selectors 0/1/2; this is distinct from
the Gen9 PBE state record. Honeykrisp and Gallium consume the generated field,
while D3D12 additionally cross-checks the selector against the resource's
numeric sample count. This remains **RE/IV** descriptor-only coverage: public
OpenGL/Vulkan/D3D sample counts, executed sampling, raster/PBE support, command
submission, and device admission do not change. D3D12 8x resources and quality
levels remain unavailable.

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
shared AGX3b/AGX3c packer. It also maps unsigned register-register `ilshr` and
the canonical low-u32 `imul` and four-register `imadd` selections, plus the
canonical compact f32 `add` and the no-kill, scheduler-neutral i16-register
`yield` form on exactly the nine compiler-witnessed targets. The
multiply bridges accept only the byte-distinct implicit-zero and real-register
addend forms on the nine compiler-witnessed targets; alternate low selections,
high products, G17C, and other unwitnessed target selections remain closed.
The shift bridge represents only the grounded `sll`, `sra`, and `srl`
operations and does not emit or claim the compiler-observed count mask. These
bridges replay the exact encoding witnesses but do not admit an AGX3 device:
general instruction selection, scheduling, linking, and submission are still
missing.

Linux DRM development lives outside this Mesa repository in the separate
`linux-asahi-m3-m5-drm` worktree. Its G15-G17 tables and bounded G17 codecs are
diagnostic foundations only: initdata, RTKit transport, power sequencing,
recovery, topology, UAT handoff, and command submission remain runtime gates.
The power/recovery ledger through Linux commit `1fd3ed26d2e9` additionally
validates the exact G17S `GPCAICSWInt`,
`PushTelemetryDashboardConfigSpace`, and `FenderRegsMCW` PIO identities,
0x28-byte descriptor indexing, ordered PIO values, the six-operation pre-idle
state/synchronization model, and the Fender idle-poll predicate. Pinned
G17G/G17P images independently validate the same bridge grammar and idle-poll
tuple, but their callbacks omit G17S's direct PIO prefix. The homologous
53-instruction helper on all three targets is now proven to be a firmware-global
compare-and-swap/scheduler helper with no direct PIO access, so it cannot hide
equivalent G17G/G17P writes. The byte audit also corrects the G17S directions:
the two-op prefix is `0x21 -> SLEEP`, while the three-op prefix is
`SLEEP/NAP -> 0x21`. Independently pinned host/ADT evidence derives G17P slot
0 at `0x301014000` and slot `0x24` at `0x270d40000`, plus G17G slot 0 at
`0x381014000` and slot `0x24` at `0x370d40000`; every mapped region is
`0x4000` bytes. Their complete ten-record tables and zeroed configure chains
prove slot `0x0b` is unbound on both targets. The model performs no MMIO or
firmware writes. The pinned G17S host and J714s ADT additionally ground slot 0
at `0x280414000`, the target-configure slot `0x0b` override at `0x284258000`,
and slot `0x24` at `0x2100d40000`; every mapping is `0x4000` bytes. Its complete
19-record SGX-relative table omits slot `0x0b`, while the target override
installs that absolute binding and reasserts the ADT-derived slot-0 address.
Linux ownership, scheduler slow-path semantics, and executable programming
remain fail-closed.
The pinned G17P/G17G/G17S host-management routes also disprove treating the
firmware's type-`0xb` callback completion as the host type-7 IOP-status power
ACK. Type 7 reaches `_handlePowerAck` and wakes `RTBuddy + 0x158`; type `0xb`
stores remote state and wakes the distinct management-endpoint field at
`+0x108`. Because the firmware loads the callback destination from an object
field whose endpoint identity is still unproven, the end-to-end linkage and
all executable recovery/power gates remain **FC**.
It also records the exact opaque six-u64 PM blocks for pinned G17P/HAL200 and
G17G/G17S HAL300 hosts without assigning field semantics or extrapolating to
G17A/G17C. For T6050/G17S it validates the measured 14-state core/SRAM
performance tables and their two mirrored firmware properties; rail ownership,
voltage control, a third domain, and hardware programming remain unresolved.
It also requires the exact tag, UUID, and SHA-256 jointly for each pinned
G17S/G17C GFX/GFX1 nested-firmware identity. Other G17 variants, FTAB
extraction, fixups, loading, and live G17C ownership remain fail-closed.
The recovery-word grammar is now location-scoped too: independently pinned
G15G/S/C/D and G16G accept only mapped-state offset `0x5190`, G16S/C through
the G16X driver accept only `0x4ae0`, and pinned G17P/G17G/G17S also accept
only `0x4ae0`. These are pure decoders, not firmware-state mappings or MMIO.

## Evidence and update policy

The current in-tree anchors are:

- [`agx_chip.c`](../../src/asahi/lib/agx_chip.c) and
  [`test-chip.c`](../../src/asahi/lib/tests/test-chip.c): identity, ISA,
  descriptor, sampler, command-stream, submission, RT, and device-open gates.
- [`test-feature-matrix.py`](../../src/asahi/lib/tests/test-feature-matrix.py):
  requires both tables above to contain every named `enum agx_chip` identity
  exactly once, in enum order, with complete and classified capability cells.
- [`AGX3b.xml`](../../src/asahi/isa/AGX3b.xml),
  [`AGX3c.xml`](../../src/asahi/isa/AGX3c.xml), and
  [`test-agx3-pack.c`](../../src/asahi/compiler/test/test-agx3-pack.c): AGX3
  decode tables and the bounded pack subset.
- [`agx_texture_side_entry.h`](../../src/asahi/lib/agx_texture_side_entry.h)
  and its [focused tests](../../src/asahi/lib/tests/test-texture-side-entry.c):
  exact 8-byte Gen9/Gen10 texture-heap side-entry layout and the bounded,
  fail-closed packing subset.
- [`d12_native_descriptor_core.h`](../../src/asahi/d3d12/d12_native_descriptor_core.h)
  and its
  [host tests](../../src/asahi/d3d12/tests/d12_native_descriptor_core_tests.c):
  generation-sized D3D12 heaps and fail-closed software-PBE rules.
- [`hk_native_descriptor.h`](../../src/asahi/vulkan/hk_native_descriptor.h),
  its [packing helper](../../src/asahi/vulkan/hk_native_descriptor_pack.c), and
  [focused test](../../src/asahi/vulkan/tests/test-hk-native-descriptor.c):
  generation-sized Honeykrisp image records, Gen9/10 sampled-texture selectors,
  native Gen9/10 PBE hardware words, and the fail-closed AGX3 software-sideband
  policy.
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
  D3D scalar `iadd`, `isub`, `iand`, `ior`, `ixor`, `inot`, `ishl`, `iashr`,
  `ilshr`, `imul`, four-register `imadd`, and compact f32 `add` mapping into
  exact AGX3b/AGX3c encodings.
- [`d12_raytracing_core.h`](../../src/asahi/d3d12/d12_raytracing_core.h) and its
  [host tests](../../src/asahi/d3d12/tests/d12_raytracing_core_tests.c): DXR
  host contracts, explicit Tier-0 rationale, bounded BVH helpers, and exact
  generation/validation of the fixed AGX3b/AGX3c barrier-plus-`update_node`
  expansion and G17S-only fixed `update_bvh` witness.
- [`d12_discard_core.h`](../../src/asahi/d3d12/d12_discard_core.h) and its
  [host tests](../../src/asahi/d3d12/tests/d12_discard_core_tests.c): public
  `DiscardResource` list/region validation and the conservative no-op contract.

Any feature commit that changes an identity, generation axis, runtime gate, or
API capability must update this matrix in the same change. Promote a status
only with a linked evidence artifact and a focused test result; record a new
unknown as **FC** rather than inferring it from a neighboring chip. Runtime
support requires compiler, descriptors, samplers, command stream, kernel UAPI,
power/boot, and submission to be ready together.
