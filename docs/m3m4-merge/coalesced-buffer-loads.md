# Coalesce bounded readonly scalar buffer loads

Native compute now groups four adjacent, 16-byte-aligned readonly scalar loads
from the same descriptor and offset expression. A single vector load executes
only when the complete group is in bounds. Partial groups retain the original
four scalar bounds checks. The pass stops at stores, barriers and block ends,
and excludes volatile/coherent or mutable loads. Descriptor bases have at least
16-byte alignment, so an admitted group cannot wrap its 32-bit byte offset.

Host validation covers 52 partial/wrapping boundary combinations against a
64-bit scalar-memory model, explicit unsafe-group rejections, 810 bound checks
and the existing sparse/alias lowering suite. The dedicated GPU oracle covers
14 descriptor lengths from 1 to 127 bytes, including partial words/vectors and
offsets near 4 GiB. All pass on M3 and M4; diagnostic counters confirm that
each shader actually uses the new grouping path. The selected 15 frozen UBO
robustness cases pass, as do the initial M3 43 sparse/alias cases.

Qualified compute candidate: 0b3d4a340a3112734e386b7e32e2074963d4ad603f79610ffa3b367133f7422e.
M3 check 20260926T011201156075Z and M4 check 20260926T011634993163Z
match reference RGB frames 1, 16 and 31 exactly.

| Device | Completed 30-frame times (s) | Median replay FPS | Prior median |
| --- | --- | --- | --- |
| M3 Pro | 6.604316308, 6.565038126, 6.590937924 | 4.55170 | 4.47202 |
| M4 | 9.326227743, 9.330429319, 9.332246623 | 3.21529 | 3.15479 |

Same frozen Satisfactory Vulkan gameplay capture, 3840x2160 output and
2880x1620 internal rendering, same replayer/adaptations. All runs retain
watchdog and thermal guards. These are replay rates, not live gameplay.
The 5 FPS objective remains unmet. A fragment-stage trial and a vector-default
partial fallback trial were exact but added no clear improvement; neither is
enabled. The optional `HK_COALESCE_BOUNDED_AUDIT` prints group counts; value
`nir` additionally prints the input IR for regression-test qualification.
Private game data, telemetry, shader captures and images remain local only.
