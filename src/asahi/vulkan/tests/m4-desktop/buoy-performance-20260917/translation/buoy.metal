#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wmissing-braces"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

template<typename T, size_t Num>
struct spvUnsafeArray
{
    T elements[Num ? Num : 1];
    
    thread T& operator [] (size_t pos) thread
    {
        return elements[pos];
    }
    constexpr const thread T& operator [] (size_t pos) const thread
    {
        return elements[pos];
    }
    
    device T& operator [] (size_t pos) device
    {
        return elements[pos];
    }
    constexpr const device T& operator [] (size_t pos) const device
    {
        return elements[pos];
    }
    
    constexpr const constant T& operator [] (size_t pos) const constant
    {
        return elements[pos];
    }
    
    threadgroup T& operator [] (size_t pos) threadgroup
    {
        return elements[pos];
    }
    constexpr const threadgroup T& operator [] (size_t pos) const threadgroup
    {
        return elements[pos];
    }
};

struct BuoyUniforms
{
    packed_float3 iResolution;
    float iTime;
    float4 iMouse;
};

struct main0_out
{
    float4 shadertoy_out_color [[color(0)]];
};

static inline __attribute__((always_inline))
float2 NoisePrecise(thread const float3& x, texture2d<float> iChannel0, sampler iChannel0Smplr)
{
    float3 p = floor(x);
    float3 f = fract(x);
    f = (f * f) * (float3(3.0) - (f * 2.0));
    float2 uv = p.xy + (float2(37.0, 17.0) * p.z);
    float4 rg = mix(mix(iChannel0.sample(iChannel0Smplr, ((uv + float2(0.5)) / float2(256.0)), level(0.0)), iChannel0.sample(iChannel0Smplr, (((uv + float2(1.0, 0.0)) + float2(0.5)) / float2(256.0)), level(0.0)), float4(f.x)), mix(iChannel0.sample(iChannel0Smplr, (((uv + float2(0.0, 1.0)) + float2(0.5)) / float2(256.0)), level(0.0)), iChannel0.sample(iChannel0Smplr, ((uv + float2(1.5)) / float2(256.0)), level(0.0)), float4(f.x)), float4(f.y));
    return mix(rg.yw, rg.xz, float2(f.z));
}

static inline __attribute__((always_inline))
float WavesSmooth(thread float3& pos, constant BuoyUniforms& _188, texture2d<float> iChannel0, sampler iChannel0Smplr)
{
    pos *= float3(0.20000000298023223876953125);
    float f = 0.0;
    pos += (float3(0.0, 0.100000001490116119384765625, 0.100000001490116119384765625) * _188.iTime);
    for (int i = 0; i < 2; i++)
    {
        pos = (pos.yzx + (pos.zyx * float3(1.0, -1.0, 1.0))) / float3(1.41421353816986083984375);
        float3 param = pos;
        f = (f * 2.0) + (sqrt(powr(NoisePrecise(param, iChannel0, iChannel0Smplr).x - 0.5, 2.0) + 0.00999999977648258209228515625) * 2.0);
        pos *= 2.0;
    }
    f /= 4.0;
    return (0.5 - f) * 1.0;
}

static inline __attribute__((always_inline))
void ComputeBoatTransform(constant BuoyUniforms& _188, texture2d<float> iChannel0, sampler iChannel0Smplr, thread float3& boatPosition, thread float3& boatRight, thread float3& boatForward, thread float3& boatUp)
{
    spvUnsafeArray<float3, 5> samples;
    samples[0] = float3(0.0);
    samples[1] = float3(0.0, 0.0, 0.5);
    samples[2] = float3(0.0, 0.0, -0.5);
    samples[3] = float3(0.5, 0.0, 0.0);
    samples[4] = float3(-0.5, 0.0, 0.0);
    float3 param = samples[0];
    float _676 = WavesSmooth(param, _188, iChannel0, iChannel0Smplr);
    samples[0].y = _676;
    float3 param_1 = samples[1];
    float _681 = WavesSmooth(param_1, _188, iChannel0, iChannel0Smplr);
    samples[1].y = _681;
    float3 param_2 = samples[2];
    float _686 = WavesSmooth(param_2, _188, iChannel0, iChannel0Smplr);
    samples[2].y = _686;
    float3 param_3 = samples[3];
    float _691 = WavesSmooth(param_3, _188, iChannel0, iChannel0Smplr);
    samples[3].y = _691;
    float3 param_4 = samples[4];
    float _696 = WavesSmooth(param_4, _188, iChannel0, iChannel0Smplr);
    samples[4].y = _696;
    boatPosition = ((((samples[0] + samples[1]) + samples[2]) + samples[3]) + samples[4]) / float3(5.0);
    boatRight = samples[3] - samples[4];
    boatForward = samples[1] - samples[2];
    boatUp = fast::normalize(cross(boatForward, boatRight));
    boatRight = fast::normalize(cross(boatUp, boatForward));
    boatForward = fast::normalize(boatForward);
    boatPosition += (boatUp * 0.0);
}

static inline __attribute__((always_inline))
void CamPolar(thread float3& pos, thread float3& ray, thread const float3& origin, thread const float2& rotation, thread const float& _distance, thread const float& zoom, thread const float2& fragCoord, constant BuoyUniforms& _188, thread float3& localRay)
{
    float2 c = float2(cos(rotation.x), cos(rotation.y));
    float2 _173 = float2(sin(rotation.x), sin(rotation.y));
    float4 s;
    s.x = _173.x;
    s.y = _173.y;
    float4 _178 = s;
    float2 _180 = -_178.xy;
    s.z = _180.x;
    s.w = _180.y;
    float2 _196 = fragCoord - (float2(_188.iResolution[0], _188.iResolution[1]) * 0.5);
    ray.x = _196.x;
    ray.y = _196.y;
    ray.z = _188.iResolution[1u] * zoom;
    ray = fast::normalize(ray);
    localRay = ray;
    float3 _212 = ray;
    float3 _217 = ray;
    float2 _222 = (_212.yz * c.xx) + (_217.zy * s.zx);
    ray.y = _222.x;
    ray.z = _222.y;
    float3 _227 = ray;
    float3 _232 = ray;
    float2 _237 = (_227.xz * c.yy) + (_232.zx * s.yw);
    ray.x = _237.x;
    ray.z = _237.y;
    pos = origin - (float3(c.x * s.y, s.z, c.x * c.y) * _distance);
}

static inline __attribute__((always_inline))
float2 Noise(thread const float3& x, texture2d<float> iChannel0, sampler iChannel0Smplr)
{
    float3 p = floor(x);
    float3 f = fract(x);
    f = (f * f) * (float3(3.0) - (f * 2.0));
    float2 uv = p.xy + (float2(37.0, 17.0) * p.z);
    float4 rg = iChannel0.sample(iChannel0Smplr, (((uv + f.xy) + float2(0.5)) / float2(256.0)), level(0.0));
    return mix(rg.yw, rg.xz, float2(f.z));
}

static inline __attribute__((always_inline))
float Waves(thread float3& pos, constant BuoyUniforms& _188, texture2d<float> iChannel0, sampler iChannel0Smplr)
{
    pos *= float3(0.20000000298023223876953125);
    float f = 0.0;
    pos += (float3(0.0, 0.100000001490116119384765625, 0.100000001490116119384765625) * _188.iTime);
    for (int i = 0; i < 5; i++)
    {
        pos = (pos.yzx + (pos.zyx * float3(1.0, -1.0, 1.0))) / float3(1.41421353816986083984375);
        float3 param = pos;
        f = (f * 2.0) + (abs(Noise(param, iChannel0, iChannel0Smplr).x - 0.5) * 2.0);
        pos *= 2.0;
    }
    f /= 32.0;
    return (0.5 - f) * 1.0;
}

static inline __attribute__((always_inline))
float OceanDistanceField(thread const float3& pos, constant BuoyUniforms& _188, texture2d<float> iChannel0, sampler iChannel0Smplr)
{
    float3 param = pos;
    float _951 = Waves(param, _188, iChannel0, iChannel0Smplr);
    return pos.y - _951;
}

static inline __attribute__((always_inline))
float TraceOcean(thread const float3& pos, thread const float3& ray, constant BuoyUniforms& _188, texture2d<float> iChannel0, sampler iChannel0Smplr)
{
    float h = 1.0;
    float t = 0.0;
    for (int i = 0; i < 100; i++)
    {
        if ((h < 0.00999999977648258209228515625) || (t > 100.0))
        {
            break;
        }
        float3 param = pos + (ray * t);
        h = OceanDistanceField(param, _188, iChannel0, iChannel0Smplr);
        t += h;
    }
    if (h > 0.100000001490116119384765625)
    {
        return 0.0;
    }
    return t;
}

static inline __attribute__((always_inline))
float TraceBoat(thread const float3& pos, thread const float3& ray, thread float3& boatPosition)
{
    float3 c = boatPosition;
    float r = 1.0;
    c -= pos;
    float t = dot(c, ray);
    float p = length(c - (ray * t));
    if (p > r)
    {
        return 0.0;
    }
    return t - sqrt((r * r) - (p * p));
}

static inline __attribute__((always_inline))
float WavesDetail(thread float3& pos, constant BuoyUniforms& _188, texture2d<float> iChannel0, sampler iChannel0Smplr)
{
    pos *= float3(0.20000000298023223876953125);
    float f = 0.0;
    pos += (float3(0.0, 0.100000001490116119384765625, 0.100000001490116119384765625) * _188.iTime);
    for (int i = 0; i < 8; i++)
    {
        pos = (pos.yzx + (pos.zyx * float3(1.0, -1.0, 1.0))) / float3(1.41421353816986083984375);
        float3 param = pos;
        f = (f * 2.0) + (abs(NoisePrecise(param, iChannel0, iChannel0Smplr).x - 0.5) * 2.0);
        pos *= 2.0;
    }
    f /= 256.0;
    return (0.5 - f) * 1.0;
}

static inline __attribute__((always_inline))
float OceanDistanceFieldDetail(thread const float3& pos, constant BuoyUniforms& _188, texture2d<float> iChannel0, sampler iChannel0Smplr)
{
    float3 param = pos;
    float _959 = WavesDetail(param, _188, iChannel0, iChannel0Smplr);
    return pos.y - _959;
}

static inline __attribute__((always_inline))
float3 OceanNormal(thread const float3& pos, constant BuoyUniforms& _188, texture2d<float> iChannel0, sampler iChannel0Smplr)
{
    float2 d = float2(0.00999999977648258209228515625 * length(pos), 0.0);
    float3 param = pos + d.xyy;
    float3 param_1 = pos - d.xyy;
    float3 norm;
    norm.x = OceanDistanceFieldDetail(param, _188, iChannel0, iChannel0Smplr) - OceanDistanceFieldDetail(param_1, _188, iChannel0, iChannel0Smplr);
    float3 param_2 = pos + d.yxy;
    float3 param_3 = pos - d.yxy;
    norm.y = OceanDistanceFieldDetail(param_2, _188, iChannel0, iChannel0Smplr) - OceanDistanceFieldDetail(param_3, _188, iChannel0, iChannel0Smplr);
    float3 param_4 = pos + d.yyx;
    float3 param_5 = pos - d.yyx;
    norm.z = OceanDistanceFieldDetail(param_4, _188, iChannel0, iChannel0Smplr) - OceanDistanceFieldDetail(param_5, _188, iChannel0, iChannel0Smplr);
    return fast::normalize(norm);
}

static inline __attribute__((always_inline))
float3 Sky(thread const float3& ray)
{
    return float3(0.4000000059604644775390625, 0.449999988079071044921875, 0.5);
}

static inline __attribute__((always_inline))
float3 WorldToBoat(thread const float3& dir, thread float3& boatRight, thread float3& boatForward, thread float3& boatUp)
{
    return float3(dot(dir, boatRight), dot(dir, boatUp), dot(dir, boatForward));
}

static inline __attribute__((always_inline))
float3 ShadeBoat(thread float3& pos, thread const float3& ray, constant BuoyUniforms& _188, thread float3& boatPosition, thread float3& boatRight, thread float3& boatForward, thread float3& boatUp)
{
    pos -= boatPosition;
    float3 norm = fast::normalize(pos);
    float3 param = pos;
    pos = WorldToBoat(param, boatRight, boatForward, boatUp);
    float3 lightDir = float3(-0.534522473812103271484375, 0.801783740520477294921875, 0.2672612369060516357421875);
    float ndotl = dot(norm, lightDir);
    float3 light = (float3(1.0, 0.89999997615814208984375, 0.800000011920928955078125) * smoothstep(-0.100000001490116119384765625, 1.0, ndotl)) + float3(0.0599999986588954925537109375, 0.100000001490116119384765625, 0.100000001490116119384765625);
    float aa = 4.0 / _188.iResolution[0u];
    float3 albedo = float3(1.0, 0.00999999977648258209228515625, 0.0);
    albedo = mix(float3(0.039999999105930328369140625), albedo, float3(smoothstep(0.25 - aa, 0.25, abs(pos.y))));
    albedo = mix(mix(float3(1.0), float3(0.039999999105930328369140625), float3(smoothstep((-aa) * 4.0, aa * 4.0, cos(precise::atan2(pos.x, pos.z) * 6.0)))), albedo, float3(smoothstep(0.20000000298023223876953125 - (aa * 1.5), 0.20000000298023223876953125, abs(pos.y))));
    albedo = mix(float3(0.039999999105930328369140625), albedo, float3(smoothstep(0.0500000007450580596923828125 - (aa * 1.0), 0.0500000007450580596923828125, abs(abs(pos.y) - 0.60000002384185791015625))));
    albedo = mix(float3(1.0, 0.800000011920928955078125, 0.07999999821186065673828125), albedo, float3(smoothstep(0.0500000007450580596923828125 - (aa * 1.0), 0.0500000007450580596923828125, abs(abs(pos.y) - 0.64999997615814208984375))));
    float3 col = albedo * light;
    float3 h = fast::normalize(lightDir - ray);
    float s = (powr(fast::max(0.0, dot(norm, h)), 100.0) * 100.0) / 32.0;
    float3 specular = float3(1.0) * s;
    float3 rr = reflect(ray, norm);
    float3 param_1 = rr;
    specular += mix(float3(0.0, 0.039999999105930328369140625, 0.039999999105930328369140625), Sky(param_1), float3(smoothstep(-0.100000001490116119384765625, 0.100000001490116119384765625, rr.y)));
    float ndotr = dot(norm, ray);
    float fresnel = powr(1.0 - abs(ndotr), 5.0);
    fresnel = mix(0.001000000047497451305389404296875, 1.0, fresnel);
    col = mix(col, specular, float3(fresnel));
    return col;
}

static inline __attribute__((always_inline))
float4 Noise(thread const int2& x, texture2d<float> iChannel0, sampler iChannel0Smplr)
{
    return iChannel0.sample(iChannel0Smplr, ((float2(x) + float2(0.5)) / float2(256.0)), level(0.0));
}

static inline __attribute__((always_inline))
float WaveCrests(thread const float3& ipos, thread const float2& fragCoord, constant BuoyUniforms& _188, texture2d<float> iChannel0, sampler iChannel0Smplr)
{
    float3 pos = ipos;
    pos *= float3(0.20000000298023223876953125);
    float f = 0.0;
    pos += (float3(0.0, 0.100000001490116119384765625, 0.100000001490116119384765625) * _188.iTime);
    float3 pos2 = pos;
    for (int i = 0; i < 6; i++)
    {
        pos = (pos.yzx + (pos.zyx * float3(1.0, -1.0, 1.0))) / float3(1.41421353816986083984375);
        float3 param = pos;
        f = (f * 1.5) + (abs(Noise(param, iChannel0, iChannel0Smplr).x - 0.5) * 2.0);
        pos *= 2.0;
    }
    pos = pos2 * 64.0;
    pos.y = (-0.0500000007450580596923828125) * _188.iTime;
    for (int i_1 = 6; i_1 < 16; i_1++)
    {
        pos = (pos.yzx + (pos.zyx * float3(1.0, -1.0, 1.0))) / float3(1.41421353816986083984375);
        float3 param_1 = pos;
        f = (f * 1.5) + powr(abs(Noise(param_1, iChannel0, iChannel0Smplr).x - 0.5) * 2.0, 1.0);
        pos *= 2.0;
    }
    f /= 1500.0;
    int2 param_2 = int2(fragCoord);
    f -= (Noise(param_2, iChannel0, iChannel0Smplr).x * 0.00999999977648258209228515625);
    return powr(smoothstep(0.4000000059604644775390625, -0.100000001490116119384765625, f), 6.0);
}

static inline __attribute__((always_inline))
float3 ShadeOcean(thread const float3& pos, thread const float3& ray, thread const float2& fragCoord, constant BuoyUniforms& _188, texture2d<float> iChannel0, sampler iChannel0Smplr, thread float3& boatPosition, thread float3& boatRight, thread float3& boatForward, thread float3& boatUp)
{
    float3 param = pos;
    float3 norm = OceanNormal(param, _188, iChannel0, iChannel0Smplr);
    float ndotr = dot(ray, norm);
    float fresnel = powr(1.0 - abs(ndotr), 5.0);
    float3 reflectedRay = ray - ((norm * 2.0) * ndotr);
    float3 refractedRay = ray + (norm * ((-cos(1.33000004291534423828125 * acos(-ndotr))) - ndotr));
    refractedRay = fast::normalize(refractedRay);
    float3 param_1 = reflectedRay;
    float3 reflection = Sky(param_1);
    float3 param_2 = pos - (reflectedRay * 0.0);
    float3 param_3 = reflectedRay;
    float t = TraceBoat(param_2, param_3, boatPosition);
    if (t > 0.0)
    {
        float3 param_4 = pos + (reflectedRay * (t - 0.0));
        float3 param_5 = reflectedRay;
        float3 _1116 = ShadeBoat(param_4, param_5, _188, boatPosition, boatRight, boatForward, boatUp);
        reflection = _1116;
    }
    float3 param_6 = pos - (refractedRay * 0.0);
    float3 param_7 = refractedRay;
    t = TraceBoat(param_6, param_7, boatPosition);
    float3 col = float3(0.0, 0.039999999105930328369140625, 0.039999999105930328369140625);
    if (t > 0.0)
    {
        float3 param_8 = pos + (refractedRay * (t - 0.0));
        float3 param_9 = refractedRay;
        float3 _1140 = ShadeBoat(param_8, param_9, _188, boatPosition, boatRight, boatForward, boatUp);
        col = mix(col, _1140, float3(exp(-t)));
    }
    col = mix(col, reflection, float3(fresnel));
    float3 param_10 = pos;
    float2 param_11 = fragCoord;
    col = mix(col, float3(1.0), float3(WaveCrests(param_10, param_11, _188, iChannel0, iChannel0Smplr)));
    return col;
}

static inline __attribute__((always_inline))
float3 ToGamma(thread const float3& col)
{
    return powr(col, float3(0.4545454680919647216796875));
}

static inline __attribute__((always_inline))
void mainImage(thread float4& fragColor, thread const float2& fragCoord, constant BuoyUniforms& _188, thread float3& localRay, texture2d<float> iChannel0, sampler iChannel0Smplr, thread float3& boatPosition, thread float3& boatRight, thread float3& boatForward, thread float3& boatUp)
{
    ComputeBoatTransform(_188, iChannel0, iChannel0Smplr, boatPosition, boatRight, boatForward, boatUp);
    float2 camRot = float2(0.5) + (float2(-0.3499999940395355224609375, 4.5) * (_188.iMouse.yx / float2(_188.iResolution[1], _188.iResolution[0])));
    float3 param_2 = float3(0.0);
    float2 param_3 = camRot;
    float param_4 = 3.0;
    float param_5 = 1.0;
    float2 param_6 = fragCoord;
    float3 param;
    float3 param_1;
    CamPolar(param, param_1, param_2, param_3, param_4, param_5, param_6, _188, localRay);
    float3 pos = param;
    float3 ray = param_1;
    float3 param_7 = pos;
    float3 param_8 = ray;
    float to = TraceOcean(param_7, param_8, _188, iChannel0, iChannel0Smplr);
    float3 param_9 = pos;
    float3 param_10 = ray;
    float tb = TraceBoat(param_9, param_10, boatPosition);
    bool _1205 = to > 0.0;
    bool _1214;
    if (_1205)
    {
        _1214 = (to < tb) || (tb == 0.0);
    }
    else
    {
        _1214 = _1205;
    }
    float3 result;
    if (_1214)
    {
        float3 param_11 = pos + (ray * to);
        float3 param_12 = ray;
        float2 param_13 = fragCoord;
        result = ShadeOcean(param_11, param_12, param_13, _188, iChannel0, iChannel0Smplr, boatPosition, boatRight, boatForward, boatUp);
    }
    else
    {
        if (tb > 0.0)
        {
            float3 param_14 = pos + (ray * tb);
            float3 param_15 = ray;
            float3 _1242 = ShadeBoat(param_14, param_15, _188, boatPosition, boatRight, boatForward, boatUp);
            result = _1242;
        }
        else
        {
            float3 param_16 = ray;
            result = Sky(param_16);
        }
    }
    result *= (1.10000002384185791015625 * smoothstep(0.3499999940395355224609375, 1.0, localRay.z));
    float3 param_17 = result;
    fragColor = float4(ToGamma(param_17), 1.0);
}

fragment main0_out main0(constant BuoyUniforms& _188 [[buffer(0)]], texture2d<float> iChannel0 [[texture(0)]], sampler iChannel0Smplr [[sampler(0)]], float4 gl_FragCoord [[position]])
{
    main0_out out = {};
    out.shadertoy_out_color = float4(1.0);
    float4 color = float4(100000002004087734272.0);
    float2 param_1 = gl_FragCoord.xy;
    float4 param;
    float3 localRay;
    float3 boatPosition;
    float3 boatRight;
    float3 boatForward;
    float3 boatUp;
    mainImage(param, param_1, _188, localRay, iChannel0, iChannel0Smplr, boatPosition, boatRight, boatForward, boatUp);
    color = param;
    if (out.shadertoy_out_color.x < 0.0)
    {
        color = float4(1.0, 0.0, 0.0, 1.0);
    }
    if (out.shadertoy_out_color.y < 0.0)
    {
        color = float4(0.0, 1.0, 0.0, 1.0);
    }
    if (out.shadertoy_out_color.z < 0.0)
    {
        color = float4(0.0, 0.0, 1.0, 1.0);
    }
    if (out.shadertoy_out_color.w < 0.0)
    {
        color = float4(1.0, 1.0, 0.0, 1.0);
    }
    out.shadertoy_out_color = float4(color.xyz, 1.0);
    return out;
}


struct BuoyVertex { float4 position [[position]]; };
vertex BuoyVertex vs(uint id [[vertex_id]]) { float2 p=float2((id<<1)&2,id&2); return {float4(p*2.0-1.0,0,1)}; }
