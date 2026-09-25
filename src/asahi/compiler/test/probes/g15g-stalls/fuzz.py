import random, sys
random.seed(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
N = int(sys.argv[2]) if len(sys.argv) > 2 else 150
out = ['#include <metal_stdlib>', 'using namespace metal;']
names = []
FUN = ['sqrt(fabs({a}))', 'sin({a})', 'exp2({a}*0.01f)', 'rsqrt(fabs({a})+1.0f)', '({a}*{b}+{c})', '({a}-{b})', 'max({a},{b})', 'fract({a})', '({a}>{b}?{a}:{c})', 'float(uint({a})^uint({b}))', 'log2(fabs({a})+1.0f)']
for k in range(N):
    body = ['  float v0 = a[i];', '  float v1 = u.x;']
    nv = 2
    depth = 0
    for s in range(random.randint(3, 14)):
        r = random.random()
        pick = lambda: f'v{random.randrange(nv)}'
        if r < 0.15:
            body.append(f'  float v{nv} = a[(i + {random.randint(1,99)}u + uint(fabs({pick()}))) & 1023u];')
        elif r < 0.22:
            body.append(f'  float v{nv} = t.sample(s, float2({pick()}, {pick()}) * 0.01f, level(0)).x;')
        elif r < 0.27:
            body.append(f'  float v{nv} = float(atomic_fetch_add_explicit(c + (i & 7u), uint({pick()}), memory_order_relaxed));')
        elif r < 0.32:
            body.append(f'  float v{nv} = tg[(l + {random.randint(1,63)}u) & 63u] + {pick()};')
        elif r < 0.37:
            body.append(f'  float v{nv} = simd_sum({pick()});')
        elif r < 0.47:
            body.append(f'  float v{nv} = {pick()}; if ({pick()} > {pick()}) {{ v{nv} = {random.choice(FUN).format(a=pick(), b=pick(), c=pick())}; }}')
        elif r < 0.55:
            body.append(f'  float v{nv} = {pick()}; for (int q = 0; q < int(u.y) && q < 16; q++) {{ v{nv} = v{nv} * {pick()} + a[(i + uint(q)) & 1023u]; }}')
        elif r < 0.6:
            body.append(f'  o[(i + {random.randint(1,50)}u) & 1023u] = {pick()}; float v{nv} = {pick()} * 2.0f;')
        else:
            body.append(f'  float v{nv} = ' + random.choice(FUN).format(a=pick(), b=pick(), c=pick()) + ';')
        nv += 1
    body.append('  o[i] = ' + ' + '.join(f'v{j}' for j in random.sample(range(nv), min(nv, 4))) + ';')
    name = f'fz{k}'
    names.append(name)
    out.append(f'kernel void {name}(device float *o [[buffer(0)]], device const float *a [[buffer(1)]], constant float4 &u [[buffer(2)]], device atomic_uint *c [[buffer(3)]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], uint i [[thread_position_in_grid]], uint l [[thread_position_in_threadgroup]])\n{{\n  threadgroup float tg[64]; tg[l & 63u] = a[i & 1023u]; threadgroup_barrier(mem_flags::mem_threadgroup);\n' + '\n'.join(body) + '\n}')
open('fuzz.metal', 'w').write('\n'.join(out) + '\n')
open('fuzz.txt', 'w').write('\n'.join(names) + '\n')
