// Shader X2 : Real-Time Depth Of Field Simulation Method 2

cbuffer cb0 : register(b0)

{
    float2 RcpBufferDim;
}

static const float4 vWeights0 = {0.080, 0.075, 0.070, 0.100};
static const float4 vWeights1 = {0.065, 0.060, 0.055, 0.050};

static const float2 vertTapOffs[7] = {
    float2(0.0, 0.0),
    float2(0.0, 1.3366),
    float2(0.0, 3.4295),
    float2(0.0, 5.4264),
    float2(0.0, 7.4359),
    float2(0.0, 9.4436),
    float2(0.0, 11.440),
};

Texture2D<float4> texSource : register(t0);
SamplerState samplerLinear : register(s0);

float4 main(
	float4 Pos : SV_POSITION,
	float2 Tex : TEXCOORD0) : SV_TARGET
{
    float4 s0, s1, s2, s3, s4, s5, s6;

    // Acumulated color and weights
    float4 vColorWeightSum;

    float2 vTap0 = Tex;
    float2 vTap1 = Tex + vertTapOffs[1] * RcpBufferDim;
    float2 vTap2 = Tex + vertTapOffs[2] * RcpBufferDim;
    float2 vTap3 = Tex + vertTapOffs[3] * RcpBufferDim;
    float2 vTap1Neg = Tex - vertTapOffs[1] * RcpBufferDim;
    float2 vTap2Neg = Tex - vertTapOffs[2] * RcpBufferDim;
    float2 vTap3Neg = Tex - vertTapOffs[3] * RcpBufferDim;

    // Sample taps with coordinates from VS
    s0 = texSource.Sample(samplerLinear, vTap0);
    s1 = texSource.Sample(samplerLinear, vTap1);
    s2 = texSource.Sample(samplerLinear, vTap2);
    s3 = texSource.Sample(samplerLinear, vTap3);
    s4 = texSource.Sample(samplerLinear, vTap1Neg);
    s5 = texSource.Sample(samplerLinear, vTap2Neg);
    s6 = texSource.Sample(samplerLinear, vTap3Neg);

    // Modulate sampled color values by the weights stored
    // in the alpha channel of each sample
    s0.rgb = s0.rgb * s0.a;
    s1.rgb = s1.rgb * s1.a;
    s2.rgb = s2.rgb * s2.a;
    s3.rgb = s3.rgb * s3.a;
    s4.rgb = s4.rgb * s4.a;
    s5.rgb = s5.rgb * s5.a;
    s6.rgb = s6.rgb * s6.a;

    // Aggregate all samples weighting them with pre-defined
    // kernel weights, weight sum in alpha
    vColorWeightSum = s0 * vWeights0.w + (s1 + s4) * vWeights0.x + (s2 + s5) * vWeights0.y + (s3 + s6) * vWeights0.z;

    // Compute tex coords for other taps
    float2 vTap4 = Tex + vertTapOffs[4] * RcpBufferDim;
    float2 vTap5 = Tex + vertTapOffs[5] * RcpBufferDim;
    float2 vTap6 = Tex + vertTapOffs[6] * RcpBufferDim;
    float2 vTap4Neg = Tex - vertTapOffs[4] * RcpBufferDim;
    float2 vTap5Neg = Tex - vertTapOffs[5] * RcpBufferDim;
    float2 vTap6Neg = Tex - vertTapOffs[6] * RcpBufferDim;

    // Sample the taps
    s0 = texSource.Sample(samplerLinear, vTap4);
    s1 = texSource.Sample(samplerLinear, vTap5);
    s2 = texSource.Sample(samplerLinear, vTap6);
    s3 = texSource.Sample(samplerLinear, vTap4Neg);
    s4 = texSource.Sample(samplerLinear, vTap5Neg);
    s5 = texSource.Sample(samplerLinear, vTap6Neg);

    // Modulate sampled color values by the weights stored
    // in the alpha channel of each sample
    s0.rgb = s0.rgb * s0.a;
    s1.rgb = s1.rgb * s1.a;
    s2.rgb = s2.rgb * s2.a;
    s3.rgb = s3.rgb * s3.a;
    s4.rgb = s4.rgb * s4.a;
    s5.rgb = s5.rgb * s5.a;

    // Aggregate all samples weighting them with pre-defined
    // kernel weights, weight sum in alpha
    vColorWeightSum += (s1 + s3) * vWeights1.x + (s1 + s4) * vWeights1.y + (s2 + s5) * vWeights1.z;

    // Average combined sample for all samples in the kernel
    vColorWeightSum.rgb /= vColorWeightSum.a;

    // Account for scale factor applied in previous pass
    // (blur along the X axis) to output values
    // in 16 bit 0 to 1 range
    return vColorWeightSum * 256.0;
}