#include "DoFCommon.hlsli"
#include "PixelPacking.hlsli"

#define SUPPORT_TYPED_UAV_LOADS 1

Texture2D<float> LNFullDepth : register(t0);
#if SUPPORT_TYPED_UAV_LOADS
    RWTexture2D<float3> DstColor : register(u0);
#else
    RWTexture2D<uint> DstColor : register(u0);
#endif

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 st = DTid.xy;

    float2 UV = (st + 0.5) * RcpBufferDim;
    float Depth = LNFullDepth[st];

    float3 DoFColor = ComputeCoC(Depth);
    if (DoFColor.x < 0)
        DoFColor = DoFColor * float3(-1.0, 0.0, 0.0);

#if SUPPORT_TYPED_UAV_LOADS
    DstColor[st] = DoFColor;
#else
    DstColor[st] = Pack_R11G11B10_FLOAT(DoFColor);
#endif
}
