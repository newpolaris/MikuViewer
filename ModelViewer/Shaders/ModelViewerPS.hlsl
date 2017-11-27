//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):	James Stanard
//				Alex Nankervis
//
// Thanks to Michal Drobot for his feedback.

#include "ModelViewerRS.hlsli"
#include "Lighting.hlsli"

// outdated warning about for-loop variable scope
#pragma warning (disable: 3078)
// single-iteration loop
#pragma warning (disable: 3557)

struct VSOutput
{
    sample float4 position : SV_Position;
    sample float3 worldPos : WorldPos;
    sample float2 uv : TexCoord0;
    sample float3 viewDir : TexCoord1;
    sample float3 shadowCoord : TexCoord2;
    sample float3 normal : Normal;
    sample float3 tangent : Tangent;
    sample float3 bitangent : Bitangent;
};

Texture2D<float3> texDiffuse		: register(t0);
Texture2D<float3> texSpecular		: register(t1);
//Texture2D<float4> texEmissive		: register(t2);
Texture2D<float3> texNormal			: register(t3);
//Texture2D<float4> texLightmap		: register(t4);
//Texture2D<float4> texReflection	: register(t5);

ByteAddressBuffer lightGrid : register(t68);
ByteAddressBuffer lightGridBitMask : register(t69);

SamplerState sampler0 : register(s0);

[RootSignature(ModelViewer_RootSig)]
float3 main(VSOutput vsOutput) : SV_Target0
{
    uint2 pixelPos = vsOutput.position.xy;
    float3 diffuseAlbedo = texDiffuse.Sample(sampler0, vsOutput.uv);
    float3 colorSum = 0;
    {
        float ao = texSSAO[pixelPos];
        colorSum += ApplyAmbientLight( diffuseAlbedo, ao, AmbientColor );
    }

    float gloss = 128.0;
    float3 normal;
    {
        normal = texNormal.Sample(sampler0, vsOutput.uv) * 2.0 - 1.0;
        AntiAliasSpecular(normal, gloss);
        float3x3 tbn = float3x3(normalize(vsOutput.tangent), normalize(vsOutput.bitangent), normalize(vsOutput.normal));
        normal = normalize(mul(normal, tbn));
    }

    float3 specularAlbedo = float3( 0.56, 0.56, 0.56 );
    float specularMask = texSpecular.Sample(sampler0, vsOutput.uv).g;
    float3 viewDir = normalize(vsOutput.viewDir);
    colorSum += ApplyDirectionalLight( diffuseAlbedo, specularAlbedo, specularMask, gloss, normal, viewDir, SunDirection, SunColor, vsOutput.shadowCoord );

#define POINT_LIGHT_ARGS \
    diffuseAlbedo, \
    specularAlbedo, \
    specularMask, \
    gloss, \
    normal, \
    viewDir, \
    vsOutput.worldPos, \
    lightData.pos, \
    lightData.radiusSq, \
    lightData.color

#define CONE_LIGHT_ARGS \
    POINT_LIGHT_ARGS, \
    lightData.coneDir, \
    lightData.coneAngles

#define SHADOWED_LIGHT_ARGS \
    CONE_LIGHT_ARGS, \
    lightData.shadowTextureMatrix, \
    lightIndex

    static const uint lightCount = 10;
    for (uint lightIndex = 0; lightIndex < lightCount; lightIndex++)
    {
        LightData lightData = lightBuffer[lightIndex];
        switch (lightData.type)
        {
        case 0: // sphere
            colorSum += ApplyPointLight( POINT_LIGHT_ARGS );
            break;
        case 1: // cone
            colorSum += ApplyConeLight( CONE_LIGHT_ARGS );
            break;
        case 2: // cone w/ shadow map
            colorSum += ApplyConeShadowedLight( SHADOWED_LIGHT_ARGS );
            break;
        }
    }
    return colorSum;
}
