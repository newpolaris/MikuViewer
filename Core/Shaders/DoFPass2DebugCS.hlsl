#include "DoFCommon.hlsli"
#include "PixelPacking.hlsli"

Texture2D<float> LNFullDepth : register(t0);
RWTexture2D<float3> DstColor : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 st = DTid.xy;

    float2 UV = (st + 0.5) * RcpBufferDim;
    float Depth = LNFullDepth[st];

    float3 DoFColor = ComputeCoC(Depth);
    if (DoFColor.x < 0)
        DoFColor = DoFColor * float3(-1.0, 0.0, 0.0);
    DstColor[st] = DoFColor;
}
