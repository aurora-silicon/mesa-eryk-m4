#include <metal_stdlib>
using namespace metal;
kernel void cs(device uchar4 *o [[buffer(0)]], device uchar4 *in [[buffer(1)]], uint tid [[thread_index_in_threadgroup]]) {
 threadgroup uchar4 s[32]; s[tid]=in[tid]; threadgroup_barrier(mem_flags::mem_threadgroup); o[tid]=s[(tid+1)&31];
}
