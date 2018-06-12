// Chapter 28. Practical Post-Process Depth of Field

// Examples 28-2. A Pixel Shader That Calculates the Actual Near CoC
Texture2D<float4> texShrunk : register(t0);
Texture2D<float4> texBlurred : register(t1);

SamplerState samplerLinear : register(s0);

// This is the pixel shader function that calculates the actual
// value used for the near circle of confusion.
// "texCoords" are 0 at the bottom left pixel and 1 at the top right.
float4 main(
	float4 Pos : SV_POSITION,
	float2 Tex : TEXCOORD0) : SV_TARGET
{
    float3 color;
    float coc;
    float4 shrunk = texShrunk.Sample(samplerLinear, Tex);
    float4 blurred = texBlurred.Sample(samplerLinear, Tex);
    color = shrunk.rgb;
    coc = 2 * max(blurred.a, shrunk.a) - shrunk.a;
    return float4(color, coc);
}