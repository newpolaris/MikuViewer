#include "pch.h"
#include "PipelineState.h"
#include "DepthOfField.h"
#include "CommandContext.h"
#include "BufferManager.h"
#include "GraphicsCore.h"
#include "FullScreenTriangle.h"

#include "CompiledShaders/FullScreenTriangleVS.h"
#include "CompiledShaders/DoFCombinePS.h"
#include "CompiledShaders/DoFPrefilterCS.h"
#include "CompiledShaders/DoFDownsamplingPS.h"
#include "CompiledShaders/DoFBlurHorzPS.h"
#include "CompiledShaders/DoFBlurVertPS.h"
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

    GraphicsPSO s_DoFDownsamplingPSO;
    GraphicsPSO s_DoFBlurHorzPSO;
    GraphicsPSO s_DoFBlurVertPSO;
    GraphicsPSO s_DoFCombinePSO;

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

    D3D11_VIEWPORT s_MainViewport, s_Viewport;
    D3D11_RECT s_MainScissor, s_Scissor;

	IndirectArgsBuffer s_IndirectParameters;
}

void DepthOfField::Initialize( void )
{
    const auto input = FullScreenTriangle::InputLayout;

#define CreatePSO( ObjName, ShaderByteCode ) \
	ObjName.SetPrimitiveTopologyType( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST ); \
	ObjName.SetInputLayout( uint32_t(input.size()), input.data() ); \
	ObjName.SetVertexShader( MY_SHADER_ARGS( g_pFullScreenTriangleVS ) ); \
	ObjName.SetPixelShader( MY_SHADER_ARGS( ShaderByteCode ) ); \
    ObjName.Finalize();

    CreatePSO(s_DoFBlurHorzPSO, g_pDoFBlurHorzPS);
    CreatePSO(s_DoFBlurVertPSO, g_pDoFBlurVertPS);
    CreatePSO(s_DoFCombinePSO, g_pDoFCombinePS);
    CreatePSO(s_DoFDownsamplingPSO, g_pDoFDownsamplingPS);

#undef CreatePSO

#define CreatePSO( ObjName, ShaderByteCode ) \
    ObjName.SetComputeShader( MY_SHADER_ARGS( ShaderByteCode ) ); \
    ObjName.Finalize();

    CreatePSO(s_DoFPreFilterCS, g_pDoFPrefilterCS);
    CreatePSO(s_DoFPass2DebugCS, g_pDoFPass2DebugCS);

    s_Viewport.TopLeftX = 0.f;
    s_Viewport.TopLeftY = 0.f;
    s_Viewport.Width = (float)g_DoFBlurColor[0].GetWidth();
    s_Viewport.Height = (float)g_DoFBlurColor[0].GetHeight();
    s_Viewport.MinDepth = 0.0f;
    s_Viewport.MaxDepth = 1.0f;

    s_Scissor.left = 0;
    s_Scissor.top = 0;
    s_Scissor.right = (LONG)g_DoFBlurColor[0].GetWidth();
    s_Scissor.bottom = (LONG)g_DoFBlurColor[0].GetHeight();

    s_MainViewport.TopLeftX = 0.f;
    s_MainViewport.TopLeftY = 0.f;
    s_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
    s_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
    s_MainViewport.MinDepth = 0.0f;
    s_MainViewport.MaxDepth = 1.0f;

    s_MainScissor.left = 0;
    s_MainScissor.top = 0;
    s_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
    s_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();
}

void DepthOfField::Shutdown( void )
{
    s_DoFBlurHorzPSO.Destroy();
    s_DoFBlurVertPSO.Destroy();
    s_DoFCombinePSO.Destroy();
    s_DoFPreFilterCS.Destroy();
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
    Context.SetPipelineState(s_DoFPreFilterCS);
    Context.SetDynamicDescriptor(0, LinearDepth.GetSRV());
    Context.SetDynamicDescriptor(0, g_DofCocBuffer.GetUAV());
    Context.Dispatch2D(BufferWidth, BufferHeight);

    Context.SetDynamicDescriptor(0, UAV_NULL);

    // Downsampling to 1/4
    GraphicsContext& gfxContext = Context.GetGraphicsContext();
    gfxContext.SetViewportAndScissor(s_Viewport, s_Scissor);
    gfxContext.SetPipelineState(s_DoFDownsamplingPSO);
    gfxContext.SetConstants(0, 1.0f / s_Viewport.Width, 1.f / s_Viewport.Height, { kBindPixel });
    gfxContext.SetDynamicDescriptor(0, g_DofCocBuffer.GetSRV(), { kBindPixel } );
    gfxContext.SetDynamicDescriptor(1, g_SceneColorBuffer.GetSRV(), { kBindPixel } );
    gfxContext.SetDynamicSampler(0, SamplerLinearClamp, { kBindPixel } );
    gfxContext.SetRenderTarget(g_DoFBlurColor[0].GetRTV());
    FullScreenTriangle::Draw(gfxContext);

    // Gaussian blur X
    gfxContext.SetPipelineState(s_DoFBlurHorzPSO);
    gfxContext.SetRenderTarget(nullptr);
    gfxContext.SetDynamicDescriptor(0, g_DoFBlurColor[0].GetSRV(), { kBindPixel } );
    gfxContext.SetRenderTarget(g_DoFBlurColor[1].GetRTV());
    FullScreenTriangle::Draw(gfxContext);

    // Gaussian blur Y
    gfxContext.SetPipelineState(s_DoFBlurVertPSO);
    gfxContext.SetRenderTarget(nullptr);
    gfxContext.SetDynamicDescriptor(0, g_DoFBlurColor[1].GetSRV(), { kBindPixel } );
    gfxContext.SetRenderTarget(g_DoFBlurColor[0].GetRTV());
    FullScreenTriangle::Draw(gfxContext);

    // Combine
    gfxContext.SetPipelineState(s_DoFCombinePSO);
    gfxContext.SetViewportAndScissor(s_MainViewport, s_MainScissor);
    gfxContext.SetRenderTarget(g_PostEffectsBufferTyped.GetRTV());
    gfxContext.SetDynamicConstantBufferView(0, sizeof(cbuffer), &cbuffer, { kBindPixel } );
    gfxContext.SetDynamicDescriptor(0, g_SceneColorBuffer.GetSRV(), { kBindPixel } );
    gfxContext.SetDynamicDescriptor(1, g_DofCocBuffer.GetSRV(), { kBindPixel } );
    gfxContext.SetDynamicDescriptor(2, g_DoFBlurColor[0].GetSRV(), { kBindPixel } );
    FullScreenTriangle::Draw(gfxContext);

    Context.CopyBuffer(g_SceneColorBuffer, g_PostEffectsBufferTyped);
}
