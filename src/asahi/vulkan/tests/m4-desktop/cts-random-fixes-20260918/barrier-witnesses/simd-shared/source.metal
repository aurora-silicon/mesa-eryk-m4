#include <metal_stdlib>
using namespace metal;
kernel void cs(device uint *out [[buffer(0)]], uint i [[thread_index_in_threadgroup]]) {
threadgroup uint x[1024]; x[i]=i;
simdgroup_barrier(mem_flags::mem_threadgroup);
out[i]=x[i^1];
}
