
## 2026-08-07 04:00 — Codex audit landed; second real bug found and fixed

Codex full-tree audit completed (traces/codex-audit-2026-08-07.md, ~1.7 MB with
reasoning trace). It cites file:line throughout, which is what made it checkable.

### VERIFIED AND FIXED: size_t underflow in d12_list_upload / _upload_zero

Codex's #1 improvement item. Verified by reading the code before acting:

```c
size_t floor = (list->data_floor - size) & ~(align - 1);   /* underflows */
if (floor <= used + D12_DISPATCH_STREAM_MARGIN) { ...refuse... }
memcpy(d12_list_stream_map(list) + floor, data, size);
```

Both operands are size_t. An upload larger than `data_floor` wraps `floor` to
near SIZE_MAX, which then **passes** the guard -- a huge floor reads as abundant
free space -- and the memcpy lands far outside the stream BO. **The bounds check
could be defeated by precisely the case it was written to catch.** Fixed by
checking `size > data_floor` before subtracting, in both functions.
mesa commit follows this entry. Heap corruption, reachable from any oversized
upload; not yet tied to a specific failing test.

### Codex's structural findings worth keeping (not yet acted on)

Cited, plausible, NOT independently verified beyond the two above:
- Views should own prepacked hardware descriptors, as hk does
  (`hk_image_view.c:248-288`); rebuilding texture/PBE state from the resource at
  draw time is why view format, mip/layer range and RTV address interpretation
  keep getting lost. This is the same root cause as tonight's SubresourceIndex
  bug and the sRGB/tilebuffer divergence — three symptoms, one design flaw.
- The descriptor ABI needs `{base,size}` and real heap identity; a flat
  address-only root table cannot express buffer OOB semantics or SM6.6 indexing.
- Rendering needs an attachment/load-store state machine;
  `ClearRenderTargetView` should not be the only way to create a render segment.
- PSO creation needs one unwind path — failure branches call `free(pso)` while
  linked BOs, code BOs and binaries have already been allocated.
- Copy and footprint logic should share one subresource abstraction. **This
  independently corroborates tonight's SubresourceIndex fix** and flags
  `GetCopyableFootprints` (`d12_device.c:1520-1577`) as having the same class of
  defect — worth checking next.

### Score for the outside-reports experiment so far

Claims that VERIFIED: SubresourceIndex ignored (2 agents + me), no BGRA on AGX,
1024 hardware sampler slots, MIT D3D reference tessellator in src/poly, size_t
upload underflow (Codex). Claims that did NOT verify: "dxil-spirv is MIT" (no
LICENSE reachable upstream; docs left conservative), and an agent's own subagent
inventing "1,099 tests" where the file has 557. **Roughly one in three
high-impact claims was wrong — the verification step is not ceremony.**

### Rebooting now to measure

Codex is finished, so the reboot is unblocked. Two fixes are committed and build
clean but are UNMEASURED: SubresourceIndex (6bf82ba79c8) and the upload
underflow. Measure after reboot in order: baseline -> subresource -> full suite
-> then re-try the unjudged format experiment.
