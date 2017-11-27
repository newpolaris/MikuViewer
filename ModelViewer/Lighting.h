#pragma once

#include <cstdint>

class StructuredBuffer;
class ByteAddressBuffer;
class ColorBuffer;
class ShadowBuffer;
class GraphicsContext;
class IntVar;
namespace Math
{
    class Vector3;
    class Matrix4;
    class Camera;
}

// must keep in sync with HLSL
struct LightData
{
    float pos[3];
    float radiusSq;
    float color[3];

    uint32_t type;
    float coneDir[3];
    float coneAngles[2];

    float shadowTextureMatrix[16];
};

namespace Lighting
{
    enum { MaxLights = 128 };
    enum { kMinLightGridDim = 8 };

    extern LightData m_LightData[MaxLights];
    extern StructuredBuffer m_LightBuffer;
    extern ByteAddressBuffer m_LightGrid;
    extern ByteAddressBuffer m_LightGridBitMask;

    extern std::uint32_t m_FirstConeLight;
    extern std::uint32_t m_FirstConeShadowedLight;

    extern ColorBuffer m_LightShadowArray;
    extern ShadowBuffer m_LightShadowTempBuffer;
    extern Math::Matrix4 m_LightShadowMatrix[MaxLights];

    void CreateRandomLights(const Math::Vector3 minBound, const Math::Vector3 maxBound);
    void Shutdown(void);
}

