#include <metal_stdlib>
using namespace metal;
kernel void cs(depth2d<float> t [[texture(0)]],sampler s [[sampler(0)]],device float*out[[buffer(0)]],uint2 p[[thread_position_in_grid]]) {
 sparse_color<float> v=t.sparse_sample_compare(s,float2(p)/128.0,0.5,level(0));
 out[p.y*128+p.x]=v.resident()?v.value():-1;
}
