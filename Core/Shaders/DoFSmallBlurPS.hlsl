// Chapter 28. Practical Post-Process Depth of Field

#include "DoFCommon.hlsli"

// Applies a 3 x 3 blur to the image in colorMapSampler,
// which is the same size as the render target.
// The sample weights are 1/16 in the corners, 2/16 on the edges,
// and 4/16 in the center.

// Examples 28-3. A Pixel Shader That Calculates the Actual Near CoC
Texture2D<float4> texColor : register(t0);

SamplerState samplerLinear : register(s0);

// This vertex and pixel shader applies a 3 x 3 blur to the image in
// colorMapSampler, which is the same size as the render target.
// The sample weights are 1/16 in the corners, 2/16 on the edges,
// and 4/16 in the center.
float4 main(
	float4 Pos : SV_POSITION,
	float2 Tex : TEXCOORD0) : SV_TARGET
{
    float4 halfPixel = { -0.5, 0.5, -0.5, 0.5 };
    float2 quarter = 4 * RcpBufferDim;
    float4 texCoords = Tex.xxyy + halfPixel * quarter.xxyy;

    float4 color = 0.0;
    color += texColor.Sample(samplerLinear, texCoords.xz);
    color += texColor.Sample(samplerLinear, texCoords.yz);
    color += texColor.Sample(samplerLinear, texCoords.xw);
    color += texColor.Sample(samplerLinear, texCoords.yw);
    return color / 4;
}