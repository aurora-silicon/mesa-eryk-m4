# M3 opt-in uniform preamble qualification

Expose the existing native integer/F32 preamble and descriptor-prefetch options
on the shared G15/G16 shader ABI. Defaults remain disabled. Existing shader-cache
flag bits distinguish these paths. Tested on J514S/T6030, not inferred from M4.

With the separately qualified native PMP memory/fabric request 4/2, the
unchanged 3024x1964 Black Forest sample improved from 22.50 FPS to 27.00 FPS
with HK_M4_NATIVE_PREAMBLE=true and AGX_NATIVE_FLOAT_PREAMBLE=true. Both
race screenshots were inspected. These are ten-second application swap-return
samples, not a complete benchmark or physical scanout measurement. Different
wall-time windows can cover different race positions, so small deltas need repeats.
Descriptor prefetch additionally yielded 24.50 FPS and is not selected.

The retained adapted M4/lavapipe GFXReconstruct reference produced identical
frame-10 pixels for both compiler paths versus the same M3 reference with only
the memory request. Adaptations and binary hashes are in each manifest. This
11-frame reference is a correctness probe, not the racing performance result.
No new broad CTS qualification is claimed here. All runs used the existing
45-second watchdog and half-second temperature guard; peak sample temperature
was 71.75 C. Full raw logs/screenshots remain under workspace build/.
