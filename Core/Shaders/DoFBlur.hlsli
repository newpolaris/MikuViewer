static const int BlurRadius = 3;
static const float Offset[3] = {0.0, 1.384615, 3.2307692308};
static const float Weight[3] = {0.227027, 0.3162162162, 0.0702702703};

Texture2D<float4> image : register(t0);
SamplerState samLinearClamp : register(s0);

struct PixelShaderInput
{
    float4 svPosition : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};