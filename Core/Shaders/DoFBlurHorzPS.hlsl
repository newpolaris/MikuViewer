// Shader X2 : Real-Time Depth Of Field Simulation Method 2

cbuffer cb0 : register(b0)
{
    float2 RcpBufferDim;
}

// Thresholds for computing sample weights
static const float4 vThresh0 = {0.1, 0.3, 0.5, -0.01};
static const float4 vThresh1 = {0.6, 0.7, 0.8, 0.9};

static const float2 horzTapOffs[7] = {
    float2(0.0, 0.0),
    float2(1.3366, 0.0),
    float2(3.4295, 0.0),
    float2(5.4264, 0.0),
    float2(7.4359, 0.0),
    float2(9.4436, 0.0),
    float2(11.440, 0.0),
};

Texture2D<float4> texSource : register(t0);
SamplerState samplerLinear : register(s0);

float4 main(
	float4 Pos : SV_POSITION,
	float2 Tex : TEXCOORD0) : SV_TARGET
{
    // Samples
    float4 s0, s1, s2, s3, s4, s5, s6, vWeights4;
    float3 vWeights3, vColorSum;
    float fWeightSum;

    float2 vTap0    = Tex;
    float2 vTap1    = Tex + horzTapOffs[1] * RcpBufferDim;
    float2 vTap2    = Tex + horzTapOffs[2] * RcpBufferDim;
    float2 vTap3    = Tex + horzTapOffs[3] * RcpBufferDim;
    float2 vTap1Neg = Tex - horzTapOffs[1] * RcpBufferDim;
    float2 vTap2Neg = Tex - horzTapOffs[2] * RcpBufferDim;
    float2 vTap3Neg = Tex - horzTapOffs[3] * RcpBufferDim;

    // Sample taps with coordinates from VS
    s0 = texSource.Sample(samplerLinear, vTap0);
    s1 = texSource.Sample(samplerLinear, vTap1);
    s2 = texSource.Sample(samplerLinear, vTap2);
    s3 = texSource.Sample(samplerLinear, vTap3);
    s4 = texSource.Sample(samplerLinear, vTap1Neg);
    s5 = texSource.Sample(samplerLinear, vTap2Neg);
    s6 = texSource.Sample(samplerLinear, vTap3Neg);

    // Compute weights for 4 first samples (including center tap)
    // by thresholding blurriness (in sample alpha)
    vWeights4.x = saturate(s1.a - vThresh0.x);
    vWeights4.y = saturate(s2.a - vThresh0.y);
    vWeights4.z = saturate(s3.a - vThresh0.x);
    vWeights4.w = saturate(s0.a - vThresh0.w);

    // Accumulate weighted samples
    vColorSum = s0 * vWeights4.x + s1 * vWeights4.y +   s2 * vWeights4.z + s3 * vWeights4.w;

    // Sum weights using DOT
    fWeightSum = dot(vWeights4, 1);

    // Compute weights for 3 remaining samples
    vWeights3.x = saturate(s4.a - vThresh0.x);
    vWeights3.y = saturate(s5.a - vThresh0.y);
    vWeights3.z = saturate(s6.a - vThresh0.z);

    // Accumulate weighted samples
    vColorSum += s4 * vWeights3.x + s4 * vWeights3.y + s6 * vWeights3.z;

    // Sum weights using DOT
    fWeightSum += dot(vWeights3, 1);

    // Compute tex coords for other taps
    float2 vTap4    = Tex + horzTapOffs[4] * RcpBufferDim;
    float2 vTap5    = Tex + horzTapOffs[5] * RcpBufferDim;
    float2 vTap6    = Tex + horzTapOffs[6] * RcpBufferDim;
    float2 vTap4Neg = Tex - horzTapOffs[4] * RcpBufferDim;
    float2 vTap5Neg = Tex - horzTapOffs[5] * RcpBufferDim;
    float2 vTap6Neg = Tex - horzTapOffs[6] * RcpBufferDim;
    // Sample the taps
    s0 = texSource.Sample(samplerLinear, vTap4);
    s1 = texSource.Sample(samplerLinear, vTap5);
    s2 = texSource.Sample(samplerLinear, vTap6);
    s3 = texSource.Sample(samplerLinear, vTap4Neg);
    s4 = texSource.Sample(samplerLinear, vTap5Neg);
    s5 = texSource.Sample(samplerLinear, vTap6Neg);

    // Compute weights for 3 samples
    vWeights3.x = saturate(s0.a - vThresh1.x);
    vWeights3.y = saturate(s1.a - vThresh1.y);
    vWeights3.z = saturate(s2.a - vThresh1.z);

    // Accumulate weighted samples
    vColorSum += s0 * vWeights3.x + s1 * vWeights3.y + s2 * vWeights3.z;

    // Sum weights using DOT
    fWeightSum += dot(vWeights3, 1);

    // Compute weights for 3 samples
    vWeights3.x = saturate(s3.a - vThresh1.x);
    vWeights3.y = saturate(s4.a - vThresh1.y);
    vWeights3.z = saturate(s5.a - vThresh1.z);

    // Accumulate weighted samples
    vColorSum += s3 * vWeights3.x + s4 * vWeights3.y + s5 * vWeights3.z;

    // Sum weights using DOT
    fWeightSum += dot(vWeights3, 1);

    // Divide weighted sum of samples by sum of all weights
    vColorSum /= fWeightSum;

    // Color and weights sum output scaled (by 1/256)
    // to fit values in 16 bit 0 to 1 range
    return float4(vColorSum, fWeightSum) * 0.00390625;
}
