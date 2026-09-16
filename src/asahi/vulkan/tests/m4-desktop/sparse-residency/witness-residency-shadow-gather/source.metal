#include <metal_stdlib>
using namespace metal;
kernel void cs(depth2d<float> t [[texture(0)]],sampler s [[sampler(0)]],device float4*out[[buffer(0)]],uint2 p[[thread_position_in_grid]]) {
 sparse_color<float4> v=t.sparse_gather_compare(s,float2(p)/128.0,0.5);
 out[p.y*128+p.x]=v.resident()?v.value():float4(-1);
}
