#pragma once

#include <cstdint>

class GraphicsContext;
namespace Math
{
    class Vector3;
    class Matrix4;
    class BaseCamera;
}

namespace Deferred
{
    using namespace Math;
    enum class LightType : uint32_t
    {
        Point = 0,
        Spot = 1,
        SpotShadow = 2,
        Directional = 3
    };

    void Initialize( void );
    void Render( std::function<void(int)> func, GraphicsContext& gfxContext, Math::BaseCamera& camera );
    void Shutdown( void );
}
