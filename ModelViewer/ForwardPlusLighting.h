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
// Author(s):  Alex Nankervis
//             James Stanard
//

#pragma once

//using namespace Graphics;

#include "Lighting.h"

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

namespace Forward
{
    extern IntVar LightGridDim;

    void InitializeResources(void);
    void FillLightGrid(GraphicsContext& gfxContext, const Math::Camera& camera);
    void Shutdown(void);
}
