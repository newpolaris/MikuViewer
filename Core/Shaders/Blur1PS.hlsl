#include "DoFBlur.hlsli"

float4 main(PixelShaderInput input) : SV_Target
{
    float2 texSize;
    image.GetDimensions(texSize.x, texSize.y);

	float4 blurColor = 0;
	blurColor = image.Sample(samLinearClamp, input.texcoord) * Weight[0];
	for (int i = 1; i < BlurRadius; i++) {
		blurColor += Weight[i] * image.Sample(samLinearClamp, input.texcoord + float2(Offset[i]/texSize.x, 0.f));
		blurColor += Weight[i] * image.Sample(samLinearClamp, input.texcoord - float2(Offset[i]/texSize.x, 0.f));
	}
	return blurColor;
}
