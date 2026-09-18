# Factorio button seams: diagnosis, not a driver fix

The Steam menu has dashed diagonals across button backgrounds and damaged panel
borders. A retained two-frame Vulkan capture reproduces these on native M4
without running Factorio, FEX or X11. The user closed Factorio after capture;
no further game launch is required for this reproducer.

The fault is localized to texture-coordinate gradients along triangle edges:

* Native replay retains the seams. Host lavapipe has clean button backgrounds.
  Vertex/index data, projection UBO, base-level GUI atlas and the dumped LUT
  slice match byte for byte for draws 10414 and 10434.
* Changing only the original GUI fragment shader's first image sample from
  implicit LOD to explicit LOD zero removes the seams. The original sprite
  flags, tint calculations, shader interface and other sample are retained.
  This is a diagnostic substitution, not a correct general-purpose fix.
* A simplified implicit sample retains the seams, as does textureGrad using
  dFdx/dFdy. The fault is not limited to automatic sampler LOD selection.
* `gradient.frag` exposes the derivatives themselves. In draw 10434's central
  button interior, lavapipe produces RGBA (5,39,0,255) at all 19,400 pixels.
  Native M4 disagrees at 374 pixels along the diagonal: one or both gradient
  channels saturate. This is evidence of incorrect gradients before sampling.
  Later GUI draws overwrite this diagnostic; inspect the selected draw's raw
  attachment, not the final whole-frame screenshot.

The original compiler bundle reports needs_coarse_quad_helper_invocations=true.
The exact faulty interpolation/helper-lane or raster-state instruction remains
unresolved. Do not claim a missing metadata flag or a completed driver fix.
Generic isolated quad probes pass, including the same narrow atlas coordinates,
large texture dimensions, source-alpha blending and positive-height viewport.
They are negative controls, not a reproducer of the application failure.

## Reproduction and limitations

The private capture and game assets remain under workspace
`build/steam-crash-20260918/`; they are not redistributed here. `evidence.json`
records the trace hash, draw, measured region and guarded experiment identifiers.
`dump-config.json` selects the two button draws and their resources. Compile the
diagnostic with `glslangValidator -V gradient.frag -o REPLACEMENTS/sh229`; that
directory must contain only replacement SPIR-V files.

Use `tools/vulkan-replay-software.py` with `--replace-shaders REPLACEMENTS`,
`--dump-config dump-config.json`, `--frames 1`, `--seconds 90` and the explicit
`--remove-unsupported` adaptation. Native replay must run through
`tools/gpu-experiment.py` and target `vulkan-trace.py`, with the same replacement
and dump selection, `--end-frame 1`, `--memory-translation rebind` and
`--omit-pipeline-cache`. Retain watchdog and thermal monitoring.

Both replays adapt swapchains to offscreen and rebind memory. Software replay
also removes unsupported capabilities; mapped-linear-image layout differences
corrupt some text. Therefore this is evidence for the isolated button draws,
not proof that the entire cross-device frame is a valid pixel reference or
that the unmodified application passes. Native guard passes mean execution
completed, not that the pixels are correct. Selected hardware runs peaked below
65 C, except the earlier draw-dump run at 68 C.

The capture used unassisted memory tracking and an F10 two-frame trigger inside
Steam pressure-vessel. A page-guard attempt crashed during application startup;
its interaction with FEX/application signal handling was not separately resolved.
The temporary capture launch option was removed after capture. Normal Steam was
restored with Factorio left closed. A unique pre-capture save was created as
`Nulliusm4-guicapture-20260918.zip`; the existing Nullius save was not overwritten.

`gui-seam-probe.c` is the independent negative control. Build with the command
in its header; the argument is an RGBA8 readback filename (2560x1664). Hardware
requires the experiment guard. `M4_PROBE_SOFTWARE=1` permits llvmpipe.
`M4_PROBE_ATLAS=1` uses an 8192x3888 gray base level and red higher mips, with
the captured central-quad geometry. Optional `M4_PROBE_IMPLICIT_ONLY`,
`M4_PROBE_EXTRA_VARYING`, `M4_PROBE_BLEND_SRC`, `M4_PROBE_UPPER_LEFT`,
`M4_PROBE_BLEND` and `M4_PROBE_CONSTANT_UV` select the documented source variants.
