# Windowed Vulkan Buoy

TekF's Shadertoy Buoy, https://www.shadertoy.com/view/XdsGDB, with the retained
original GLSL body and linear/repeating noise texture. Shader license:
CC BY-NC-SA 3.0, https://creativecommons.org/licenses/by-nc-sa/3.0/ .
See ../shadertoy-buoy/manifest.json for the source and texture hashes.

The user initially requested 1920x1080, then increased both machines to **4K
3840x2160** on 2026-09-18. The final player always shades a 3840x2160 UNORM
image. The resizable 960x540 logical-pixel SDL window displays a letterboxed
blit; Retina scaling and window resizing do not change the shader workload.
The shader wrapper supplies fixed resolution, time starting at 10, zero mouse,
and the Vulkan-to-WebGL fragment-coordinate Y conversion. No shader algorithm,
iteration count or texture content is changed. Colors are already gamma encoded
by the shader; the swapchain is explicitly SDR UNORM to avoid double encoding.

FPS and milliseconds/frame update each second in the window title and a small
on-image overlay (Hyprland may hide titles). They measure completed application
frames including queue/present overhead, not hardware scanout rate or isolated
GPU shader time. FIFO presentation, one application frame in flight. M5/macOS
versus M4/Linux changes both hardware and drivers: this is not an isolated
compiler comparison. Escape/window close quits. Default duration is 600 seconds;
`--seconds N` accepts 1..1200. There is deliberately no unbounded stress mode.

## Build

From the workspace root:

```
python3 tools/buoy-vulkan/prepare.py
cmake -S tools/buoy-vulkan -B build/buoy-vulkan/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/buoy-vulkan/cmake
cd build/buoy-vulkan
./cmake/buoy-vulkan --seconds 30
```

Preparation needs Pillow, glslangValidator and spirv-val; runtime needs Vulkan
and SDL2. It emits the exact same SPIR-V and raw texture for both platforms.
The prepare script accepts --workspace-root and --output for mirrored copies.
Without CMake on Linux:

```
g++ -std=c++17 -O2 main.cpp $(pkg-config --cflags --libs sdl2 vulkan) -o buoy-vulkan
```

On macOS use the Vulkan SDK's include/library locations with CMake's
Vulkan_INCLUDE_DIR/Vulkan_LIBRARY, and SDL2_DIR pointing at an installed SDL2
CMake configuration. SDL2 2.32.10 was built privately, not installed globally.

Always run from the asset directory (buoy.vert.spv, buoy.frag.spv, noise.rgba).
Optional BUOY_CAPTURE=/absolute/path.ppm saves the first rendered frame at time
10 using a single readback; omit it for clean timing. Warm-up/first-readback time
is included in the first FPS interval. Compare subsequent steady intervals.

## Machine launch details

mac-m5: /private/var/tmp/buoy-vulkan, logged-in GUI user eryk (UID 501).
Run through `launchctl asuser 501 sudo -u eryk` from an accessible directory.
VK_DRIVER_FILES and VK_ICD_FILENAMES explicitly select
/private/var/tmp/cts-comparison-20260915/sdk/macOS/share/vulkan/icd.d/libkosmickrisp_icd.json.
SDL_VULKAN_LIBRARY selects the same SDK's macOS/lib/libvulkan.dylib.
This uses KosmicKrisp, not MoltenVK. SDK 1.4.357.1 was already installed.

omarchy-m4: /root/buoy-vulkan, XDG_RUNTIME_DIR=/run/user/1000,
WAYLAND_DISPLAY=wayland-1, SDL_VIDEODRIVER=wayland,
ASAHI_M4_EXPERIMENTAL=1, ASAHI_MESA_DEBUG=nocompress,
VK_DRIVER_FILES=/root/buoy-version-default.json. This selects our tested private
sampler-version Vulkan library v7 (SHA256
f9fd7d2b6fabda42a7714b88438ca68b4c7e183feaac4e367a23a2ca3d4a937e).
Do not launch hardware workloads directly: use tools/gpu-experiment.py with the
mandatory 60-second watchdog, six-channel 0.5-second temperature monitor and
95 C cutoff. The 600-second application run uses a 660-second guard. Default
laptop desktop and global driver installation are unchanged.

The initial 1080p smoke run needed the ASAHI_M4_EXPERIMENTAL opt-in; its first
attempt failed device enumeration before GPU execution. The corrected run
completed under the guard. The final 4K evidence is recorded under
_provenance/experiments/20260917T220951619511Z-buoy-vulkan-window-4k;
local Mac output and both builds/captures are under build/buoy-vulkan.
