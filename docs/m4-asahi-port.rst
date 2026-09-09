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
