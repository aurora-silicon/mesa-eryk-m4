# Experimental M3 integration of current M4 Mesa

This isolated `m3-wayland` branch starts at M4 Mesa `6a30d044f34` and
reapplies the three M3 compute, graphics and repeated poisoned-readback
commits. The existing `m3-asahi` working tree, including its uncommitted
changes, is preserved. Target: J514S / T6030 / G15S B1.

The Vulkan driver, Zink, EGL and GBM cross-build together with:

```
-Dvulkan-drivers=asahi -Dgallium-drivers=zink -Dglx=disabled
-Degl=enabled -Dgbm=enabled -Dplatforms=x11,wayland
-Dgles1=disabled -Dgles2=enabled -Dllvm=disabled
-Dmesa-clc=system -Dprecomp-compiler=system -Dbuild-tests=false
```

The development installation is isolated at `/opt/asahi-m3-wayland`.
Its ICD is `share/vulkan/icd.d/asahi_icd.armv8-a.json`. Enable the M3 path
with `ASAHI_M3_EXPERIMENTAL=1`; hardware probes run with the M3 kernel's
watchdog and temperature guard. This is not a default desktop installation.

On 2026-09-20, four consecutive 128x128 offscreen Vulkan draws completed
with poisoned destinations and exact host pixel checks, checksum
`689fef1d5e252325`. Evidence in the source-collection workspace:
`_provenance/m3-compute-reuse-20260920/render06/reuse-render06-test/`.
The 256x256 probe faults with both the older M3 Mesa and this integration;
that failure remains under kernel/command-stream investigation.

This validates the updated compiler on this small graphics workload. It
does not establish broad ISA compatibility, conformance, or GPU Wayland.
Many M4 feature gates remain chip-specific and require M3 qualification.

## G15S helper-table correction

The large-frame fault was the generated USC lead's helper-table coverage.
G15S fell through to the six-entry G17P table. The M3 native capture has ten
entries, matching G16G. A larger zero-padded mapping alone merely moved the
fault to its guard; generated ten-entry tables completed the same workloads.
The production change selects ten entries for G15S and retains the normal
16 KiB base mapping. No captured helper library is needed or installed.

Evidence `render23/reuse-render23-test` under the same workspace provenance
root: separate Vulkan clients each completed four poison-checked draws at
512x512, 256x256 with culled geometry/clear, 257x139 and 128x128. Kernel
retirement and fault checks passed. Diagnostic controls are recorded in
render20 (native capture), render21 (generated six-slot 64 KiB image, failed),
and render22 (generated ten-slot 64 KiB image, passed). These controls are
not the production implementation. GPU Wayland remains the next objective.
