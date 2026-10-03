# Gen4 array LOD query bias

Own `g15s-array-lod-query.metal` was compiled with AIR-NT32023.921 on mac-m5
for applegpu_g15s. The complete 188-byte fragment text is retained as hex;
constant entry is zero, main entry 64, and SMP at 126 is
05800ca8b0000d00010020000100, identical to the existing G16G array query
witness in test_m4_query_lod. This is offline compiler evidence, not a
native execution capture. Raw manifest/archive: build/m3-texture-bank-study-
20261003/native-query-array-g15s.

Three M3 CTS cases return zero through the biased 2D-array query form,
with the earlier qualified Mesa DSO as well as the LOD-bank experiment.
Signed/unsigned array clamp_max cases pass after using the unbiased query
and reconstructing bias, sampler clamps, view minimum and mip selection
from the API descriptor in NIR. The shadow-array case then exposes a second
problem: enabled image-view minima publish the shared F16 software sampler
state on M3 as well as M4, but reconstruction previously decoded that state
as hardware sampler bits on M3. Use the shared G15/G16 ABI feature predicate
for this layout. All three now pass, and all 190 pinned LOD query cases pass.

Other Gen4 query shapes retain their existing instruction-bias path. The
HAL200 path retains its existing reconstruction. The capability surface is
unchanged. Watchdog, all-channel temperature, case indices, QPA expected/
actual values and control variants are retained under gpu-replay-bench/
diagnostics/m3-macos-driver-gap-20261003/parallel-lod.
