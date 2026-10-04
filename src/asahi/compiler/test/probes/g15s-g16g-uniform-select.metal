#include <metal_stdlib>
using namespace metal;
struct V { float4 position [[position]]; float4 value; };
vertex V vs(uint id [[vertex_id]]) {
    V v;
    v.position = float4((id & 1) ? 1.0f : -1.0f, (id & 2) ? 1.0f : -1.0f, 0.0f, 1.0f);
    v.value = float4(float(id) * 0.25f, float(id) * 0.5f, float(id) * 0.75f, float(id));
    return v;
}
fragment float4 fs(V v [[stage_in]], constant float4 &c [[buffer(0)]]) {
    float a = v.value.x < c.x ? c.y : v.value.y;
    float b = c.z < v.value.y ? v.value.z : c.w;
    float d = v.value.z == c.w ? c.x : v.value.w;
    float e = max(v.value.w, c.y);
    return float4(a, b, d, e);
}
