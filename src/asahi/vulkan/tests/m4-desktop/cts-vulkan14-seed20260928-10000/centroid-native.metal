#include <metal_stdlib>
using namespace metal;
struct V { float4 p [[position]]; float4 c [[user(c),centroid_perspective]]; };
vertex V vs(const device float4 *v [[buffer(0)]], uint id [[vertex_id]]) { return {v[2*id],v[2*id+1]}; }
fragment float4 fs(V in [[stage_in]]) {return in.c;}
