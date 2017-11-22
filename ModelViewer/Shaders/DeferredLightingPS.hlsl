#include "lighting.hlsli"

SamplerState linearRepeat : register(s0);
SamplerState linearClamp : register(s1);

// Parameters required to convert screen space coordinates to view space params.
cbuffer ScreenToViewParams : register(b3)
{
    float4x4 InverseWorldToClip;
    float2 ScreenDimensions;
}

cbuffer LightIndexBuffer : register(b4)
{
    // The index of the light in the Lights array.
    int LightIndex;
}

cbuffer Constants : register(b5)
{
    float4x4 ModelToProjection;
    float4x4 ModelToShadow;
    float3 ViewerPos;
};

// The normal from the screen space texture.
Texture2D<float3> NormalTexture : register(t0);
// The specular power from the screen space texture.
Texture2D<float> SpecularPowerTexture : register(t1);
Texture2D<float3> DiffuseTexture : register(t2);
Texture2D<float > SpecularMaskTexture : register(t3);
// The depth from the screen space texture.
Texture2D<float> DepthTexture : register(t4);

// Convert clip space coordinates to view space
float4 ClipToWorld( float4 clip )
{
    // View space position.
    float4 world = mul( InverseWorldToClip, clip );
    // Perspecitive projection.
    return world / world.w;
}

// Convert screen space coordinates to view space.
float4 ScreenToWorld( float4 screen )
{
    // Convert to normalized texture coordinates
    float2 texCoord = screen.xy / ScreenDimensions;

    // Convert to clip space
    float4 clip = float4( float2( texCoord.x, 1.0f - texCoord.y ) * 2.0f - 1.0f, screen.z, screen.w );

    return ClipToWorld( clip );
}


// Deferred lighting pixel shader.
[earlydepthstencil]
float3 main( float4 PosHS : SV_Position ) : SV_Target
{
    float3 colorSum = (float3)0;

    int2 texCoord = PosHS.xy;
    float depth = DepthTexture.Load(int3(texCoord, 0));
    float3 position = ScreenToWorld(float4(texCoord, depth, 1.0f) ).xyz;
    float3 shadowCoord = mul(ModelToShadow, float4(position, 1.0)).xyz;
    float3 viewDir = normalize(position - ViewerPos);
    float3 diffuseAlbedo = DiffuseTexture.Load(int3(texCoord, 0));
    float specularMask = SpecularMaskTexture.Load(int3(texCoord, 0));

    // Unpack the normal
    float3 normal = NormalTexture.Load(int3( texCoord, 0 )) * 2 - 1;
    // Unpack the specular power
    float gloss = SpecularPowerTexture.Load(int3( texCoord, 0 )) * 128.0;
    LightData lightData = lightBuffer[LightIndex];

    float3 specularAlbedo = float3( 0.56, 0.56, 0.56 );

#define POINT_LIGHT_ARGS \
    diffuseAlbedo, \
    specularAlbedo, \
    specularMask, \
    gloss, \
    normal, \
    viewDir, \
    position, \
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
    LightIndex

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
    if (LightIndex == -1)
        colorSum += ApplyDirectionalLight( diffuseAlbedo, specularAlbedo, specularMask, gloss, normal, viewDir, SunDirection, SunColor, shadowCoord );

    return colorSum;
}
