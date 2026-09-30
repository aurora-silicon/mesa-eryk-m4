# M3 bounded shader inputs

G15S now defaults to the existing GPU UBO input loader and fragment
variants for ordinary buffers/samplers. Other chips retain their defaults.
The three existing diagnostic environment switches remain available, including
explicit false overrides; compilation and shader/pipeline cache UUIDs share
one policy. This does not disable robustness or sampler features.

Only immutable descriptors and statically known UBO words are admitted. The
CPU examines descriptor metadata; the GPU reads uniform contents at execution.
Bounds, sparse aliases, mutable descriptor bindings, uncommon samplers and
capacity overflow retain the general path.

Validation evidence in `/home/council/dev/dirtyroom_m4/build/minecraft-auto-prefetch`
and `/home/council/dev/gpu-replay-bench/diagnostics/m3-auto-prefetch-*`.
Host descriptor-prefetch tests and ARM64 build pass. Minecraft Vulkan and
OpenGL/Zink first/middle/last frames are pixel-identical to retained references.
Prior paired normal-boot measurements: 55.321 to 80.590 Vulkan replay FPS
(+45.68%), unchanged 3024x1837 capture and API capabilities. Fresh automatic-policy medians: 54.5009 FPS disabled versus 78.3465 FPS
enabled (+43.75%). The 686-case frozen-index targeted CTS selection completed:
418 Pass, 268 NotSupported, no other outcomes. Saved checks also pass for
Satisfactory Vulkan gameplay, Factorio Wayland, 0 A.D. Vulkan and SuperTuxKart
Zink at the existing one-level tolerance. M4 was not exercised.

Frostpunk's first cold check exceeded 37 seconds, without a kernel GPU fault;
its evidence was saved and watchdog recovery completed. The bounded
retirement/CPU-progress guard allowed the follow-up to complete normally.
Its image check remains failed under the existing one-level tolerance: frame
16 has a two-level difference in isolated pixels. Two identical optimization-
off control runs also differ by two levels at two pixels, as do two identical
optimization-on runs. This demonstrates pre-existing repeatability limits;
we have not loosened the checker or counted these as image passes.

The follow-up vertex experiment is preserved separately at
`experiment/m3-vertex-inputs-20260930` (`b5822dbc3f5`), not enabled here. It
reduces measured tiling spans, but retains edge-pixel differences and needs
further qualification. No experimental vertex path is installed for the desktop.

`tools/m3-stage-vulkan-candidate.py` stages isolated hash-named Vulkan libraries,
records the source revision and patch, and verifies transferred hashes. It
does not select them for the desktop or run GPU work. Example from workspace
root: `python3 tools/m3-stage-vulkan-candidate.py --label test --output build/test-stage`.
The copy alongside this document is for provenance; the workspace-root tool
is the reusable development entry point.
