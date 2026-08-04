# d3d12agx findings — traps, and how they were established

Each entry cost at least one wrong hypothesis. They are recorded because
every one of them is invisible in the code afterwards: the fix is small, the
reason it was needed is not.

The standing rule that produced most of these: **measure, don't guess.** When
you have a hypothesis, first ask what one number would falsify it, then go
get that number.

---

## The AGX substrate

### A texel fetch consumes a sampler
`txf` lowers to `smpi ... ss0` — the backend reads sampler state 0 even
though HLSL's `Load` names no sampler. Nothing in an application will ever
supply it, so the device owns one (`agx_pack_txf_sampler`, mirroring
`hk_device.c`). **Symptom when missing: every fetch returns zeroes**, with a
perfectly correct descriptor.

The count matters as much as the word: `sampler_state_register_count` must
follow `info.uses_txf`, or the hardware never loads the state at all.

### Uniforms are published BY REFERENCE, always

`agx_usc_uniform(&b, base, regs, address)` fills uniform registers *from
memory at `address`*. Publishing a **pointer** therefore takes two steps:
upload the pointer somewhere, then point the uniform at that -- the
address-of-an-address the root table already uses.

Pointing a uniform straight at the struct instead is silent and
plausible-looking: the shader loads the struct's first eight bytes and
uses them as the address. `poly_vertex_params` begins with
`index_buffer`, so the software vertex shader took the index buffer to be
its own parameter block and wrote nothing anywhere, with no fault.

A **value** uniform is the opposite: the mask behind
`D12_UNIFORM_VS_OUTPUTS` is published by pointing the uniform at the mask
itself. Getting these two backwards is invisible in the driver and
obvious in one line of `agxdecode`.

### Samplers are state registers, not heap handles
The backend's sampler operand is a **16-bit index into sampler state
registers**, loaded by the USC SAMPLER word. `bindless_sampler_agx` is a
different mechanism entirely (a device sampler heap) whose heap offset is its
*second* source.

Feeding it a 32-bit byte offset fails the AGX IR validator with
`expected el size 1, got el size 2`. A constant sampler slot is
`tex->sampler_index`; a computed one is a 16-bit `nir_tex_src_sampler_offset`
source — and the source *count* must not include the sampler in the constant
case, or one source is left uninitialised and every constant-sampler shader
crashes in `nir_instr_insert`.

### Shader rodata is not published for you
Constants the compiler parks in `info.rodata` must be published with
`agx_usc_immediates`. Nothing else does it — a shader with rodata otherwise
computes with whatever the previous dispatch left in those uniforms. Silent
and latent.

### agx_fast_link does not copy the PREAMBLE

`agx_fast_link` links main + prolog + epilog into `linked.bo`. The
preamble is **not** copied there, so `info.preamble_offset` is
meaningless against `linked.bo->va->addr`. The USC then points the
preshader at whatever sits at that offset, which decoded as a run of
`jmp_incomplete 0` and killed the render command outright -- the target
was not even cleared, which reads like a missing draw rather than a bad
shader address.

Keep the compiled part's own BO and take the preamble address from it.
hk stores exactly this as `hk_shader::preamble_addr` (hk_shader.c:1856).

It went unnoticed for the whole campaign because no graphics shader here
had a preamble until the geometry rasterization program: with
`promote_constants = false` the vertex and fragment shaders never
generated one. The entry below is the same landmine seen from the other
side.

### `promote_constants` needs a correctly addressed preamble
Turning it on fails every graphics test while changing nothing about compute.
Cause: the graphics path takes the preamble's address from
`st->info.preamble_offset` (pre-link) but the code from `st->linked.bo`
(post-`agx_fast_link`). The offset does not survive the link, so the GPU runs
whatever sits at that offset. Fix the address before re-enabling it.

### Builder-created NIR is marked `info.internal`
The AGX backend routes internal shaders through an extra scratch pass pair
intended for CL kernels, which leaves small arrays as variable derefs the
emitter cannot take (`UNREACHABLE("should've been lowered")` on
`nir_instr_type_deref`). Application shaders must clear `info.internal`.

### Lowering passes are stage-agnostic even when their placement isn't
`agx_nir_lower_multisampled_image_store` was only run for fragment shaders;
compute MS stores reached the backend raw. Likewise
`agx_nir_lower_discard_zs_emit` gates its demote lowering on
`info.fs.uses_discard` — a translator that emits `demote` without setting the
flag gets an assert, not a lowering.

### Colour stores must be 4 components
`agx_nir_lower_tilebuffer` trims to the render target's component count, and
trimming can only narrow. A shader writing `.xy` to an RGBA target must still
hand the lowering a 4-vector; the padding lanes are undef, which is exactly
what D3D says unwritten components are.

### `load_uvs_index_agx` is the driver's to lower
`agx_nir_lower_uvs` emits it for indexed output stores. Honeykrisp defers to
draw time via its root descriptor because linking happens there; a driver
whose layout is fixed at compile time can resolve it to a constant from the
same `agx_assign_uvs` the PSO runs.

### Vertex inputs need `agx_nir_lower_vs_input_to_prolog`
Building a prolog is not enough — the *main* shader must be lowered too, or
`load_input` reaches the backend unhandled.

### NIR fixes shift amounts at 32 bits
A 64-bit shift takes a 32-bit count. Any pass that harmonises operand widths
must exempt shifts and convert the amount instead.

---

## DXIL / LLVM 3.7 bitcode

### GLOBALVAR hides the address space in the flags
DXC's LLVM 3.7 does not write a pointer type for operand 0 — dumping
`groupshared uint scratch[64]` gives `[64 x i32]`, an array, with the address
space folded into operand 1:

    flags = addrspace << 2 | explicit-type | isconst

Reading the address space off operand 0's type finds nothing, and every
groupshared array looks like address space 0.

### ALLOCA names the ALLOCATED type
Not a pointer to it. `BitcodeWriter` pushes `getAllocatedType()`.

### The constant block pre-reserves the value slot
A constant record's value is pushed *before* the per-code switch. Pushing
another inside a case shifts every later value id in the module — which
presents as a mysterious "operand is not a constant or a defined instruction"
far from the cause.

The same class of bug produced a long-lived mystery where an alloca consumed
by a pointer **bitcast** looked undefined: pointer identity must propagate
through bitcasts and selects-between-pointers, since neither has an SSA value.

### `atomicrmw`'s operation is a literal
It is not a value reference. Running it through relative-to-absolute value
decoding silently produces a bogus index.

### DXC truncates trailing undef operands
A 1D gradient sample legitimately ends at `ddy0`, three operands short of the
fixed signature. Reads past the operand count must mean "absent", not a
refusal.

### A hull shader has TWO function bodies

DXIL splits it: the entry runs once per output control point, and a
separate **patch constant function** — named by `HS_STATE` operand 0 — is
the only thing that writes `SV_TessFactor`. NIR and poly's TCS lowering
follow GLSL, where one shader writes both, so the bodies have to be
merged. Translate only the entry and the factors stay zero, the
tessellator counts zero vertices, and the draw is empty with no error
anywhere.

Two traps in merging them:

- **Entry selection.** DXC emits the patch constant function FIRST, so
  "the module's first definition" picks the wrong body. 18 corpus hull
  shaders were compiling their patch constant function and counting as
  passes.
- **Appending it as new blocks segfaults `nir_lower_goto_ifs`.** The
  entry's terminating `RET` has just been dropped, so its final block has
  no edge to them. Concatenate into the entry's last block instead, and
  refuse a patch constant function that has control flow of its own.

### Relative operands are numbered per FUNCTION, not per module

LLVM discards a function's local values when the function ends, so every
body numbers its locals from the same module-level base. A parser that
keeps one global value array and decodes relative operands against its
running length is off by the previous bodies' local count in **every body
after the first**.

Invisible in a module with one body, which is every shader except a hull
shader. It presented as `call to a non-dx.op function`: the callee
resolved to an instruction five slots past the intrinsic it meant.

### Tess levels are not generic patch constants

`patch_outputs_written` is indexed **from `VARYING_SLOT_PATCH0`**, and the
tess-level slots sit BELOW it. `BITFIELD64_BIT(slot - VARYING_SLOT_PATCH0)`
therefore underflows and sets a garbage bit; `util_last_bit` reported 28
patch constants where there were none, and `poly_tcs_out_offs_el` pushed
every per-vertex output past the end of the stride the driver had sized.
Tess levels belong in `outputs_written`, which poly places at fixed
offsets 0..5.

### Hull and domain shaders address control points PER VERTEX

A hull shader's ordinary outputs are per control point and a domain
shader's inputs are per control point. Both must use the per-vertex
intrinsics indexed by the invocation ID / the `loadInput` vertex operand.

`poly_nir_lower_tcs` addresses a plain `store_output` with an
**undefined** vertex index, so every invocation of a patch writes the
same slot and all but one control point is silently never written. A
domain shader that falls through to the vertex-attribute path gets
`VERT_ATTRIB_GENERIC0` slots and no index, so every corner reads the same
undefined attribute.

All three of the above present identically — a fully tessellated patch
that draws no pixels — and none is visible without dumping the TCS buffer
between the stages.

### Mesh and amplification shaders do not use the compute NumThreads tag

Their thread count lives inside the **MSState** entry property (tag 9)
and **ASState** (tag 10) — operand 0 of each is the numthreads *node*,
not a scalar. A parser that only knows tag 4 finds nothing and leaves the
workgroup at 1×1×1, so only the first thread of every mesh shader runs
and only the first vertex is ever written.

MSState is `{numthreads, maxVertexCount, maxPrimitiveCount,
outputTopology, payloadSizeInBytes}`; ASState is
`{numthreads, payloadSizeInBytes}`.

### A mesh shader's outputs_written is always empty

It writes through global stores into the driver's parameter block, not
`store_output`, so its NIR output mask says nothing about what it
produces. Gating the rasterisation shader on it produced a program that
wrote only the default point size — `ldimm r0, 1.0; st_var r0, 4; stop`.
Slot 0 is POSITION by the output layout and is unconditional.

The per-vertex stride is likewise FIXED at `D12_MESH_MAX_SLOTS` records
rather than the live slot count, because that is what the translator
emits for `storeVertexOutput`. Both sides must agree or vertex N is
written at one offset and read at another.

### An AGX indirect grid is SIX words, not three

`AGX_CDM_MODE_INDIRECT_LOCAL` reads three THREAD counts followed by three
local sizes, and the `CDM_LOCAL_SIZE` packet is then omitted from the
stream. That is what makes a GPU-written dispatch possible at all: the
API gives thread GROUPS, the hardware wants threads, and with an indirect
grid the multiply cannot happen on the CPU. A setup kernel has to write
both halves.

### Do not overload a slot the translator writes

The mesh draw descriptor was parked in `D12_MESH_OFF_DISPATCH`, which is
where the translator writes `dispatchMesh`'s grid. It worked for as long
as nothing wrote that slot — and would have silently clobbered the draw
the moment an amplification shader existed. Driver-side scratch belongs
in slots the translator never touches.

### The container declares the stage — honour it or refuse
`d12_dxil_container_stage()` returning plain `false` for shader kinds ≥ 6 let
a caller's default fill the gap, and library/mesh shaders silently compiled
as FRAGMENT — inflating a pass count with results that meant nothing. Read
the raw kind and refuse by name.

---

### A binop's operation is chosen by its operands' TYPE — so every value needs one

**This was the g5_texture wall, and it cost more than anything else in the
campaign.** LLVM 3.7 bitcode has one `BINOP` record: code 0 is `add` for
integers and `fadd` for floats, code 2 is `mul` or `fmul`, and so on. The
record does not say which. **The operand type is the only thing that says.**

So a value whose `result_type` is left unset silently reads as an integer,
and arithmetic on it compiles to integer arithmetic *on float bits*:

    %13 = imul %9.y, %11 (0x437f0000)     <- should be fmul by 255.0
    %14 = iadd %13, %12 (0x3f000000)      <- should be fadd  by 0.5

Two records were missing a type:

- **`EXTRACTVAL`** — its result type is the *member* type of the aggregate,
  not the aggregate's. Every `dx.op` that returns a struct (`sample`,
  `textureLoad`, `bufferLoad`, `splitDouble`) reaches arithmetic through
  `extractvalue`, so every texture and buffer computation was affected.
- **`BINOP` itself** — a binop must record its own result type, or a *chain*
  of them loses the thread: `a*b + c` is a float add only if the multiply
  feeding it was known to be float.

The symptom was maximally misleading: the descriptor, the sampler, the USC
words, the uniform chain, the preamble and the `smpi` encoding were all
verified byte-correct through `agxdecode`, because they *were* correct. The
fetch returned the right texels and the shader then did integer maths on
them. **Nothing in the hardware-facing half of the driver was ever wrong.**

The experiment that finally pointed away from the descriptor: flipping the
descriptor's layout field Linear→Twiddled, confirming the encoded byte
changed (`02`→`12`), and observing **zero** change in the output. A
descriptor field that cannot affect the result is not being used the way
the theory requires.

---

## The g5_texture wall — the eliminations that were made along the way

`agxdecode` is wired into `d12_queue.c` (`ASAHI_MESA_DEBUG=trace`), which
was the roadmap's first recommendation and is now a permanent
instrument: it reads the USC words, descriptors and launch packets the
way the hardware does, rather than the way the driver believes it wrote
them. Everything below was verified through it and needs no re-checking.

**Proven correct, by Mesa's own decoder:**

- The texture descriptor. The decoder reads it as Dimension 2D (not
  multisampled), Layout Linear, Channels R8G8B8A8, Type Unorm, Width 16,
  Height 16, First/Last level 0, Address exactly the BO VA,
  `Stride: 0x30` = 48 = 64 − 16 (the field's bytes-minus-16
  convention), `Depth (linear): 1`. Byte-identical in shape to what
  `hk_image_view.c` builds for a linear 2D image.
- The uniform chain. The USC publishes halves 8-11 loaded from an
  address whose contents are the heap base, exactly 8 bytes on -- the
  address-of-an-address indirection, confirmed from the hardware side.
- The preamble. It is bound and it does write the bindless offset
  register with zero.
- The sampler. `Start 0, Count 1`, decoding to Nearest/Nearest,
  Clamp-to-border, Transparent black -- the txf sampler, resolving.
- The instruction. `smpi` is the ISA's "Sample with integer coordinates"
  (`intcoord="1"` in `AGX2.xml`), and our encoding matches the ISA's own
  test vector in `isa/test/cases.txt` almost bit for bit.

**Ruled out by experiment:**

- *Not* a bindless-vs-bound problem. Rebinding the texture through a
  USC TEXTURE state register produced the identical wrong answer, with
  the shader confirmed to have changed (`smpi ..., ts0, ss0`). Both
  paths read the same descriptor, so the fault is not in how the shader
  names the texture. (That change was reverted: it cost five corpus
  shaders and bought nothing.)
- *Not* a LOD or mip problem. Clamping the txf sampler to
  `maximum_lod = 0` with `MIP_FILTER_NONE` changed nothing, so the
  `auto_lod` mode a compute shader compiles to is not selecting a
  nonexistent level.
- *Not* the 32-bit coordinate tuple. `agx_nir_lower_texture` documents
  and builds a 32-bit tuple deliberately ("Combine into the final 32-bit
  tuple"), so two 32-bit coordinate registers are what the backend
  intends.

All of the above was correct, and none of it was the fault. It is kept
because it is a map of the hardware path that is now known-good, and
because it records how much verification can accumulate around a
hypothesis that was wrong from the start. The suspicion never left the
GPU-facing half of the driver; the bug was in the bitcode reader.

---

## Method

- **A loud failure beats a plausible wrong answer.** Every unimplemented path
  in the translator refuses by name. Do not replace one with a plausible
  constant: `startVertexLocation` returning 0 would compile and be wrong for
  every draw with a nonzero start vertex.
- **The count hides regressions.** Compare the pass *set*, not its size:
  `comm -23 before.txt after.txt` must be empty. A change that trades one
  shader for another shows no movement in the total.
- **A test expectation computed by hand is a hypothesis, not an oracle.**
  `g4_triangle` asserted the triangle covered exactly a quarter of the
  image, from a comment describing vertices the shader does not have —
  and `g5_depth` graded at a point inside neither triangle it draws.
  Both passed for months. They passed *because* the translator was
  broken: integer adds on float bits gave `tri.dxil` vertices of
  (−inf, −1, ~0), garbage that happened to cover 25%. **Fixing a real bug
  made two green suites go red**, which is exactly backwards, and is the
  signature of an expectation calibrated against the defect.

  The fix is a model that derives the answer from the shader, so it
  cannot agree with a broken translator by luck. `g4_triangle` now
  rasterises the shader's own clip-space vertices on the CPU with D3D's
  documented pixel-centre sampling and top-left fill rule, and compares
  **all 65536 pixels**. It matched the GPU exactly — 10965 — including
  the 85 pixel centres that land precisely on the hypotenuse and are
  covered only because that edge is a left edge.

  Note what the weaker test could not have caught: strict-interior
  rasterisation gives 10880, and every one of those 85 pixels is a
  real fill-rule behaviour that is now verified rather than assumed.
- **A suite that nothing runs is not green.** `run-suites.sh` covered only
  the acceptance suites, so the G1-G4 milestone smokes went unrun. When
  the descriptor grew from 32 to 40 bytes for UAV counter addresses,
  `g3_smoke`'s hardcoded increment failed silently for a day. Every test
  binary belongs in the runner.
- **Assert the contract, not a literal.** That same assertion encoded a
  size that was correct when written and stale a day later. The check is
  now "a non-zero, opaque stride that separates two descriptors", which
  cannot go stale — and the separation is what the test's own pixel-exact
  clears already prove.
- **A test that prints usage exits 0.** Any harness that checks only the last
  line reads that as a pass. `tests/run-suites.sh` treats a missing verdict as
  a failure for this reason.
- **Tools must share the driver's shader key.** The standalone compile tool
  once used different `promote_constants`/`reserved_preamble` settings, so its
  disassembly described a shader the driver never ran — and several
  hypotheses were formed against it before that was noticed.
- **agxdecode writes to a FILE.** `ASAHI_MESA_DEBUG=trace` alone looks
  like it does nothing, because the decode goes to `agxdecode.dump.NNNN`.
  Use `AGXDECODE_DUMP_FILE=stderr ASAHI_MESA_DEBUG=trace`. The decoder
  went unused for most of this campaign for want of that one variable,
  and it then answered two bugs in a single line each: a uniform whose
  buffer decoded as zeroes, and a preshader that decoded as
  `jmp_incomplete 0`.
- **Do not build while another process is editing the tree.** One confident,
  wrong conclusion ("the translator broke rasterisation") came from a build
  taken mid-edit and was refuted by a clean rebuild.
- **A scalarizer ate two thirds of every triangle.** `dx.op.emitIndices`
  takes a mesh primitive's three indices in ONE call — there is no way to
  spell one third of it. `nir_to_dxil` runs `nir_lower_io_to_scalar` over
  all shader outputs, which split the index-triple store into three
  single-component stores; each then emitted a full `emitIndices` with
  its one component broadcast to all three operands. The GPU dutifully
  wrote `2 2 2 / 2 2 2` — a degenerate triangle per primitive, zero
  pixels — while the vertices, the counts and the draw were all correct.
  Fixed with a filter that keeps `VARYING_SLOT_PRIMITIVE_INDICES` stores
  vectorized. The tell was in the buffer dump, not the render target:
  **when a stage produces nothing visible, dump what the next stage
  reads.** That is now the fourth stage in a row where it was the whole
  debugging story.
- **A branch nested under an impossible condition never runs.** The mesh
  and amplification entry-property emission first landed inside
  `else if (shader_kind == DXIL_COMPUTE_SHADER) { if (shader_kind ==
  DXIL_MESH_SHADER) ... }`. It compiled, it looked right in review, and
  it emitted nothing. The symptom was an empty entry-property list, which
  is also what a shader with no properties looks like — so nothing
  complained. Reading back the emitted container with our own parser is
  what caught it.
- **DXIL's MSState topology is not a D3D primitive topology.** Every
  triangle-output mesh shader dxc produces carries a `2` in that field,
  where `D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST` would be 4. Confirmed
  against all fifteen corpus mesh shaders, which agree on 2. The line
  value remains inferred from the enum ordering and unconfirmed.
- **`dx.op.dispatchMesh`'s overload suffix is a struct type name**
  (`dx.op.dispatchMesh.struct.Payload`), not a scalar type. Mesa's
  descriptor-string function-declaration machinery cannot express that,
  so the declaration has to be built from explicit types.
- **Signatures are built from VARIABLES, not intrinsics.** A probe that
  emits `store_per_vertex_output` directly produces a valid shader with
  an EMPTY output signature — nothing walks the intrinsics to discover
  what was written. Declaring the output variable alongside the
  already-lowered store is what fills OSG1.
