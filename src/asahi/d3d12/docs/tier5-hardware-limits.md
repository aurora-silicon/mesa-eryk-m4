# The last 13: what each needs, and what blocks it

321 of 334 corpus shaders compile. This documents the remaining 13 —
investigated 2026-08-04, deferred deliberately rather than abandoned.

They are the least valuable shaders in the corpus (hardware-limited
exotica) and the most expensive to make *correct*. Two of them cannot be
done correctly at all without a decision that trades robustness for a
number. Recorded here so nobody re-derives the analysis, and so nobody
"fixes" them by fabricating semantics.

---

## fp64 arithmetic — 6 shaders

`cs_denorm_fp16_fp64_fp32_{any,ftz,preserve}`,
`cs_denorm_fp64_fp32_{any,ftz,preserve}`

**Blocker: none. This is implementable; it is only large.**

Measured: the *only* 64-bit float operation across all six is `fadd`.
Everything else (`iadd`, `pack_64_2x32_split`, `load_const`) is integer
or bit manipulation that `nir_lower_int64` already handles. AGX has no
fp64 ALU, so that one `fadd` reaches the backend and dies in the
packer ("Disassembly error hit").

The shaders do this:

```hlsl
double2 v = ROBuf.Load<double2>(0);
precise double v2 = v.x + v.y;
RWBuf.Store<double>(0, v2);
```

The result is stored and checked, and the tests are *specifically about
denormal handling* — so a soft-float implementation must be **bit-exact
including denormals**. An approximately-right one is worse than none.

### The route

Mesa carries a soft-fp64 reference in `src/compiler/glsl/float64.glsl`
(Berkeley SoftFloat derived, 3-clause BSD — compatible, needs
attribution). `nir_lower_doubles(shader, softfp64, options)` consumes it,
but the softfp64 shader has to be compiled from GLSL, and **this build
has no GLSL frontend** (`-Dgallium-drivers=` empty, and Honeykrisp does
not need one). radv passes `NULL` and only gets the options that do not
need the library; full software fp64 does.

So: port `__fadd64` and its chain to `nir_builder` directly. It is far
shorter than the GLSL because NIR has native 64-bit integer ops (lowered
later by `nir_lower_int64`), so the manual 32-bit half-splitting in
`float64.glsl` collapses. Estimate ~200 lines for:

- `__extractFloat64{Sign,Exp,Frac}` — shifts and masks
- `__shift64{,Extra}RightJamming` — the sticky bit is what makes rounding
  correct; this is where a sloppy port goes wrong
- `__add64` / `__sub64` — native 64-bit int
- `__countLeadingZeros32` — `nir_uclz`
- `__roundAndPackFloat64` / `__normalizeRoundAndPackFloat64`
- `__propagateFloat64NaN`, `__packFloat64`

### The condition on doing it

**Ship it with a bit-exactness harness or not at all.** A `g5_fp64` GPU
test that computes `a + b` on the GPU and compares against host `double`
across a vector that includes: zeros and signed zeros, denormals either
side, values whose exponents differ by 0/1/52/53/large, overflow to
infinity, NaN propagation, and ties-to-even rounding cases. The host
`double` is a genuine oracle — this is one of the few places in the
campaign where an exact reference is free.

Without that harness this becomes six more shaders that compile and are
unproven, which is the pattern already flagged as the campaign's
dominant risk.

---

## 64-bit atomics — 3 shaders — HARDWARE WALL

`cs_64bit_atomics`, `cs_64bit_atomics_shared`, `cs_64bit_atomics_typed`

Honeykrisp reports `shaderBufferInt64Atomics`, `shaderSharedInt64Atomics`
and `shaderImageInt64Atomics` all **false**. AGX has 32-bit atomics and
**no 64-bit compare-and-swap**, which is what a lock-free emulation would
need.

Note `cs_64bit_atomics_shared` currently reaches `DXIL->NIR OK` and then
dies silently in the backend — it is *not* working, it just fails later
than the others.

**The only correct emulation is a per-address spinlock**: acquire a
32-bit lock word, do the 64-bit read-modify-write non-atomically, release.
That is:

- slow (serialises every access to an address),
- needs a lock word allocated alongside every 64-bit atomic target, and
- **can deadlock under divergence** — if two lanes of the same wave
  contend for one lock and the hardware does not let the holder make
  progress while the waiter spins, the wave hangs.

That last point is a real robustness tradeoff, not an implementation
detail. **Decision required before implementing.** Leaving them refused
by name is the honest default.

---

## attributeAtVertex — 1 shader — CROSS-STAGE ABI WALL

`ps_barycentrics`

`GetAttributeAtVertex(attr, i)` reads a varying from a *specific* vertex
of the triangle rather than the interpolated value. AGX has flat
interpolation (which gives the provoking vertex only) and
`load_barycentric_pixel`, but no per-vertex fragment inputs. Honeykrisp
reports `fragmentShaderBarycentric = false`.

Emulating it means the **vertex** shader emits three flat copies of every
attribute the fragment shader might index — a change to the varying ABI
for every pipeline, made by a translator that only ever sees one shader
at a time. It cannot be done from the fragment side alone.

**Decision required**: change the cross-stage varying ABI to serve one
test, or leave unsupported. Note the report should stay honest either
way — the device does not currently claim barycentric support anywhere.

---

## WaveMMA — 3 shaders

`cs_wmma_alloca`, `cs_wmma_extract_insert`, `cs_wmma_layout_assumptions`

SM 6.9 cooperative matrix. The failures are "undefined operand: value N
kind INSTRUCTION" and "operand references an out-of-range value" — values
produced by `dx.op.waveMatrix*` intrinsics that are neither translated
nor numbered, so later operands resolve to nothing.

Needs the opaque matrix type modelled (layout, fragment-per-lane
mapping) and the whole `waveMatrix` family translated. AGX has no matrix
unit, so every operation lowers to plain per-lane arithmetic plus wave
shuffles. Tractable, but it is a feature, not a fix — and nothing in the
D3D12 ecosystem needs it yet.

---

## Why these were deferred

Ranked by value-per-hour they are the worst work available. Set against
them: **102 shaders compile today that have never executed** — every
geometry, tessellation, mesh, amplification, work-graph and ray-query
shader. Runtime dispatch converts those 102 from unproven to proven.
Tier 5 would add 13 more unproven.

The compile-vs-conform distinction in `replication.md` is the whole
point: a corpus number without execution behind it is a claim about the
compiler, not about the driver.
