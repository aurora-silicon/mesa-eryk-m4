#include <metal_stdlib>
using namespace metal;
kernel void cs(texturecube_array<int> tex [[texture(0)]],
               device const float4 *coord [[buffer(0)]],
               device int4 *out [[buffer(1)]], uint i [[thread_position_in_grid]]) {
   constexpr sampler s(coord::normalized, address::clamp_to_edge, filter::nearest);
   out[i] = tex.sample(s, coord[i].xyz, uint(coord[i].w));
}
