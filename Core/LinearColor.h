#pragma once

namespace Gamma
{
    using namespace DirectX;
    using namespace Math;
    extern bool bSRGB;
    XMFLOAT3 Convert( XMFLOAT3 t );
    XMFLOAT4 Convert( XMFLOAT4 t );
    Color Convert( Color t );
}
