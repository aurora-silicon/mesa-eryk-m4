#include <metal_stdlib>
using namespace metal;
kernel void cs(device uint *out [[buffer(0)]], threadgroup atomic_uint *local [[threadgroup(0)]], uint i [[thread_index_in_threadgroup]]) {
 uint a = atomic_fetch_add_explicit(local + i, 1u, memory_order_relaxed);
 uint b = atomic_fetch_add_explicit(local + i, 1u, memory_order_relaxed);
 out[i] = a + b;
}
