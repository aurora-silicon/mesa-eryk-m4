# M3 dynamic UBO optimization from M4

Port M4 8e6543d18d7, retaining the existing G15/G16 shader ABI gate. Resolve
statically selected dynamic UBOs in the draw root, including dynamic offsets,
and prefetch eligible constant vector reads word by word. Existing optional
HK_M4_PREFETCH_UBOS/PREFETCH_VARIANT controls, robust bounds and sparse fallback
remain intact. Expanded ARM64 hk_descriptor_prefetch_test passes under qemu.

70 targeted Vulkan 1.4.6.2 dynamic UBO cases pass on M3; both guarded segments
complete, peak66 C. Frozen global catalog indices and full results retained.

Native M3 Vulkan capture frames200-202 (starting-line countdown) replayed with
offscreen swapchain, memory rebinding, omitted pipeline cache and unsupported
feature removal option. Full-4K GPU pass sums divided by3 fall17.451->15.407ms;
these aggregates include state setup and readback, not steady live FPS. Final
3840x2160 RGB comparison: 11487 differing pixels, max channel difference2/255.
This is a small numerical difference, not a pixel-exact pass.

The live native Vulkan Black Forest race sample rises61.394->66.994 FPS:
670 present returns in10.000893s, median14.959ms, p9516.972ms, peak74.5 C.
Both endpoint screenshots inspected: correct visible scene/kart/HUD. PNGs are
reduced to1280x720; raw3840x2160 PPM hashes retained. Same saved quality settings,
scale1 and private Weston GL headless3840x2160@1000Hz kiosk-shell/Xwayland.
Native Vulkan is a different game renderer from OpenGL/Zink; this does not
establish identical effects. This is a ten-second race sample, not a complete
benchmark or physical scanout measurement. No120 FPS claim. Existing clock,
thermal and watchdog settings unchanged. No global feature flags enabled.
