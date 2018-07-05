// Chapter 28. Practical Post-Process Depth of Field
// Example 28-1: A Shader That Downsamples the Scene and Initialize the Near CoC

// Render tharget size is one-quarter the scene rendering size

#include "DoFCommon.hlsli"

Texture2D<float> LNFullDepth : register(t0);
Texture2D<float3> SrcColor : register(t1);

SamplerState samplerLinear : register(s0);

static const float2 bilinearOffset[4] = {
    RcpBufferDim * float2(-1.0, -1.0),
    RcpBufferDim * float2(+1.0, -1.0),
    RcpBufferDim * float2(-1.0, +1.0),
    RcpBufferDim * float2(+1.0, +1.0)
};

static const float2 depthOffset[4] = {
    RcpBufferDim * float2(-1.5, -1.5),
    RcpBufferDim * float2(-0.5, -1.5),
    RcpBufferDim * float2(+0.5, -1.5),
    RcpBufferDim * float2(+1.5, -1.5)
};

static const float2 rowOffset[4] = {
    float2(0.0, RcpBufferDim.y) * 0,
    float2(0.0, RcpBufferDim.y) * 1,
    float2(0.0, RcpBufferDim.y) * 2,
    float2(0.0, RcpBufferDim.y) * 3,
};

float4 GetCocDepthOffset(float2 texCoord)
{
    float4 curcoc = 0.0;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float depth = LNFullDepth.Sample(samplerLinear, texCoord + rowOffset[i]);
        curcoc[i] = ComputeNearCoC(depth);
    }
    return curcoc;
}

float4 main(
	float4 Pos : SV_POSITION,
	float2 Tex : TEXCOORD0) : SV_TARGET
{
    // Use bilinear filtering to average 4 color samples
    float3 color = 0;
    [unroll]
    for (int i = 0; i < 4; i++)
        color += SrcColor.Sample(samplerLinear, Tex + bilinearOffset[i]);
    color /= 4.0;

    float4 coc = 0.0;
    for (int j = 0; j < 4; j++)
        coc = max(coc, GetCocDepthOffset(Tex + depthOffset[j]));

    return float4(color, Max4(coc));
}
