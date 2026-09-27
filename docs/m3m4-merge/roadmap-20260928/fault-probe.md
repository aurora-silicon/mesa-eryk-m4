# Deliberately overlong finite compute: M4 failure propagation

Kernel `38ffd53dec64`, module SHA256
`f27d332e2c34743c7515b3610d70409122c499335d89e099f93fa337a74eacae`.
Shared Mesa binary remains
`912b52dd8c52f7a693e5ee84193cff891fe728ef308d7706b98a5362ce39ead8`.

A one-invocation compute shader performs one billion dependent integer steps,
using a count read from its input SSBO. It is a finite fault-injection workload,
not a conformance or performance test. The guard first stopped the display
session, armed/verified the 60-second watchdog, and sampled all thermal channels.

Observed:

- Kernel job 140 exceeded its bounded completion deadline and recorded errno
  -110 (ETIMEDOUT). This is the expected guard `gpu_timeout`, not a rendering Pass.
- `vkWaitForFences` returned -4 (VK_ERROR_DEVICE_LOST).
- A later queue submission returned -4 too.
- `vkDestroyDevice` and `vkDestroyInstance` returned; the probe printed its
  device-destroyed receipt. Global loss reporting is observable in userspace.
- Before recovery, the guard copied the 1,970,952-byte M4FWD001 devcoredump.
  Its first-error record preserves -110, with one in-flight command, no pending
  packets and DMA retirement still unproven. Snapshot timing is sequential,
  not an atomic hardware snapshot.
- Seven thermal samples were retained; peak 59.5 C. The guard requested a
  reboot with the watchdog still armed. The normal display manager returned.
- All 88 rendering smoke cases passed after recovery; peak 70.75 C.

The guard can terminate the surrounding Python wrapper once it observes the
kernel fault, so its final wrapper exit receipt may be absent. The flushed C
probe messages and guard/kernel/snapshot evidence are retained independently.

Evidence: `_provenance/experiments/20260927T213437534968Z-roadmap-m4-bounded-timeout`
and `20260927T213635473634Z-m4-first-fault-recovered-smoke`. Selected receipts,
compressed snapshot and complete probe sources are committed here. Compile the
probe with the vendored Vulkan headers, e.g. `aarch64-linux-gnu-gcc -O2
-Imesa/m3m4-merged/include tools/vk-buffer-oracle.c -ldl -o vk-buffer-oracle`.
The `M4_ORACLE_EXPECT_DEVICE_LOST` mode is explicit and does not make ordinary
oracle runs accept device loss. Run hardware only through `gpu-experiment.py`.

This proves one deliberate failure's API/evidence/fallback behavior. It does
not prove that DMA stopped, that two admitted packets both woke on hardware,
that resources were reclaimed, or that a GPU-only reset works. Those P1 tests
remain open; the conservative ownership retention is intentional.
