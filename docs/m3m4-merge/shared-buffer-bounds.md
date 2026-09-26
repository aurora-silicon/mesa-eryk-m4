# Share bounded-load limits

For a positive byte count N, `offset + N <= bound` is equivalent to
`offset < usub_sat(bound, N - 1)`, evaluating the former in 64 bits.
Adjust the shared descriptor bound rather than saturating and incrementing
every separate access offset. This preserves zero-length ranges and rejects
wrapping offsets. The existing element-index fast path remains unchanged.

Host tests compare the actual NIR builder against the independent 64-bit CPU
expression for 810 combinations, including zero, exact-end and UINT32_MAX
boundaries, for scalar/vector byte counts 1 through 128.

Both M3 and M4 pass all 15 pinned general-queue UBO robustness2 cases at frozen
indices 2343092, 2343100, 2343108, 2343116, 2343124, 2343227, 2343235, 2343243,
2343251, 2343259, 2343362, 2343370, 2343378, 2343386, 2343394 (catalog SHA256
72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf).
M3 additionally passes the 43 sparse/alias oracles. The initial exclusive
compute-queue case 2343093 was NotSupported; it is not counted as a pass and
its evidence is retained. That runner's failed-child handling latched the
watchdog; evidence was saved and a controlled reboot restored the desktop.

Satisfactory frames 1/16/31 remain exact on both machines. Qualified Mesa
SHA256 a491e5dd12ffc61e6464ed1d29e0718d6e9faad22635579090d5dfae1bec31a5;
checks M3 20260926T003835661205Z, M4 20260926T004816419721Z.
M3 times: 6.726261699, 6.694914980, 6.708383441 s (median 4.47202 FPS).
M4 times: 9.509338546, 9.508356232, 9.513250521 s (median 3.15479 FPS).
These are small changes from 4.45425 / 3.13040 FPS; do not describe them as
large gains. M3 timing followed reboot to 57d3bff3-6679-493d-a8f0-61341841d078;
the kernel module/options are unchanged. Watchdog, thermal, frame-completion,
resolution, capture and exact-image requirements remain unchanged.

The tested expression was subsequently placed in an inline helper so the host
boundary tests exercise production code. Full captures remain private.
