M4 Asahi integration branch
==========================

This branch starts from Aurora ``agx3`` commit
``e58d46899eb1f351863bccab4b6e8c87ed32b95e``, fetched on 2026-09-09.
Its paired kernel is the ``m4-asahi-port`` branch importing the complete
Aurora Linux ``drm-agx3`` Asahi driver at ``4ac2664258f3``.

The separate lab Mesa branch ends at ``c2cb7a5934b``. Its J713 shader and
desktop experiments supply evidence, but its private ``g16-lab`` device
initialization and diagnostic memory policy are not imported here.

Two compiler corrections are transferred first: full-width reciprocals must
reach the full-width encoder, and mixed-width float selects must bypass the
all-half encoder. The mixed-width case includes its AIR regression test.
These changes do not enable a new device or claim M4 support through the
Asahi kernel runtime. Normal M4 device admission and the generation-specific
render state still need integration once the kernel runtime is available.

The final SuperTuxKart lab attempt rejected fragment AIR
``$r3l = convert #11, ^r3, #0x1``. That instruction remains a separate compiler
coverage issue; importing the current source does not resolve it.

Initial build and test checkpoint
---------------------------------

The fresh AArch64 build includes Honeykrisp Vulkan, Zink, EGL, GBM and the
Wayland/X11 platforms. Its system ``mesa_clc``, ``vtn_bindgen2`` and
``asahi_clc`` tools are built from this same worktree in a separate host build.
Neither build uses generated compiler tools from the old lab branch.

The imported ``LocalIndexZeroExtendUsesCopyAndClear`` unit test supplied an
8-byte copy buffer to ``agx3_clr``, which initializes 10 bytes. Correcting the
test to use separate correctly sized buffers allows the full suite to finish.
This changes only the test; the production packer already uses two buffers.

On 2026-09-09 the resulting host suite reports 229 passing tests out of 246.
All 17 failures also occur with the original upstream packer at ``e58d46899eb``.
The baseline comparison retains the test buffer correction and excludes the
new mixed-width select regression, which does not exist in upstream. It links
an object compiled from the original upstream ``agx3_pack_binary.c`` ahead of
the otherwise unchanged compiler archive. This covers the only production
compiler file modified on this port branch.

The changes fix two additional baseline failures:
``ProjectiveReciprocalMatchesNativeOperandForm`` and
``UpdateLightmapNativeIntegerAndMaskForms``. The new mixed-width float select
regression and the half-reciprocal and existing mixed-width select tests pass.
There are no newly failing shared unit tests in this comparison.

The graphics compile smoke test still fails for AGX3b single-instanced
varyings, flat colour and discard, and AGX3c meta RGBA upload. Its failure
output matches the upstream control byte for byte. These unresolved checks
must not be represented as a passing graphics suite or hardware validation.

Workspace evidence is under ``build/mesa-asahi-upstream-control/``, including
``comparison.json``, the upstream control source and both baseline test logs.
The port logs are ``build/mesa-asahi-tests.log`` and
``build/mesa-asahi-graphics-tests.log``. No new Mesa runtime has been installed
on the J713 as part of this source integration.

First Vulkan compute on the Asahi M4 port
----------------------------------------

The opt-in ``ASAHI_M4_EXPERIMENTAL=1`` path admits only T8132/G16G with
one die/cluster and USC 3 / HAL 200. It preserves the real hardware identity,
uses the native AGX3 compiler and HAL200 Ioto/CDM path, and builds M4's
ten-entry helper tables instead of Neo's six-entry tables. Executable BOs
receive GPU read-only bindings. The initial advertised queue supports compute
and transfer; graphics remains disabled until the M4 render backend is ready.
Other G16 variants are not admitted.

``vulkan-compute-1`` on J713 boot
``84bf42d0-9ba9-419f-9d09-5ee8d728a2ac`` passed all 32 output values from
``g17p_vulkan_compute_min.c`` compiled with ``TEST_DEVICE_ID=0x8132``. This
is the test's existing SPIR-V computation ``0x12340000 + global_id`` with a
storage-buffer descriptor, compiled by this Mesa and submitted through the
new Asahi DRM scheduler/GEM/GPUVM driver (kernel commit ``31dcf1308678``).
The test's historical executable label still says G17P; the selected device ID
and actual hardware were M4/T8132. The test also passed Vulkan fence completion,
followed by the separate open-file DRM teardown test and clean module unload.
Watchdog timeout was verified at 60 seconds; six-channel thermal peak 53.75 C.
Evidence is in the workspace's
``_provenance/asahi-pivot-20260909/hardware/vulkan-compute-1/``.

Native and AArch64 builds passed. Five of six focused Ioto/chip/container tests
passed; ``asahi-agx-ioto-program-core`` fails its pre-existing negative resource
composition check at line 642. Compiling the same seven source files from the
pre-change HEAD reproduces that failure (``build/m4-ioto-baseline/result.log``).
No new Vulkan graphics, Wayland, Hyprland or SuperTux result is claimed yet.

Native graphics recording (qualification in progress)
----------------------------------------------------

The opt-in M4 device now records HAL200 graphics through the common Honeykrisp
path: native PPP/VDM, linked vertex/fragment containers, and BG/EOT programs.
The real G16G compiler identity and ten-core helper framing remain intact.
VDM command buffers own read-only low aliases; their lifetimes follow command
buffers. Architectural context-save backing belongs to the kernel GPUVM.
No lab ioctl, captured render graph or Mesa fixed scratch allocation is used.
Graphics queue advertisement is experimental until hardware tests establish
rendered pixels and then window-system operation. This is not desktop parity.

Both host and AArch64 builds pass. Four focused PPP/bind-tail tests pass. The
unchanged direct-draw test fails its whole-struct memcmp assertion at line 235;
its implementation and test have no changes in this port. Full-byte M4 kernel
wire comparisons separately pass against the previously executed encoders.

The first graphics attempt stopped before render submission: the live
VertexProgramVariant encoder still admitted G17P only. The remaining native
HAL200 VDM, bind-tail, PPP and meta-copy gates now admit G16G using the existing
M4 encoding evidence. Five focused encoding tests pass, including an explicit
M4/G17P byte comparison for the direct-draw packet. The earlier direct-draw
failure was a test comparing unspecified C struct padding; it now compares
semantic fields. Hardware rendered output remains the next qualification gate.

Verified M4 rendering and repeated submissions
---------------------------------------------

``vulkan-render-3`` verified every pixel of the native 32x32 RGBA8 triangle
through host image copy (checksum ``a513efed37c25325``). Following the kernel
USC freelist capacity fix, ``vulkan-render-repeat-4`` retired eight render
passes across two client VMs, including four GPU copies. All four host-copy
renders passed; GPU image-to-buffer copy retired but returned an incorrect
green channel. Hyprland and SuperTux remain unqualified on this driver.
The test accepts ``ASAHI_TEST_REPEAT=1..1000`` for bounded resubmission; the
command buffer permits reuse and each iteration waits on its own fence.
The hardware run used a verified 60-second watchdog and six-channel thermal
guard, with peak observed temperature 53.5 C.

``vulkan-render-repeat-5`` passes both readback paths, four submissions each,
with the same full-image checksum. M4 now selects explicit channel packing
and raw memory stores in the meta-copy shader, matching the native HAL200
path. The previous formatted-store selection lost the green/blue channels.
Safe open-file teardown passed; thermal peak 51.5 C, watchdog 60 seconds.

The isolated EGL/Zink compositing test ``zink-composite-1`` passes eight
frames / 8192 pixels (maximum channel error 1), with nearest/linear texture
sampling, premultiplied blending, scissor damage, texture reuse, and native
fence export/import/wait. Renderer: ``zink Vulkan 1.4(Apple M4 (G16G B0)
(MESA_HONEYKRISP))``. This first run uses local textures; DMA-BUF sharing and
actual Hyprland remain subsequent gates. Peak PMP temperature 52 C, watchdog
60 seconds, clean DRM teardown.
