#include <metal_stdlib>
using namespace metal;
kernel void cs(device uint *out [[buffer(0)]], uint i [[thread_index_in_threadgroup]]) {
threadgroup uint x[1024]; x[i]=i;
atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope_threadgroup);
out[i]=x[i^1];
}
