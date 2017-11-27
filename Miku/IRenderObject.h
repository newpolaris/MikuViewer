#pragma once

#include "Math/BoundingBox.h"

class GraphicsContext;

namespace Graphics
{
    enum eObjectFilter { kOpaque = 0x1, kCutout = 0x2, kTransparent = 0x4, kOverlay = 0x10, kAll = 0xFF, kNone = 0x0 };
    class IRenderObject
    {
    public:
        virtual void Draw( GraphicsContext& gfxContext, eObjectFilter Filter ) = 0;
        virtual void Update( float deltaT ) = 0;
        virtual Math::BoundingBox GetBoundingBox() = 0;
    };
}
