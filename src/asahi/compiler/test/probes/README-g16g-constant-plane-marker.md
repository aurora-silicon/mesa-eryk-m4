# G16G constant varying planes carry a marker the coefficient math must honour

`dEQP-VK.draw.{renderpass,dynamic_rendering.*}.multiple_interpolation.{separate,structured}.with_sample_decoration.1_sample`
(8 cases) failed with "smooth and sample produced different results without
multisampling". The reference render of a single `sample`-qualified `vec4`
varying returned alpha equal to the interpolated W (128 at the w=0.5 vertex,
255 where w=1) for a constant vertex alpha of 1.0; RGB matched the centroid
reference exactly.

## Reproduction

`g16g-interp-sample-probe.c` (also `tools/interp-sample-probe.c`) replicates
the CTS render: interleaved vec4 position and colour in a vertex buffer, the
`#version 430` single-varying shader pairs generated from the CTS templates
(`vert_*.vert`/`frag_*.frag` compiled with `glslangValidator -V --vn`), a
128x128 R8G8B8A8 target cleared by the render pass, one perspective triangle.
Arguments name the pipelines to render (`smooth flat noperspective centroid
sample`, the CTS five-varying pair `multi0..multi4`, `+name` for sample rate
shading); `A:a0,a1,a2` overrides the vertex alphas, `K:r,g,b,a` gives every
vertex the same colour, `F` renders to R32G32B32A32_SFLOAT and prints raw
words. On the M4 run only through `gpu-experiment.py` and `run-m4-gl.py`.

* `sample` failed exactly like the CTS (2,967 pixels with alpha below 255,
  minimum 128); `smooth`, `centroid` and every earlier probe passed because
  their vertex shaders wrote a compile-time constant alpha that the UVS
  lowering elides, so the fragment side never loaded that plane.
* `A:1,0.5,0.25` (varying alpha) matched `smooth` exactly.
* `K:0.25,0.5,0.75,0.5` (every channel constant) returned every channel
  multiplied by W.
* Over-declaring the HAL200 fragment iteration count, the varying-table
  coefficient or slot counts, the smooth/flat/linear UVS counts, or padding
  the table with a trailing binding changed nothing; setting the iteration
  count to the coefficient register count broke the hardware `interp` path
  instead (it is the binding count).

Raw coefficient words through the `F` target (experiment
`20260915T2*-claude-interpprobe12`): a varying plane holds ordinary floats
(`A=bbbbe414 B=3c6ade04 C=3b8cea24`); a plane whose three vertex values are
equal holds `A=00000001 B=00000001 C=<value>`, without the 1/W factor of a
perspective plane. The hardware `f32interp` and its projective multiply
honour that marker (the smooth path was always right); LDCF-based software
interpolation must select C when it is present.

## Witness (AIR-NT 32023.917, applegpu_g16g)

`g16g-interpolate-at-offset.metal` compiled with
`build/claude-fix/witness/mcompile.py --stage render --function fsoffset`
loads the same five coefficient registers (`ldcfr` f15 = 0..4, identical
base fields) before its own evaluation chain, so Apple's software path reads
the same registers; the legacy Mesa `cf_valid` check ("significand nonzero
and exponent 0 or 1", kept as an XXX) is that marker test.

## Fix

`agx_nir_lower_interpolation.c`: on the native path select the constant
channel when A has a nonzero significand and an exponent of 0 or 1, written
with two masks, one unsigned compare and one inequality. The earlier
shifted-add chain of `cf_valid` misevaluated on G16G for ordinary
coefficients with the float LSB set (A = 2/137 on a 137-pixel wide target),
which is why the fallback had been skipped there
(`pipeline.*.multisample_interpolation.*distinct_values.137_191_1.samples_4`,
six cases, now pass with the marker test).
