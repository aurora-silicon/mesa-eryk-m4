# J613 25G83 D3: session integration

The D3 version of `../j613-d2/`, as installed and qualified on the J613 on 2026-10-07. Same files and install paths,
renamed `d2` → `d3`, with two changes:

- **Greeter environment.** SDDM filters its own environment before it starts the Qt greeter, so on D2 the greeter
  never saw `ASAHI_M3_EXPERIMENTAL=1` or the Mesa paths: EGL failed to initialise and the greeter aborted. The
  generator now also writes `[General] GreeterEnvironment=` with that environment into the runtime SDDM config
  (supported by SDDM's helper, `src/helper/Backend.cpp`).
- **Mesa hash.** `j613-d3-gpu-env` expects the Gallium library built from this branch with the G15G index-alignment
  fix (`asahi: align G15G direct index uploads before drawing`): sha256 `edb7c015…`. Change `expected=` for your own
  build.

Kernel: `satchlj/j613-kernel-25g83` (aurora-silicon/dirtyroom-linux) up to its D3 commit, release
`7.1.9-eryk-m4j613-j613-d3`. Mesa: this branch built into `/opt/mesa-j613-25g83-d3` (Gallium `asahi` + Vulkan
`asahi`). With any other kernel release every file here does nothing. No UWSM file was used for D3; D2's
`uwsm-env.d-99-j613-d2` works with the release and prefix changed.

The generator masks `m4j613-test-autoreboot.service`, a unit specific to our test setup. Drop that line elsewhere.

Status: installed on the J613 and booted from the installed stage 1 (ESP); clean native-GPU SDDM greeter, see
`J613-NOTES.md`.
