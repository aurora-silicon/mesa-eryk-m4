# Steam startup: Zink mutable modifier images

On J713, starting Steam through FEX/muvm aborted host Xwayland in
`zink_resource.c:create_image()` at `assert(format_list)`. The retained core
shows a 16x16 owned `B8G8R8A8_UNORM` resource with the Apple tiled modifier
`0x0c00000000000001`, promoted to mutable for an `R8G8B8A8_UNORM` render view.
The Gallium binding flags are `0x3000000a`; this is not a Vulkan CTS failure.

`init_ici()` removes the restricted linear/sRGB format list when general
mutability is requested. For modifier tiling, Vulkan still requires a nonempty
explicit list. The fix supplies the registry's compatible format class, filtered
to formats Zink exposes on the device, and includes uncompressed block-compatible
views when required. The assertion now checks the actual selected tiling, since
modifier negotiation can fall back to ordinary linear tiling.

Specification: [VkImageCreateInfo mutable modifier requirement](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageCreateInfo.html).

## Validation

ARM64 and x86_64 Gallium and the manual probe build successfully. All hardware
runs use the mandatory 60-second watchdog, six-channel 0.5-second thermal monitor
and 95 C cutoff. Installed Gallium SHA256:
`c1661154f811c7217b89a0c83c5801784fb2e74664acdbe8adf4c3cd3f65ced6`.
Vulkan and the kernel were not changed.

- `20260916T202513507480Z-zink-mutable-direct-before`: the exact direct regression
  built with HEAD's original `zink_resource.c` reproduces `assert(format_list)`.
  The fixed source was restored before the run; the executable is the separately
  retained baseline binary, not the installed fixed shared library.
- `20260916T202431301169Z-zink-mutable-direct-after`: both RGBA/BGRA view directions
  pass, 512 pixels checked, with no Vulkan validation errors.
- `20260916T202842753671Z-zink-mutable-controls`: linear modifier passes the same
  direct oracle; existing tiled EGL dma-buf drawing/readback passes at 1x and 4x
  MSAA for both formats, CPU/PBO and RGBA/BGRA readback. Validation is enabled.
- `20260916T202617515770Z-steam-xwayland-recovery`:
  only Xwayland was restarted. The original Lua wrapper was restored byte for
  byte; the Wayland session remained running. New Xwayland answers X11 requests.
- `20260916T202654363678Z-steam-zink-fixed-startup-r1`: the real Steam client
  advances to its updater, downloads 48,687 of 496,367 KB in the bounded run,
  and renders the updater window (`steam-update.png`). Xwayland remains alive
  without another assertion. Peak temperature is 60.75 C. The outer report says
  `missing_success_evidence` because its expected regex omitted the word
  `bounded`; transport and the application guard succeeded. This is retained
  honestly as a reviewed application observation, not silently relabeled.

The guarded application run was closed at its deadline. The user subsequently
launched Steam normally and reported that it was starting. Login and games have
not been qualified.

## Reproduction

Build the manual target (not registered as an automatic hardware test):

```sh
ninja -C build/mesa-m4-asahi-arm64 src/gallium/drivers/zink/zink_mutable_modifier_probe
```

Copy it to the target, then run under `tools/gpu-experiment.py` with the explicit
Asahi ICD, `ASAHI_M4_EXPERIMENTAL=1`, `ASAHI_MESA_DEBUG=nocompress`,
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`, and `GALLIUM_THREAD=0`.
Pass `0x0c00000000000001` or `0` as its single modifier argument. Disabling
Gallium threading is specific to this probe's direct internal context API;
the real Steam run uses the ordinary desktop environment.

The archived commands, source identities, executable hashes, watchdog and
thermal evidence are in `qualification.tar.gz`. The core backtraces reconstruct
the failing Gallium call before Vulkan image creation; a Vulkan replay alone
cannot reproduce an assertion that happens before the Vulkan call is made.

## Separate findings retained

The exploratory `dmabuf-readback-probe texture-view` case imports a dma-buf,
unlike the owned buffer in Xwayland's core. Its BGRA case returns zero pixels
through Zink's immutable-handle/transient path even after the allocation fix.
It remains unresolved and is not counted as a pass. An ordinary owned GBM/EGL
`native-view` control passes even with the old library and therefore does not
serve as the assertion regression; the direct probe above does.

The earlier frozen login screen was a separate DCP failure before Steam was
installed: an unhandled D114 callback latched the display failure. Reboot
recovered the panel; this Zink fix does not implement that callback.
