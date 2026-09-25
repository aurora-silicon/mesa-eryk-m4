# Satisfactory shadow-draw fault after image64 enablement

The native Vulkan capture faults on M3 in a 512x512 depth-only pass. Draw
99359 uses seven live inputs including location 16. The source key holds
32 API locations, but the old compaction loop visited only 16, leaving
compact slot 6 uninitialized. Retained compiler inputs show an R8 format
and stride 43691 on M3, 43690 on M4, instead of the R32_UINT/zero-stride
instance attribute. This is independent of 64-bit image atomics.

Iterate the live source locations and check the separate destination capacity.
Ignore unused declarations in static compaction and dynamic key/stride filling.
The public limits remain 16 live attributes/bindings. The application capture
itself declares location 16 despite a captured maxVertexInputAttributes of 16;
accepting a sparse high location safely is robustness, not an advertised
32-input implementation or proof that this capture is Vulkan-valid.

A private standalone reproducer retains the original vertex shader and all
inputs for one draw, using independent coherent allocations and a 512x512x6
D32 target. Before the fix M3 faults for both direct and indirect draws; M4
completes with an empty depth target. With the fix both chips cover the same
72 pixels as lavapipe. M3 and M4 depth outputs are byte-identical; the maximum
absolute difference from lavapipe is 2.4281689547933638e-08, with no coverage
disagreement. These are diagnostic draws, not full-game qualification.

The standalone assets and full guarded evidence remain in the private local
gpu-replay-bench repository, provenance/satisfactory-image64-20260925.
The public hk_vbo_raw host regression covers sparse locations 16 and 31,
unused declarations, missing formats, and capacity checks.
