# Factorio button seams: fragment-statistics prolog fix

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
This was the initial diagnosis checkpoint; the statistics-query interaction
below subsequently isolated and fixed the missing helper lifetime contract.
Generic isolated quad probes pass, including the same narrow atlas coordinates,
large texture dimensions, source-alpha blending and positive-height viewport.
Those runs lacked a fragment-statistics query and remain useful negative
controls. Adding the query turns the generated-quad probe into a reproducer.


## Root cause and fix

Factorio enables a fragment-invocation statistics query. Its separately compiled
prolog reads active-thread SR2 and input-sample-mask SR39, then atomically counts
covered samples. Neither the prolog NIR nor its derivative metadata describes
operations in the main shader linked after it. The backend therefore marked
both input reads as allowing helper termination. Main-shader interpolation then
received incomplete quads along primitive edges, corrupting derivatives and LOD.

Nonterminal secondary fragment parts now preserve helpers regardless of their
own derivative metadata. Both SR2 and SR39 reads omit the helper-terminating
marker while helpers are needed. Statistics, sample masks and application
shaders remain enabled and unchanged. Fixing only SR2 was insufficient: the
following SR39 read independently terminated the helper lanes.

The captured 36-vertex draw has exactly the same 374 erroneous gradient pixels
in a standalone GL program only when the query is enabled. With the fix it has
zero errors; the query still reports 24,800 invocations before and after. The
original Factorio Vulkan replay now has clean borders/buttons without shader
substitution. Its selected 19,400-pixel button interior matches lavapipe exactly.

The self-contained generated-quad regression needs no Factorio data:

```sh
env M4_PROBE_ATLAS=1 M4_PROBE_EXTRA_VARYING=1 M4_PROBE_BLEND_SRC=1 \
    M4_PROBE_UPPER_LEFT=1 M4_PROBE_STATS=1 M4_PROBE_CHECK_ATLAS=1 \
    ./gui-seam-probe output.rgba
```

On M4 wrap this command in the mandatory watchdog/thermal experiment guard and
use the existing private Zink environment. For the host software control add
`M4_PROBE_SOFTWARE=1 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe`.
The probe creates gray mip zero and red higher mips, checks 116,400 interior
pixels across six quads, and requires a nonzero statistics result. The old driver
selects red mip levels at 2,544 pixels; fixed M4 and llvmpipe have zero errors
and both report 121,680 fragment invocations. An initial checker used unflipped
GL readback rows and incorrectly included an empty region; the checker now
accounts for the lower-left readback convention. The retained failure is a
checker-coordinate error, not a failed driver fix.

`M4_PROBE_CAPTURE_VERTICES=FILE` optionally loads exactly 36 vertices (864 bytes:
float2 position, float2 UV, RGBA8 tint, uint flags) for the original nine-slice
geometry. `M4_PROBE_GRADIENT_OUTPUT=1` then renders its gradients directly.
The self-contained regression above does not use either option.

Validation: 35 targeted Vulkan CTS cases pass; 13 report NotSupported. This
covers derivatives, statistics queries and sample-mask configurations; preserve
the full selected list and original catalog indices in `fixed-validation.json`.
The broad offline graphics compiler test has three pre-existing failures
(half2 vertex and flat-color byte expectations, and a now-supported discard
case). Rebuilding against the unmodified packer produces the same three failures;
they are not claimed as passing or changed by this fix.

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
