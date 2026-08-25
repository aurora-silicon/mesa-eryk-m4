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
| G15G | M3 | AGX3 / `agx3b` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch and 1x tile I/O, register-only structured CF, slots/waits; disassembler | **IV** — Gen9, 32 B | **IV** — Gen4, 32 B | **RE** — G15/classic; CDM launch/pad/barrier/terminate + VDM PPP hdr/viewport/barrier/state/link packed; direct/indirect draw **IV** |
| G15S | M3 Pro | AGX3 / `agx3b` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch, register-only structured CF, slots/waits; disassembler | **IV** — Gen9, 32 B | **IV** — Gen4, 32 B | **RE** — G15/classic; CDM launch/link/pad/barrier/terminate + VDM PPP hdr/viewport/barrier/state/link packed; direct/indirect draw **IV** |
| G15C | M3 Max | AGX3 / `agx3b` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch, register-only structured CF, slots/waits; disassembler | **IV** — Gen9, 32 B | **IV** — Gen4, 32 B | **RE** — G15/classic; CDM launch/link/pad/barrier/terminate + VDM PPP hdr/viewport/barrier/state/link packed; direct/indirect draw **IV** |
| G15D | M3 Ultra | AGX3 / `agx3b` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch, register-only structured CF, slots/waits; disassembler | **IV** — Gen9, 32 B | **IV** — Gen4, 32 B | **RE** — G15/classic; CDM launch/link/pad/barrier/terminate + VDM PPP hdr/viewport/barrier/state/link packed; direct/indirect draw **IV** |
| G16P | Phone part; marketing mapping ungrounded | AGX3 / `agx3b` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch, register-only structured CF, slots/waits; disassembler | **IV** — Gen9, 32 B | **IV** — Gen4 HAL200, 32 B | **RE/FC** — HAL200 / submission unknown |
| G16G | M4 | AGX3 / `agx3b` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch and 1x tile I/O, register-only structured CF, slots/waits; disassembler | **IV** — Gen9, 32 B | **IV** — Gen4 HAL200, 32 B | **RE** — HAL200/classic; CDM launch/pad/barrier/terminate + VDM PPP hdr/viewport/barrier/state/link packed; direct/indirect draw **IV** |
| G16S | M4 Pro | AGX3 / `agx3b` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch, register-only structured CF, slots/waits; disassembler | **IV** — Gen9, 32 B | **IV** — Gen4 HAL200, 32 B | **RE** — HAL200/dual; CDM launch/link/pad/barrier/terminate + VDM PPP hdr/viewport/barrier/state/link packed; direct/indirect draw **IV** |
| G16C | M4 Max | AGX3 / `agx3b` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch, register-only structured CF, slots/waits; disassembler | **IV** — Gen9, 32 B | **IV** — Gen4 HAL200, 32 B | **RE** — HAL200/dual; CDM launch/link/pad/barrier/terminate + VDM PPP hdr/viewport/barrier/state/link packed; direct/indirect draw **IV** |
| G17P | A18 Pro | AGX3 / `agx3b` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch, register-only structured CF, slots/waits; disassembler | **IV** — Gen9, 32 B | **IV** — Gen4 HAL200, 32 B | **RE** — HAL200/dual; CDM launch/pad/barrier/terminate + VDM PPP hdr/viewport/barrier/state/link packed; direct/indirect draw **IV** |
| G17A | A18-class, marketing unknown | AGX3 / `agx3b` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch, register-only structured CF, slots/waits; disassembler | **IV** — Gen9, 32 B | **IV** — Gen4 HAL200, 32 B | **RE/FC** — HAL200 / submission unknown |
| G17G | M5 | AGX3 / `agx3c` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch, register-only structured CF, slots/waits; disassembler | **IV** — Gen10, 32 B | **IV** — Gen5, 32 B | **RE** — HAL300/SKSM; CDM launch/pad/Gen7 barriers/terminate + VDM PPP hdr/viewport/barrier/state/link packed; direct/indirect draw **IV** |
| G17S | M5 Pro | AGX3 / `agx3c` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch and 1x tile I/O, register-only structured CF, slots/waits; disassembler | **IV** — Gen10, 32 B | **IV** — Gen5, 32 B | **RE** — HAL300/SKSM; CDM launch/link/pad/Gen7 barriers/terminate + VDM PPP hdr/viewport/barrier/state/link packed; direct/indirect draw **IV** |
| G17C | M5 Max | AGX3 / `agx3c` | **IV** — incomplete native backend: ALU, device memory, SR reads, f32 vary/UVS, bounded 2D LOD0 fetch, register-only structured CF, slots/waits; disassembler | **IV** — Gen10, 32 B | **IV** — Gen5, 32 B | **RE** — HAL300/SKSM; CDM launch/link/pad/Gen7 barriers/terminate + VDM PPP hdr/viewport/barrier/state/link packed; direct/indirect draw **IV** |

The AGX3 pack subset currently covers only individually grounded instructions:
`ldimm.u32`, canonical scheduler `nop`, workgroup barrier, shader `end`, thread
enable, the zero-argument `update_node` expansion, the G17S-only fixed
`update_bvh` witness, the bounded hardware `intersect_ray` and `continue_ray`
f5 codecs, and the canonical no-kill, scheduler-neutral i16-register `yield`,
plus 32-bit register-plus-u8 `iadd` and the canonical three-register form (AGX3b
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
A distinct compact f32 register multiply is implemented for the exact AGX3b
selection witnessed on G15G/S/D, G16P/G/S, and G17P and the distinct AGX3c
selection witnessed on G17S, conservatively exposing even half-registers
0--126. The distinct 14-byte AGX3c selection is separately implemented on
its directly witnessed G17G and G17S identities over the same conservative
register domain. The exact canonical four-register f32 fused multiply-add is
also packed and decoded as an eight-byte AGX3b selection on G15G/S/D,
G16P/G/S, and G17P and a sixteen-byte AGX3c selection on G17G/S. It preserves
the two physical multiplicands in encoded order and likewise exposes only even
half-registers 0--126. Register bit 7, alternate context selections, modifiers,
unfused-MAD semantic claims, and every unwitnessed identity remain closed.
The exact register-register f32 minimum and maximum selections are independently
implemented as distinct six-byte AGX3b `fsel` forms on G15G/S/D, G16P/G/S, and
G17P and distinct fourteen-byte AGX3c `fseli32` forms on G17G/S. The AGX3c
forms duplicate both inputs across their comparison and selection fields;
unpacking rejects any incoherent copy. Both use the same conservative even
0--126 register domain. Other predicates, generic select, modifiers, and IEEE
NaN/signed-zero policy beyond the witnessed compiler selections remain
unclaimed.
The exact ten-byte scalar f32 reciprocal selected by the pinned `1.0f / x`
source is separately packed and decoded on G15G/S/D, G16P/G/S, G17P, and
G17G/S. A fresh nine-target replay is byte-identical to the pinned `frecip`
corpus, while independent live-source, last-use, and eight-live-value probes
identify its destination and source fields. Apple-table composition covers all
4,096 even-register pairs in 0--126 on each table variant. Only the fixed
direct last-use/dependency context is exposed; alternate dependency selections,
live-source encodings, reciprocal accuracy/refinement policy, other special
functions, modifiers, high registers, scheduling, lowering, runtime use, and
unwitnessed identities remain closed.
The exact signed i32-to-f32 register conversion is also implemented as the
eight-byte AGX3b `int2flt` selection on G15G/S/D, G16P/G/S, and G17P and the
ten-byte AGX3c selection on G17G/S. Independent producer/consumer captures
correlate its destination and source fields on both table variants. It exposes
only even half-registers 0--126. The byte-distinct unsigned u32-to-f32
`int2flt` selection is independently implemented for the same seven AGX3b and
two AGX3c identities. Its zero-register seed is reproduced by a separate
source probe, while nonzero `P2i_div` producer/consumer captures independently
correlate both operands on each table variant. It uses the same conservative
even 0--126 register domain. The complementary f32-to-i32 and f32-to-u32
round-toward-zero language-cast selections are separately packed and decoded
as ten-byte `flt2int` forms on all nine identities. The direct fptosi/fptoui
seeds and independent PC probes pin their byte-distinct signedness selectors.
`P2f_ldexp` and the compiler's division expansions correlate the destination
and source fields on both table variants. The exact ten-byte `f32rint`
selection emitted immediately before PC_f2ir's signed cast is separately
packed and decoded on those same nine identities. A nine-target PC replay and
an independent nonzero-register last-use probe pin that fixed context, while
live-source producer/consumer probes and exhaustive Apple-table composition
correlate both operands over even half-registers 0--126. Direct
`rint`/`trunc`/`ceil`/`floor` sources distinguish their selector values, but
only the observed `rint` selection is exposed. Other rounding selectors,
tie/exceptional-value semantics, and alternate scheduler/last-use contexts
remain rejected. All five forms expose only even half-registers 0--126.
Saturation, NaN/out-of-range behavior, source-reuse/kill variants, alternate
contexts, modifiers, high registers, and conversion lowering policy remain
closed.
The arithmetic, bitwise, shift, multiply, multiply-add, float-add, reciprocal,
and five conversion/rounding forms have exhaustive operand and every-bit
rejection tests plus exact G15/G16/G17 corpus replay; the separate shift-count mask and
G15G stall
placement remain an unsolved scheduler rule. This is not a general shader
encoder. The production compiler does now dispatch AGX3b/AGX3c packing by the
device encoding and emits the grounded ALU subset, canonical device
loads/stores, their scoreboard-carried dependency waits, and the register-only
structured-control-flow subset described below. It rejects the shader when any
instruction or operand shape falls outside that subset.
This is an incomplete, driver-gated code-generation path; the generated AGX3
disassemblers and hardware-RT detection do not make device open supported.

The same production path now implements `get_sr` as the AGX3 `rld` form. It
maps the directly isolated compute built-ins for threadgroup position,
threads-per-threadgroup, dispatch-threads-per-threadgroup, local position,
local/subgroup indices, and global position to their corpus-proven Apple SR
numbers and 16- or 32-bit destination widths. The encoder reproduces all 306
clean AGX3b/AGX3c SR-read witnesses byte-exactly, escalates from the compact to
the long form when a wait mask requires it, and refuses unattributed SR 34/36,
unmapped Mesa SRs, wrong widths, invalid registers, and out-of-range slots.
`get_sr` participates in the six-slot AGX3b/eight-slot AGX3c allocator as an
asynchronous producer. A native end-to-end test proves the dependency chain
`get_sr(global_id.x) -> indexed device load -> add -> store`, including the
load's wait on the SR slot and the ALU's wait on the load slot.

Structured integer and floating-point control flow is now wired through the
production packer for both AGX3 encodings. Mesa `if`, canonical `else`, loop
test, conditional-break, pop, exec-none skip, and exec-any backedge IR maps to
the witnessed `icmp`/`fcmp` + `cndst`/`cndef`/`cndlt`/`cndend` + `bn`/`b`
sequences. Branches reserve ten bytes and receive the encoding-specific signed
48-bit displacement scatter after all block offsets are known; tests exercise
both a forward `bn` and backward `b` on AGX3b and AGX3c. The late pseudo pass
preserves AGX3's architectural exec-mask model instead of writing AGX2's
`r0l` nesting counter, and failed packing rolls back the whole emitted prefix.
The supported comparison domain is deliberately narrow: 32-bit registers in
the directly witnessed low range, integer register/immediate-8 relations, and
the normalized IEEE float relations. Unconditional break, noncanonical else,
unwitnessed condition values/forms, and any condition after an asynchronous
load or SR read remain fail-closed until the compare/cnd hazard fields are
grounded. This is compiler implementation validation, not device admission or
hardware execution evidence.

The production packer also emits the grounded AGX3 fragment-input and vertex-
output forms. Constant coefficient indices become scalar f32 `f32interp`
instructions at center or centroid; perspective interpolation is the witnessed
software RHW-interpolate, reciprocal, channel-interpolate, multiply sequence.
Raw coefficient `ldcfr` loads participate in the six-slot AGX3b/eight-slot
AGX3c dependency allocator, while 32-bit `uvsw` writes carry rotating nonzero
slots, dependency waits, and the allocator's real last-use kill bit. The shared
encoders reproduce all 970 checked varying corpus instructions plus exact
AGX3b/AGX3c center, centroid, coefficient-load, and UVS-write witnesses. They
reject f16 interpolation, dynamic coefficient indices, explicit at-sample
coordinates, invalid registers, zero/out-of-range slots, and wait bits outside
the encoding. The production packer now also emits the directly witnessed
single-sample `ld_tile` and `st_tile` subset. Tile loads allocate an asynchronous
scoreboard slot, and a consuming store receives the dependency wait across
straight-line fallthrough blocks. The encoder maps Mesa's 8/16/32-bit tile
formats, checks value width against the component mask, carries the global
first/last-load markers, allocates the full fourteen bytes required by AGX3c,
and fails the whole binary on an invalid operand. Exact G15G and G16G AGX3b
plus G17S AGX3c witnesses are exposed through the D3D12 validation bridge.
Per-sample/MSAA selection, explicit coordinates, partial-load masks, incoming
waits on loads, multi-wait stores, AGX3b high-register stores, tile decoding,
and every unwitnessed target remain closed. This is compiler and host
validation; device admission, fragment execution, Vulkan lowering, and D3D12
execution remain closed.

The production packer now also emits the directly witnessed, non-array 2D
LOD0 texel-fetch selection. The bounded encoder covers 16- or 32-bit four-
component results, masks 1 and 15, aligned f32 coordinate pairs, immediate
texture indices 0--15, sampler zero, the six-slot AGX3b/eight-slot AGX3c
scoreboard domain, zero or one compact dependency wait, coordinate last-use,
and the observed final-texture marker. It reproduces the exact AGX3b and AGX3c
witnesses and rejects array, offset, compare, gather, sparse, bindless,
register-selected texture/sampler, MSAA, alternate LOD, wider mask, and
noncanonical encodings. Scoreboard state is preserved across linear
fallthrough blocks; any control-flow boundary after an asynchronous producer
remains fail-closed until CFG-aware scoreboard merging is grounded. The D3D12
ISA core maps the same subset only on the nine compiler-witnessed G15--G17
targets. Final-marker behavior is grounded only for the pinned straight-line
texture ladders, not path-sensitive control flow. This is compiler and host
encoding validation, not complete texture lowering, descriptor execution,
device admission, Vulkan feature exposure, or D3D12 execution support.

The D3D12 AGX3 ISA bridge target-gates the same grounded integer/float
compare, exec-stack, directional branch, scalar interpolation, coefficient-
load, and UVS-write building blocks for the nine directly witnessed G15--G17
compiler targets. It separately target-gates the bounded tile load/store bridge
to the directly captured G15G, G16G, and G17S encodings. Its exhaustive host
test checks the AGX3b/AGX3c byte sequences and every rejection path.
Honeykrisp now has an equivalent target-aware bridge for that same witnessed
single-sample subset: full-component loads with no incoming wait and stores
with at most one compact dependency wait. Both bridges remain ISA primitives,
not claims that DXIL or SPIR-V/NIR tile lowering, shader integration, render-
target/MSAA state, runtime scheduling, submission, or API exposure is complete.

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

The generated command-stream packer and a strict shared wrapper now encode
and decode the exact 40-byte AGX3 CDM launch. Its 48-bit, 64-byte-aligned
pipeline pointer is split between word 1 bits 31:22 and all of word 2; both
finalize-template payloads are preserved, and global/local dimensions are
bounded exactly. A semantic constructor now implements the two directly
witnessed thread-count modes, one/two/three-dimensional control words, the
G15X-only threadgroup control bit, and the emitter's bit-19 clear when both
higher global axes are smaller than two. Its field builders expose only the
recovered finalize ranges and reject every reserved-bit mutation. Directly
pinned launch emitters cover G15G/G15X, G16G/G16X, G17P, and G17G/G17X, which
map to the eleven per-chip rows marked above.
G16P and G17A remain rejected. A separate exact eight-byte CDM link widens the
target from 40 to 48 bits and is gated more narrowly to the directly pinned
G15X/G16X/G17X aggregate identities; base/phone link forms are not inferred.
The exact four-byte barrier is independently pinned for G15G/G15X, G16G/G16X,
G17P, and G17G/G17X. Pre-Gen7 inspection proves only the canonical flag sets
`0` and `0b0101`; the G17 Gen7 emitter proves all sixteen mappings from input
flag bits 0--3 to output bits 12, 11, 10, and 1. G16P and G17A remain closed
for the barrier record. D3D12 and Honeykrisp now select the exact `0b0101`
strong post-dispatch flush on all eleven grounded identities instead of
emitting the AGX2 barrier packet; unsupported identities set their command
list/buffer error. Virtual-substream barriers additionally encode a
one-biased eight-bit substream in bits 21--28;
the aliasing input 255 is rejected. Gen7 may append the independently pinned
fixed `0x60000502` secondary barrier. The exact four-byte type-5 link-padding
token exposes only its proved three-bit count, while type 6 is recognized but
stays semantically closed. The exact eight-byte stream terminator is common to
all eleven launch-grounded identities. The D3D12 host core maps and strictly
validates these records. Command-list close now selects the exact target-aware
CDM terminator for ordinary and pre-graphics compute segments instead of
unconditionally appending the AGX2 token. Honeykrisp likewise selects it after
CDM merging. Unsupported command generations and the ungrounded G16P/G17A
paths set a list/command-buffer error. These remain **RE/IV** pieces rather
than a complete CDM/VDM/Ioto stream. Honeykrisp chunk growth now also selects
the exact target-aware direct VDM link on all eleven grounded identities and
the narrower CDM link on the seven directly witnessed G15X/G16X/G17X
identities; compute-stream merging uses the same path. An ungrounded base or
phone CDM link fails the command buffer instead of emitting an AGX2 record.
Live shader-metadata-to-finalize mapping, the remaining link lifecycle,
executable shader payloads,
queue submission, device admission, and API capability are still
independently gated, so the command/submission cells remain **RE**.

A strict shared record-at-a-time decoder and D3D12 entry point now classify
that grounded CDM subset inside a longer byte stream. They distinguish launch,
link, type-5 padding, canonical and virtual-substream barriers, the Gen7 fixed
secondary barrier, and termination while reporting the exact consumed length.
Unknown types 4/6/7, reserved-bit mutations, truncated records, and every
identity outside each record's own evidence gate fail with a cleared result.
The decoder deliberately does not infer record ordering, follow links, read
GPU memory, or turn the subset into an executable stream.

The shared VDM core and D3D12 bridge also pack and decode the exact 28-byte
AGX3 VDM State Update. Its widened two-word vertex-pipeline field preserves
the raw 22-bit state-template payload and splits the 48-bit, 64-byte-aligned
pipeline address identically to CDM. The vertex-shader word, both vertex-output
words, and the trailing vertex word are preserved losslessly as raw payloads
because their complete semantics remain ungrounded. Direct G15G/G15X,
G16G/G16X, G17P, and G17G/G17X plugin claims cover the same eleven rows;
G16P and G17A fail closed. All 224 record bits are classified and round-trip,
but PPP payload semantics, Ioto USC stream, and submission remain incomplete.

The shared VDM decoder, Honeykrisp validator, and D3D12 bridge also implement
the exact 36-byte direct Tessellate record on those same eleven identities.
The fixed `0xb2f00c00` header carries the high factor-buffer address byte;
words 1 and 8 complete its 48-bit address, with exact patch/instance counts,
base patch/instance, 19-bit instance-stride word, and factor-buffer-size-minus-
one fields between them. All seven directly inspected G15, HAL200, and HAL300
emitters are byte-identical and advance by `0x24`, so the record cannot classify
generation. Indirect forms, live tessellation emission/lowering, submission,
capabilities, G16P, and G17A remain closed.

The shared VDM and D3D12 cores additionally pack and decode the exact eight-
byte PPP State Update header on the same eleven directly pinned identities.
It carries a 40-bit, four-byte-aligned PPP payload pointer and an eight-bit
payload size in 32-bit words; all sixteen reserved header bits must be zero.
The pointed-to PPP payload grammar beyond the bounded viewport-only subset,
allocation, live draw emission, and submission are still unimplemented, and
G16P/G17A remain fail-closed.

The first bounded pointed-to payload subset is now implemented and decoded in
the shared core and D3D12 bridge: the exact viewport-only envelope produced by
the seven pinned G15G/G15X, G16G/G16X, G17P, G17G, and G17X emitters. It begins
with header `0x00000c00` plus `(N - 1)` for 1--16 viewports, stored in bits
12--15 on G15/G16/G17P and bits 14--17 on G17G/S/C. The body is
exactly N raw 64-bit region-clip slots, one raw 32-bit viewport-control word,
and N six-word viewport transforms ordered translate/scale X, Y, and Z, for a
total of `8 + 32*N` bytes. All body words round-trip losslessly; only the six
transform positions have directly recovered ordering, while clip/control
semantics remain deliberately unnamed. A one-viewport payload is genuinely
byte-identical across the generations because both count fields are zero;
larger cross-generation forms reject by their exact derived length. Other PPP
header bits, payload blocks, allocation, VDM pointer following, live viewport
lowering/emission, and submission remain closed, as do G16P and G17A.

The shared VDM Index List core and D3D12 bridge additionally encode and decode
the exact direct and indirect draw records for those eleven targets. Direct
non-indexed records are 16 bytes on G15/HAL200 and 20 bytes on HAL300; direct
indexed records are 28 and 32 bytes respectively. Indirect non-indexed records
are 12 and 16 bytes, while indirect indexed records are 32 and 36 bytes. Both
indexed paths preserve the exact 48-bit index-buffer split, U16/U32 forms,
restart bit, and ten directly witnessed primitive-table values. The indirect
form additionally preserves the 48-bit, four-byte-aligned argument pointer and
encodes the available index range as rounded-up dwords minus one; production
restart prefixes use the canonical U16/U32 indices. HAL300 carries its exact
extra fixed word `7`, which the G15/HAL200 forms omit. Native Honeykrisp and
D3D12 recording now select these constructors for live direct and indirect
draw recording. Honeykrisp's GPU-written robust-index helper also receives the
actual target, so its generated direct record cannot silently fall back to an
AGX2 layout. The owning command buffer/list still fails closed for U8 indices,
unaligned or out-of-range VAs, empty indirect index ranges, G16P/G17A, and every
other ungrounded form. CPU ICB template construction/splicing, complete stream
execution, submission, and device admission remain absent, so the overall
command/submission cells remain **RE** while this record-emission slice is
**IV**.

The exact direct VDM Stream Link is also packed and decoded in the shared and
D3D12 cores for those eleven targets. It is eight bytes on G15, HAL200, and
HAL300 alike: word 0 carries block type 4 plus target bits 32--39, and word 1
carries target bits 0--31. The recovered emitter genuinely remains 40-bit
through HAL300 and imposes no extra target alignment. The returning/call bit,
stream return and terminate records, allocation, live linking, and submission
remain closed.

A strict shared VDM record-at-a-time decoder and D3D12 bridge now classify all
seven grounded record families from a potentially longer stream: PPP State
Update, the canonical memory barrier, full State Update, direct non-indexed
and indexed Index List, direct Stream Link, and Stream Terminate. Record
length is derived from both the exact header and the target's G15/HAL200
versus HAL300 form. Types 5/7, truncated or
reserved-bit-mutated records, unsupported identities, and wrong-generation
lengths fail with a cleared result. The decoder does not inspect PPP payloads,
follow links, read GPU memory, emit commands, or submit work.

The exact canonical type-1 VDM memory barrier is independently packed and
decoded as the four-byte little-endian word `0x21000960` on those eleven
identities. The aggregate G15X emitter can OR bits 2--4 when a separate
`hasMultipleAGCs` property is true, producing `0x2100097c`, but no per-chip
value for that property is pinned; the alternate form therefore remains
rejected on every identity.

The exact type-6 VDM Stream Terminate is independently packed and decoded as
the four-byte little-endian word `0xc0000000` on those same eleven identities.
Every pinned G15 through G17P render-context constructor stores the identical
constant pair `{0, 0xc0000000}` at context `+0x7500`, and
`emitEndCommandStream` copies the second word from `+0x7504`; HAL300 uses the
corresponding `+0x74f0/+0x74f4` offsets with the same bytes. Type-7 Stream
Return `0xe0000000` remains rejected because the plugin sweep found no aligned
emitter evidence. The D3D12 render-segment close and Honeykrisp graphics close
paths now select this exact target-aware terminator instead of the AGX2 token
on the eleven grounded identities. Unsupported identities fail the owning list
or command buffer. The rest of live VDM emission, allocation, stream-link
lifecycle, submission, and device admission remain closed.

Honeykrisp now consumes that same target-aware decoder through a Vulkan-side
validation bridge on the eleven pinned identities. Its focused tests replay all
seven record kinds and the fail-closed identity/length boundary. This does not
connect the records to `hk_cmd_buffer`, change AGX3 device admission, or expose
any Vulkan feature.

Honeykrisp also validates the bounded viewport-only PPP payload through the
same shared target-aware decoder on those eleven identities. Its bridge checks
all 1--16 viewport counts, exact shared-byte round trips, the genuine
one-viewport cross-generation identity, larger cross-generation rejection, and
malformed identity/length/header boundaries. It likewise remains disconnected
from `hk_cmd_buffer`, live PPP allocation/emission, device admission, and Vulkan
feature exposure.

## Mesa API and kernel relationship

| Mesa identity | Gallium / OpenGL | Honeykrisp / Vulkan | D3D12 AGX / WDDM UMD | DXR | Linux DRM relationship |
|---|---|---|---|---|---|
| G13G | **RS** — AGX2 path | **RS** — AGX2 path | **RS** — AGX2 execution; UMD contracts build-tested | **FC** — no hardware RT; Tier 0 | **RS** — existing `drm/asahi` path |
| G13X | **RS** — AGX2 path | **RS** — AGX2 path | **RS** — AGX2 execution; UMD contracts build-tested | **FC** — no hardware RT; Tier 0 | **RS** — existing `drm/asahi` path |
| G14G | **RS** — AGX2 path | **RS** — AGX2 path | **RS** — AGX2 execution; UMD contracts build-tested | **FC** — no hardware RT; Tier 0 | **RS** — existing `drm/asahi` path |
| G14X | **RS** — AGX2 path | **RS** — AGX2 path | **RS** — AGX2 execution; UMD contracts build-tested | **FC** — no hardware RT; Tier 0 | **RS** — existing `drm/asahi` path |
| G15P | **FC** — device axes unknown | **FC** — device axes unknown | **FC** — AGX2 shader code only; device gate closed | **FC** — no hardware RT; Tier 0 | **RE/FC** — identity routed; no runtime bring-up |
| G15G | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native Gen4 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, bounded tile ISA, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity, 42-bit UAT, recovery, kick route, and ring-publication preflight; no runtime bring-up |
| G15S | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native Gen4 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity, 42-bit UAT, recovery, kick route, and ring-publication preflight; no runtime bring-up |
| G15C | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native Gen4 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity, 42-bit UAT, recovery, kick route, and ring-publication preflight; no runtime bring-up |
| G15D | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native Gen4 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity, 42-bit UAT, recovery, kick route, and ring-publication preflight; no runtime bring-up |
| G16P | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity/HAL routed; submission unknown; no runtime bring-up |
| G16G | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, bounded tile ISA, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — identity/HAL/classic, recovery, kick route, and ring-publication preflight; no runtime bring-up |
| G16S | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — fixed G16 SKSM channel classes, recovery, kick/ring preflight; firmware ACK unknown |
| G16C | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — fixed G16 SKSM channel classes, recovery, kick/ring preflight; firmware ACK unknown |
| G17P | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB helpers tested; traversal missing; Tier 0 | **RE/FC** — fixed G16 SKSM channels, HAL200 power, kick/ring preflight; firmware ACK unknown |
| G17A | **IV/FC** — native Gen9 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen9 textures + native HAL200 sampler heaps; PBE/commands gated | **IV/FC** — Gen9 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host contracts tested; traversal missing; Tier 0 | **RE/FC** — dual-role/HAL routed; submission unknown; no runtime bring-up |
| G17G | **IV/FC** — native Gen10 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen10 textures + native Gen5 sampler heaps; PBE/commands gated | **IV/FC** — Gen10 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB + Gen5 BVH helpers tested; traversal missing; Tier 0 | **RE/FC** — fixed PI_300 G SKSM channels, power/boot, kick/ring models; firmware ACK unknown |
| G17S | **IV/FC** — native Gen10 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen10 textures + native Gen5 sampler heaps; PBE/commands gated | **IV/FC** — Gen10 heaps, strides, samplers, bounded tile ISA, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB + Gen5 BVH helpers tested; traversal missing; Tier 0 | **RE/FC** — fixed PI_300 X SKSM channels, PM/PIO/idle, recovery drain, SKSM completion/host-event consumers, kick/ring models, and strict loaded paired `a010` admission before SGX MMIO; UAT/start/ACK unknown |
| G17C | **IV/FC** — native Gen10 texture heaps/strides; PBE/codegen/commands gated | **IV/FC** — Gen10 textures + native Gen5 sampler heaps; PBE/codegen/commands gated | **IV/FC** — Gen10 heaps, strides, samplers, and host/DDI contracts; no AGX3 execution | **IV/RE** — host + Gen1 BVH/triangle/AABB + Gen5 BVH helpers tested; traversal missing; Tier 0 | **RE/FC** — fixed PI_300 X SKSM channels, boot, kick/ring models, and strict paired `a000` firmware admission; authoritative handoff observation/firmware ACK unknown |

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

The shared compiler now also packs and strictly decodes one byte-witnessed
`intersect_ray` differential pair per AGX3 encoding. AGX3b admits only f5
half-register values `{0,4}` in the canonical 12-byte form; AGX3c admits only
`{0,8}` in the canonical 14-byte form. Every anonymous non-f5 field is held to
the captured value and decode succeeds only after exact repack. Cross-target
corpus matches gate this slice to G15G/G15S/G15D, G16G/G16S, G17P, and
G17G/G17S; same-encoding G15C/G16C/G17C and the non-target phone identities
remain closed. Honeykrisp and the D3D12 RT core expose generation/decode
bridges for those exact bytes, but the likely state/SCS meaning of f5 remains
an inference. Required scoped barriers, SCS layout, traversal lowering,
scheduling, command emission, submission, Vulkan RT exposure, and DXR tier
therefore remain **FC**.

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
unwitnessed. The shared packer now represents the canonical first-site
AGX3b/AGX3c hardware instruction while varying only its Apple-table f5
half-register field over values observed in the pinned corpus: AGX3b
`{0,2,4,6,8,10}` and AGX3c `{0,2,8,10,12,16,18}`. Decode is canonical-
repack strict, and all other selector, flag, wait, hint, and second-site forms
remain rejected. The state/SCS meaning of f5 is inferred from surrounding
lowering; no intrinsic operand, SCS layout, barrier sequence, compiler
allocation/finality rule, traversal lowering, device admission, or API
capability is claimed.

The D3D12 RT core and Honeykrisp validation bridge consume that same bounded
f5 codec only on the compiler-proven target set: AGX3b
G15G/G15S/G15D/G16P/G16G/G16S/G17P and AGX3c G17G/G17S. They reject both
encoding forms on every other identity, including the same-encoding
G15C/G16C/G17A/G17C cases. The Honeykrisp bridge is not connected to command
buffers or Vulkan feature exposure, and D3D12 still exposes no traversal,
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
`0x1000`. Mode 2 is now correlated to the tier-1 sparse-residency companion
texture. Its direct i64 stores binary32 `(width_tiles, height_tiles)` from
texture+0x288/+0x28c rather than a payload/auxiliary pair; the normal parent's
high word names that companion's same-index heap slot. The producer, paired
heap-index allocation, clone path, and destruction lifetime are pinned on
G15G, G16G, G17P, G17G, and G17X plugin classes. A shared, tested 8-byte
side-entry helper packs and decodes the proven normal subset plus exact,
positive integral mode-2 tile counts through 2^24, rejecting wrong forms,
unknown cases, and overflow. The D3D12 descriptor core maps that same subset
and records the Gen9/Gen10 8-byte side stride, but does not allocate or bind a
side heap while its command-stream pointer channel remains unknown. Executed
G15/G16 nonzero atomic payloads and live side-heap submission remain **FC**;
Honeykrisp and D3D12 therefore still expose neither image writes nor atomics.

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
canonical compact f32 `add`, target-gated compact and long f32 `multiply`, and
the target-gated four-register f32 fused multiply-add, plus the no-kill,
scheduler-neutral i16-register `yield` form. It also maps the exact target-
gated register-register f32 minimum and maximum selections. It additionally
maps the exact signed i32-to-f32 register conversion on the same nine target
identities and conservative even 0--126 domain, plus the byte-distinct unsigned
u32-to-f32 selection on that exact target set and domain. It additionally maps
the exact f32-to-i32 and f32-to-u32 RTZ selections on those nine identities and
the same register domain, plus the exact PC_f2ir-context `f32rint` selection.
It also maps the exact ten-byte scalar f32 reciprocal on that same nine-target,
even-register 0--126 domain and admits only the fixed direct last-use/dependency
context. Live-source and alternate dependency selections, other special
functions, reciprocal accuracy/refinement policy, modifiers, high registers,
scheduling, lowering, and runtime use remain closed.
That bridge admits only its directly observed round-to-integral selector and
fixed scheduler/last-use context. Other rounding modes, saturation,
NaN/out-of-range or tie behavior, alternate source/context selections, high
registers, and conversion lowering policy remain closed. The
multiply bridges accept only the byte-distinct implicit-zero and real-register
addend forms on the nine compiler-witnessed targets; alternate low selections,
high products, G17C, and other unwitnessed target selections remain closed.
Compact f32 multiply is gated to the eight directly witnessed identities and
the long AGX3c selection to G17G/G17S; both restrict operands to even
half-registers 0--126 and reject alternate anonymous/context selections. The
FMA bridge is independently gated to the nine direct witnesses named in the
compiler section and enforces the same conservative register domain. The f32
minimum and maximum bridges use that same target gate and domain while
rejecting one another, alternate predicates, and inconsistent AGX3c duplicate
source fields.
The shift bridge represents only the grounded `sll`, `sra`, and `srl`
operations and does not emit or claim the compiler-observed count mask. These
bridges replay the exact encoding witnesses but do not admit an AGX3 device:
general instruction selection, scheduling, linking, and submission are still
missing.

Linux DRM development lives outside this Mesa repository in the separate
`linux-asahi-m3-m5-drm` worktree. Its G15-G17 tables and bounded G17 codecs are
diagnostic foundations only: initdata, RTKit transport, power sequencing,
recovery, topology, UAT handoff, and command submission remain runtime gates.
The power/recovery, submission, and firmware ledger through Linux commit
`ede4ea7e023d`
additionally
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
The same ledger now represents the exact host firmware-kick word on the
independently pinned G15G/S/C/D, G16G/S/C, and G17P/G/S/C identities: bit 55
is the fixed tag, bits 48--53 carry the six-bit message type, and bits 2--4
carry the three-bit kick index. G15/G16 always route to `GFX`; G17 routes
message type 7 to `GFX1` and all other proved types to `GFX`. Independently
pinned G16S/C and G17P/G/S/C hosts also agree on the SKSM queue-ID field at
shift 40 with mask `0x7f`. Every pinned G15--G17 host routes the kick through
RTBuddy's asynchronous-note sender to endpoint `0x21` at object offset
`0x130`, not the distinct ordinary-message endpoint `0x20` at `0x128`; the
vtable slots and opaque send arguments are target-validated too. The same
hosts now have an exact pure data-master ring model, including the materially
different backing layouts. G15G uses shared entry/control batches owned at
firmware offsets `+0x310/+0x548`; G15S/C/D allocate both entries and indices
from Fender ScratchRAM; G16G uses ScratchRAM indices but shared entries owned
at `+0x318`; G16S/C and G17P/S/C use shared batches at `+0x350/+0x588`; and
G17G uses shared batches at `+0x358/+0x590`. Shared entries are twelve
`0x1800`-byte slices with raw caching option 1, while shared controls are
twelve `0x30`-byte slices with raw option 0; both use the proved false outer
allocation argument and release then zero their owner slot. Their per-ring CPU
and firmware pointers occupy object `+0x18/+0x20` for entries and
`+0x08/+0x10` for controls, with read/CFI/write indices at control
`+0x00/+0x10/+0x20`. Scratch-backed rings instead carry the scratch object at
`+0x28`, CPU pointers at `+0x30/+0x38/+0x40`, and firmware pointers at
`+0x48/+0x50/+0x58`; their exact mapping values prove inhibit-cache mappings
and an additional read-only bit for read/CFI cells. Host read/CFI setters trap
on that layout and the write setter stores under the ScratchRAM lock. Every
layout retains 24-byte entries and a 256-index ring with one reserved slot.
Publication records slot selection, command encode, `dmb ish`, write-index
reread, eight-bit increment/wrap, and write-index store in that order; cold
initialization creates 12 rings on G15/G16 and 24 on G17. The stripped names
of raw shared-data caching options, firmware consumer ownership,
acknowledgment, and the complete notification lifetime remain
closed, so this is a bounded submission preflight rather than an executable
DRM submission path.
The classic firmware-facing table is now pinned independently on the same
eleven identities. It contains twelve `0x20`-byte descriptors in
priority-major order across four priorities and TA/3D/compute ordinals 0/1/2;
each descriptor carries read, CFI, write, and entries firmware pointers at
`+0x00/+0x08/+0x10/+0x18`. The table CPU-pointer field, TA/3D/compute ring
bases, and per-priority object stride are respectively `0x800`,
`0x368/0x408/0x4a8`, `0x28` on G15G; `0xbc0`,
`0x368/0x548/0x728`, `0x78` on G15S/C/D; `0xbc8`,
`0x370/0x550/0x730`, `0x78` on G16G; `0x840`,
`0x3a8/0x448/0x4e8`, `0x28` on G16S/C; `0xa80`,
`0x3a8/0x448/0x4e8`, `0x28` on G17P/S/C; and `0xa88`,
`0x3b0/0x450/0x4f0`, `0x28` on G17G. Shared-control layouts mirror read and
CFI to write before publishing the table, while the ScratchRAM layouts on
G15S/C/D and G16G perform no host read/CFI stores through their trapping
setters. The model computes class/priority routes without reading or modifying
shared state. The firmware writer that advances/acknowledges the read index
remains unproved and explicitly false. The host channel factories do, however,
close the former classic-versus-SKSM selection unknown: G16S/C and G17P
directly allocate the G16 SKSM CL/3D/TA subclasses, while G17G and G17S/C
directly allocate their exact `AGX::PI_300::G::A0` and
`AGX::PI_300::X::A0` SKSM subclasses. All twelve pinned factory methods are
branch-free and use object sizes `0x1d0/0x1d8/0x1e0` for CL/3D/TA. G17A
remains ungrounded. Without the normal firmware acknowledgment writer and
complete completion lifetime, this still does not enable executable
submission.
The sole pinned true firmware bundle additionally grounds a G17S-only
recovery drain/discard handler at GFX text `+0x2649c`. It traverses four
priority groups in TA/3D/compute order, uses the exact `0x20` descriptor
records below runtime `+0x20` with `0x60` priority stride, initializes each CFI
pointer from its write-index snapshot, expects entry types 0/1/2, and advances
each read index with a 32-bit load/add-one/mask-`0xff`/store sequence up to
`0x14` times per data master. Direct status handling stores 1, executes
`dsb sy`, runs the recovery body, stores 0, and executes a second `dsb sy`.
This path is explicitly recovery/discard rather than normal consumption and
is rejected for G17P/G/C and every earlier target. A separate G17S SKSM
completion consumer is now modeled independently: the host provides four
`0x20`-byte descriptors and the firmware visits records 1, 3, and 0, consuming
`0x40`-byte entries after decoding the low 32-bit producer status and count.
It performs the exact cache-maintenance, validation, handler, and modulo read
advance sequence before publishing the processed count to selector-specific
MMIO at `0x21190`, `0x211a0`, and `0x21188`. The pinned path has no direct
barrier, release store, or atomic before those publications. This consumer,
plus the still-unmodeled device-control and timestamp consumers, does not own
the classic four-priority submission ring. The distinct G17S host
firmware-event consumer is now modeled from its raw interrupt entry point:
interrupts 0 and 4 converge on clear-outstanding and ring drain, then consume
GFX followed by GFX1 through validator objects at `+0x7e0/+0xa20`. Each
`0x48`-byte entry is copied as eighteen words, checked against a 64-bit event
type mask, advanced modulo capacity, and followed by the exact `dmb ish`
before shared-read publication. The firmware producer ordering and any causal
link from SKSM processed-count MMIO to these interrupts remain explicitly
unproved. Classic acknowledgment therefore remains unproved and executable
submission remains disabled.
The pinned G17P/G17G/G17S host-management routes also disprove treating the
firmware's type-`0xb` callback completion as the host type-7 IOP-status power
ACK. Type 7 reaches `_handlePowerAck` and wakes `RTBuddy + 0x158`; type `0xb`
stores remote state and wakes the distinct management-endpoint field at
`+0x108`. The callback constructor's exact dataflow now proves that object
field carries explicit management endpoint 0 through allocation,
registration, handler context, and completion. This closes the endpoint
linkage unknown, while the remaining executable recovery/power sequence gates
stay **FC**.
It also records the exact opaque six-u64 PM blocks for pinned G17P/HAL200 and
G17G/G17S HAL300 hosts without assigning field semantics or extrapolating to
G17A/G17C. For T6050/G17S it validates the measured 14-state core/SRAM
performance tables and their two mirrored firmware properties; rail ownership,
voltage control, a third domain, and hardware programming remain unresolved.
Its offline parser admits a G17S or G17C firmware identity only after one
atomic call validates both exact GFX and GFX1 `rkos/ftab` containers. The
parser enforces
the recovered two-entry geometry, selects only `a010` for G17S or `a000` for
G17C, and checks each nested Mach-O command layout, entry point, size, UUID,
and whole-image SHA-256. The resulting opaque token is variant-bound, so role
swaps, mixed S/C pairs, mutations, and an otherwise-valid pair for the wrong
GPU cannot clear the identity gate. m1n1 commit `62dd1dd9` now publishes a
versioned six-region no-map handoff for the exact measured T6050/J714s G17S
pair. Linux validates its ordered roles, UUIDs, IOVAs, remaps, sizes, flags,
and non-overlap, maps only the two `__TEXT` resources, hashes the actual loaded
bytes, and rejects failure before SGX MMIO is mapped. A successful token clears
only `FIRMWARE_IDENTITY`; power, UAT, recovery, transport, and submission stay
closed. G17G/G17P identities, relocations, fixups, complete cache ordering,
firmware acknowledgment, and live G17C ownership therefore remain fail-closed.
The same pinned G17X host and RTBuddy images now ground the firmware-start
handshake around those identities. The host clears the two role flags and the
combined byte, starts GFX then GFX1 through the shared `+0x888` role method,
requires both starts to succeed, and waits for either role callback before
publishing host-booted state. RTBuddy services `0x20` and `0x21` populate
objects `+0x128/+0x130`; after both converge they are enabled in that order and
the host role callback is invoked through authenticated slot `+0xb10`. The
callback records its role timestamp, wakes the combined event source, builds
the exact `0x0081`-prefixed message with a 44-bit token, and sends through role
slot `+0x898`. This is a built diagnostic state model, not a runnable boot
sequence: loader MMIO/power/cache ordering, interrupt acknowledgment,
power-callback causality, and the cold-boot-to-submission edge remain closed.
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
- [`agx3_pack_binary.c`](../../src/asahi/compiler/agx3_pack_binary.c),
  [`agx3_sr_core.c`](../../src/asahi/compiler/agx3_sr_core.c), and their
  [native compiler tests](../../src/asahi/compiler/test/test-agx3-sr.cpp): the
  incomplete production AGX3 backend, exact special-register reads, block-local
  scoreboard assignment, and fail-closed end-to-end packing.
- [`agx3_cmdstream.h`](../../src/asahi/genxml/agx3_cmdstream.h),
  [`agx3_cdm_decode_core.h`](../../src/asahi/lib/agx3_cdm_decode_core.h),
  [`agx_vdm_state_core.h`](../../src/asahi/lib/agx_vdm_state_core.h),
  [`agx_vdm_state_update_core.h`](../../src/asahi/lib/agx_vdm_state_update_core.h),
  [`agx_vdm_decode_core.h`](../../src/asahi/lib/agx_vdm_decode_core.h),
  [`agx_vdm_barrier_core.h`](../../src/asahi/lib/agx_vdm_barrier_core.h),
  [`agx_vdm_stream_terminate_core.h`](../../src/asahi/lib/agx_vdm_stream_terminate_core.h),
  [`agx_vdm_ppp_state_core.h`](../../src/asahi/lib/agx_vdm_ppp_state_core.h),
  [`agx_ppp_viewport_payload_core.h`](../../src/asahi/lib/agx_ppp_viewport_payload_core.h),
  [`agx_ppp_w_clamp_cull2_core.h`](../../src/asahi/lib/agx_ppp_w_clamp_cull2_core.h),
  [`agx_vdm_index_list_core.h`](../../src/asahi/lib/agx_vdm_index_list_core.h),
  [`agx_vdm_stream_link_core.h`](../../src/asahi/lib/agx_vdm_stream_link_core.h),
  and their focused tests: exact target-gated AGX3 CDM
  launch/link/padding/barrier/terminate, the exact eight-byte PPP State Update
  header, its bounded viewport-only and W-clamp/Cull-2 pointed payloads, the
  complete raw-preserving 28-byte VDM State Update, direct and indirect VDM
  index-list representations, and direct 40-bit VDM Stream Link plus the fixed
  Stream Terminate. Honeykrisp and D3D12 now select the grounded draw records,
  Honeykrisp selects the directly grounded links, and both front ends select
  the fixed terminators; ICB construction, queue submission, and device
  admission remain gated.
- [`agx_texture_side_entry.h`](../../src/asahi/lib/agx_texture_side_entry.h)
  and its [focused tests](../../src/asahi/lib/tests/test-texture-side-entry.c):
  exact 8-byte Gen9/Gen10 texture-heap side-entry layout and the bounded,
  fail-closed packing subset.
- [`d12_native_descriptor_core.h`](../../src/asahi/d3d12/d12_native_descriptor_core.h)
  and its
  [host tests](../../src/asahi/d3d12/tests/d12_native_descriptor_core_tests.c):
  generation-sized D3D12 heaps and fail-closed software-PBE rules.
- [`d12_agx3_ppp_viewport_payload_core.h`](../../src/asahi/d3d12/d12_agx3_ppp_viewport_payload_core.h)
  and its
  [host tests](../../src/asahi/d3d12/tests/d12_agx3_ppp_viewport_payload_core_tests.c):
  the D3D12 validation bridge for the same exact viewport-only PPP envelope,
  without live viewport lowering or command emission.
- [`d12_agx3_ppp_w_clamp_cull2_core.h`](../../src/asahi/d3d12/d12_agx3_ppp_w_clamp_cull2_core.h)
  and its focused host tests: the strict D3D12 encode/decode bridge for the
  exact twelve-byte target-aware W-clamp/Cull-2 payload, without live state
  lowering or emission.
- [`hk_native_descriptor.h`](../../src/asahi/vulkan/hk_native_descriptor.h),
  its [packing helper](../../src/asahi/vulkan/hk_native_descriptor_pack.c), and
  [focused test](../../src/asahi/vulkan/tests/test-hk-native-descriptor.c):
  generation-sized Honeykrisp image records, Gen9/10 sampled-texture selectors,
  native Gen9/10 PBE hardware words, and the fail-closed AGX3 software-sideband
  policy.
- [`hk_agx3_vdm_decode.h`](../../src/asahi/vulkan/hk_agx3_vdm_decode.h) and its
  [focused test](../../src/asahi/vulkan/tests/test-hk-agx3-vdm-decode.c):
  Honeykrisp's fail-closed bridge to the shared grounded VDM record decoder.
- [`hk_agx3_ppp_viewport_payload.h`](../../src/asahi/vulkan/hk_agx3_ppp_viewport_payload.h)
  and its
  [focused test](../../src/asahi/vulkan/tests/test-hk-agx3-ppp-viewport-payload.c):
  Honeykrisp's validation-only bridge to the shared bounded PPP viewport
  decoder.
- [`hk_agx3_ppp_w_clamp_cull2.h`](../../src/asahi/vulkan/hk_agx3_ppp_w_clamp_cull2.h)
  and its focused test: Honeykrisp's strict validation bridge for the same
  exact W-clamp/Cull-2 payload.
- [`hk_agx3_tile_core.h`](../../src/asahi/vulkan/hk_agx3_tile_core.h) and its
  [focused test](../../src/asahi/vulkan/tests/test-hk-agx3-tile-core.c): the
  bounded G15G/G16G/G17S single-sample Honeykrisp tile load/store bridge, with
  lowering, shader append, render-target state, submission, and Vulkan
  exposure kept closed.
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
  `ilshr`, `imul`, four-register `imadd`, compact f32 `add`, compact/long f32
  `multiply`, four-register f32 fused multiply-add, and register-register f32
  minimum/maximum plus signed/unsigned integer-to-f32 and f32-to-signed/unsigned
  integer RTZ mappings, the PC_f2ir-context `f32rint` selection, and the fixed-
  context scalar f32 reciprocal, structured comparisons/exec-stack/branches,
  scalar varying interpolation/coefficient-load/UVS-write primitives, and the
  target-gated G15G/G16G/G17S single-sample tile load/store subset into exact
  AGX3b/AGX3c encodings.
- [`d12_raytracing_core.h`](../../src/asahi/d3d12/d12_raytracing_core.h) and its
  [host tests](../../src/asahi/d3d12/tests/d12_raytracing_core_tests.c): DXR
  host contracts, explicit Tier-0 rationale, bounded BVH helpers, and exact
  generation/validation of the fixed AGX3b/AGX3c barrier-plus-`update_node`
  expansion, the bounded `intersect_ray` f5 pair, and the G17S-only fixed
  `update_bvh` witness.
- [`d12_discard_core.h`](../../src/asahi/d3d12/d12_discard_core.h) and its
  [host tests](../../src/asahi/d3d12/tests/d12_discard_core_tests.c): public
  `DiscardResource` list/region validation and the conservative no-op contract.

Any feature commit that changes an identity, generation axis, runtime gate, or
API capability must update this matrix in the same change. Promote a status
only with a linked evidence artifact and a focused test result; record a new
unknown as **FC** rather than inferring it from a neighboring chip. Runtime
support requires compiler, descriptors, samplers, command stream, kernel UAPI,
power/boot, and submission to be ready together.
