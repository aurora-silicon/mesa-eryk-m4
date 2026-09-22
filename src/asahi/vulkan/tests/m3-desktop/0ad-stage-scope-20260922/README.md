# M3 graphics stage scope, 2026-09-22

0 A.D. uses fragment|compute destinations (0x880) for sampled attachments.
Compute is not a graphics-stream stage. Exclude that bit when classifying
fragment-only dependencies; compute streams keep their own dependencies, and
submission still requires an adjacent render predecessor and the explicit
kernel capability/early-tiling opt-in. Vertex dependencies remain ordered.

The pinned CTS selection passed 32 cases with four unsupported storage cases.
The 200-loop native game replay retained the exact reference screenshot SHA256
`ec64e34c347316daea3738ef689e1ccc1c4a759f9b3d88a497df360a79c72a2c`
in 4.218 seconds. A sixteen-band dependent-render oracle with mixed
COLOR_ATTACHMENT_OUTPUT|COMPUTE destinations passed 1000 submissions plus four
in a fresh VM, including untouched-region poison checks. Retired early tiling
increased from 118227 to 131978, confirming the path was actually exercised.
Live unchanged 3024x1964 native Vulkan averaged 94.706 FPS. This is not a
120 FPS result and does not establish a significant performance improvement.

All experiments completed the M3 60-second watchdog, five-channel 0.5-second
thermal monitor and 95 C stop. Raw evidence stays outside Git under
`build/m3-0ad-120/0ad-120-{graphics-stage-scope-replay,stage-scope-cts,vk-stage-scope,fragment-compute-oracle}`.
