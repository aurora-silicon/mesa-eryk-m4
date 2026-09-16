struct Varyings {
   float4 position : SV_Position;
   float2 uv : TEXCOORD0;
};

Varyings vs(uint id : SV_VertexID) {
   Varyings v;
   v.uv = float2((id << 1) & 2, id & 2);
   v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0.5, 1);
   return v;
}

float4 ps(Varyings v) : SV_Target {
   return float4(v.uv, 0.25, 1);
}

RWStructuredBuffer<uint> outputData : register(u0);
[numthreads(64, 1, 1)]
void cs(uint3 id : SV_DispatchThreadID) {
   outputData[id.x] = (id.x * 17u) ^ 0x12345678u;
}
