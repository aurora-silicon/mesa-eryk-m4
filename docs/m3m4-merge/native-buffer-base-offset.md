# Preserve native buffer base-plus-offset addressing

The proven direct-buffer path previously stripped its private top-bit marker
after adding the offset. That outer mask hid the base-plus-32-bit-offset form
from the backend, which then emitted full 64-bit arithmetic per access.
Move tag removal through proven unsigned 32-bit offsets and robust zero-page
selections to the base. Raw BDA loads, sparse mappings and unknown provenance
retain the original translation. Bounds checks remain intact.

Qualified library SHA256: f88c3d061875ad42625cffcb9ab6468b56cf0b35a3cda1509b67d4722f87b506.
Host tests cover sparse/alias translation and the zero-page selection and
32-bit-address carries. The 43 GPU sparse/alias tests pass on both machines.
Satisfactory frames 1/16/31 match their earlier references exactly on both.

| Device | Completed 30-frame times (s) | Median replay FPS | Prior median |
| --- | --- | --- | --- |
| M3 Pro | 6.735134856, 6.725604937, 6.769821883 | 4.45425 | 4.27308 |
| M4 | 9.578558246, 9.590147301, 9.583427059 | 3.13040 | 2.99675 |

M3 boot 274dca22-7518-412f-b11e-24be7cdf3646, module
372990bcae131aa66402160e199b6f579b8c57b3fe04f3dc57e51d9163bbfba4.
M4 boot 7b02370e-2a9b-445b-935f-db0f87b80aa7, unchanged module
cca3c80ff012b744aa8d33e5073ce18206a6dd2669b7643027307d347a8de15b.
Checks: M3 20260926T003534839168Z, M4 20260926T003637843384Z.
GPU capture, frame count, resolution, adaptations and exact-image criterion
are unchanged. These are replay rates, not live-game performance claims.
All hardware runs used the watchdog and thermal guards. Full trace/evidence
are retained in the private benchmark repository.
