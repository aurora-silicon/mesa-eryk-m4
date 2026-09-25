#include <metal_stdlib>
using namespace metal;
#define STD device float *o [[buffer(0)]], device const float *a [[buffer(1)]], device const float *b [[buffer(2)]], constant float4 &u [[buffer(3)]], uint i [[thread_position_in_grid]]
#define TX texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float4 *o4 [[buffer(0)]], device const float2 *c [[buffer(1)]], device const float *lv [[buffer(2)]], uint i [[thread_position_in_grid]]

/* multi-slot waits */
kernel void w_two_loads_one_use(STD) { o[i] = a[i] * b[i]; }
kernel void w_three_loads_one_use(STD) { o[i] = fma(a[i], b[i], a[i + 9]); }
kernel void w_load_and_sr(STD) { o[i] = a[i] * float(i); }
kernel void w_load_tex_use(TX) { float x = lv[i]; float4 v = t.sample(s, c[i], level(0)); o4[i] = v * x; }
kernel void w_six_loads(STD) { o[i] = a[i] + a[i+1] + a[i+2] + a[i+3] + a[i+4] + a[i+5] + a[i+6] + a[i+7]; }
kernel void w_eight_loads_tree(STD) { float x0=a[i],x1=a[i+64],x2=a[i+128],x3=a[i+192],x4=b[i],x5=b[i+64],x6=b[i+128],x7=b[i+192]; o[i]=((x0*x1)+(x2*x3))*((x4*x5)+(x6*x7)); }

/* loop-carried waits: consumer at loop head */
kernel void w_pointer_chase(device uint *o [[buffer(0)]], device const uint *a [[buffer(1)]], constant uint &n [[buffer(2)]], uint i [[thread_position_in_grid]])
{ uint j = i; for (uint k = 0; k < n; k++) j = a[j]; o[i] = j; }
kernel void w_pointer_chase_alu(device uint *o [[buffer(0)]], device const uint *a [[buffer(1)]], constant uint &n [[buffer(2)]], uint i [[thread_position_in_grid]])
{ uint j = i; for (uint k = 0; k < n; k++) j = a[j] * 3u + k; o[i] = j; }
kernel void w_loop_load_acc(STD) { float s = 0; for (int k = 0; k < int(u.x); k++) s = s * a[i + k] + 1.0f; o[i] = s; }
kernel void w_loop_tex(texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float4 *o [[buffer(0)]], constant int &n [[buffer(1)]], uint i [[thread_position_in_grid]])
{ float2 p = float2(i, 0) * 0.01f; float4 acc = 0; for (int k = 0; k < n; k++) { float4 v = t.sample(s, p, level(0)); p = v.xy; acc += v; } o[i] = acc; }
kernel void w_loop_after_load(STD) { float x = a[i]; float s = 0; for (int k = 0; k < int(u.x); k++) s = s * x + 1.0f; o[i] = s; }
kernel void w_if_load(STD) { float x = 0; if (i & 1) x = a[i]; o[i] = x * u.y; }
kernel void w_if_load_use_inside(STD) { float x = u.z; if (a[i] > 0) { x = b[i]; x = x * x; } o[i] = x; }
kernel void w_load_before_branch_use_after(STD) { float x = a[i]; float y = u.x; if (u.y > 0) y = sin(y); o[i] = x + y; }

/* WAR hazards: async reader of a GPR then overwrite */
kernel void w_store_then_reuse(STD) { float x = a[i]; o[i] = x; x = x * 2.0f; o[i + 64] = x; x = x + 3.0f; o[i + 128] = x; }
kernel void w_many_stores(STD) { float x = a[i]; for (int k = 0; k < 8; k++) { o[i + k * 64] = x; x = x * 1.5f + 1.0f; } }
kernel void w_tg_store_load(device float *o [[buffer(0)]], device const float *a [[buffer(1)]], uint i [[thread_position_in_grid]], uint l [[thread_position_in_threadgroup]])
{ threadgroup float sm[64]; sm[l] = a[i]; threadgroup_barrier(mem_flags::mem_threadgroup); float x = sm[63 - l]; sm[l] = x * 2.0f; threadgroup_barrier(mem_flags::mem_threadgroup); o[i] = sm[l ^ 1] + x; }

/* textures: GPR-reading sample operands */
kernel void w_tex_lod_loaded(TX) { o4[i] = t.sample(s, c[i], level(lv[i])); }
kernel void w_tex_bias_grad(TX) { float2 p = c[i]; o4[i] = t.sample(s, p, gradient2d(p * 0.1f, p * 0.2f)); }
kernel void w_tex_offset(TX) { o4[i] = t.sample(s, c[i], level(0), int2(1, -1)); }
kernel void w_tex_read_loaded(texture2d<float> t [[texture(0)]], device float4 *o [[buffer(0)]], device const uint2 *c [[buffer(1)]], uint i [[thread_position_in_grid]]) { o[i] = t.read(c[i]); }
kernel void w_tex_write_loaded(texture2d<float, access::write> t [[texture(0)]], device const float4 *a [[buffer(0)]], device const uint2 *c [[buffer(1)]], uint i [[thread_position_in_grid]]) { t.write(a[i], c[i]); }
kernel void w_tex_sample_chain3(TX) { float4 v = t.sample(s, c[i], level(0)); v = t.sample(s, v.xy, level(0)); v = t.sample(s, v.zw, level(0)); o4[i] = v; }
kernel void w_tex_array(texture2d_array<float> t [[texture(0)]], sampler s [[sampler(0)]], device float4 *o [[buffer(0)]], device const float2 *c [[buffer(1)]], device const uint *sl [[buffer(2)]], uint i [[thread_position_in_grid]]) { o[i] = t.sample(s, c[i], sl[i], level(0)); }
kernel void w_tex_depth_cmp(depth2d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float *o [[buffer(0)]], device const float3 *c [[buffer(1)]], uint i [[thread_position_in_grid]]) { o[i] = t.sample_compare(s, c[i].xy, c[i].z, level(0)); }
kernel void w_tex_3d(texture3d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float4 *o [[buffer(0)]], device const float3 *c [[buffer(1)]], uint i [[thread_position_in_grid]]) { o[i] = t.sample(s, c[i], level(0)); }
kernel void w_tex_cube(texturecube<float> t [[texture(0)]], sampler s [[sampler(0)]], device float4 *o [[buffer(0)]], device const float3 *c [[buffer(1)]], uint i [[thread_position_in_grid]]) { o[i] = t.sample(s, c[i], level(0)); }
kernel void w_tex_uint(texture2d<uint> t [[texture(0)]], device uint4 *o [[buffer(0)]], device const uint2 *c [[buffer(1)]], uint i [[thread_position_in_grid]]) { o[i] = t.read(c[i]) + 1; }

/* atomics */
kernel void w_atomic_gpr_operand(device atomic_uint *c [[buffer(0)]], device const uint *a [[buffer(1)]], device uint *o [[buffer(2)]], uint i [[thread_position_in_grid]])
{ uint v = atomic_fetch_add_explicit(c + (i & 7), a[i], memory_order_relaxed); o[i] = v + a[i+1]; }
kernel void w_atomic_max_ret(device atomic_uint *c [[buffer(0)]], device const uint *a [[buffer(1)]], device uint *o [[buffer(2)]], uint i [[thread_position_in_grid]])
{ uint v = atomic_fetch_max_explicit(c, a[i] * 3u, memory_order_relaxed); o[i] = v * v; }
kernel void w_atomic_chain(device atomic_uint *c [[buffer(0)]], device uint *o [[buffer(1)]], uint i [[thread_position_in_grid]])
{ uint v = atomic_fetch_add_explicit(c, 1u, memory_order_relaxed); uint w = atomic_fetch_add_explicit(c + 1, v, memory_order_relaxed); o[i] = w; }
kernel void w_atomic_xchg_float(device atomic_float *c [[buffer(0)]], device float *o [[buffer(1)]], device const float *a [[buffer(2)]], uint i [[thread_position_in_grid]])
{ float v = atomic_exchange_explicit(c + i, a[i], memory_order_relaxed); o[i] = v * 2.0f; }
kernel void w_tg_atomic_ret(device uint *o [[buffer(0)]], device const uint *a [[buffer(1)]], uint i [[thread_position_in_grid]], uint l [[thread_position_in_threadgroup]])
{ threadgroup atomic_uint cc[4]; if (l < 4) atomic_store_explicit(&cc[l], 0u, memory_order_relaxed); threadgroup_barrier(mem_flags::mem_threadgroup);
  uint v = atomic_fetch_add_explicit(&cc[l & 3], a[i], memory_order_relaxed); o[i] = v + 1; }

/* special registers and uniforms */
kernel void w_sr_many(device uint *o [[buffer(0)]], uint3 g [[thread_position_in_grid]], uint3 tgp [[threadgroup_position_in_grid]], uint3 ltp [[thread_position_in_threadgroup]], uint si [[simdgroup_index_in_threadgroup]], uint sl [[thread_index_in_simdgroup]], uint3 tpt [[threads_per_threadgroup]])
{ o[g.x] = (g.y ^ tgp.z) * (ltp.y + si) + sl * tpt.x; }
kernel void w_sr_late_use(device float *o [[buffer(0)]], device const float *a [[buffer(1)]], uint i [[thread_position_in_grid]], uint l [[thread_index_in_simdgroup]])
{ float x = a[i]; x = x * x + 1.0f; x = x * x + 2.0f; o[i] = x + float(l); }
kernel void w_uniform_array(device float *o [[buffer(0)]], constant float4 *uu [[buffer(1)]], device const uint *idx [[buffer(2)]], uint i [[thread_position_in_grid]]) { o[i] = uu[idx[i] & 7].x * uu[1].y; }

/* control flow + waits at block boundaries */
kernel void w_switch_loads(STD) { float r; switch (i & 3) { case 0: r = a[i]; break; case 1: r = b[i]; break; case 2: r = a[i + 1] * b[i + 1]; break; default: r = 0; } o[i] = r * u.x; }
kernel void w_nested_loop_loads(STD) { float s = 0; for (int k = 0; k < int(u.x); k++) { float x = a[k]; for (int j = 0; j < int(u.y); j++) s = s * x + b[j]; } o[i] = s; }
kernel void w_break_loop(STD) { float s = 0; for (int k = 0; k < 64; k++) { float x = a[i + k]; if (x < 0) break; s += x; } o[i] = s; }
kernel void w_discard_like_return(STD) { float x = a[i]; if (x > u.x) { o[i] = b[i]; return; } o[i] = x; }
kernel void w_simd_after_load(STD) { float x = a[i]; o[i] = simd_shuffle_down(x, 1u) + simd_max(x); }
kernel void w_quad_after_load(STD) { float x = a[i]; o[i] = quad_shuffle(x, 2u) + quad_sum(x); }
kernel void w_half_loads(device half *o [[buffer(0)]], device const half *a [[buffer(1)]], device const half *b [[buffer(2)]], uint i [[thread_position_in_grid]]) { o[i] = a[i] * b[i] + a[i + 3]; }
kernel void w_long_load_chain(STD) { float x = a[i]; float y = b[uint(x)]; float z = a[uint(y)]; float w = b[uint(z)]; o[i] = x + y + z + w; }
kernel void w_int64_load(device ulong *o [[buffer(0)]], device const ulong *a [[buffer(1)]], uint i [[thread_position_in_grid]]) { o[i] = a[i] * a[i + 1]; }
kernel void w_char_short_loads(device uint *o [[buffer(0)]], device const uchar *a [[buffer(1)]], device const ushort *b [[buffer(2)]], uint i [[thread_position_in_grid]]) { o[i] = a[i] + b[i]; }
kernel void w_vec_loads(device float4 *o [[buffer(0)]], device const float4 *a [[buffer(1)]], device const float2 *b [[buffer(2)]], uint i [[thread_position_in_grid]]) { o[i] = a[i] * b[i].x + float4(b[i + 1], 0, 1); }
