#include "DoFCommon.hlsli"

Texture2D<float> LNFullDepth : register(t0);
RWTexture2D<float> CocBuffer : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 st = DTid.xy;
    float Depth = LNFullDepth[st];
    CocBuffer[st] = ComputeCoC(Depth);
}
