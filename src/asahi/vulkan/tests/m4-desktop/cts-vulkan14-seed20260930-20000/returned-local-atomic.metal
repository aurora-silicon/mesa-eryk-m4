#include <metal_stdlib>
using namespace metal;
kernel void cs(device uint *out [[buffer(0)]], uint lane [[thread_index_in_threadgroup]]) {
    auto p = reinterpret_cast<threadgroup atomic_uint *>(ulong(lane * 4));
    out[lane] = atomic_fetch_add_explicit(p, lane + 1, memory_order_relaxed);
}
