// Chapter 28. Practical Post-Process Depth of Field
// Example 28-4. This Pixel Shader Merges the Far CoC with the Near CoC and Applies It to the Screen

#include "DoFCommon.hlsli"

Texture2D<float3> texColor : register(t0); // Original source image
Texture2D<float4> texSmallBlur : register(t1); // Output of DofNearCoc
Texture2D<float3> texLargeBlur : register(t2); // Output of DofDownsample
Texture2D<float> texLinearDepth : register(t3);

SamplerState samplerLinear : register(s0);

float3 GetColorSampleOffset(float2 texCoords, float2 offset)
{
    return texColor.Sample(samplerLinear, texCoords + offset * RcpBufferDim);
}

float3 GetSmallBlurSample(float2 texCoords)
{
    const float weight = 4.0 / 17;
    float3 sum = 0.0; // Unblurred sample done by alpha blending
    sum += weight * GetColorSampleOffset(texCoords, float2(+0.5, -1.5));
    sum += weight * GetColorSampleOffset(texCoords, float2(-1.5, -0.5));
    sum += weight * GetColorSampleOffset(texCoords, float2(-0.5, +1.5));
    sum += weight * GetColorSampleOffset(texCoords, float2(+1.5, +0.5));
    return sum;
}

float4 InterpolateDof(float3 small, float3 median, float3 large, float t)
{
    float d0 = 0.1526;
    float d1 = 0.3263;
    float d2 = 0.5211;

    // Efficiently calculate the cross-blend weights for each sample.
    // Let the unblurred sample to small blur fade happen over distance
    // d0, the small to medium blur over distance d1, and the medium to
    // large blur over distance d2, where d0 + d1 + d2 = 1
    float4 dofLerpScale = float4(-1 / d0, -1 / d1, -1 / d2, 1/ d2);
    float4 dofLerpBias = float4(1.0, (1.0 - d2)/d1, 1 / d2, (d2 - 1) / d2);
    float4 weights = saturate(t * dofLerpScale + dofLerpBias);
    weights.yz = min(weights.yz, 1 - weights.xy);

    // Unblurred sample with weight "weights.x" done by alpha blending
    float3 color = weights.y * small + weights.z * median + weights.w * large;
    float alpha = dot(weights.yzw, float3(16.0 / 17, 1.0, 1.0));

    return float4(color, alpha);
}

float3 main(
	float4 Pos : SV_POSITION,
	float2 Tex : TEXCOORD0) : SV_TARGET
{
    float3 small = GetSmallBlurSample(Tex);
    float4 median = texSmallBlur.Sample(samplerLinear, Tex);
    float3 large = texLargeBlur.Sample(samplerLinear, Tex);

    float nearCoc = median.a;
    float depth = texLinearDepth.Sample(samplerLinear, Tex);
    float coc = 0;
    if (depth > ForegroundRange)
    {
        coc = nearCoc; // We don't want to blur the sky.
    }
    else
    {
        // dofEqFar.x and dofEqFar.y specify the linear ramp to convert
        // to depth for the distant out-of-focus region.
        // dofEqFar.z is the ratio of the far to the near blur radius.
        // float farCoc = saturate(dofEqFar.x * depth + dofEqFar.y);
        // coc = max(nearCoc, farCoc * dofEqFar.z);
        float farCoc = ComputeFarCoC(depth);
        coc = max(nearCoc, farCoc * FarCocMult);
    }

    // Interpolate between original full-resolution and
    // blurred images based on blurriness
    float3 dest = texColor.Sample(samplerLinear, Tex);
    float4 source = InterpolateDof(small, median.rgb, large, coc);

#ifdef SUPPORT_DEBUGGING
    if (DebugMode)
    {
        return coc.xxx;
    }
    else
#endif
    {
        // Blend premultiplied alpha
        return source.rgb + dest * (1 - source.a);
    }
}
