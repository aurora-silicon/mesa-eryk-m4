# G16G demote, derivatives and quad helper lanes

`dEQP-VK.glsl.demote.{basic,function,static_loop,dynamic_loop,function_static_loop}_deriv`
(5) and `dEQP-VK.draw.renderpass.shader_invocation.helper_invocation{,_volatile,_volatile_mem_model}`
(3) failed on the native path: after `demote`, derivatives of a constant
varying plus `gl_FragCoord` and `dFdx(float(helperInvocationEXT()))` were
wrong in every quad, and demoted lanes kept performing SSBO stores.

## Evidence

`g16g-demote-derivative-probe.{vert,frag}` (storage-buffer fragment probe,
`vulkan-fragment-probe.py --storage-stage fragment`) and
`g16g-helper-quad-probe.frag` (2x2 and 4x4 extents) on the M4, experiments
`claude-derivprobe1..4`, `claude-killprobe1..2`, `claude-helperprobe1..3`,
`claude-quadprobe1`:

- With the kill MOVMSK at the demote point, derivatives after the demote
  read stale neighbours. With the kill deferred to the end of the shader,
  live lanes computed every derivative and quad broadcast correctly, and
  the colour kill still suppressed the store in unconditional, conditional
  and CTS-shaped shaders.
- Derivatives along a primitive edge were still wrong while the shader read
  SR2 for `helperInvocationEXT()` in the original long form (f48); reading
  it in the short form, as Apple's dfdx-using `simd_is_helper_thread`
  fragment does (`g16g-helper-quad.metal` fsdemotehelper: `rld 04841426`
  after the initial MOVMSK, versus `rld 0484110601000000` in fshelper
  without derivatives), keeps the out-of-primitive helper lanes.
- Demoted lanes wrote through plain SSBO stores: the hardware does not
  predicate helper writes on G16G, so `nir_lower_helper_writes` now lowers
  plain stores as well as atomics on the native path.
- Acquiring coverage class 1 around bare kills (an experiment) lost the
  depth writes of z_export static alpha-to-coverage shaders and of forced
  early sample-mask shaders; only test-running updates acquire it.
- In a 2x2 framebuffer drawn as two triangles, pixel (1,1) is subgroup lane
  7 (a second quad with three helper lanes); quad broadcasts and
  derivatives are consistent within each quad.

## Change

- `agx_nir_lower_sample_mask_deferred`: on the native path, shaders whose
  NIR needs coarse or full quad helpers accumulate every `demote_samples`
  into one killed mask and issue a single coverage update at the end
  (fused with the late tests when no early tests or depth write run them).
  Other shaders keep the in-place kills.
- `agx3_pack_binary.c`: the SR2 helper query uses the short form in those
  shaders; kills never acquire class 1.
- `agx_nir_lower_frag_sidefx`: all helper writes are predicated in
  software on G17P.

Hardware: the 8 cases pass (`claude-demote-final3`); the 7 early-fragment
cases, 3 z_export cases, reg1000 (815/186) and 1,500 multisample,
sample-mask, discard, depth-stencil and occlusion cases pass
(`claude-ef-five-*`, `claude-zexport-final2`, `claude-reg1000-demote3`,
`claude-demote-reg3b`).
