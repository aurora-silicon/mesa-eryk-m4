#include <metal_stdlib>
using namespace metal;
kernel void cs(texturecube<float> tex [[texture(0)]], device const float4 *dirs [[buffer(0)]], device float4 *out [[buffer(1)]], uint i [[thread_position_in_grid]]) {
 constexpr sampler s(coord::normalized, address::clamp_to_edge, filter::linear, mip_filter::linear);
 out[i] = tex.sample(s, dirs[i].xyz, level(dirs[i].w));
}
