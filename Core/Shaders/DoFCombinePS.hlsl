Texture2D<float3> texColor : register(t0);
Texture2D<float> texCoc : register(t1);
Texture2D<float4> texBlurred : register(t2);

SamplerState samplerLinear : register(s0);

float3 main(
	float4 Pos : SV_POSITION,
	float2 Tex : TEXCOORD0) : SV_TARGET
{
    float3 color = texColor.Sample(samplerLinear, Tex);
    float coc = texCoc.Sample(samplerLinear, Tex);
    float4 blur = texBlurred.Sample(samplerLinear, Tex);

    // Interpolate between original full-resolution and
    // blurred images based on blurriness
    return lerp(color, blur.rgb, coc);
}