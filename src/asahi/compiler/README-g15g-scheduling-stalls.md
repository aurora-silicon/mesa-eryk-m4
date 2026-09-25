# G15G scheduling stalls

The M3 base GPU (G15G, T8122, J613) executes the AGX3b instruction set that
G15S/G15C/G15D, the G16 parts and G17P also use, but Apple's compiler pads
G15G code with extra 2-byte words. This file records what those words are,
exactly where Apple puts them, and how the rule was derived from Apple's own
compiler output. `agx3_g15g_stalls.c` implements it; `agx3_pack_binary()`
runs it on every finished shader part when the device key's
`scheduling_stalls` is set (`agx_chip_needs_scheduling_stalls()`, G15G only).

Nothing here has executed on G15G hardware. The claim is narrower: for every
shader in the corpus below, Mesa's pass turns Apple's G15G code with the
stalls removed back into Apple's G15G code, byte for byte.

## The rule

**Encoding.** A stall is 8 bytes:

    06 (W<<2)   06 00   06 00   06 00

i.e. a `nop` (`nop_ep0`, bits 0..4 = 00110) whose six-bit field at bits
10..15 -- the "anonymous" f2..f7 in `AGX3b.xml` -- is a scoreboard wait mask
W (bit n-1 = slot n, the same layout as `end`'s wait fence), followed by three
plain `06 00` nops. Every one of the 8,347 stalls in the corpus has exactly
this shape.

**Placement.** Treat an instruction as a *register producer* of slot s when
its decoded field vector has signal slot `f12 = s` (1..6) and destination file
`f9 = 2` (general registers): device and threadgroup loads (`lddev`, `ldtg`),
special-register reads (`rld`), texture results (`smp`), coefficient loads
(`ldcfr`) and threadgroup atomics with a return (`atomictg`). Producers into
the staging banks (`f9 = 4`: device atomics, `clr`; `f9 = 5`: texture/image
coordinate staging `copy`/`pack_smpir`/ALU ops, `uvsw`, `imgwci`, `depthf`,
`movmsk`) are not register producers.

Run a forward may-dataflow of the set P of slots that may still owe a
register result:

* a register producer adds its slot; an instruction's wait mask removes its
  slots;
* edges are fallthrough (not after `end`), `b`/`bn` targets, and one edge
  from each `cndst`/`cndef` (and conservatively `cndlt`/`cndcf`) to the
  `cndef`/`cndend` that closes its exec-mask region -- a region whose lanes
  are all inactive executes as a no-op, so a wait inside it may not have
  happened. Stack depth counts `cndst` pushes (`f12`) and `cndend` pops
  (`f10`);
* each program in the stream starts with P empty (the stream start, and the
  instruction after an `end` plus `06 00` filler, where Apple's container
  places the main program after the constant program).

Then every instruction whose wait mask W meets P gets a stall in front of it
whose value is `W & P`. `end` counts as waiting on all six slots, so it
drains any register load still in flight (Apple pads `end` after a dead
load). An instruction that is itself a waiting `nop` is left alone by Apple
(a lone `06 fc` wait-all nop appears identically in G15S code); Mesa, which
emits standalone wait nops, turns such a nop into the stall by appending the
three `06 00` words when it covers pending register slots.

**Branches.** `b` and `bn` carry a 48-bit signed byte displacement (field
`f0`, bytes 3..8) measured from the start of the branch instruction. After
padding, each displacement is recomputed as new(target) - new(branch), where
new(target) is the start of the stall in front of the target, if any, so the
wait also runs on the branch path. Nothing else moves: displacements change by
exactly 8 bytes per stall between branch and target.

What Apple does *not* key on: instruction count since the last stall, the
producer's latency class (a `sqrt`/`rsqrt`/`sin` result consumed immediately
never gets a stall -- ALU results are not scoreboarded), the distance from the
producer to the consumer, or basic-block boundaries as such.

## Evidence

Toolchain: Xcode Metal toolchain `Apple metal version 32023.921
(metalfe-32023.921.6)`, `AIR-NT 32023.921`, macOS 27.2 (26B5091g). Each
kernel is compiled on its own (`metal-nt -arch applegpu_g15g|g15s|g16g
-platform_version macos 27.0 27.0 -N pipeline.mtlp-json lib.metallib`), and
the code is the `__TEXT,__text` section of the Mach-O nested inside
`__TEXT,__compute` (render pipelines: `__vertex`, `__fragment`). The stream
starts with the constant program (`copy`; `end`), `06 00` filler to 0x40,
then the main program. `test/probes/g15g-stalls/run-corpus.sh` rebuilds
everything below from the witness sources in that directory.

Instructions are delimited with Mesa's `AGX3b.xml` Python walker and
decomposed into Apple's field vectors with the composer tables
(`agx3_compose/agx3_decompose.c`, a reentrant form of Aurora's
`decompose.c`): a decomposition is only accepted if `agx3_compose()`
re-encodes it to the same bytes. Every one of the 15,818 distinct
instructions in the corpus decomposes; where several field vectors round-trip,
they agree on every field the rule reads.

Corpus (`run-corpus.sh`, 1,540 shaders; a further 56 compute and render
shaders from the neighbouring `g16g-*.metal` probes were checked the same way
during development):

| set | shaders | what |
| --- | ---: | --- |
| main, main-fast | 274 + 274 | fp32/fp16 unary ops and transcendentals, dependency chains and consumers at distance 1..16, int ops, device/threadgroup memory, atomics, textures (sample/read/write/gather), branches/loops/switch/early return, subgroup and quad ops, system values, mixed kernels; with and without `-fno-fast-math` |
| targeted, targeted-fast | 47 + 47 | multi-slot waits, loop-carried loads, waits at joins, WAR on stores, texture operand staging, atomics with register and staging operands, SR reads |
| render | 26 | 13 vertex+fragment pipelines: varyings, textures, discard, derivatives, loops, atomics |
| fuzz, fuzz2 | 200 + 100 | random kernels: loads, textures, atomics, threadgroup memory, reductions, predicated `if`, loops |
| fuzz3, fuzz4 | 300 + 150 | a later generator adding `break`, switch, image reads, halves; generated after the rule was fixed, as a holdout |
| intuni, opt | 88 + 34 | Aurora's agx3-integer-uniform and agx3-optimization corpora |

Results (`run-corpus.sh`, 1,540 shaders, 8,347 stalls):

* Placement and value: the rule (Python reference model `predict.py`, and the
  C pass replaying Apple's stripped code) reproduces Apple's G15G bytes for
  1,540/1,540 shaders: 8,347/8,347 stalls, no extra stall, no miss, every
  value and every branch displacement equal. The fuzz3/fuzz4 holdout (450
  shaders, 4,186 stalls) was generated and compiled after the rule was frozen
  and matched without changes.
* Stall shape: all 8,347 are `06 xx` + three `06 00`.
* Value: 8,299 stalls equal the next instruction's wait mask; 47 are a strict
  subset of it (a multi-slot wait mixing a register producer with a staging
  producer carries only the register slots) and one precedes an `end` whose
  own wait field is 0. 35 stalls wait on more than one slot.
* Ablations: "a stall before every waiting instruction, value = its wait"
  matches only 700/1,540 shaders: 1,761 stalls Apple does not emit (`rld`
  readbacks, `smp`, device atomics, `cndst`, `uvsw`, `clr`, `imgwci`,
  `movmsk`, `depthf` waiting only on staging producers, and the constant
  program's `rld` waiting on nothing) and 48 wrong or missing (the subset
  values and the `end` drain); dropping the region-skip edges misses 9 stalls
  in 9 shaders; dropping the `end` drain misses 1.
* Streams: removing the stalls from G15G code gives G15S code byte for byte
  in 685 shaders and up to branch displacements (each off by exactly 8 bytes
  per stall crossed) in 46 more. The other 809 differ in register allocation,
  scheduling or instruction count -- Apple's G15G compile is not otherwise
  identical to G15S, especially for the fuzz kernels -- which is why the pass
  is checked against G15G code with its own stalls stripped rather than
  against G15S. G16G code equals G15S code in all 1,540.

## Tests

`test/test-agx3-g15g-stalls.c` (meson test `agx3_g15g_stalls`) replays 20
Apple shaders chosen to cover every clause above
(`test/agx3_g15g_stall_vectors.h`, regenerated by
`probes/g15g-stalls/gen-test-vectors.py`), checks idempotence (re-running over
padded code inserts nothing), offset mapping and fail-closed decoding. With a
path argument it also replays the full `vectors.txt` that `run-corpus.sh`
writes: 1,540 shaders, all byte-exact (`1560 shaders, 8424 stalls, 0
failures` including the 20 built-in vectors).

## Uncertain

* What the three `06 00` words do on hardware (delay slots for the wait to
  take effect is the obvious guess) is not established; the pass copies
  Apple.
* Waiting nops: Apple never needed to pad one in the corpus. Mesa's conversion
  of a pending-covering wait nop into a stall is the conservative choice, not
  a witnessed one.
* `cndlt`/`cndcf` region-skip edges are conservative additions; the corpus is
  matched with or without them.
* Mnemonics Mesa can emit that never appear in Apple's corpus (for example
  `lldcol`, `ldtps`, `imgwblk`) get their wait fields from the composer tables
  (fields depositing bits 12..17) and their producer class from `f9`; a
  form the decomposer cannot decode makes the shader fail rather than run
  unpadded.
