#include <metal_stdlib>
using namespace metal;
kernel void cs(device atomic_uint *lock [[buffer(0)]], device uint *out [[buffer(1)]], threadgroup atomic_uint *local [[threadgroup(0)]], uint i [[thread_index_in_threadgroup]]) {
 for (;;) {
  uint expected = 0;
  if (atomic_compare_exchange_weak_explicit(lock, &expected, 1u, memory_order_relaxed, memory_order_relaxed)) {
   atomic_thread_fence(mem_flags::mem_device | mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope_device);
   uint old = atomic_fetch_or_explicit(local + i, 0u, memory_order_relaxed);
   atomic_exchange_explicit(local + i, old + 1u, memory_order_relaxed);
   atomic_thread_fence(mem_flags::mem_device | mem_flags::mem_threadgroup, memory_order_seq_cst, thread_scope_device);
   atomic_store_explicit(lock, 0u, memory_order_relaxed);
   out[i] = old;
   return;
  }
 }
}
