# AGX3 (G17P) shader packer: what it cannot encode yet

**Status: BLOCKING, fix in flight.** No real compute workload could compile for
G17P, and therefore neither could a conformance suite run. This is independent
of the command-stream, launch and memory work — those now execute correctly.

The matrix below was measured from the runtime side (Vulkan pipeline creation)
against the tree as it stood before the compiler workstream began changing
`src/asahi/compiler/agx3_pack_binary.c` and friends. A compiler-side fix for
exactly this is now in flight in the same worktree — see
`src/asahi/compiler/test/test-agx3-multi-store.c`, whose own header describes
the same limit ("a single store encodes in every addressing form, but two
stores together were refused"). That test and `test-agx3-compile-fail-closed.c`
are currently red, so **a build taken while they are red carries half-finished
codegen and must not be used to measure encodability, nor shipped as an
artifact.** Re-measure once they go green; this file records what the limit
*was* and how to test it, not a claim about today's tree.

The gate is `agx_compile_shader_nir()` returning false, surfaced as
`VK_ERROR_FEATURE_NOT_PRESENT` from `vkCreateComputePipelines` with
"AGX backend cannot encode shader for G17P" (`hk_shader.c`, the
`vk_errorf(dev, VK_ERROR_FEATURE_NOT_PRESENT, ...)` after
`agx_compile_shader_nir`).

## Measured matrix

Bisected by compiling one-line GLSL variants and observing whether pipeline
creation succeeds. No hardware required: this reproduces entirely under the
G17P drm-shim (see "Reproducing" below), so it costs a laptop run, not a boot.

| shader body (one `main`, SSBO at set 0 binding 0)                  | encodes |
| ------------------------------------------------------------------ | ------- |
| `values[0] = 0x11110000u;`                                          | yes     |
| `values[0] = 0x11110000u + gl_GlobalInvocationID.x;`                | yes     |
| `values[gl_GlobalInvocationID.x] = 0x12340000u + gid;`              | yes     |
| `values[gl_LocalInvocationID.x] = 0x44440000u + lid;`               | yes     |
| `values[gl_WorkGroupID.x] = 0x11110000u + gid;`                     | yes     |
| `if (g<16) values[g] = A; else values[g] = B;`                      | yes\*   |
| `values[0] = A;` **and** `values[32u + g] = B;`                     | **no**  |
| `values[w] = A;` **and** `values[32u + g] = B;`                     | **no**  |
| `x = values[31u - g];` **then** `values[g] = 0x12340000u + x;`      | **no**  |
| any `atomicAdd` / `atomicMax` / `atomicOr` on the SSBO              | **no**  |

\* NIR flattens the two branches into a single store with a selected value, so
this is one store by the time the backend sees it — it is not a
counter-example.

## The rule, as far as it is pinned

**At most one SSBO access per shader.** Not "one store": a shader with one SSBO
*load* and one SSBO *store* also fails. A `load_preamble` / uniform read does
not count (every probe above performs two or three), and neither does a
constant-data (rodata) load — `g17p_vulkan_compute.comp` reads a 65-element
constant array and stores once, and that compiles and runs on hardware.

**Atomics do not encode at all**, in any position.

## Why it matters beyond conformance

It also bounds what any *diagnostic* probe can express, which is easy to lose a
day to. In particular:

- An invocation counter is impossible — counting needs an atomic.
- A probe cannot both write a fixed witness slot and exercise index-addressed
  stores, because that is two SSBO accesses.
- `g17p_vulkan_compute_count.comp` exists in the shape it does purely because
  of this: every invocation stores to the *same* slot with its own ID as the
  *value*, which is the most that one SSBO access can be made to reveal.

## Worked example: two shaders, one instruction apart

> **Caution:** the byte diff and the hardware behaviour table below are
> observations and stand. The *interpretation* originally attached to them —
> that the store's operand was unnamed and positional — is retracted; see the
> retraction section that follows. The real cause was a missing
> write-after-write hazard check.


The clearest instance of the underlying defect, captured byte-exact from GPU
memory (`HK_G17P_LAUNCH_TRACE=1`, shim, ICD `6f4d66bc`). Two compute shaders
differing by one line of GLSL:

```glsl
/* min   */ output_buffer.values[gl_GlobalInvocationID.x] = 0x12340000u + gl_GlobalInvocationID.x;
/* cover */ output_buffer.values[gl_GlobalInvocationID.x] = 0xcafe0000u;
```

Compiled output, aligned:

```
common prefix   47 bytes  67 00 54 00 ... 1c 80 02      descriptor loads, address
                                                        computation, LDIMM opcode
                                                        AND destination field
min    middle   15 bytes  12 00 00 a0 01                rest of LDIMM imm (0x12340000)
                          9f 01 56 02 02 04 00 a8 13 05 the iadd
cover  middle    5 bytes  ca 00 00 f0 07                rest of LDIMM imm (0xcafe0000)
common suffix   18 bytes  e7 20 54 02 04 00 20 00 11 00 00 90 18 00    the STORE
                          0e 00 00 00                                  stop
```

Three facts, each checked rather than inferred:

1. **The store is byte-for-byte identical in both** — same opcode, same operand
   fields, same wait mask. Address computation is therefore not the variable.
2. **Both LDIMMs encode their constant correctly.** The immediate's high byte
   is at byte 3 (`12` / `ca`) and its low byte at bits 3..10 of the final
   halfword: `0x34 << 3 == 0x1a0` and `0xfe << 3 == 0x7f0`, both matching. The
   constant is *loaded* correctly in both shaders.
3. The only structural difference is min's 10-byte `iadd`.

In min, the store's operands are produced by that `iadd`. In cover the `iadd`
does not exist — there is no arithmetic to perform — **and the store was not
re-encoded**. So cover's store issues naming a register that nothing in the
shader ever writes, and reads whatever it happens to hold. This is the
immediate-encoded-as-a-register defect in its purest form: a bare literal with
no arithmetic at all, so the constant has no path to the store.

The behavioural consequence, same ICD and same boot conditions:

| probe | value expression                | result                          |
| ----- | ------------------------------- | ------------------------------- |
| min   | `0x12340000 + gid`              | runs; writes index 0, correct   |
| full  | `0x12340000 + i + deltas[i]`    | runs; 8 of 32, `0x12340000+4*i` |
| count | one slot, value `= id`          | runs; `values[0] = 0x00000000`  |
| cover | bare literal `0xcafe0000`       | **GMMU fault, nothing written** |

All four are index-derived SSBO stores. The three that *compute* a value run;
the one storing a bare literal faults — because computing a value is what
causes an instruction to exist that writes the store's source register.

**Unproven, deliberately.** An unwritten source register explains a wrong
*value* cleanly. It does not by itself explain a GMMU *fault*. The most
plausible bridge is that the eliminated instruction also carried the scoreboard
wait that made the address safe, so removing it let the store issue before its
address had landed — but that has not been demonstrated, and it should not be
repeated as though it had.

**Use as a regression test.** Under corrected codegen the LDIMM's destination
must be the store's source register (or the store re-encoded to match). That is
checkable entirely under the shim with `HK_G17P_LAUNCH_TRACE=1` — dump the
shader binary and confirm the prefix is followed by an LDIMM and a store that
actually connect. No hardware, no boot. If they still do not connect, the fix
is incomplete.

## RETRACTED: the "positional data operand" model

**This section previously claimed that `device_store` names no source register
and stores whatever the immediately preceding instruction produced. That is
WRONG. Do not propagate it.** It is kept as a retraction because the claim was
circulated before it was checked.

### What is actually true

`device_store` **does** name its data register, at **byte 3, in half-register
units**. Confirmed two independent ways:

- A controlled encoder sweep varying only the data register, everything else
  fixed: `data=r0/r1/r2/r3/r5` produce byte 3 = `0x00/0x02/0x04/0x06/0x0a`.
- Mesa's own encoder says so in source: `agx3_mem_core.c common_overrides()`
  does `KV(2, d->data)`, and field 2 lands in byte 3.

The index register is likewise named explicitly, at byte 5.

`0x54` / `0x55` / `0x56` in byte 2 is **not** an addressing mode. It is the top
of the **scoreboard wait mask** (slots 1-4 live in byte 1; slot 5 sets byte 2 to
`0x55`, slot 6 to `0x56`). So the "undefined mode `0x55`" flagged here earlier
is simply a store waiting on slot 5 — entirely legitimate.

Our own data agrees once the fields are read correctly: `lid`'s store has byte
3 = `0x06` (half 6 = r3) and its `iadd2` writes r3. The operand was named, and
named correctly, all along.

### The real cause of the wrong values

A missing **write-after-write hazard check** in the AGX3 slot allocator
(`agx3_slot_core.c`), found by recompiling the exact `lid` binary and reading
the IR:

```
r0...r3 = device_load ... slot 0      <- async, claims r0,r1,r2,r3
$r3     = mov_imm #0x44440000         <- writes r3 while that load is IN FLIGHT
r3      = iadd ^r3, $r2l.abs, #0x0    <- the value lives in r3
device_store ^r3, ^r0:r1, ^r4.abs     <- waits for the load, THEN reads r3
```

Only `r0:r1` of the 4-word descriptor load are used, so the allocator reused r3
for the value. The store correctly waits for the load — and that is what kills
it: the load retires and overwrites r3 with the descriptor's **fourth word**
before the store reads it. The store writes that word. Descriptor word 4 is
zero, which is the observed zero.

The pass computed waits from **sources only** (read-after-write) and never
checked destinations; worse, a synchronous write cleared the pending tag, so
the in-flight load on that register was forgotten. Mesa's AGX2 pass has an
explicit write-after-write block; the AGX3 port dropped it. Fixed by adding the
destination check.

### Why the wrong model looked so convincing

The five-probe correlation in the worked example above is real: the probes
whose value-producing op was adjacent to the store were exactly the ones with
correct values. But adjacency was a *proxy*. What actually mattered was whether
an async load was still in flight over the register holding the value, and
"something scheduled in between" correlates with that without being it.

Three things should have lowered confidence at the time:

1. The field semantics came from a **secondary source** (the ISA-DB's prose)
   rather than from the encoder that emits the bytes. The DB's own sweep looked
   at bytes +8/+9/+11 and concluded the data register "is not in this
   instruction" — it was looking in the wrong place.
2. A correlation over five samples with a plausible mechanism is not a
   mechanism. There was no controlled experiment varying one field.
3. The model predicted that identical store bytes must mean identical
   operands. They *were* identical and the operands *were* correct — which
   should have been treated as evidence against the model rather than for it.

**When a value is wrong on this ISA, read the encoder source or sweep one field
at a time before theorising from a disassembler's field names.**

## Disassembling a G17P shader binary

```
cd g17-upstream/agx-re/tools/agx-isa
python3 agxisa.py tokenize <raw hex of the shader binary>
```

> **Caution:** this disassembler's `device_store` field assignment is wrong in
> at least three places. It labels byte 3 `extmode` when byte 3 is the **data
> register** (half-register units), labels byte 2 `addr_mode` when byte 2 is the
> top of the **scoreboard wait mask**, and its prose asserts the data register
> "is not in this instruction". Instruction *boundaries* and other families
> decode fine, and it is still the fastest way to see the shape of a program —
> but for `device_store` fields trust `agx3_mem_core.c` and the encoder, not
> this tool. Believing its field names cost an entire wrong theory; see the
> retraction above.

Shader bytes come from the driver: run a probe under the drm-shim with
`HK_G17P_LAUNCH_TRACE=1` and `grep -A9 "shader-binary"`.

```
cover (70 bytes, CORRECT)
67005400000000001dc00240020067005408010000005700004002001ca010469f50540002008010
502b22001c8002ca0000f007e7205402040020001100009018000e000000

lid (62 bytes, ZERO)
670054000100000017000040020024a410263c800244000020029f215606020c10a8130543042900
43000001e7105406000420001100009018000e000000
```

## Reproducing

```
docker run --rm -v <repo>:/workspace -w /workspace/<mesa worktree> \
  mesabuild:meson-1.7.2-llvm18-wsi2 bash -lc '
    apt-get update -qq && apt-get install -y -qq glslang-tools libvulkan1
    # regenerate tests/g17p_vulkan_compute_count_spv.h from your .comp, then
    ninja -C <build>
    cd <build>/src/asahi/vulkan
    LD_PRELOAD=$PWD/../drm-shim/libasahi_noop_drm_shim.so \
    ASAHI_SHIM_CHIP=g17p \
    VK_DRIVER_FILES=$PWD/asahi_devenv_icd.aarch64.json \
      ./g17p-vulkan-compute-count'
```

Two traps, both of which produced wrong answers before being caught:

- Use `asahi_devenv_icd.aarch64.json`, not `asahi_icd.aarch64.json`. The latter
  points at the *installed* `/usr/lib` driver, so the loader silently runs some
  other build and every result is about the wrong binary.
- Rebuild with a bare `ninja -C <build>`. Naming the executable target skips
  the regenerated SPIR-V header, so the previous shader keeps running and every
  variant "fails" identically.
