#include "pch.h"
#include "PipelineState.h"
#include "DepthOfField.h"
#include "CommandContext.h"
#include "BufferManager.h"
#include "GraphicsCore.h"
#include "FullScreenTriangle.h"

#include "CompiledShaders/FullScreenTriangleVS.h"
#include "CompiledShaders/DoFMergePS.h"
#include "CompiledShaders/DoFMergeDebugPS.h"
#include "CompiledShaders/DoFPrefilterCS.h"
#include "CompiledShaders/DoFDownPS.h"
#include "CompiledShaders/DoFBlur1PS.h"
#include "CompiledShaders/DoFBlur2PS.h"
#include "CompiledShaders/DoFNearCocPS.h"
#include "CompiledShaders/DoFSmallBlurPS.h"
#include "CompiledShaders/DoFPass2DebugCS.h"

using namespace Graphics;

namespace DepthOfField
{
    extern EnumVar ApertureSize;
    extern NumVar FocalLength;
    extern NumVar FocusDistance;
    extern NumVar FilmSize;
    extern float ApertureFNumber();
    extern float ApertureWidth();

	enum {
        kFStop1_8, kFStop2_0, kFStop2_2, kFStop2_5, kFStop2_8, kFStop3_2, kFStop3_5, kFStop4_0,
        kFStop4_5, kFStop5_0, kFStop5_6, kFStop6_3, kFStop7_1, kFStop8_0, kFStop9_0, kFStop10_0,
        kFStop11_0, kFStop13_0, kFStop14_0, kFStop16_0, kFStop18_0, kFStop20_0, kFStop22_0,
        kFStopCount
    };
	const char* FStopLabels[] = {
        "f/1.8", "f/2.0", "f/2.2", "f/2.5", "f/2.8", "f/3.2", "f/3.5", "f/4.0",
        "f/4.5", "f/5.0", "f/5.6", "f/6.3", "f/7.1", "f/8.0", "f/9.0", "f/10.0",
        "f/11.0", "f/13.0", "f/14.0", "f/16.0", "f/18.0", "f/20.0", "f/22.0"
    };
	EnumVar ApertureSize("Graphics/Camera/F-Stops", kFStop16_0, kFStopCount, FStopLabels);
    NumVar FocalLength("Graphics/Camera/Focal Length(mm)", 35.0f, 0.0f, 300.0f, 1.0f);
    NumVar FocusDistance("Graphics/Camera/Focus Distance(m)", 10.0f, 1.0f, 1000.0f, 1.0f);
    NumVar FilmSize("Graphics/Camera/Film Size(mm)", 35.0f, 1.0f, 100.0f, 0.1f);

    float ApertureFNumber()
    {
        static const float FNumbers[] =
        {
            1.8f, 2.0f, 2.2f, 2.5f, 2.8f, 3.2f, 3.5f, 4.0f, 4.5f, 5.0f, 5.6f, 6.3f, 7.1f, 8.0f,
            9.0f, 10.0f, 11.0f, 13.0f, 14.0f, 16.0f, 18.0f, 20.0f, 22.0f
        };
        static_assert(_countof(FNumbers) == kFStopCount, "FStop Label mismatch");

        return FNumbers[ApertureSize];
    }

    // ApertureWidth (mm)
    float ApertureWidth()
    {
        return (FocalLength / ApertureFNumber()) * 0.5f;
    }

	BoolVar Enable("Graphics/Depth of Field/Enable", false);
	BoolVar EnablePreFilter("Graphics/Depth of Field/PreFilter", true);
	BoolVar MedianFilter("Graphics/Depth of Field/Median Filter", true);
	BoolVar MedianAlpha("Graphics/Depth of Field/Median Alpha", false);
	NumVar FocalDepth("Graphics/Depth of Field/Focal Center", 0.1f, 0.0f, 1.0f, 0.01f);
	NumVar FocalRange("Graphics/Depth of Field/Focal Radius", 0.1f, 0.0f, 1.0f, 0.01f);
	NumVar ForegroundRange("Graphics/Depth of Field/FG Range(m)", 80.0f, 1.0f, 100.0f, 1.0f);
    NumVar FarCocMult("Graphics/Depth of Field/Far Coc Multiply factor", 0.95f, 0.0f, 1.0f, 0.1f);
	NumVar AntiSparkleWeight("Graphics/Depth of Field/AntiSparkle", 1.0f, 0.0f, 10.0f, 1.0f);
	const char* DebugLabels[] = { "Off", "CoC" };
	EnumVar DebugMode("Graphics/Depth of Field/Debug Mode", 0, _countof(DebugLabels), DebugLabels);
	BoolVar DebugTiles("Graphics/Depth of Field/Debug Tiles", false);
	BoolVar ForceSlow("Graphics/Depth of Field/Force Slow Path", false);
	BoolVar ForceFast("Graphics/Depth of Field/Force Fast Path", false);

    GraphicsPSO s_DoFDownPSO;
    GraphicsPSO s_DoFBlur1PSO;
    GraphicsPSO s_DoFBlur2PSO;
    GraphicsPSO s_DoFSmallBlurPSO;
    GraphicsPSO s_DoFMergeSO;
    GraphicsPSO s_DoFMergeDebugSO;
    GraphicsPSO s_DoFNearCocPSO;

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

    D3D11_VIEWPORT s_MainViewport, s_QuarterViewport;
    D3D11_RECT s_MainScissor, s_QuarterScissor;

	IndirectArgsBuffer s_IndirectParameters;

    void DofDownsample(GraphicsContext& gfxContext, ColorBuffer& blur, ColorBuffer& down);
    void DofNearCoc(GraphicsContext& gfxContext, ColorBuffer& destination, const ColorBuffer& blur, const ColorBuffer& down);
    void DofMerge(GraphicsContext& gfxContext, ColorBuffer& destination, const ColorBuffer& smallBlur, const ColorBuffer& largeBlur);
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

    CreatePSO(s_DoFBlur1PSO, g_pDoFBlur1PS);
    CreatePSO(s_DoFBlur2PSO, g_pDoFBlur2PS);
    CreatePSO(s_DoFMergeSO, g_pDoFMergePS);
    CreatePSO(s_DoFMergeDebugSO, g_pDoFMergeDebugPS);
    CreatePSO(s_DoFDownPSO, g_pDoFDownPS);
    CreatePSO(s_DoFNearCocPSO, g_pDoFNearCocPS);
    CreatePSO(s_DoFSmallBlurPSO, g_pDoFSmallBlurPS);

#undef CreatePSO

#define CreatePSO( ObjName, ShaderByteCode ) \
    ObjName.SetComputeShader( MY_SHADER_ARGS( ShaderByteCode ) ); \
    ObjName.Finalize();

    CreatePSO(s_DoFPreFilterCS, g_pDoFPrefilterCS);
    CreatePSO(s_DoFPass2DebugCS, g_pDoFPass2DebugCS);

    s_QuarterViewport.TopLeftX = 0.f;
    s_QuarterViewport.TopLeftY = 0.f;
    s_QuarterViewport.Width = (float)g_DoFBlurColor[0].GetWidth();
    s_QuarterViewport.Height = (float)g_DoFBlurColor[0].GetHeight();
    s_QuarterViewport.MinDepth = 0.0f;
    s_QuarterViewport.MaxDepth = 1.0f;

    s_QuarterScissor.left = 0;
    s_QuarterScissor.top = 0;
    s_QuarterScissor.right = (LONG)g_DoFBlurColor[0].GetWidth();
    s_QuarterScissor.bottom = (LONG)g_DoFBlurColor[0].GetHeight();

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
    s_DoFBlur1PSO.Destroy();
    s_DoFBlur2PSO.Destroy();
    s_DoFMergeSO.Destroy();
    s_DoFPreFilterCS.Destroy();
    s_DoFPass2DebugCS.Destroy();
}

void DepthOfField::Render( CommandContext& BaseContext, float /*NearClipDist*/, float FarClipDist )
{
    ScopedTimer _prof(L"Depth of Field", BaseContext);

    if (!g_bTypedUAVLoadSupport_R11G11B10_FLOAT)
    {
        // WARN_ONCE_IF(!g_bTypedUAVLoadSupport_R11G11B10_FLOAT, "Unable to perform final pass of DoF without support for R11G11B10F UAV loads");
        // Enable = false;
    }

    uint32_t BufferWidth = (uint32_t)g_SceneColorBuffer.GetWidth();
    uint32_t BufferHeight = (uint32_t)g_SceneColorBuffer.GetHeight();

    __declspec(align(16)) struct DoFConstantBuffer
    {
        float FocalCenter, FocalRange;
        float FocalMinZ, FlocalMaxZ;
        float RcpBufferWidth, RcpBufferHeight;
        float ForegroundRange;
        float FarCocMult;
        float FocusDistance;
        float FocalLength;
        float ApertureWidth;
        float FilmSize;
        float FarClipDist;
        uint32_t BufferWidth;
        uint32_t DebugState;
    };

    // For proper scale matric treat FarClipDist as (cm)

    DoFConstantBuffer cbuffer =
    {
        (float)FocalDepth, (float)FocalRange,
        (float)FocalDepth - (float)FocalRange, (float)FocalDepth + (float)FocalRange,
        1.0f / BufferWidth, 1.0f / BufferHeight,
        ForegroundRange / (FarClipDist/100),
        FarCocMult,
        FocusDistance,
        FocalLength/1000,
        ApertureWidth()/1000,
        FilmSize/1000,
        FarClipDist/100,
        BufferWidth,
        (uint32_t)DebugMode
    };

    GraphicsContext& gfxContext = BaseContext.GetGraphicsContext();
    gfxContext.SetDynamicConstantBufferView(0, sizeof(cbuffer), &cbuffer, { kBindPixel } );
	gfxContext.SetViewportAndScissor(s_QuarterViewport, s_QuarterScissor);
    gfxContext.SetDynamicSampler(0, SamplerLinearClamp, { kBindPixel } );

    // Chapter 28. Practical Post-Process Depth of Field
    DofDownsample(gfxContext, g_DoFBlurColor[0], g_DoFDownColor);
    DofNearCoc(gfxContext, g_DoFNearCocBuffer[0], g_DoFBlurColor[0], g_DoFDownColor);
    DofMerge(gfxContext, g_PostEffectsBufferTyped, g_DoFNearCocBuffer[0], g_DoFBlurColor[0]);

    gfxContext.CopyBuffer(g_SceneColorBuffer, g_PostEffectsBufferTyped);
}

void DepthOfField::DofDownsample(GraphicsContext& gfxContext, ColorBuffer& blur, ColorBuffer& down)
{
    // Used BlurColor as temp buffer
    ASSERT(&g_DoFBlurColor[1] != &blur);

    ColorBuffer& LinearDepth = g_LinearDepth[ Graphics::GetFrameCount() % 2 ];

    // Downsamples the Scene and Initializes the Near CoC
    gfxContext.SetPipelineState(s_DoFDownPSO);
    gfxContext.SetDynamicDescriptor(0, LinearDepth.GetSRV(), { kBindPixel } );
    gfxContext.SetDynamicDescriptor(1, g_SceneColorBuffer.GetSRV(), { kBindPixel } );
    gfxContext.SetRenderTarget(down.GetRTV());
    FullScreenTriangle::Draw(gfxContext);
    gfxContext.SetRenderTarget(nullptr);

    gfxContext.CopyBuffer(blur, down);

    enum { kNumBlurPass = 2 };
    for (int i = 0; i < kNumBlurPass; i++)
    {

        // Gaussian blur X
        gfxContext.SetPipelineState(s_DoFBlur1PSO);
        gfxContext.SetDynamicDescriptor(0, blur.GetSRV(), {kBindPixel});
        gfxContext.SetRenderTarget(g_DoFBlurColor[1].GetRTV());
        FullScreenTriangle::Draw(gfxContext);
        gfxContext.SetRenderTarget(nullptr);

        // Gaussian blur Y
        gfxContext.SetPipelineState(s_DoFBlur2PSO);
        gfxContext.SetDynamicDescriptor(0, g_DoFBlurColor[1].GetSRV(), {kBindPixel});
        gfxContext.SetRenderTarget(blur.GetRTV());
        FullScreenTriangle::Draw(gfxContext);
        gfxContext.SetRenderTarget(nullptr);
    }
}

void DepthOfField::DofNearCoc(GraphicsContext& gfxContext, ColorBuffer& destination, const ColorBuffer& blur, const ColorBuffer& down)
{
    gfxContext.SetPipelineState(s_DoFNearCocPSO);
    gfxContext.SetDynamicDescriptor(0, down.GetSRV(), { kBindPixel } );
    gfxContext.SetDynamicDescriptor(1, blur.GetSRV(), { kBindPixel } );
    gfxContext.SetRenderTarget(g_DoFNearCocBuffer[1].GetRTV());
    FullScreenTriangle::Draw(gfxContext);

    gfxContext.SetPipelineState(s_DoFSmallBlurPSO);
    gfxContext.SetRenderTarget(nullptr);
    gfxContext.SetDynamicDescriptor(0, g_DoFNearCocBuffer[1].GetSRV(), { kBindPixel } );
    gfxContext.SetRenderTarget(destination.GetRTV());
    FullScreenTriangle::Draw(gfxContext);

    gfxContext.SetRenderTarget(nullptr);
}

void DepthOfField::DofMerge(GraphicsContext& gfxContext, ColorBuffer& destination, const ColorBuffer& smallBlur, const ColorBuffer& largeBlur)
{
    ColorBuffer& LinearDepth = g_LinearDepth[ Graphics::GetFrameCount() % 2 ];

    gfxContext.SetPipelineState(DebugMode > 0 ? s_DoFMergeDebugSO : s_DoFMergeSO);
    gfxContext.SetViewportAndScissor(s_MainViewport, s_MainScissor);
    gfxContext.SetRenderTarget(destination.GetRTV());
    gfxContext.SetDynamicDescriptor(0, g_SceneColorBuffer.GetSRV(), { kBindPixel } );
    gfxContext.SetDynamicDescriptor(1, smallBlur.GetSRV(), { kBindPixel } );
    gfxContext.SetDynamicDescriptor(2, largeBlur.GetSRV(), { kBindPixel } );
    gfxContext.SetDynamicDescriptor(3, LinearDepth.GetSRV(), { kBindPixel } );
    FullScreenTriangle::Draw(gfxContext);
    gfxContext.SetRenderTarget(nullptr);
}
