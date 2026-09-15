#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float4 c; };
vertex V vs(uint vid [[vertex_id]], device const float4 *p [[buffer(0)]]) { V o; o.pos = p[vid]; o.c = p[vid+3]; return o; }
struct OutM { float4 c [[color(0)]]; uint m [[sample_mask]]; };
/* early tests + sample mask 0 (CTS sample_count_early_fragment_tests_depth_samples_N) */
[[early_fragment_tests]] fragment OutM fsearlymask0(V v [[stage_in]]) { OutM o; o.m = 0u; o.c = float4(1.0, 1.0, 0.0, 1.0); return o; }
/* no early tests + sample mask 0xAAAAAAAA (the paired no_early_fragment_tests shader) */
fragment OutM fsmaskaa(V v [[stage_in]]) { OutM o; o.m = 0xAAAAAAAAu; o.c = float4(1.0, 1.0, 0.0, 1.0); return o; }
/* early tests + discard after an atomic (CTS discard_early_fragment_tests_depth) */
[[early_fragment_tests]] fragment float4 fsearlydiscard(V v [[stage_in]], device atomic_uint *cnt [[buffer(1)]]) { atomic_fetch_add_explicit(cnt, 1u, memory_order_relaxed); discard_fragment(); return float4(1.0, 1.0, 0.0, 1.0); }
/* early tests + sample mask 0 + discard (CTS samplemask_early_fragment_tests_depth_samples_N) */
[[early_fragment_tests]] fragment OutM fsearlymaskdiscard(V v [[stage_in]], device atomic_uint *cnt [[buffer(1)]]) { atomic_fetch_add_explicit(cnt, 1u, memory_order_relaxed); OutM o; o.m = 0u; o.c = float4(1.0, 1.0, 0.0, 1.0); discard_fragment(); return o; }
/* no early tests + discard, for comparison */
fragment float4 fsdiscardonly(V v [[stage_in]]) { discard_fragment(); return float4(1.0, 1.0, 0.0, 1.0); }
