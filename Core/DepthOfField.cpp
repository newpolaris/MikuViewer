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
// Author:  James Stanard
//

#include "pch.h"
#include "PipelineState.h"
#include "DepthOfField.h"
#include "CommandContext.h"
#include "BufferManager.h"
#include "GraphicsCore.h"

#include "CompiledShaders/DoFPass2DebugCS.h"

using namespace Graphics;

namespace DepthOfField
{
	BoolVar Enable("Graphics/Depth of Field/Enable", false);
	BoolVar EnablePreFilter("Graphics/Depth of Field/PreFilter", true);
	BoolVar MedianFilter("Graphics/Depth of Field/Median Filter", true);
	BoolVar MedianAlpha("Graphics/Depth of Field/Median Alpha", false);
	NumVar FocalDepth("Graphics/Depth of Field/Focal Center", 0.1f, 0.0f, 1.0f, 0.01f);
	NumVar FocalRange("Graphics/Depth of Field/Focal Radius", 0.1f, 0.0f, 1.0f, 0.01f);
	NumVar ForegroundRange("Graphics/Depth of Field/FG Range", 100.0f, 10.0f, 1000.0f, 10.0f);
	NumVar AntiSparkleWeight("Graphics/Depth of Field/AntiSparkle", 1.0f, 0.0f, 10.0f, 1.0f);
	const char* DebugLabels[] = { "Off", "Foreground", "Background", "FG Alpha", "CoC" };
	EnumVar DebugMode("Graphics/Depth of Field/Debug Mode", 0, _countof(DebugLabels), DebugLabels);
	BoolVar DebugTiles("Graphics/Depth of Field/Debug Tiles", false);
	BoolVar ForceSlow("Graphics/Depth of Field/Force Slow Path", false);
	BoolVar ForceFast("Graphics/Depth of Field/Force Fast Path", false);

	ComputePSO s_DoFPass1CS;				// Responsible for classifying tiles (1st pass)
	ComputePSO s_DoFTilePassCS;				// Disperses tile info to its neighbors (3x3)
	ComputePSO s_DoFTilePassFixupCS;		// Searches for straggler tiles to "fixup"

	ComputePSO s_DoFPreFilterCS;			// Full pre-filter with variable focus
	ComputePSO s_DoFPreFilterFastCS;		// Pre-filter assuming near-constant focus
	ComputePSO s_DoFPreFilterFixupCS;		// Pass through colors for completely in focus tile

	ComputePSO s_DoFPass2CS;				// Perform full CoC convolution pass
	ComputePSO s_DoFPass2FastCS;			// Perform color-only convolution for near-constant focus
	ComputePSO s_DoFPass2FixupCS;			// Pass through colors again
	ComputePSO s_DoFPass2DebugCS;			// Full pass 2 shader with options for debugging

	ComputePSO s_DoFMedianFilterCS;			// 3x3 median filter to reduce fireflies
	ComputePSO s_DoFMedianFilterSepAlphaCS;	// 3x3 median filter to reduce fireflies (separate filter on alpha)
	ComputePSO s_DoFMedianFilterFixupCS;	// Pass through without performing median

	ComputePSO s_DoFCombineCS;				// Combine DoF blurred buffer with focused color buffer
	ComputePSO s_DoFCombineFastCS;			// Upsample DoF blurred buffer
	ComputePSO s_DoFDebugRedCS;				// Output red to entire tile for debugging
	ComputePSO s_DoFDebugGreenCS;			// Output green to entire tile for debugging
	ComputePSO s_DoFDebugBlueCS;			// Output blue to entire tile for debugging

	// IndirectArgsBuffer s_IndirectParameters;
}

void DepthOfField::Initialize( void )
{
#define CreatePSO( ObjName, ShaderByteCode ) \
    ObjName.SetComputeShader( MY_SHADER_ARGS(ShaderByteCode) ); \
    ObjName.Finalize();

    CreatePSO(s_DoFPass2DebugCS, g_pDoFPass2DebugCS);
}

void DepthOfField::Shutdown( void )
{
    s_DoFPass2DebugCS.Destroy();
}

void DepthOfField::Render( CommandContext& BaseContext, float NearClipDist, float FarClipDist )
{
    ScopedTimer _prof(L"Depth of Field", BaseContext);

    if (!g_bTypedUAVLoadSupport_R11G11B10_FLOAT)
    {
        // WARN_ONCE_IF(!g_bTypedUAVLoadSupport_R11G11B10_FLOAT, "Unable to perform final pass of DoF without support for R11G11B10F UAV loads");
        // Enable = false;
    }

    ComputeContext& Context = BaseContext.GetComputeContext();
    // Context.SetRootSignature(s_RootSignature);

    ColorBuffer& LinearDepth = g_LinearDepth[ Graphics::GetFrameCount() % 2 ];

    uint32_t BufferWidth = (uint32_t)LinearDepth.GetWidth();
    uint32_t BufferHeight = (uint32_t)LinearDepth.GetHeight();

    __declspec(align(16)) struct DoFConstantBuffer
    {
        float FlocalCenter, FocalRange;
        float FocalMinZ, FlocalMaxZ;
        float RcpBufferWidth, RcpBufferHeight;
    };
    DoFConstantBuffer cbuffer =
    {
        (float)FocalDepth, (float)FocalRange,
        (float)FocalDepth - (float)FocalRange, (float)FocalDepth + (float)FocalRange,
        1.0f / BufferWidth, 1.0f / BufferHeight,
    };
    Context.SetDynamicConstantBufferView(0, sizeof(cbuffer), &cbuffer);
    Context.SetPipelineState(s_DoFPass2DebugCS);
    Context.SetDynamicDescriptor(0, LinearDepth.GetSRV());
    Context.SetDynamicDescriptor(0, g_SceneColorBuffer.GetUAV());
    Context.Dispatch2D(BufferWidth, BufferHeight);
}
