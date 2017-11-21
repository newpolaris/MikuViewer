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

#include "ForwardPlusLighting.h"
#include "PipelineState.h"
#include "CommandContext.h"
#include "Camera.h"
#include "BufferManager.h"

// #include "CompiledShaders/FillLightGridCS_8.h"
// #include "CompiledShaders/FillLightGridCS_16.h"
// #include "CompiledShaders/FillLightGridCS_24.h"
// #include "CompiledShaders/FillLightGridCS_32.h"

using namespace Math;
using namespace Graphics;

namespace Forward
{
    IntVar LightGridDim("Application/Forward+/Light Grid Dim", 16, Lighting::kMinLightGridDim, 32, 8 );

    ComputePSO m_FillLightGridCS_8;
    ComputePSO m_FillLightGridCS_16;
    ComputePSO m_FillLightGridCS_24;
    ComputePSO m_FillLightGridCS_32;

    void InitializeResources(void);
    void FillLightGrid(GraphicsContext& gfxContext, const Camera& camera);
    void Shutdown(void);
}

void Forward::InitializeResources( void )
{
}

void Forward::Shutdown(void)
{
}

void Forward::FillLightGrid(GraphicsContext& gfxContext, const Camera& camera)
{
}
