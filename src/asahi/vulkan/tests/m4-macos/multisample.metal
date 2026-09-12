// SPDX-License-Identifier: MIT
#include <metal_stdlib>
using namespace metal;
vertex float4 vs(uint id [[vertex_id]]) {
    float2 p[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};
    return float4(p[id], 0, 1);
}
struct Output { float4 color [[color(0)]]; uint mask [[sample_mask]]; };
fragment Output fs(constant uint &mask [[buffer(0)]]) {
    return {float4(1,0,0,1), mask};
}
// Explicit GPU resolve keeps the render pass store-only, matching Linux's
// stored multisample attachment followed by a separate resolve operation.
struct ResolveParams { uint mask, width, samples; };
kernel void resolve_msaa(texture2d_ms<float,access::read> image [[texture(0)]],
                         device uchar4 *output [[buffer(0)]],
                         constant ResolveParams &p [[buffer(1)]],
                         uint2 xy [[thread_position_in_grid]]) {
    if (any(xy >= p.width)) return;
    float4 color=0;
    for (uint s=0; s<p.samples; ++s) color+=image.read(xy,s);
    output[xy.y*p.width+xy.x]=uchar4(round(color*(255.0f/p.samples)));
}
kernel void resolve_single(texture2d<float,access::read> image [[texture(0)]],
                           device uchar4 *output [[buffer(0)]],
                           constant ResolveParams &p [[buffer(1)]],
                           uint2 xy [[thread_position_in_grid]]) {
    if (any(xy >= p.width)) return;
    output[xy.y*p.width+xy.x]=uchar4(round(image.read(xy)*255.0f));
}
