// single-iteration loop
#pragma warning (disable: 3557)

Texture2D<float3> Velocity : register(t0);
Texture2D<float3> SrcTexture : register(t1);
RWTexture2D<float3> DestTexture : register(u0);

SamplerState LinearSampler : register(s0);

cbuffer ScreenParam : register(b0)
{
    float2 ScreenDimensions;
}

static const uint NumSampling = 10;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID)
{
    float2 velocity = Velocity[DTid.xy].xy;
    float2 texCoord = DTid.xy / ScreenDimensions;
    float3 color = SrcTexture[DTid.xy];
    for (uint i = 1; i < NumSampling; i++)
    {
        float2 lookUp = texCoord + velocity*(float(i)/float(NumSampling - 1) - 0.5);
        color += SrcTexture.SampleLevel(LinearSampler, lookUp, 0);
    }
    DestTexture[DTid.xy] = color / NumSampling;
}
