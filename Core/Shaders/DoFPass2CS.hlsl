#define PoissonKernel_ 4

#include "DoFCommon.hlsli"
#include "PCFKernels.hlsli"

Texture2D<float> LNFullDepth : register(t0);
Texture2D<float> CocBuffer : register(t1);
Texture2D<float3> SrcColor : register(t2);
RWTexture2D<float3> DstColor : register(u0);

SamplerState LinearSampler : register(s0);

// ShaderX2_Real-TimeDepthOfFieldSimulation Method 1
// Bluring with Possion sampling that scaling with CoC Size

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float kMaxCocSize = 12.0;

    uint2 st = DTid.xy;
    float Depth = LNFullDepth[st];

    float3 ColorSum = SrcColor[st];
    float TotalContribution = 1.0;

    float CenterDepth = LNFullDepth[st];
    float SizeCoc = CocBuffer[st] * kMaxCocSize;

    for (int i = 0; i < kPoissonSample; i++)
    {
        float2 tapCoord = (st + PoissonDisk[i] * SizeCoc) * RcpBufferDim;
        float3 tapColor = SrcColor.SampleLevel(LinearSampler, tapCoord, 0);

        ColorSum += tapColor;
        TotalContribution += 1.0;
    }
    ColorSum /= TotalContribution;

    DstColor[st] = ColorSum;
}