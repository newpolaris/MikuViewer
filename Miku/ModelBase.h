#pragma once

#include "IModel.h"

class BoolVar;
class NumVar;

namespace Graphics {
namespace ModelBase {
    enum PrimtiveMeshType {
        kBoneMesh,
        kSphereMesh,
        kBatchMax
    };

    extern BoolVar s_bEnableDrawBone;
    extern BoolVar s_bEnableDrawBoundingSphere;
    extern BoolVar s_bExcludeSkyBox;
    extern NumVar s_ExcludeRange;

    void Initialize();
    void Shutdown();
    void Append( PrimtiveMeshType Type, const class Math::Matrix4& Transform );
    void Flush( GraphicsContext& GfxContext );
}
}