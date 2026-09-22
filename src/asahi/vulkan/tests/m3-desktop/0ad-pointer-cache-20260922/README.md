# Immutable native pointer uploads, 2026-09-22

Cache the last immutable native pointer upload for each supported size in the
command buffer. Reuse requires exact byte equality, including table addresses,
query address and routing clamp; descriptor object identity is insufficient.
Cache the two immutable UBO eligibility scalar uploads as well. Reset clears
both caches before uploader BO recycling. This does not cache UBO contents or
modify existing uploaded memory, and submission never mutates the caches.

The incremental arm64 build passes. Twenty pinned command-buffer reset/reuse,
secondary, simultaneous-use, push-descriptor and multi-draw CTS cases all Pass.
A 200-loop game replay completes in 4.268 seconds with the exact reference hash
`ec64e34c347316daea3738ef689e1ccc1c4a759f9b3d88a497df360a79c72a2c`.
Live native Vulkan averages 99.743 FPS in the unchanged 3024x1964 scene,
compared with the preceding 94.706 FPS stage-scope sample. These short samples
do not establish sustained performance or the unfinished 120 FPS goal.

Every experiment completed the M3 60-second watchdog and five-channel thermal
guard (0.5-second samples, 95 C stop). Private raw evidence remains under
`build/m3-0ad-120/0ad-120-{pointer-cache-replay,pointer-cache-cts,vk-pointer-cache}`.
