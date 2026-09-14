#include <metal_stdlib>
using namespace metal;
kernel void cs(device uint *p [[buffer(0)]], device atomic_uint *g [[buffer(1)]], device uint *o [[buffer(2)]], uint i [[thread_position_in_grid]]) {
 uint j=1023-i;
 p[i]=i+(p[j]>>31);
 atomic_thread_fence(mem_flags::mem_device,memory_order_seq_cst,thread_scope_device);
 atomic_store_explicit(&g[i],1u,memory_order_relaxed);
 uint f=atomic_load_explicit(&g[j],memory_order_relaxed);
 atomic_thread_fence(mem_flags::mem_device,memory_order_seq_cst,thread_scope_device);
 o[i]=f?p[j]:i;
}
