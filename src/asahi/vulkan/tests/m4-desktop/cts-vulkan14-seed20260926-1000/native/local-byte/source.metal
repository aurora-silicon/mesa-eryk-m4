#include <metal_stdlib>
using namespace metal;
kernel void probe(device ushort *in [[buffer(0)]], device uint2 *out [[buffer(1)]], uint i [[thread_position_in_grid]], uint t [[thread_index_in_threadgroup]]) { threadgroup ushort v; if(t==0) v=in[i]; threadgroup_barrier(mem_flags::mem_threadgroup); threadgroup volatile uchar *b=(threadgroup volatile uchar *)&v; out[i]=uint2(b[0],b[1]); }
