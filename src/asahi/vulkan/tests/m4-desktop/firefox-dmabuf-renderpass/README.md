# Firefox YouTube imported-image render-pass abort (2026-09-21)

The user supplied `/home/e/crash.txt`. Saved Firefox core PID 7077 aborts in
`zink_update_barriers`, called by WebRender's Renderer thread through
`zink_draw`, the threaded-context flush, and EGL swap. `check_rp` is true:
the active render pass uses TC's predicted final depth discard. The barrier
ends that pass, leaving `ctx->in_rp` false. Its resource is a 300x150 BGRA8
imported dma-buf sampled in the vertex and fragment stages. Shader-extension
warnings earlier in the browser log are not the abort.

## Cause and correction

Zink releases exported/imported images to FOREIGN ownership at submission
end. Sampling one again requires an acquire even when the layout remains
SHADER_READ_ONLY_OPTIMAL. Binding the sampler records new batch usage before
that deferred barrier; unbinding the last view also moves lifetime usage
into the current batch. Old ordered-access flags then look like a real
current-batch dependency. Synchronization ends the render pass instead of
placing the first-use acquire in the reordered command buffer.

Defer sampler image usage until its pending barrier has examined old usage.
When unbinding migrates lifetime tracking from an older batch, reset its
reordering flags: that bookkeeping did not execute a GPU access. Preserve
actual current-batch dependencies. Keep the render-pass assertion and normal
render-pass tracking, depth discard, batching, and hardware acceleration.

## Reproducer

`gl-dmabuf-renderpass-probe.c` is mirrored from workspace
`tools/gl-dmabuf-renderpass-probe.c`. Compile with:

```
cc -Wall -Wextra -O2 gl-dmabuf-renderpass-probe.c -lEGL -lGL -o gl-dmabuf-renderpass-probe
```

It exports a green texture as a dma-buf and reimports it through EGL. Each
of 200 submissions first draws a near red occluder on the left half, then
samples the imported image behind it, discards depth, and flushes. Only at
the end does it read back all 4096 pixels: red left, green right. This keeps
multiple submissions in flight rather than serializing every frame with
readback. `--unbind-before-flush` leaves the imported image unbound at the
batch boundary; `--vertex-sampler` additionally samples it in the vertex
stage. Test all four combinations.

Run hardware only through the mandatory watchdog/thermal guard, e.g. from
the workspace after copying the executable to the target:

```
python3 tools/gpu-experiment.py --name dmabuf-renderpass-retest \
  --hypothesis 'Imported sampler preserves depth across submissions' \
  --expect 'PASS: imported sampler' --seconds 40 -- \
  env ZINK_DEBUG= ASAHI_MESA_DEBUG= AGX_MESA_DEBUG= HK_G17P_LAUNCH_TRACE=0 \
  VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation VK_LAYER_VALIDATE_SYNC=1 \
  python3 /root/run-m4-gl.py /root/gl-dmabuf-renderpass-probe --vertex-sampler
```

Clearing the GL helper's diagnostic `ZINK_DEBUG=sync` default is essential:
that option intentionally splits render passes and changes the scenario.

## Retained evidence

- `153850...baseline`: pre-fix Mesa plus temporary diagnostic logging aborts
  on the second acquire. First access has layout 8->5, FOREIGN ownership,
  unordered flags 1/1. Second has layout 5->5, FOREIGN ownership, flags 0/0
  and usage matching the current batch. Same assertion as Firefox.
- `154352...lifetime-fixed`: 200 submissions pass, 4096 final pixels correct.
- `154546...variants`: all four variants pass, 800 submissions total,
  zero incorrect final pixels and no Vulkan or synchronization validation
  errors. Peak temperature 64.5 C.
- `154645...sampler-regressions`: 16 ordinary/custom-border sampler rounds
  pass; depth-comparison test checks 24,576 pixels with zero failures.
- `154438...fixed-playback`: actual YouTube Big Buck Bunny playback for
  20 seconds using a copy of the closed ordinary Firefox profile. WebRender
  reports Zink/M4, one accelerated window, video time advances to 20.016738,
  paused=false, readyState=4, no media error or browser abort. Peak 67.5 C.
  This is a crash check, not a smooth-playback claim: the 720p60 sample
  reports 1201 total frames and 1079 dropped frames. Video performance needs
  separate investigation. The compositor screenshot timed out; the browser
  screenshot was retained. No DPMS diagnosis is inferred from that timeout.

All runs armed the 60-second watchdog and collected all-channel thermals.
Full cores, screenshots, patches and experiment telemetry remain in workspace
`_provenance/firefox-youtube-20260921` and `_provenance/experiments`.
Installed Gallium SHA256:
`032a634bdf2cdf8381cfc7ae01bf01be9aa785c9bd31c833023ced141510366b`.
Temporary barrier logging was removed from the final build.

The Firefox helper now records an optional compositor capture timeout instead
of failing an otherwise completed browser check. Browser automation failures,
process exits, and watchdog/thermal checks still fail the experiment.
