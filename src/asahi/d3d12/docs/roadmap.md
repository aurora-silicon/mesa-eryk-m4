# d3d12agx roadmap — from 219 compiling to a conformance number

Written 2026-08-04, at `dev`.

This document exists to be **attacked**. Every step states what it changes,
how you know it worked, and what would prove it wrong. Where a decision is
contestable it says so explicitly, in a **CONTESTED** note — those are the
places a second opinion is worth the most.

---

## 0. Where we actually are

**321 of 334 corpus shaders compile DXIL → NIR → AGX machine code.**
No SPIR-V, no Vulkan layer, no dxc at run time.

The corpus is vkd3d-proton's test shaders used as a **compile-only proxy**:

- **Compiling** means the translator and the AGX backend accepted it.
- **Conforming** means it produces the right pixels/bytes on hardware.

Six GPU acceptance suites pass; `g5_texture` fails on a known wall.

**The compiler is no longer the bottleneck, and the gap it leaves is now
the project's dominant risk.** Geometry, tessellation, mesh,
amplification, work graphs and ray queries all COMPILE. None of them
DISPATCH. That is 102 shaders in the "compiles but unproven" state, and
the GPU suites cover none of them.

### The remaining 13 — all Tier 5

Full analysis, including the two that cannot be done correctly without a
robustness tradeoff, is in
[`tier5-hardware-limits.md`](tier5-hardware-limits.md).


| Bucket | N | Why |
|---|---|---|
| fp64 arithmetic | 6 | AGX has no fp64 ALU; needs soft-float |
| WaveMMA | 3 | SM 6.9 cooperative matrix lowering |
| 64-bit atomics | 3 | hk reports all three Int64Atomics features false |
| `attributeAtVertex` | 1 | hk reports `fragmentShaderBarycentric` false |

Tiers 1–4 are **done**: named refusals (222), geometry and tessellation
on the `poly` emulation (269), mesh and amplification as compute (291),
work graphs and inline raytracing (321).

### What "done" means for tiers 2–4

Each shipped a documented ABI the runtime must honour, and none of those
runtimes exist:

- **Geometry** expands into four programs (count, main, copy, pre-GS);
  only the main body is compiled.
- **Tessellation** needs the tessellator dispatched around it.
- **Mesh/amplification** need buffer allocation and the amplification →
  mesh hand-off (`D12_UNIFORM_MESH_PARAMS`).
- **Work graphs** need a scheduler (`D12_UNIFORM_NODE_PARAMS`).
- **Ray queries** compile to "traversal found nothing", which is correct
  only because the device reports `RaytracingTier 0`. Real traversal
  needs BVH build plus a traversal kernel.

---|---|---|
| Work Graphs (SM 6.8 GPU scheduling) | ~21 | §3.6 — needs a scheduler |
| Raytracing (library + `rayquery`) | 9 | §3.6 — needs BVH + traversal |
| fp64 arithmetic | 6 | §4.2 — soft-float |
| WaveMMA (SM 6.9 cooperative matrix) | 4 | §4.4 |
| 64-bit atomics | 3 | §4.1 — hk reports the feature absent |

Tiers 1–3 of the earlier plan are **done**: the three named refusals
(222), geometry and tessellation via the `poly` compute emulation (269),
and mesh/amplification translated directly to compute (291).

---|---|---|---|
| **1 — no emulation** | `waveMultiPrefixOp`, immediate constant buffers, SM 6.8 draw params (§2) | 3 | **222** |
| **2 — emulation ALREADY IN TREE** | geometry 15, hull 18, domain 14 (§3) | 47 | **269** |
| **3 — greenfield, moderate** | mesh 19 + amplification 3 (§3.5) | 22 | **291** |
| **4 — greenfield, large** | Work Graphs ~21, raytracing 9 (§3.6) | 30 | **321** |
| **5 — numeric / ISA** | fp64 6, WaveMMA 3, 64-bit atomics 3, barycentrics 1 (§4) | 13 | **334** |

Two corrections to earlier readings of this corpus:

- The 29 "library" shaders are **not** all raytracing. About 21 are **SM 6.8
  Work Graphs** (`broadcast_custom_input`, `thread_custom_input`,
  `coalesce_custom_input`, `two_level_*`, `embedded_subobject`,
  `local_root_signature`). Emulating those means writing a GPU work
  scheduler — a different and much larger problem than RT traversal.
- Honeykrisp exposes `geometryShader` and `tessellationShader` as **true** on
  this GPU but exposes **no** mesh, task, raytracing or ray-query features.
  So Tier 2 is wiring; Tiers 3 and 4 are design.

---|---|---|
| Stage-locked (hull/domain/geometry/mesh/amplification/library/RT/node) | 98 | needs the GS/tess campaign (§3) |
| fp64 arithmetic | 6 | needs soft-float (§4.2) |
| WaveMMA (SM 6.9 cooperative matrix) | 4 | out of scope until it matters |
| Hardware-absent: 64-bit atomics ×3, attributeAtVertex, rayquery | 5 | §4.1, §4.3 |
| Mechanisms not built, each refusing BY NAME | 3 | §2 |

**222** is the ceiling of the *current architecture* — not the conformance
limit, and not a hardware limit. Every tier above it is reachable; they
differ in how much of the machinery already exists.

---

## 1. Runtime: make the compiled work actually run

This is the critical path. Everything in §1 gates real conformance numbers,
and §1.1 gates the other two.

### 1.1 — Break the g5_texture wall  ★ HIGHEST LEVERAGE

**Symptom.** 100 of 256 `Texture2D.Load` fetches return transparent black
(out of bounds) on a 16×16 texture with coordinates 0–15. Deterministic.

**Already verified correct by measurement — do NOT re-check these:**

- Descriptor unpacks to Linear / R8G8B8A8 / Unorm / 16×16, address exactly
  the BO VA, stride field 48 (the field's convention is bytes−16 ⇒ 64).
  Checked by unpacking the bytes against `cmdbuf.xml`.
- Texture memory at that VA holds the right texels (`px[0]`, `px[1]`,
  `px[16]` all match the CPU fill).
- Pointer chain at dispatch: `textures_gpu` → cell holding `heap_gpu` →
  descriptor. Printed and compared: MATCH.
- Lowered NIR is `txf`, `vec2(x,y)` in backend1, 2D, lod 0.
- Bindless offset is `slot * 24` bytes, matching `agx_bg_eot.c`.
- `uses_txf=1`, `sampler_state_count=1`, `texture_state_count=0`.
- hk imposes no restriction on sampling linear 2D images.

**Falsified hypotheses — do NOT re-run:** tiling, stride encoding,
descriptor address, bindless offset units, USC overflow, uniform register
count, missing rodata, missing sampler, sampler-count encoding,
`promote_constants`.

**Next moves, in order:**

1. Wire `agxdecode` into our submit path (`d12_queue.c`). It is not called
   today, so `ASAHI_MESA_DEBUG=trace` prints nothing useful. Mesa's own
   decoder parsing our command stream is the highest-information tool we
   have not used.
2. Dump the complete USC word sequence and diff it against one hk emits for
   a compute `texelFetch`. The remaining suspect is texture-unit *setup*,
   not the descriptor.
3. If both are inconclusive: build a minimal Vulkan compute `texelFetch`
   through Honeykrisp on the same box and diff the two command streams.
   Heavy, but it is the definitive oracle.

**Done when:** `g5_texture` passes, and the fix is explained in one sentence
naming the mechanism.

**CONTESTED:** it may be faster to skip straight to (3). The counter-argument
is that (1) is reusable infrastructure for every future runtime bug.

### 1.2 — Publish sampler state registers

**Contract already implied by the compiler** (landed in `0da20af`): a
constant sampler slot becomes `tex->sampler_index`, a computed one a 16-bit
`sampler_offset` source. Both index **sampler state registers**, which the
USC SAMPLER word loads — *not* a device sampler heap.

**Work:** build sampler state registers from bound sampler heaps and static
samplers, exactly as the device-wide `txf_sampler` at `ss0` is built today
(`d12_device.c`, mirroring `hk_device.c`).

**Done when:** a new `g5_sampler` GPU test samples a texture with a
non-default filter/address mode and matches a CPU reference.

**Depends on §1.1** — a sampling test cannot pass while fetches are broken.

### 1.3 — Publish the SM 6.6 descriptor heap

**Contract already implied** (landed in `dac8799`, `b6de66a`): heap handles
index `struct d12_descriptor` **in memory** at `D12_UNIFORM_HEAP`, with
`address` at offset 0 and `counter_address` at offset 8 — this is
shader-visible ABI, asserted in `d12_private.h`.

**Work:** publish the bound shader-visible heap's GPU address in the
reserved uniform, and make `CreateShaderResourceView` write hardware texture
descriptors into a parallel heap so heap-handle texture ops resolve.

**Unlocks:** every `ResourceDescriptorHeap[]` shader already compiling.

**Done when:** a `g5_heap` test binds a resource only via
`ResourceDescriptorHeap[i]` and reads correct data.

### 1.4 — Attribute-fetching vertex prolog

Compile side is done: `agx_nir_lower_vs_input_to_prolog` runs, so VS inputs
become prolog-exported registers. The runtime still builds only a
`static_vi` prolog that fixes up vertex/instance IDs.

**Work:** key a prolog from the PSO's `D3D12_INPUT_LAYOUT_DESC` and the
bound vertex buffers.

**Done when:** a draw with a real input layout renders correctly.

---

## 2. Close the three named compile mechanisms (→ 222)

Each currently refuses by name. Small, independent, verifiable.

| # | Item | Approach | Done when |
|---|---|---|---|
| 2.1 | `waveMultiPrefixOp` | partitioned waterfall — same shape as the WaveMatch loop in `6da7bea`, scanning within each mask partition | shader compiles; a `g5_wave` case checks the scan against a CPU model |
| 2.2 | Immediate constant buffers | upload the initializer data, lower `load_constant` to global reads (AGX backend has none) — needs the data-carrying-initializer path that `pointer_for_value` deliberately refuses today | `ps_immediate_constant_buffer` compiles AND a GPU test reads the right table values |
| 2.3 | SM 6.8 draw parameters | publish a per-draw sysval carrying StartVertexLocation/StartInstanceLocation; `AGX_ABI_VUNI_FIRST_VERTEX`/`BASE_INSTANCE` slots exist but only the prolog-building side maps them | `vs_draw_args` compiles and a draw with nonzero start vertex is correct |

**Note on 2.3:** a constant 0 would compile it today and be *wrong* for any
draw with a nonzero start vertex — which the runtime supports. It refuses on
purpose.

---

## 3. The geometry/tessellation campaign (→ ~290)  ★ BIGGEST WIN

**98 shaders are stage-locked** — the single largest bucket, ~29% of the
corpus. Almost all are hull/domain/geometry/mesh.

**The key fact:** this is *not* invention. Honeykrisp already reports
`geometryShader = true` and `tessellationShader = true` on this GPU,
implemented by compute emulation whose kernels are already in our tree:
`src/asahi/libagx/geometry.cl`, `tessellation.cl`, `tessellator.cl`, plus
`hk_nir_passthrough_gs.c` and the NIR lowering that feeds them.

**The work is wiring DXIL's GS/HS/DS into machinery that already exists.**

Suggested decomposition (each independently testable):

1. **Recon** — read how hk drives `agx_nir_lower_gs` / the tessellator:
   what NIR shape goes in, what dispatches come out, what the index/param
   buffers look like. Produce a written map before writing code.
2. **GS first** — geometry is the simpler emulation, and
   `hk_nir_passthrough_gs.c` shows the minimal case. Translate
   `emitStream`/`cutStream`/`storeVertexOutput` onto it.
3. **Tessellation** — hull (patch constants, control points) and domain
   (`domainLocation`, `loadPatchConstant`), driving `tessellator.cl`.
4. **Mesh/amplification** — different model again (`setMeshOutputCounts`,
   `getMeshPayload`); decide from the recon whether it rides the same path.

**Done when:** each stage's corpus shaders compile *and* a GPU test draws
the expected geometry — not compile-only, given emulation's many moving
parts.

**CONTESTED:** whether this belongs before §1. My position is no — §1 gates
the *ability to verify* anything in §3, and shipping 98 more compiling-but-
unrunnable shaders would repeat the mistake this project already corrected
once (the fake 109). But it is the biggest single number on the board, so
the argument is worth having.

---

## 4. Hardware-limited and out-of-scope

### 4.1 — 64-bit atomics (3 shaders) — DEAD unless the substrate is wrong
hk reports `shaderBufferInt64Atomics`, `shaderSharedInt64Atomics`,
`shaderImageInt64Atomics` all **false**. `cs_64bit_atomics_shared` *compiles*
today — flagged runtime-suspect in WORKLOG; do not trust it until measured.
**Worth one cheap experiment:** run the compiled shared-atomic shader and see
what the hardware actually does. If it works, hk is being conservative and
this bucket reopens.

### 4.2 — fp64 arithmetic (6 shaders)
AGX has no fp64 ALU. `makeDouble`/`splitDouble` already work (pure bit
assembly). Real arithmetic needs soft-float — a large deliberate build with
prior art in Mesa. Not scheduled.

### 4.3 — attributeAtVertex (1) / rayquery + RT pipelines
`fragmentShaderBarycentric = false` in hk today — an hk gap, **not proven
hardware absence**; worth a substrate check before writing it off. RT needs
compute BVH traversal: an entire project.

### 4.4 — WaveMMA (4)
SM 6.9 cooperative matrix. Skip until something needs it.

---

## 5. The real conformance number

Everything above is still proxy work. The actual G5 goal is
**vkd3d-proton's 557 D3D12 tests executing correctly.**

- `widl` is built and installed (aarch64), which unblocks
  `-Denable_tests=true`.
- Open question when we left it: the MIDL aggregate-return ABI and library
  naming.

**This should start as soon as §1.1 lands**, even at a low pass rate. A real
executing test suite finds classes of bug a compile proxy structurally
cannot, and every number after that is honest end-to-end.

---

## 6. Standing rules for whoever picks this up

- **Measure, don't guess.** State what one number would falsify a hypothesis,
  then go get it. Every expensive mistake in this campaign was a confident
  hypothesis acted on without measurement.
- **A loud failure beats a plausible wrong answer.** Every refusal in the
  translator names its mechanism. Do not replace one with a plausible
  constant.
- **After every change:** `tests/run-suites.sh` (all suites) and
  `tests/corpus-tests/corpus-passset.sh` (corpus). Diff the passset with `comm -23 old new` — the
  count alone hides regressions that trade one shader for another.
- **Never build while an agent holds the tree.** This produced one confident,
  wrong conclusion already.
- **Keep the offline mirror current.** The fork is also mirrored as
  format-patches over the pinned `mesa-26.1.4` tag; refresh it at session end
  and verify with a clean `git am` + `diff -r`. That is what proves the
  rebuild recipe, and it is the copy that survives losing the remote.
