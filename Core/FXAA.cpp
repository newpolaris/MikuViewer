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
#include "FXAA.h"
#include "GraphicsCore.h"
#include "BufferManager.h"
#include "CommandContext.h"

#include "CompiledShaders/FXAAPass1_RGB_CS.h"
#include "CompiledShaders/FXAAPass1_Luma_CS.h"
#include "CompiledShaders/FXAAPass2HCS.h"
#include "CompiledShaders/FXAAPass2VCS.h"
#include "CompiledShaders/FXAAPass2HDebugCS.h"
#include "CompiledShaders/FXAAPass2VDebugCS.h"

// These shaders are when typed UAV loads of complex formats is available
#include "CompiledShaders/FXAAPass1_RGB2_CS.h"
#include "CompiledShaders/FXAAPass1_Luma2_CS.h"
#include "CompiledShaders/FXAAPass2H2CS.h"
#include "CompiledShaders/FXAAPass2V2CS.h"
#include "CompiledShaders/FXAAPass2HDebug2CS.h"
#include "CompiledShaders/FXAAPass2VDebug2CS.h"

#include "CompiledShaders/FXAAResolveWorkQueueCS.h"


using namespace Graphics;

namespace FXAA
{
    // Compute Shader
    ComputePSO Pass1HdrCS;
    ComputePSO Pass1LdrCS;
    ComputePSO ResolveWorkCS;
    ComputePSO Pass2HCS;
    ComputePSO Pass2VCS;
    ComputePSO Pass2HDebugCS;
    ComputePSO Pass2VDebugCS;
    IndirectArgsBuffer IndirectParameters;

    BoolVar Enable("Graphics/AA/FXAA/Enable", false);
    BoolVar DebugDraw("Graphics/AA/FXAA/Debug", false);

    // With a properly encoded luma buffer, [0.25 = "low", 0.2 = "medium", 0.15 = "high", 0.1 = "ultra"]
    NumVar ContrastThreshold("Graphics/AA/FXAA/Contrast Threshold", 0.175f, 0.05f, 0.5f, 0.025f);

    // Controls how much to blur isolated pixels that have little-to-no edge length.
    NumVar SubpixelRemoval("Graphics/AA/FXAA/Subpixel Removal", 0.50f, 0.0f, 1.0f, 0.25f);

    // This is for testing the performance of computing luma on the fly rather than reusing
    // the luma buffer output of tone mapping.
    BoolVar ForceOffPreComputedLuma("Graphics/AA/FXAA/Always Recompute Log-Luma", false);
}

void FXAA::Initialize( void )
{
#define CreatePSO( ObjName, ShaderByteCode ) \
    ObjName.SetComputeShader( MY_SHADER_ARGS(ShaderByteCode) ); \
    ObjName.Finalize();

    CreatePSO(ResolveWorkCS, g_pFXAAResolveWorkQueueCS);
    if (g_bTypedUAVLoadSupport_R11G11B10_FLOAT)
    {
        CreatePSO(Pass1LdrCS, g_pFXAAPass1_RGB2_CS);    // Use RGB and recompute log-luma; pre-computed luma is unavailable
        CreatePSO(Pass1HdrCS, g_pFXAAPass1_Luma2_CS);   // Use pre-computed luma
        CreatePSO(Pass2HCS, g_pFXAAPass2H2CS);
        CreatePSO(Pass2VCS, g_pFXAAPass2V2CS);
        CreatePSO(Pass2HDebugCS, g_pFXAAPass2HDebug2CS);
        CreatePSO(Pass2VDebugCS, g_pFXAAPass2VDebug2CS);
    }
    else
    {
        CreatePSO(Pass1LdrCS, g_pFXAAPass1_RGB_CS);     // Use RGB and recompute log-luma; pre-computed luma is unavailable
        CreatePSO(Pass1HdrCS, g_pFXAAPass1_Luma_CS);    // Use pre-computed luma
        CreatePSO(Pass2HCS, g_pFXAAPass2HCS);
        CreatePSO(Pass2VCS, g_pFXAAPass2VCS);
        CreatePSO(Pass2HDebugCS, g_pFXAAPass2HDebugCS);
        CreatePSO(Pass2VDebugCS, g_pFXAAPass2VDebugCS);
    }
#undef CreatePSO

    __declspec(align(16)) const uint32_t initArgs[6] = { 0, 1, 1, 0, 1, 1 };
    IndirectParameters.Create(L"FXAA Indirect Parameters", 2, sizeof(D3D12_DISPATCH_ARGUMENTS), initArgs);
}

void FXAA::Shutdown(void)
{
    IndirectParameters.Destroy();
}

void FXAA::Render( ComputeContext& Context, bool bUsePreComputedLuma )
{
    ScopedTimer _prof(L"FXAA", Context);

    if (ForceOffPreComputedLuma)
        bUsePreComputedLuma = false;

    ColorBuffer& Target = g_bTypedUAVLoadSupport_R11G11B10_FLOAT ? g_SceneColorBuffer : g_PostEffectsBuffer;

    DWParam Constants[] = { 
        1.0f / Target.GetWidth(), 1.0f / Target.GetHeight(), (float)ContrastThreshold, (float)SubpixelRemoval,
        g_FXAAWorkQueue.GetElementCount()-1,
        0, 0
    };
    Context.SetConstants( 0, _countof(Constants), Constants);

    // Apply algorithm to each quarter of the screen separately to reduce maximum size of work buffers.
    uint32_t BlockWidth = Target.GetWidth() / 2;
    uint32_t BlockHeight = Target.GetHeight() / 2;

    Context.SetDynamicSampler( 0, SamplerLinearClamp );
			
    for (uint32_t x = 0; x < Target.GetWidth(); x += BlockWidth)
    {
        for (uint32_t y = 0; y < Target.GetHeight(); y += BlockHeight)
        {
            // Pass 1
            Constants[5] = x, Constants[6] = y;
            Context.SetConstants( 0, _countof(Constants), Constants );

            // Begin by analysing the luminance buffer and setting aside high-contrast pixels in
            // work queues to be processed later.  There are horizontal edge and vertical edge work
            // queues so that the shader logic is simpler for each type of edge.
            // Counter values do not need to be reset because they are read and cleared at once.

            Context.TransitionResource(Target, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Context.TransitionResource(g_FXAAWorkQueue, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Context.TransitionResource(g_FXAAColorQueue, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            D3D11_UAV_HANDLE Pass1UAVs[] =
            {
                g_FXAAWorkCounters.GetUAV(),
                g_FXAAWorkQueue.GetUAV(),
                g_FXAAColorQueue.GetUAV(),
                g_LumaBuffer.GetUAV()
            };
			
            D3D11_SRV_HANDLE Pass1SRVs[] =
            {
                Target.GetSRV(),
                g_LumaBuffer.GetSRV()
            };

            if (bUsePreComputedLuma)
            {
                Context.SetPipelineState( Pass1HdrCS );
                Context.TransitionResource( g_LumaBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
                Context.SetDynamicDescriptors( 0, _countof( Pass1UAVs ) - 1, Pass1UAVs );
                Context.SetDynamicDescriptors( 0, _countof( Pass1SRVs ), Pass1SRVs );
            }
            else
            {
                Context.SetPipelineState( Pass1LdrCS );
                Context.TransitionResource( g_LumaBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
                Context.SetDynamicDescriptors( 0, _countof( Pass1UAVs ), Pass1UAVs );
                Context.SetDynamicDescriptors( 0, _countof( Pass1SRVs ) - 1, Pass1SRVs );
            }

            Context.Dispatch2D( BlockWidth, BlockHeight );

            // Pass 2

            // The next phase involves converting the work queues to DispatchIndirect parameters.
            // The queues are also padded out to 64 elements to simplify the final consume logic.
            Context.SetPipelineState( ResolveWorkCS );
            Context.TransitionResource( IndirectParameters, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
            Context.InsertUAVBarrier( g_FXAAWorkCounters );

            Context.SetDynamicDescriptor( 0, IndirectParameters.GetUAV() );
            Context.SetDynamicDescriptor( 1, g_FXAAWorkQueue.GetUAV() );
            Context.SetDynamicDescriptor( 2, g_FXAAWorkCounters.GetUAV() );

            Context.Dispatch(1, 1, 1);

            Context.InsertUAVBarrier(g_FXAAWorkCounters);
            Context.TransitionResource(IndirectParameters, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            Context.TransitionResource(g_FXAAWorkQueue, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Context.TransitionResource(g_FXAAColorQueue, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Context.TransitionResource(Target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            Context.SetDynamicDescriptor( 0, D3D11_UAV_HANDLE( nullptr ) );
            Context.SetDynamicDescriptor( 1, D3D11_UAV_HANDLE( nullptr ) );
            Context.SetDynamicDescriptor( 2, D3D11_UAV_HANDLE( nullptr ) );

            Context.SetDynamicDescriptor( 0, g_LumaBuffer.GetSRV() );
            Context.SetDynamicDescriptor( 1, g_FXAAWorkQueue.GetSRV() );
            Context.SetDynamicDescriptor( 2, g_FXAAColorQueue.GetSRV() );
            Context.SetDynamicDescriptor( 0, Target.GetUAV() );

            // The final phase involves processing pixels on the work queues and writing them
            // back into the color buffer.  Because the two source pixels required for linearly
            // blending are held in the work queue, this does not require also sampling from
            // the target color buffer (i.e. no read/modify/write, just write.)

            Context.SetPipelineState(DebugDraw ? Pass2HDebugCS : Pass2HCS);
            Context.DispatchIndirect(IndirectParameters, 0);
            Context.SetPipelineState(DebugDraw ? Pass2VDebugCS : Pass2VCS);
            Context.DispatchIndirect(IndirectParameters, 12);

            Context.InsertUAVBarrier(Target);

            Context.SetDynamicDescriptor( 0, D3D11_SRV_HANDLE( nullptr ) );
            Context.SetDynamicDescriptor( 1, D3D11_SRV_HANDLE( nullptr ) );
            Context.SetDynamicDescriptor( 2, D3D11_SRV_HANDLE( nullptr ) );
            Context.SetDynamicDescriptor( 3, D3D11_SRV_HANDLE( nullptr ) );
        }
    }
}
