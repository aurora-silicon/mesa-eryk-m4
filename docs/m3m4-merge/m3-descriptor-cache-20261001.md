# M3 descriptor pool CPU cache experiment

The CPU still reads descriptor metadata during command recording. Ordinary
descriptor pools use write-combined storage. An isolated G15S experiment
requests AGX_BO_WRITEBACK with HK_M3_WRITEBACK_DESCRIPTOR_POOLS=true; the
default remains false, and other chips retain their ordinary allocation.
This changes the pool allocation, without caching live UBO contents, changing
shader code, reducing the scene, or skipping API calls.

The isolated Vulkan library is 5ee2a672173d64076fb6aee8f56cf3982022d890bb4ed593a6d0b8eaede6d9d0,
with the qualified f568 Gallium/EGL/GBM libraries. Its 48 resource pixel
oracles pass, and both variants retain identical Minecraft first/middle/last
images. The first nine alternating pairs have medians 80.7634 and 82.0703
replay FPS (+1.62%). A continuation hits the independent kernel lifetime
submission cap; that interrupted run is retained and is not a passing repeat.

After the kernel counter fix, nine new pairs complete on one boot without GPU
fault or watchdog recovery. Separate medians are 76.2753 and 81.7834 (+7.22%),
but the samples fall into roughly 76 and 82 FPS groups. The median change
within adjacent pairs is only +1.27%; eight of nine pairs improve. Do not report
the difference of separate medians as an isolated 7.22% driver gain. The
complete samples, reversed pair order, exact image comparisons, module/library
identities and watchdog/thermal evidence remain in the local benchmark's
diagnostics/m3-descriptor-writeback-after-timeline-20261001.

The experiment remains opt-in. Broader descriptor/coherency qualification and
repeatability are required before promoting the pool cache policy. The normal
accelerated desktop keeps /opt/asahi-m3m4-f568f1256f8. These are 30-frame
3024x1837 replay timings, not Java/F3 live-game FPS or a full CTS claim.
