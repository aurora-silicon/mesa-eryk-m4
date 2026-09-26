# Native compute read scheduling

Issue device, shared, scratch and texture reads earlier within a basic block,
after register allocation. A read may cross at most 32 independent arithmetic
instructions. Preserve physical half-register RAW, WAR and WAW dependencies,
including every vector component. Do not cross other memory operations, system
register reads, uniform copies, barriers, coverage operations or control flow.
The native packer assigns scoreboard dependencies after this ordering change;
register cache and last-use hints are also regenerated afterwards.

This applies to ordinary AGX3B compute shaders. Constant programs, internal
shaders and other stages/encodings retain their existing schedules. It neither
changes arithmetic nor adds register allocation pressure.

## Validation, 2026-09-26

- 306 host compiler tests pass, including seven scheduling tests for independent
  reads, RAW/WAR/WAW overlap, half/vector registers, memory and system-read order,
  and texture reads. 24 captured hot compute/fragment inputs compile; the initial
  memory-only variant leaves fragment binaries unchanged; all 24 inputs also
  compile with the final texture-read scheduling variant.
- Both M3 Pro and M4 pass all ten selected, indexed Vulkan CTS 1.4.6.2 cases
  both with default options and with native/read-only preambles enabled (20
  executions per device):
  atomic_barrier_sum_small, branch_past_barrier, copy_image_to_ssbo_small,
  copy_ssbo_bounds, copy_ssbo_multiple_groups, copy_ssbo_to_image_small,
  image_barrier_multiple, shared_atomic_op_multiple_groups,
  shared_var_multiple_groups, ssbo_local_barrier_multiple_groups under
  dEQP-VK.compute.pipeline.basic. Selection retains the frozen catalog hash
  72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf.
- Saved Satisfactory Vulkan gameplay replay: frames 1/16/31 match all RGB
  pixels exactly on both chips. Same 3840x2160 output, 2880x1620 internal render,
  capture, replayer and adaptations. Thirty completed frames 2..31 are timed.
  Native read-only preambles remain explicitly enabled for this workload.
- Candidate repeats: M3 5.09477, 5.08640, 5.08997 FPS (median 5.08997);
  M4 3.63022, 3.63192, 3.62914 FPS (median 3.63022). Earlier baseline medians
  were about 5.0002 and 3.57537 FPS. Paired controls on the same current boots:
  M3 4.97855, 4.97668, 4.98071 FPS (median 4.97855); M4 3.55124, 3.57443,
  3.57050 FPS (median 3.57050). The candidate gains 2.24% and 1.67%
  respectively. Temperatures stayed below the startup gate during all timed
  runs; thermal policy and CPU ceilings were unchanged.

Watchdogs and 0.5-second all-channel thermal guards were active. No application
was launched, and the normal desktop Mesa was not replaced. These are saved
replay measurements, not live-game or whole-desktop qualification. M4 remains
below the user's 5 FPS target.

Checks: M3 20260926T061529034784Z; M4 20260926T061529268900Z.
CTS: M3 /root/gpu-replay-bench/runs/20260926-m3-native-read-early-cts;
M4 20260926T061826770833Z-native-read-early-cts. Full evidence retained in the
private, local benchmark repository.
