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

### The container declares the stage — honour it or refuse
`d12_dxil_container_stage()` returning plain `false` for shader kinds ≥ 6 let
a caller's default fill the gap, and library/mesh shaders silently compiled
as FRAGMENT — inflating a pass count with results that meant nothing. Read
the raw kind and refuse by name.

---

## Method

- **A loud failure beats a plausible wrong answer.** Every unimplemented path
  in the translator refuses by name. Do not replace one with a plausible
  constant: `startVertexLocation` returning 0 would compile and be wrong for
  every draw with a nonzero start vertex.
- **The count hides regressions.** Compare the pass *set*, not its size:
  `comm -23 before.txt after.txt` must be empty. A change that trades one
  shader for another shows no movement in the total.
- **A test that prints usage exits 0.** Any harness that checks only the last
  line reads that as a pass. `tests/run-suites.sh` treats a missing verdict as
  a failure for this reason.
- **Tools must share the driver's shader key.** The standalone compile tool
  once used different `promote_constants`/`reserved_preamble` settings, so its
  disassembly described a shader the driver never ran — and several
  hypotheses were formed against it before that was noticed.
- **Do not build while another process is editing the tree.** One confident,
  wrong conclusion ("the translator broke rasterisation") came from a build
  taken mid-edit and was refuted by a clean rebuild.
