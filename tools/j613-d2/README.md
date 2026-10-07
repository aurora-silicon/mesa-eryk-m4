# J613 25G83 daily-driver candidate (D2): session integration

Runtime files that put the J613's Hyprland session (SDDM greeter and user session) on the native GPU when the D2
kernel boots. With any other kernel they do nothing, so the 14.x daily kernel keeps its own setup on the same root.

- Kernel: `satchlj/j613-kernel-25g83` (aurora-silicon/dirtyroom-linux) at its D2 commits, release
  `7.1.9-eryk-m4j613-j613-d2`.
- Mesa: this branch, built into `/opt/mesa-j613-25g83-d2` (Gallium `asahi` + Vulkan `asahi`, meson
  `--prefix=/opt/mesa-j613-25g83-d2`). `j613-d2-gpu-env` refuses to start if `lib/dri/libgallium-26.1.4.so` is not
  the qualified build (sha256 `50edf703…`, built from `298de3f32e1`). Change `expected=` for your own build.

| File | Install to | What it does |
|---|---|---|
| `root-overlay/usr/lib/systemd/system-generators/j613-d2-generator` | same path | on D2 only: clones `/etc/sddm.conf.d` to `/run`, adds a Wayland greeter that runs Hyprland through `j613-d2-gpu-env`, binds the clone read-only into SDDM's mount namespace only, sets the Mesa paths for SDDM, and loads `asahi`, `gpio-shared-proxy`, `hid_apple_mtp_haptic`, `hid_magicmouse` and `brcmfmac` |
| `root-overlay/usr/libexec/j613-d2-gpu-env` | same path | checks the kernel release and the Mesa hash, picks the appledrm card by its stable path (`/dev/dri/by-path/platform-soc:display-subsystem-card`), sets the EGL/GBM/GL/Vulkan paths and `ASAHI_M3_EXPERIMENTAL=1`, clears software-rendering overrides, then execs its arguments |
| `root-overlay/usr/share/wayland-sessions/hyprland-j613-d2.desktop` | same path | "Hyprland (J613 25G83 D2)" session through the wrapper |
| `uwsm-env.d-99-j613-d2` | `~/.config/uwsm/env.d/99-j613-d2` | the same environment for a UWSM-managed session, on D2 only |

The generator also masks a `m4j613-test-autoreboot.service`: that unit is specific to our test setup. Drop that line
elsewhere.

Status: the packaged D2 root overlay passed offline checks. A boot of the D2 kernel with this integration has not been
run yet. The same Mesa build and environment ran the native desktop on the G54 kernel.
