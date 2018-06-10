Texture2D<float> CocBuffer : register(t0);
Texture2D<float3> SrcColor : register(t1);

SamplerState samplerLinear : register(s0);

float4 main(
	float4 Pos : SV_POSITION,
	float2 Tex : TEXCOORD0) : SV_TARGET
{
    float coc = CocBuffer.Sample(samplerLinear, Tex);
    float3 color = SrcColor.Sample(samplerLinear, Tex);
    return float4(color, coc);
}