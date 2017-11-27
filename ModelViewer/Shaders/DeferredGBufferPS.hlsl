#include "Lighting.hlsli"

// Per-pixel color data passed through the pixel shader.
struct PixelShaderInput
{
    float4 position : SV_Position;
    float3 worldPos : WorldPos;
    float2 texCoord : TexCoord0;
    float3 viewDir : TexCoord1;
    float3 shadowCoord : TexCoord2;
    float3 normal : Normal;
    float3 tangent : Tangent;
    float3 bitangent : Bitangent;
};

struct PixelShaderOutput
{
    float3 normal : SV_Target0;
    float gloss : SV_Target1;
    float3 diffuseAlbedo : SV_Target2;
    float specularMask : SV_Target3;
    float3 ambient : SV_Target4;
};

Texture2D<float3> texDiffuse		: register(t0);
Texture2D<float3> texSpecular		: register(t1);
//Texture2D<float4> texEmissive		: register(t2);
Texture2D<float3> texNormal			: register(t3);
//Texture2D<float4> texLightmap		: register(t4);
//Texture2D<float4> texReflection	: register(t5);

SamplerState sampler0 : register(s0);

[earlydepthstencil]
PixelShaderOutput main( PixelShaderInput input )
{
    PixelShaderOutput output;

    uint2 pixelPos = input.position.xy;
    float3 diffuseAlbedo = texDiffuse.Sample(sampler0, input.texCoord);
    float3 ambient = 0;
    {
        float ao = texSSAO[pixelPos];
        ambient += ApplyAmbientLight( diffuseAlbedo, ao, AmbientColor );
    }
    float specularMask = texSpecular.Sample(sampler0, input.texCoord).g;
    float gloss = 128.0;
    float3 normal;
    {
        normal = texNormal.Sample(sampler0, input.texCoord) * 2.0 - 1.0;
        AntiAliasSpecular(normal, gloss);
        float3x3 tbn = float3x3(normalize(input.tangent), normalize(input.bitangent), normalize(input.normal));
        normal = normalize(mul(normal, tbn));
    }
    output.normal = 0.5 * (normal + 1);
    output.gloss =  gloss / 128.0;
    output.diffuseAlbedo = diffuseAlbedo;
    output.specularMask = specularMask;
    output.ambient = ambient;
    return output;
}