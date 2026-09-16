#include <metal_stdlib>
using namespace metal;
kernel void cs(texture2d_array<uint,access::read> t [[texture(0)]],device uint4*out[[buffer(0)]],uint3 p[[thread_position_in_grid]]) {
 sparse_color<uint4> v=t.sparse_read(p.xy,p.z,0);
 out[p.y*128+p.x]=v.resident()?v.value():uint4(0xdeadbeef);
}
