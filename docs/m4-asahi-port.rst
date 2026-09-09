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
