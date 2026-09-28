# DX12 preamble qualification, 2026-09-28

M4 source 88d3ef6659b / binary b970144c6203, kernel 8f1de23c1e73,
packet8, desktop stopped. Existing watchdog and thermal guards retained.
CPU ceilings were 1.800/1.860 GHz after the staging guard; these are not
full-clock results. The normal packet32 boot and desktop were restored afterward.

With HK_M4_NATIVE_PREAMBLE and AGX_NATIVE_MEMORY_PREAMBLE enabled,
all three retained images pass the existing limit (at most 32 changed pixels,
maximum error 4/255). Warm 30-frame samples: 1.89604, 1.87650, 1.90393 FPS;
median 1.89604. These are qualified absolute measurements, not an isolated gain.

The matched ordinary-default control fails reproducibly: frame20 has 15 changed
pixels, maximum3; frame35 has 28 changed pixels, maximum5. One pixel at (782,1092)
exceeds the unchanged limit. The older suite-64fb191636d driver and a diagnostic
reverting early float16 IO widening reproduce the same failure. Thus this is
not attributable to the new preamble or cache-bounds changes. Root cause remains
open, and failed controls have no approved performance score. No tolerance or
reference was changed. The half-IO diagnostic was rejected; the CTS fix remains.

Records retain all positive/negative checks, frame identity, exact binary hashes,
thermal evidence and the original images. Preambles remain off by default.
