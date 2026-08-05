# Unverified on hardware

**Read this before trusting `dev`.** The bench GPU wedged partway through
the 2026-08-05 session, so the last two commits compile and pass the
offline harness but have **never executed on the GPU**.

Delete this file once the list is empty.

## Why the GPU stopped

```
d3d12agx_g1_smoke   PASS      device + queue creation
d3d12agx_g2_smoke   HANGS at:
    ok - queue Signal(fence, 1) after execution      <-- never returns
vulkaninfo          HANGS     a plain Vulkan client, not this driver
```

Device creation works; anything that **submits** hangs on the fence wait.
`vulkaninfo` hanging is what makes it conclusive — the wedge is system
wide, not ours. Bisected: `g2_smoke` hangs with the session's uncommitted
changes stashed, so it is not the DXBC work.

Most likely cumulative: ~159 vkd3d-proton tests crash outright per full
run and the suite was run about ten times, so a great many clients died
with work in flight. **It needs a reboot** — the driver has not recovered
on its own and the reset paths are not exposed.

## The last VERIFIED state

`3709b52` — *DXBC structured buffer load/store*. All 17 GPU suites green,
corpus 321/334, vkd3d-proton 194/557. **Every number quoted anywhere in
the docs was measured at or before this commit.**

## What has NOT run on hardware

### `e1c0858` — DXBC raw and typed UAV stores, plus a survey mode

The suite run immediately after this one is the run that hung, so it has
never been seen working.

| change | risk | what to check |
|---|---|---|
| `store_raw` (opcode 166) | writes through a root-table address | a DXBC compute test that writes a raw buffer produces correct values, not zeroes or garbage |
| `store_uav_typed` (164) | assumes 32 bits per element | correct for R32 formats; a packed format would write at the wrong stride |
| `D3D12AGX_DXBC_SURVEY` | debug only, env-gated | inert unless the variable is set |

Lowest risk of the two: it only adds opcode cases. Nothing existing
changes behaviour.

### `3d56406` — DXBC geometry emit and coverage masks; offline harness

| change | risk | what to check |
|---|---|---|
| `emit`/`cut`/`emitthencut` and `_stream` forms | emits `nir_emit_vertex` / `nir_end_primitive` | a DXBC geometry shader emits the right vertex count — `g6_geometry` covers the DXIL path only |
| input coverage mask (file 35) | reads `load_sample_mask_in` | multisample tests |
| output coverage mask (file 15) | writes `FRAG_RESULT_SAMPLE_MASK` | ditto |
| `d12_dxil_compile` DXBC path | **tool only, not the driver** | cannot affect the runtime |
| `meson.build` adds `d12_dxbc.c` to the tool | build only | — |

## Verification order after the reboot

```bash
# 1. The GPU actually came back
timeout 60 _build/src/asahi/d3d12/d3d12agx_g2_smoke     # must PASS, not hang

# 2. Nothing regressed
src/asahi/d3d12/tests/run-suites.sh                     # must be 17/17

# 3. The compile proxy is untouched
src/asahi/d3d12/tests/corpus-passset.sh <corpus> | wc -l   # must be 321

# 4. Only then, the real number
d3d12agx/scripts/vkd3d-suite.sh                         # was 194/557
```

If step 2 fails, `git revert 3d56406 e1c0858` returns to the last
verified state — neither commit is depended on by anything else.

## Also worth re-checking, verified but only once

These were green when they landed but have had little exposure:

- **fence events on a waiter thread** (`SetEventOnCompletion` with a real
  event). Thread lifetime is refcounted against the fence, but it has
  only ever run in the vkd3d suite, never under the GPU suites.
- **query heaps**. Created and released correctly; `BeginQuery`,
  `EndQuery` and `ResolveQueryData` are all still stubs, so no query
  returns real data.
