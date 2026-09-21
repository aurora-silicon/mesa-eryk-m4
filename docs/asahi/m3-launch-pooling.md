# M3 executable upload pooling

G15/G16 native ABI devices use the existing M4 USC upload pool for small draw,
compute, and tile clear/reload/store Ioto programs. Standalone allocation remains
for oversized programs and other ABIs. Command-buffer lifetime, alignment,
USC containment checks and coherent publication are unchanged.

M3 J514S validation on 2026-09-21: eight independent process probes passed
poisoned immediate readbacks, compute/render switching, GPU image copy, clear,
3024x1964 rendering and 4x MSAA. Guard: 60-second watchdog, 0.5-second PMP thermal
samples and enabled CPU cooling. Evidence is retained in workspace
build/m3-pooling-readback-02 and build/m3-vm-cache-readback-01.

Full-resolution Black Forest sample after pooling: 11.20 FPS over ten seconds.
This includes concurrent completion changes and is not an isolated pooling
speedup or a complete benchmark result. Broad regression validation follows.

Combined qualification: all 1000 frozen previously passing CTS cases pass on
J514S with the completion/VM-cache kernel (afb930e13e1a), same CTS executable
and headless tiled-X11 configuration as the earlier sample. Peak 72 C; all
bounded watchdog/thermal guards complete. Full-resolution game after kernel
preparation improvements: 14.70 FPS. This is a combined checkpoint, not an
isolated Mesa performance ratio or full conformance claim. Detailed case and
binary identities: kernel m3_lab/validation/stk-optimization-20260921/cts-1000.
