## Rejected full-screen depth scissor control

An isolated G15S command-only opt-in extends the existing native full-screen
scissor fast path to matching single-layer/single-sample D32 attachments. It
still requires exact full-screen integer viewport/scissor/render-area inputs,
ordinary depth clipping, no depth clamp/bias or shader depth writes, and no
stencil. All encoded table entries and per-draw selectors remain; the control
only clears the redundant PRIMARY scissor-enable bit for qualified draws.

Both unchanged replay checks match the reference pixels exactly. Fifteen
same-boot alternating timing pairs measure 158.20 / 162.15 FPS by separate
medians, but paired mean change is +0.61%, paired median +0.26%, with only eight
candidate wins. This does not establish a reliable gain. An unscored bounded
bind trace confirms the scissor bit is cleared on depth-tested/write-enabled
records, so the tested path is active. These records are not GPU-time samples.
The control is removed; its private library/patch, checks, all timing samples
and trace classification remain under `full-frame-depth-scissor`. No broader
CTS or live application qualification is claimed for this rejected variant.

Vulkan SHA256: `6b616d68dd1260ccf1e3afd84e609841cced27077967b1e5d92892c74facc7b9`.
All hardware work uses the 60-second watchdog, 0.5-second thermal guard,
retained output and restored completion policy. Raster-table reuse is disabled
in both timing variants. Neither M4 nor normal desktop Mesa is changed.
