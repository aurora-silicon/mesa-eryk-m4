#!/usr/bin/env python3
"""Generate the G15G scheduling-stall witness corpus (kernels.metal + cases.txt)."""
import sys, os
out = sys.argv[1] if len(sys.argv) > 1 else '.'
K = []   # (name, body)

HDR = '''#include <metal_stdlib>
using namespace metal;
'''

def kern(name, params, body):
    K.append((name, f'kernel void {name}({params})\n{{\n{body}\n}}\n'))

STD = 'device float *o [[buffer(0)]], device const float *a [[buffer(1)]], constant float4 &u [[buffer(2)]], uint i [[thread_position_in_grid]]'
STDH = 'device half *o [[buffer(0)]], device const half *a [[buffer(1)]], constant half4 &u [[buffer(2)]], uint i [[thread_position_in_grid]]'
STDI = 'device uint *o [[buffer(0)]], device const uint *a [[buffer(1)]], constant uint4 &u [[buffer(2)]], uint i [[thread_position_in_grid]]'
STDS = 'device int *o [[buffer(0)]], device const int *a [[buffer(1)]], constant int4 &u [[buffer(2)]], uint i [[thread_position_in_grid]]'

# 1. unary ops, fp32/fp16
f_unary = {
 'sqrt':'sqrt(x)', 'rsqrt':'rsqrt(x)', 'sin':'sin(x)', 'cos':'cos(x)', 'exp':'exp(x)',
 'exp2':'exp2(x)', 'log':'log(x)', 'log2':'log2(x)', 'rcp':'1.0f/x', 'div':'x/u.x',
 'floor':'floor(x)', 'ceil':'ceil(x)', 'fract':'fract(x)', 'trunc':'trunc(x)', 'rint':'rint(x)',
 'abs':'fabs(x)', 'sat':'saturate(x)', 'tan':'tan(x)', 'atan':'atan(x)', 'pow':'pow(x,u.y)',
 'fastsin':'fast::sin(x)', 'fastexp':'fast::exp(x)', 'precsqrt':'precise::sqrt(x)', 'precdiv':'precise::divide(x,u.x)',
 'tanh':'tanh(x)', 'asin':'asin(x)', 'acos':'acos(x)', 'sinh':'sinh(x)', 'fmod':'fmod(x,u.x)',
 'dfdx_free':'x*x',
}
for n, e in f_unary.items():
    kern(f'f32_{n}', STD, f'  float x = a[i];\n  o[i] = {e};')
    eh = e.replace('1.0f', '1.0h').replace('fast::', '').replace('precise::divide(x,u.x)', 'x/u.x').replace('precise::sqrt', 'sqrt')
    kern(f'f16_{n}', STDH, f'  half x = a[i];\n  o[i] = {eh};')

# 2. dependency chains at varying distance, fp32 / fp16 / int
for T, P, ty, one in (('f32', STD, 'float', '1.5f'), ('f16', STDH, 'half', '1.5h'), ('u32', STDI, 'uint', '3u'), ('i32', STDS, 'int', '3')):
    for n in (1, 2, 3, 4, 6, 8, 12, 16):
        lines = [f'  {ty} v0 = a[i];']
        for k in range(1, n + 1):
            lines.append(f'  {ty} v{k} = v{k-1} * v{k-1} + {one};' if T[0]=='f' else f'  {ty} v{k} = v{k-1} * v{k-1} + ({ty})(u.x ^ {k});')
        lines.append(f'  o[i] = v{n};')
        kern(f'{T}_chain{n}', P, '\n'.join(lines))
    # independent parallel chains with consumer at distance d
    for d in (1, 2, 3, 4, 5, 6, 8, 10):
        lines = [f'  {ty} x = a[i]; {ty} y = a[i+1];', f'  {ty} p = x * u.x;']
        for k in range(d):
            lines.append(f'  y = y * y + ({ty}){k+2};')
        lines.append(f'  o[i] = p + y;')
        kern(f'{T}_dist{d}', P, '\n'.join(lines))
    # transcendental producer at distance d
    if T[0] == 'f':
        for fn in ('sqrt', 'exp2', 'rsqrt', 'sin'):
            for d in (0, 1, 2, 3, 5, 8):
                lines = [f'  {ty} x = a[i]; {ty} y = a[i+1];', f'  {ty} t = {fn}(x);']
                for k in range(d):
                    lines.append(f'  y = y * u.x + ({ty}){k+2};')
                lines.append(f'  o[i] = t * y;')
                kern(f'{T}_{fn}_dist{d}', P, '\n'.join(lines))

# 3. int ops
iops = {'add':'x+u.x','sub':'x-u.y','mul':'x*u.x','mad':'x*u.x+u.y','div':'x/u.x','mod':'x%u.x','shl':'x<<u.x','shr':'x>>u.x',
        'and':'x&u.x','or':'x|u.x','xor':'x^u.x','clz':'clz(x)','popcount':'popcount(x)','rev':'reverse_bits(x)','min':'min(x,u.x)',
        'max':'max(x,u.x)','mulhi':'mulhi(x,u.x)','rotate':'rotate(x,u.x)','extract':'extract_bits(x,3u,7u)','select':'x>u.x?x:u.y',
        'cvt_f':'(uint)((float)x*1.5f)', 'div_const':'x/7u', 'abs_diff':'absdiff(x,u.x)'}
for n, e in iops.items():
    kern(f'u32_{n}', STDI, f'  uint x = a[i];\n  o[i] = {e};')
    kern(f'u16_{n}', 'device ushort *o [[buffer(0)]], device const ushort *a [[buffer(1)]], constant ushort4 &u [[buffer(2)]], uint i [[thread_position_in_grid]]',
         f'  ushort x = a[i];\n  o[i] = (ushort)({e.replace("3u,7u","3u,7u").replace("1.5f","1.5f").replace("7u","(ushort)7")});')

# 4. memory
kern('mem_load1', STD, '  o[i] = a[i];')
kern('mem_load2', STD, '  o[i] = a[i] + a[i+7];')
kern('mem_load4', STD, '  o[i] = a[i] + a[i+7] + a[i*3] + a[i^5];')
kern('mem_load4_indep', STD, '  float x=a[i],y=a[i+1],z=a[i+2],w=a[i+3];\n  o[i]=x; o[i+64]=y; o[i+128]=z; o[i+192]=w;')
kern('mem_load_chain', 'device float *o [[buffer(0)]], device const uint *a [[buffer(1)]], uint i [[thread_position_in_grid]]', '  uint j = a[i]; uint k = a[j]; uint l = a[k];\n  o[i] = (float)l;')
kern('mem_load_vec4', 'device float4 *o [[buffer(0)]], device const float4 *a [[buffer(1)]], constant float4 &u [[buffer(2)]], uint i [[thread_position_in_grid]]', '  o[i] = a[i] * u + a[i+1];')
kern('mem_load_use_later', STD, '  float x = a[i]; float y = u.x; for (int k=0;k<4;k++) y = y*y+1.0f;\n  o[i] = x + y;')
kern('mem_store_multi', STD, '  float x = a[i];\n  o[i]=x; o[i+1]=x*2.0f; o[i+2]=x*3.0f; o[i+3]=x+u.x;')
kern('mem_uniform_only', STD, '  o[i] = u.x * u.y + u.z;')
kern('mem_constant_array', 'device float *o [[buffer(0)]], constant float *c [[buffer(1)]], uint i [[thread_position_in_grid]]', '  o[i] = c[i & 15] * c[3];')
kern('mem_half_load', STDH, '  o[i] = a[i] * u.x + a[i+1];')
kern('mem_char_load', 'device uchar *o [[buffer(0)]], device const uchar *a [[buffer(1)]], uint i [[thread_position_in_grid]]', '  o[i] = a[i] + a[i+1];')
kern('atomic_add', 'device atomic_uint *o [[buffer(0)]], device const uint *a [[buffer(1)]], uint i [[thread_position_in_grid]]', '  atomic_fetch_add_explicit(o, a[i], memory_order_relaxed);')
kern('atomic_ret', 'device atomic_uint *c [[buffer(0)]], device uint *o [[buffer(1)]], uint i [[thread_position_in_grid]]', '  uint v = atomic_fetch_add_explicit(c, 1u, memory_order_relaxed);\n  o[i] = v * 3u;')
kern('atomic_cas', 'device atomic_uint *c [[buffer(0)]], device uint *o [[buffer(1)]], uint i [[thread_position_in_grid]]', '  uint e = 0; bool ok = atomic_compare_exchange_weak_explicit(c, &e, i, memory_order_relaxed, memory_order_relaxed);\n  o[i] = ok ? e : 7u;')
TG = 'device float *o [[buffer(0)]], device const float *a [[buffer(1)]], uint i [[thread_position_in_grid]], uint l [[thread_position_in_threadgroup]]'
kern('tg_basic', TG, '  threadgroup float s[256];\n  s[l] = a[i];\n  threadgroup_barrier(mem_flags::mem_threadgroup);\n  o[i] = s[l ^ 1] + s[255 - l];')
kern('tg_reduce', TG, '  threadgroup float s[256];\n  s[l] = a[i];\n  for (uint k = 128; k > 0; k >>= 1) { threadgroup_barrier(mem_flags::mem_threadgroup); if (l < k) s[l] += s[l + k]; }\n  if (l == 0) o[i] = s[0];')
kern('tg_atomic', 'device uint *o [[buffer(0)]], uint i [[thread_position_in_grid]], uint l [[thread_position_in_threadgroup]]', '  threadgroup atomic_uint c; if (l==0) atomic_store_explicit(&c,0u,memory_order_relaxed);\n  threadgroup_barrier(mem_flags::mem_threadgroup);\n  uint v = atomic_fetch_add_explicit(&c, 1u, memory_order_relaxed);\n  o[i] = v;')
kern('device_barrier', TG, '  o[i] = a[i]; threadgroup_barrier(mem_flags::mem_device); o[i+1] = a[i+2];')

# 5. textures
TX = 'texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float4 *o [[buffer(0)]], device const float2 *c [[buffer(1)]], uint i [[thread_position_in_grid]]'
kern('tex_sample', TX, '  o[i] = t.sample(s, c[i], level(0));')
kern('tex_sample2', TX, '  float4 x = t.sample(s, c[i], level(0)); float4 y = t.sample(s, c[i+1], level(1));\n  o[i] = x * y;')
kern('tex_sample_dep', TX, '  float4 x = t.sample(s, c[i], level(0));\n  o[i] = t.sample(s, x.xy, level(0));')
kern('tex_sample_alu', TX, '  float4 x = t.sample(s, c[i], level(0)); float2 d = c[i+1]*2.0f+1.0f; d = d*d; d=sqrt(d);\n  o[i] = x * d.x + d.y;')
kern('tex_read', 'texture2d<float> t [[texture(0)]], device float4 *o [[buffer(0)]], uint2 g [[thread_position_in_grid]]', '  o[g.x + g.y*64] = t.read(g) + t.read(g+1);')
kern('tex_write', 'texture2d<float, access::write> t [[texture(0)]], device const float4 *a [[buffer(0)]], uint2 g [[thread_position_in_grid]]', '  t.write(a[g.x] * 2.0f, g);')
kern('tex_gather', TX, '  o[i] = t.gather(s, c[i]);')

# 6. control flow
kern('cf_if', STD, '  float x = a[i];\n  if (x > u.x) x = sqrt(x) * u.y;\n  o[i] = x;')
kern('cf_ifelse', STD, '  float x = a[i]; float r;\n  if (x > u.x) r = sin(x); else r = x * x + u.z;\n  o[i] = r;')
kern('cf_loop', STD, '  float x = a[i]; float s = 0;\n  for (int k = 0; k < int(u.w); k++) s = s * x + 1.0f;\n  o[i] = s;')
kern('cf_loop_load', STD, '  float s = 0;\n  for (int k = 0; k < int(u.w); k++) s += a[i + k] * u.x;\n  o[i] = s;')
kern('cf_loop_nested', STD, '  float s = 0;\n  for (int k = 0; k < int(u.w); k++) for (int j = 0; j < int(u.z); j++) s = s * 1.5f + a[j];\n  o[i] = s;')
kern('cf_while_div', STDI, '  uint x = a[i]; uint n = 0;\n  while (x > 1) { x = (x & 1) ? 3*x+1 : x/2; n++; }\n  o[i] = n;')
kern('cf_early_ret', STD, '  float x = a[i];\n  if (x < 0) return;\n  o[i] = exp(x);')
kern('cf_switch', STDI, '  uint x = a[i]; uint r;\n  switch (x & 3) { case 0: r = x*3; break; case 1: r = x+u.x; break; case 2: r = x^u.y; break; default: r = 0; }\n  o[i] = r;')
kern('cf_uniform_if', STD, '  float x = a[i];\n  if (u.x > 0) x = x * x;\n  o[i] = x;')
kern('cf_loop_tex', 'texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], device float4 *o [[buffer(0)]], constant int &n [[buffer(1)]], uint i [[thread_position_in_grid]]', '  float4 a = 0;\n  for (int k = 0; k < n; k++) a += t.sample(s, float2(k, i) * 0.01f, level(0));\n  o[i] = a;')

# 7. simd / quad
kern('simd_sum', STD, '  o[i] = simd_sum(a[i]);')
kern('simd_shuffle', STD, '  o[i] = simd_shuffle(a[i], (ushort)(i ^ 3)) * u.x;')
kern('simd_ballot', STDI, '  o[i] = (uint)(uint64_t)simd_ballot(a[i] > u.x);')
kern('simd_prefix', STD, '  o[i] = simd_prefix_exclusive_sum(a[i]);')
kern('quad_shuffle', STD, '  o[i] = quad_shuffle_xor(a[i], 1u) + a[i];')
kern('simd_bcast_first', STD, '  o[i] = simd_broadcast_first(a[i]) * a[i];')

# 8. system values
kern('sv_many', 'device uint *o [[buffer(0)]], uint3 g [[thread_position_in_grid]], uint3 l [[thread_position_in_threadgroup]], uint3 tg [[threadgroup_position_in_grid]], uint sl [[thread_index_in_simdgroup]], uint3 gs [[threads_per_grid]]',
     '  o[g.x] = g.y * 3 + l.z + tg.x * gs.x + sl;')

# 9. mixed big kernels
kern('mix_blend', 'device float4 *o [[buffer(0)]], device const float4 *a [[buffer(1)]], device const float4 *b [[buffer(2)]], constant float4 &u [[buffer(3)]], uint i [[thread_position_in_grid]]',
     '  float4 x = a[i], y = b[i];\n  float4 r = mix(x, y, u.x);\n  r = pow(max(r, 0.0f), 2.2f);\n  float l = dot(r.xyz, float3(0.2126f, 0.7152f, 0.0722f));\n  o[i] = float4(r.xyz / (1.0f + l), normalize(x.xyz).x);')
kern('mix_matrix', 'device float4 *o [[buffer(0)]], device const float4 *a [[buffer(1)]], constant float4x4 &m [[buffer(2)]], uint i [[thread_position_in_grid]]', '  o[i] = m * a[i];')
kern('mix_matrix_chain', 'device float4 *o [[buffer(0)]], device const float4 *a [[buffer(1)]], constant float4x4 &m [[buffer(2)]], uint i [[thread_position_in_grid]]', '  o[i] = m * (m * (m * a[i]));')
kern('mix_noise', STD, '  float x = a[i]; float2 p = float2(x, x*1.7f);\n  float h = fract(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f);\n  o[i] = h * u.x + cos(h);')
kern('mix_half_math', STDH, '  half x = a[i]; half y = a[i+1];\n  half r = x*y + u.x; r = r*r - y; r = max(r, u.y); r = sqrt(r) + exp2(y);\n  o[i] = r;')
kern('mix_int_float', STDI, '  uint x = a[i];\n  float f = float(x) * 0.5f + float(u.x);\n  uint y = uint(sqrt(f)) + (x >> 3) * u.y;\n  o[i] = y ^ (y >> 7);')
kern('mix_64bit', 'device ulong *o [[buffer(0)]], device const ulong *a [[buffer(1)]], constant ulong &u [[buffer(2)]], uint i [[thread_position_in_grid]]', '  o[i] = a[i] * u + (a[i+1] >> 3);')
kern('mix_cvt', 'device half4 *o [[buffer(0)]], device const float4 *a [[buffer(1)]], device const int4 *b [[buffer(2)]], uint i [[thread_position_in_grid]]', '  o[i] = half4(a[i]) + half4(b[i]);')
kern('mix_pack', 'device uint *o [[buffer(0)]], device const float4 *a [[buffer(1)]], uint i [[thread_position_in_grid]]', '  o[i] = pack_float_to_unorm4x8(a[i]) ^ pack_float_to_snorm2x16(a[i].xy);')
kern('mix_unpack', 'device float4 *o [[buffer(0)]], device const uint *a [[buffer(1)]], uint i [[thread_position_in_grid]]', '  o[i] = unpack_unorm4x8_to_float(a[i]) + float4(unpack_snorm2x16_to_float(a[i+1]),0,1);')
kern('mix_select_chain', STD, '  float x = a[i];\n  float r = x > u.x ? x : u.y; r = r < u.z ? r*2.0f : r; r = clamp(r, 0.0f, 1.0f);\n  o[i] = r;')
kern('mix_long_alu', STD, '  float x = a[i]; float y = a[i+1]; float z = a[i+2];\n' + ''.join(f'  x = x*y+z; y = y*z+x; z = z*x+{k}.0f;\n' for k in range(12)) + '  o[i] = x+y+z;')
kern('mix_many_live', STD, '  float v[16]; for (int k=0;k<16;k++) v[k]=a[i+k];\n  float s=0; for (int k=0;k<16;k++) s = s*v[15-k] + v[k];\n  o[i] = s;')

src = HDR + '\n'.join(b for _, b in K)
open(os.path.join(out, 'kernels.metal'), 'w').write(src)
open(os.path.join(out, 'cases.txt'), 'w').write('\n'.join(n for n, _ in K) + '\n')
print(len(K), 'kernels')
