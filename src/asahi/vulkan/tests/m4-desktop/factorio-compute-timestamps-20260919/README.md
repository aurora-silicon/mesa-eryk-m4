# M4 compute-tail timestamps

HK_M4_COMPUTE_TAIL_TIMESTAMP=true attaches a timestamp to an already closed,
nonempty compute tail with an unused completion destination. It preserves
memory-barrier stream boundaries and excludes GPU-reset streams. No dispatch
moves across a barrier; no host time or delayed retirement time substitutes for
the firmware's hardware timestamp. Default remains false pending broader CTS.

Guarded J713 evidence:
- 20260919T020235882364Z-compute-tail-timestamp-host-reset: mode 7 passes
  ordered firmware timestamps, exact CPU/GPU copies and preserved fill data.
- 20260919T020423931692Z-factorio-compute-tail-frame: retained Factorio frame
  is pixel-identical to the original reference.
- 20260919T021343773048Z-factorio-volume-copy-menu: live 60-second observation
  succeeds with this flag and the separate compressed-volume-copy fix. Tail
  21.28 FPS in the scene with sixteen tiny copies per frame; not a performance
  comparison against different earlier scenes. 60 FPS is still unmet.

Known independent GPU-reset issue: mode 6 (GPU query reset, fill, barrier,
compute timestamp, NO-WAIT result copy) reports all results unavailable both
with this flag (20260919T020038639286Z-compute-tail-timestamp-nowait) and with
it disabled (20260919T020208261266Z-compute-tail-timestamp-baseline). Mode 7
changes only the reset to vkResetQueryPool and passes. Mode 5's WAIT copy hung
in 20260919T015748970645Z-compute-tail-timestamp-probe; the guard preserved the
fault dump and recovered the desktop. Use mode 6 for further diagnosis, not
unbounded WAIT loops. Do not count the GPU-reset case as passing or fixed.
Current Zink game path uses host resets. Firmware/GPU query cache ownership
remains an investigation, not an established root cause. Full CTS deferred.

## Compute-start reuse

HK_M4_COMPUTE_START_TIMESTAMP=true extends the existing timestamp-before-render
optimization to a nonempty M4 compute successor. It transfers the standalone
no-op's query destination into that successor's firmware start cell. Preserve
all dependencies and exclude GPU-reset successors, occupied starts and no-op
streams to which real work was appended. CDM UAPI serialization now includes
the already-supported start destination. This remains opt-in.

Mode 8 uses host reset, timestamp, execution-only barrier, fill, memory barrier.
The initial 20260919T022939881885Z-compute-start-timestamp-probe passed but did
not exercise absorption (the fill shared the no-op stream). The corrected
20260919T023007346003Z-compute-start-timestamp-closed-probe confirms ts_start=1
on a real compute command, ordered exact CPU/GPU query results and intact fill
data. 20260919T023025415950Z-factorio-compute-start-frame matches the original
reference with zero differing pixels. 20260919T023100327835Z-factorio-compute-start-menu
completes its 60-second observation, tail 42.98 FPS (38.41--44.70), peak 88.5 C.
Dynamic scenes prevent a paired FPS claim; 60 FPS and broad CTS remain pending.
