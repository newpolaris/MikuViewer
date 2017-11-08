#pragma once

#include "Math/BoundingBox.h"
#include <vector>
#include <memory>

class GraphicsContext;

enum eObjectFilter { kOpaque = 0x1, kCutout = 0x2, kTransparent = 0x4, kGroundPlane = 0x8, kOverlay = 0x10, kAll = 0xFF, kNone = 0x0 };
using IRenderObjectPtr = std::shared_ptr<class IRenderObject>;
using RenderList = std::vector<IRenderObjectPtr>;
class IRenderObject
{
public:
    virtual void Draw( GraphicsContext& gfxContext, eObjectFilter Filter ) = 0;
    virtual void Update( float deltaT ) = 0;
    virtual Math::BoundingBox GetBoundingBox() = 0;
};
