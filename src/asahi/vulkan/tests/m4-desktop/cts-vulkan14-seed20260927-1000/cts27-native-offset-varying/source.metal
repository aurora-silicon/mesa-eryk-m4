#include <metal_stdlib>
using namespace metal;
struct Args { float4 coord; int4 offset; };
kernel void probe(texture3d<float, access::sample> tex [[texture(0)]], sampler smp [[sampler(0)]], constant Args &a [[buffer(0)]], device float4 *out [[buffer(1)]], uint gid [[thread_position_in_grid]]) {
 out[gid] = tex.sample(smp, a.coord.xyz, level(a.coord.w + float(gid & 3)), a.offset.xyz);
}
