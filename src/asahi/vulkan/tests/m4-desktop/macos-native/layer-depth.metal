// SPDX-License-Identifier: MIT
#include <metal_stdlib>
using namespace metal;
struct Vertex { float4 position [[position]]; uint layer [[render_target_array_index]]; };
vertex Vertex vs(uint id [[vertex_id]], uint instance [[instance_id]]) {
    float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};
    return {float4(p[id],float(instance)*.25f,1),instance};
}
fragment void fs() {}

struct ColorVertex {
    float4 position [[position]];
    uint layer [[render_target_array_index]];
    float red [[flat]];
};
vertex ColorVertex vs_color(uint id [[vertex_id]], uint instance [[instance_id]]) {
    float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};
    return {float4(p[id],0,1),instance,float(instance)*.25f};
}
fragment float4 fs_color(ColorVertex v [[stage_in]]) {
    return float4(v.red,.5f,.75f,1.f);
}
