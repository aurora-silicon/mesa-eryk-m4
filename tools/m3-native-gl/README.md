# M3 native OpenGL experiment

Branch `experiment/m3-native-opengl-20261001` starts at unified Mesa
`e4911f77e871872598806a5622bcea00bed4fe69`. Native Gallium is adapted from
Neo snapshot `259af847` in `mesa/neo-minecraft-latest`. Current Aurora main
`ab0ab2209c4ea834168ec0d23da4d5edfc386015` lacks that fork's native AGX3
implementation and was not used for the import.

The port retains the real G15S identity, M3 kernel/frontend and reserved VA
ranges. Native GL uses Neo's Gallium resource/draw path, an explicitly separate
Gallium prolog/epilog ABI, our shared shader compiler, and qualified Gen4 sampler
packing shared with Vulkan. Internal native kernels compile this build's portable
libagx NIR for the real chip instead of executing AGX2 precompiled binaries.

This is an isolated bring-up branch. Vulkan in the staged prefix remains the
existing qualified binary; shared loader source changes have not yet qualified
rebuilt Vulkan. Version strings are not conformance claims. The normal desktop,
M4 installation and kernel configuration are unchanged.

Build Gallium `asahi,zink` with the existing M3/M4 cross toolchain, then build
`src/gallium/targets/dri/libgallium-26.1.4.so` and
`src/gallium/targets/dril/libdril_dri.so`. Stage with `stage-candidate.py`,
explicit `--source`, `--library`, `--dri-shim`, `--base`, and fresh `--output`.
Source patches and all installed library hashes are recorded.

Run `guarded-probe.py --renderer asahi --probe-mode MODE --default-only` with
`src/gallium/drivers/asahi/tests/native-gl-smoke.c`,
`--oracle-marker 'PASS: native-GL pixel oracle'`, and recorded library hashes.
Modes clear, draw, texture and depth run separately through M3's mandatory guard:
real 60-second watchdog, all-channel telemetry every0.5seconds,95C startup gate,
110C regulation and120C emergency limit. Failure stops subsequent GPU work.

All four initial pixel oracles pass without Zink and without reboot. The receipt
file identifies exact binaries and full external evidence. Minecraft replay and
broader GL correctness remain qualification gates; no native Minecraft timing
is claimed yet.
