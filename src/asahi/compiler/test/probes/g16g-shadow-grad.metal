#include <metal_stdlib>
using namespace metal;
constexpr sampler s(coord::normalized, address::clamp_to_edge, filter::linear, mip_filter::linear, compare_func::less);
kernel void cs2d(depth2d<float> tex [[texture(0)]], device const float4 *in [[buffer(0)]], device float *out [[buffer(1)]], uint i [[thread_position_in_grid]]) {
 float4 v = in[i]; float4 g = in[i+1];
 out[i] = tex.sample_compare(s, v.xy, v.z, gradient2d(g.xy, g.zw));
}
kernel void cs2doff(depth2d<float> tex [[texture(0)]], device const float4 *in [[buffer(0)]], device const int2 *off [[buffer(2)]], device float *out [[buffer(1)]], uint i [[thread_position_in_grid]]) {
 float4 v = in[i]; float4 g = in[i+1];
 out[i] = tex.sample_compare(s, v.xy, v.z, gradient2d(g.xy, g.zw), off[i]);
}
kernel void cs2darr(depth2d_array<float> tex [[texture(0)]], device const float4 *in [[buffer(0)]], device float *out [[buffer(1)]], uint i [[thread_position_in_grid]]) {
 float4 v = in[i]; float4 g = in[i+1];
 out[i] = tex.sample_compare(s, v.xy, (uint)v.w, v.z, gradient2d(g.xy, g.zw));
}
kernel void cs2darroff(depth2d_array<float> tex [[texture(0)]], device const float4 *in [[buffer(0)]], device const int2 *off [[buffer(2)]], device float *out [[buffer(1)]], uint i [[thread_position_in_grid]]) {
 float4 v = in[i]; float4 g = in[i+1];
 out[i] = tex.sample_compare(s, v.xy, (uint)v.w, v.z, gradient2d(g.xy, g.zw), off[i]);
}
