#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float2 uv; float4 col; float3 n; };
vertex V vs(uint vid [[vertex_id]], device const float4 *p [[buffer(0)]], constant float4x4 &m [[buffer(1)]])
{ V v; v.pos = m * p[vid]; v.uv = p[vid].xy * 0.5f; v.col = p[vid + 1]; v.n = normalize(p[vid].xyz); return v; }
fragment float4 fs_flat(V in [[stage_in]]) { return in.col; }
fragment float4 fs_tex(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) { return t.sample(s, in.uv) * in.col; }
fragment float4 fs_tex2(V in [[stage_in]], texture2d<float> t [[texture(0)]], texture2d<float> t2 [[texture(1)]], sampler s [[sampler(0)]])
{ float4 a = t.sample(s, in.uv); float4 b = t2.sample(s, a.xy + in.uv); return a * b + in.col; }
fragment float4 fs_light(V in [[stage_in]], constant float4 &l [[buffer(0)]])
{ float3 n = normalize(in.n); float d = max(dot(n, l.xyz), 0.0f); float sp = pow(max(dot(reflect(-l.xyz, n), float3(0,0,1)), 0.0f), 32.0f); return in.col * d + sp; }
fragment float4 fs_discard(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]])
{ float4 c = t.sample(s, in.uv); if (c.a < 0.5f) discard_fragment(); return c; }
fragment float4 fs_deriv(V in [[stage_in]]) { return float4(dfdx(in.uv), dfdy(in.uv)) + fwidth(in.col); }
fragment float4 fs_loop_tex(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], constant int &n [[buffer(0)]])
{ float4 a = 0; for (int k = 0; k < n; k++) a += t.sample(s, in.uv + float2(k) * 0.01f); return a; }
fragment float4 fs_buffer(V in [[stage_in]], device const float4 *b [[buffer(0)]]) { return b[uint(in.pos.x) & 63] * in.col; }
fragment float4 fs_bias(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) { return t.sample(s, in.uv, bias(in.col.x)); }
fragment float4 fs_if(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]])
{ float4 c = in.col; if (in.uv.x > 0.5f) c = t.sample(s, in.uv * 2.0f); return c * 0.5f; }
fragment half4 fs_half(V in [[stage_in]], texture2d<half> t [[texture(0)]], sampler s [[sampler(0)]]) { half4 c = t.sample(s, in.uv); return c * half4(in.col) + half(0.25); }
fragment float4 fs_depth_read(V in [[stage_in]], depth2d<float> d [[texture(0)]], sampler s [[sampler(0)]]) { return float4(d.sample_compare(s, in.uv, in.col.x)); }
fragment float4 fs_atomic(V in [[stage_in]], device atomic_uint *c [[buffer(0)]]) { uint v = atomic_fetch_add_explicit(c, 1u, memory_order_relaxed); return float4(float(v)); }
