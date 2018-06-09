#define PoissonKernel_ 3

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
    uint2 st = DTid.xy;
    float depth = LNFullDepth[st];

    float3 colorSum = SrcColor[st];
    float totalContribution = 1.0;
    float SizeCoc = CocBuffer[st];

    for (int i = 0; i < kPoissonSample; i++)
    {
        float2 tapSt = st + PoissonDisk[i] * SizeCoc * 0.5;
        float2 tapCoord = tapSt * RcpBufferDim;
        float3 tapColor = SrcColor.SampleLevel(LinearSampler, tapCoord, 0);
        float tapDepth = LNFullDepth[tapSt];
        float tapCocWeight = CocBuffer[tapSt] / MAX_COC_RADIUS;

        // Compute tap contribution based on depth and blurriness
        float tapContribution = (tapDepth > depth) ? 1.0 : tapCocWeight;

        colorSum += tapColor * tapContribution;
        totalContribution += tapContribution;
    }
    colorSum /= totalContribution;

    DstColor[st] = colorSum;
}