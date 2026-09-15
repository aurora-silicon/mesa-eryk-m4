#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float4 c; };
vertex V vs(uint vid [[vertex_id]], device const float4 *p [[buffer(0)]]) { V o; o.pos = p[vid]; o.c = p[vid+3]; return o; }
/* perspective interpolant sampled at an explicit offset: the software coefficient path */
struct VI { float4 pos [[position]]; interpolant<float4, interpolation::perspective> c; };
fragment float4 fsoffset(VI v [[stage_in]], constant float2 &off [[buffer(1)]]) { return v.c.interpolate_at_offset(off); }
/* same, at a sample index */
fragment float4 fssample(VI v [[stage_in]], constant uint &s [[buffer(1)]]) { return v.c.interpolate_at_sample(s); }
/* perspective float4 sampled with the sample qualifier at 1x */
struct VS { float4 pos [[position]]; float4 c [[sample_perspective]]; };
fragment float4 fssq(VS v [[stage_in]]) { return v.c; }
/* three-component interpolant, for the coefficient layout of a shorter binding */
struct VI3 { float4 pos [[position]]; interpolant<float3, interpolation::perspective> c; };
fragment float4 fsoffset3(VI3 v [[stage_in]], constant float2 &off [[buffer(1)]]) { return float4(v.c.interpolate_at_offset(off), 1.0); }
