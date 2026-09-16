# M4 native-context status and BO lifetime fixes

2026-09-16, Apple M4 J713. This records hardware validation of the guest
Mesa fixes, not a Steam login or game pass.

## Bugs and changes

`hk_check_status` used the host Asahi VM_STATUS ioctl on the guest virtio-gpu
fd. That returned device loss after otherwise successful native ARM64 DXVK
rendering in muvm. Use the native-context shared asynchronous error counters
on virtio; keep the native VM_STATUS path on direct DRM devices. virglrenderer
1.3.0 only forwards GET_PARAMS group0. These counters report protocol failures;
this change does not extend the protocol to forward every host VM fault.

The virtio backend also missed the M4 executable-buffer read-only GPU mapping
permission already used by the native backend. Match native allocation flags.

Finally, its bo_free callback used the generic DRM GEM_CLOSE helper. Unlike
the virtio helper, that did not flush buffered commands. GLX allocates and
frees a temporary shared memory object whose read-only shadow bind is still
buffered; closing the handle detaches the host resource first. A later flush
then causes host VM_BIND(READ, handle0, range0x4000) to return ENOENT.
virglrenderer increments async_error; Vulkan correctly reports device loss.
Use vdrm_bo_close to preserve pending-command/resource-detachment ordering.
No error is suppressed, no rendering feature is disabled.

## Reproduction and checks

`glx-readback-probe.c` creates a double-buffered visual/context, requires an
Apple M4 renderer, clears a16x16 drawable red and checks all256 RGBA pixels.
Build with `cc [-m32] FILE -lGL -lX11`. Target executables live under
/opt/m4-dx11-qualification. headless-glx-command.py creates a private native
M4 Hyprland/Xwayland; muvm-dx11-probe.py runs the program through FEX using
an explicit custom Mesa overlay. All runs use the60s hardware watchdog,
six PMP thermal sensors at0.5s,95C cutoff, and bounded process lifetimes.

- r18, before lifetime fix: GLX64 fails pixels; native-context async=1.
  Diagnostic host preload logs VM_BIND ENOENT. GPU rendering itself retires.
- r19, after: GLX64 passes256 exact pixels, no failed host VM_BIND,58.5C peak.
- r20: GLX32 passes256 exact pixels, no failed host VM_BIND,58.5C peak.
- r21: x86_64 DXVK under FEX passes FL11_1 creation,4096 exact clear pixels,
 4096 interpolated triangle pixels (one UNORM step tolerance),256 exact
 compute words.58C peak. Original harness success regex omitted D3D11;
 report is preserved and review.json records classification with the corrected
 regex. Guard and application both exited0.

The remaining GET_UNIQUE EACCES and unsupported DRM-core probe EINVAL entries
are unchanged capability probes, not new failed Asahi commands.

Host ARM64, x86_64 and i686 Vulkan builds succeed. Installed ARM64 SHA256:
afa0b9a400bf11187f6ce55c518f987c204eb4f14654af216e17ad3957d8736b.
The r5 multilib EROFS SHA256 is
95e9ad51b7bd1e605a8a416eca6eb5dd460d82297ebce3189f190faf0ce7b755.
Manifests within the image retain per-architecture source and staged hashes.

The older stock guest Mesa26.1.8 lacked our M4 changes. The original20:36 GPU
fault used its USC base0x1100000000, unlike our0x10000000000. This strongly
implicates the incompatible guest build, but the isolated fixes above are not
proof that every cause of that original full Steam failure is eliminated.

Full Steam startup subsequently exposed packaging issues in pressure-vessel;
that work remains separate from this validated driver change. DCP's idle D114
callback fault is also unresolved and is not fixed by these Mesa changes.
