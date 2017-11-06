#include "ShadowDefine.hlsli"

// Per-pixel color data passed through the pixel shader.
struct PixelShaderInput
{
	float4 posH : SV_POSITION;
	float3 posV : POSITION0;
    float4 shadowPosH : POSITION1;
	float3 normalV : NORMAL;
	float2 uv : TEXTURE;
};

struct Material
{
	float3 diffuse;
	float alpha; // difuse alpha
	float3 specular;
	float specularPower;
	float3 ambient;
};

static const int kSphereNone = 0;
static const int kSphereMul = 1;
static const int kSphereAdd = 2;

cbuffer MaterialConstants : register(b0)
{
	Material material;
	int sphereOperation;
	int bUseTexture;
	int bUseToon;
};

cbuffer PassConstants : register(b1)
{
    float3 SunDirection; // V
    float3 SunColor;
    float4 ShadowTexelSize;
}

Texture2D<float4> texDiffuse : register(t1);
Texture2D<float3> texSphere : register(t2);
Texture2D<float3> texToon : register(t3);
Texture2D<float> texShadow : register(t4);
SamplerState sampler0 : register(s0);
SamplerState sampler1 : register(s1);
SamplerComparisonState samplerShadow : register(s2);

#define UsePlaneDepthBias_ 1
static const float Bias = 0.f;
// #include "Shadow.hlsli"

float CalcShadowFactor( float4 shadowPosH )
{
	// Complete projection by doing division by w.
	shadowPosH.xyz /= shadowPosH.w;

	// Depth in NDC space.
	float depth = shadowPosH.z;

    float2 texShadowSize;
    texShadow.GetDimensions(texShadowSize.x, texShadowSize.y);
    float2 ShadowTexelSize = 1.0 / texShadowSize;

	// Texel size.
	const float dx = ShadowTexelSize.x;

	float percentLit = 0.0f;
	const float2 offsets[9] =
	{
		float2(-dx,  -dx), float2(0.0f,  -dx), float2(dx,  -dx),
		float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
		float2(-dx,  +dx), float2(0.0f,  +dx), float2(dx,  +dx)
	};

	[unroll]
	for(int i = 0; i < 9; ++i)
	{
		percentLit += texShadow.SampleCmpLevelZero( samplerShadow, shadowPosH.xy + offsets[i], depth - 0.00).r;
	}

	return percentLit /= 9.0f;
}

float3 GetShadow( float4 ShadowPosH, float3 PosH )
{
    return CalcShadowFactor(ShadowPosH);

    float2 texShadowSize;
    texShadow.GetDimensions(texShadowSize.x, texShadowSize.y);
    float2 ShadowTexelSize = 1.0 / texShadowSize;

    // Complete projection by doing division by w.
    float3 ShadowPos = ShadowPosH.xyz / ShadowPosH.w;

    float2 TransTexCoord = ShadowPos.xy;
    if (!any( saturate( TransTexCoord ) != TransTexCoord ))
    {
        ShadowPos = saturate( ShadowPos );
        return texShadow.SampleCmpLevelZero( samplerShadow, ShadowPos.xy, ShadowPos.z );
    }
    return 1.0f;
}

// A pass-through function for the (interpolated) color data.
float4 main(PixelShaderInput input) : SV_TARGET
{
	float3 lightVecV = normalize( -SunDirection );
	float3 normalV = normalize( input.normalV );
	float intensity = dot( lightVecV, normalV ) * 0.5 + 0.5;
	//
	// Toon texture :
	//
	// For v, from 0.0 to 1.0 which means from bright to dark.
	// For u, can't find valid equation. Usually, it is ignored in various model.
	//
	// http://trackdancer.deviantart.com/art/MMD-PMD-Tutorial-Toon-Shaders-Primer-394445914
	//
	float2 toonCoord = float2(0.5, 1.0 - intensity);

	float3 toEyeV = -input.posV;
	float3 halfV = normalize( toEyeV + lightVecV );

	float NdotH = dot( normalV, halfV );
	float specularFactor = pow( max(NdotH, 0.001f), material.specularPower );

	float3 diffuse = material.diffuse * SunColor;
	float3 ambient = material.ambient;
	float3 specular = specularFactor * material.specular;

	float texAlpha = 1.0;
	float3 texColor = float3(1.0, 1.0, 1.0);
	if (bUseTexture)
	{
		float4 tex = texDiffuse.Sample( sampler0, input.uv );
		texColor = tex.xyz;
		texAlpha = tex.w;
	}

	float2 sphereCoord = 0.5 + 0.5*float2(1.0, -1.0) * normalV.xy;
	if (sphereOperation == kSphereAdd)
		texColor += texSphere.Sample( sampler0, sphereCoord );
	else if (sphereOperation == kSphereMul)
		texColor *= texSphere.Sample( sampler0, sphereCoord );

	float3 color = texColor * (ambient + diffuse) + specular;
    float3 shadow = GetShadow( input.shadowPosH, input.posH.xyz ) * 0.5 + 0.5;
    // return float4(shadow, 1);
#if 1
	if (bUseToon)
        color *= texToon.Sample( sampler0, toonCoord );
    color *= shadow;
#else
	if (bUseToon)
    {
        if (shadow < 1.00)
        {
            color *= texToon.Sample( sampler0, float2(0.0, 0.55) ) * shadow;
        }
        else
        {
            color *= texToon.Sample( sampler0, toonCoord ) * shadow;
        }
    }
    else
    {
        color *= shadow;
    }
#endif

	float alpha = texAlpha * material.alpha;
	return float4(color, alpha);
}
