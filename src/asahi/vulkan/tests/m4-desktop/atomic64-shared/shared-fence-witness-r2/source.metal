#include <metal_stdlib>
using namespace metal;
kernel void cs(device uint *out [[buffer(0)]], threadgroup atomic_uint *local [[threadgroup(0)]], uint i [[thread_index_in_threadgroup]]) {
 uint a = atomic_fetch_add_explicit(local + i, 1u, memory_order_relaxed);
 atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope_threadgroup);
 uint b = atomic_fetch_add_explicit(local + i, 1u, memory_order_relaxed);
 out[i] = a + b;
}
