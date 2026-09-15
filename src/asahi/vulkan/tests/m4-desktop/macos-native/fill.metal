#include <metal_stdlib>
using namespace metal;

kernel void cs(device uint4 *output [[buffer(0)]],
               uint i [[thread_position_in_grid]])
{
    output[i] = uint4(0x7b2da916u);
}

// Diagnostic capture gate; never selected for the ungated reference runs.
kernel void cs_gated(device uint4 *output [[buffer(0)]],
                     device atomic_uint *gate [[buffer(1)]],
                     uint i [[thread_position_in_grid]])
{
    if (i == 0) {
        atomic_store_explicit(&gate[1], 1u, memory_order_relaxed);
        uint n = 0;
        for (; n < 20000000u; ++n)
            if (atomic_load_explicit(&gate[0], memory_order_relaxed)) break;
        if (n == 20000000u) atomic_store_explicit(&gate[2], 1u, memory_order_relaxed);
    }
    output[i] = uint4(0x7b2da916u);
}
