#include <tuple>
#include <d3d11.h>
#include "GameCore.h"
#include "GraphicsCore.h"
#include "PipelineState.h"
#include "CommandContext.h"
#include "GpuBuffer.h"
#include "Lighting.h"
#include "BufferManager.h"
#include "Camera.h"
#include "VectorMath.h"
#include "Color.h"
#include "DeferredLighting.h"
#include "PrimitiveUtility.h"
#include "DebugHelper.h"
#include "InputLayout.h"
#include "Camera.h"

#include "CompiledShaders/ScreenQuadVS.h"
#include "CompiledShaders/DeferredLightingVS.h"
#include "CompiledShaders/DeferredLightingPS.h"
#include "CompiledShaders/DeferredLightingDebugPS.h"
#include "CompiledShaders/DeferredGBufferPS.h"
#include "CompiledShaders/ModelViewerVS.h"
#include "CompiledShaders/ModelViewerPS.h"

using namespace Math;
using namespace Lighting;
using namespace Graphics;

namespace Deferred
{
    InputDesc vertElem[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    Matrix4 m_FullScreenProjMatrix;

    ColorBuffer m_DiffuseTexture;
    ColorBuffer m_SpecularMask;
    ColorBuffer m_SpecularPowerTexture;
    ColorBuffer m_NormalTexture;

    GraphicsPSO m_GBufferPSO;
    GraphicsPSO m_GBuffer2PSO;
    GraphicsPSO m_Deferred1PSO;
    GraphicsPSO m_Deferred2PSO;
    GraphicsPSO m_DirectionalLightPSO;
    GraphicsPSO m_LightDebugPSO;
    GraphicsPSO m_ModelPSO;
    GraphicsPSO m_CutoutModelPSO;

    Matrix4 GetLightTransfrom( const LightData& Data, const Matrix4& ViewToProj );
    void RenderSubPass( GraphicsContext& gfxContext, LightType Type, const Matrix4& ViewToClip, GraphicsPSO& PSO );
}

BoolVar s_bLightBoundary( "Application/Deferred/Light Boundary", false );

void Deferred::Initialize( void )
{
    m_FullScreenProjMatrix = Math::OrthographicMatrix( 2, 2, 0, 1, Math::g_ReverseZ );
    const uint32_t width = g_NativeWidth, height = g_NativeHeight;

    m_NormalTexture.Create( L"Normal Buffer", width, height, 1, DXGI_FORMAT_R11G11B10_FLOAT );
    m_SpecularPowerTexture.Create( L"SpecularPower Buffer", width, height, 1, DXGI_FORMAT_R8_UNORM );
    m_DiffuseTexture.Create( L"Diffuse Buffer", width, height, 1, DXGI_FORMAT_R10G10B10A2_UNORM );
    m_SpecularMask.Create( L"Specular Buffer", width, height, 1, DXGI_FORMAT_R8_UNORM );

    m_LightDebugPSO.SetInputLayout( _countof( PrimitiveUtility::Desc ), PrimitiveUtility::Desc );
    m_LightDebugPSO.SetVertexShader( MY_SHADER_ARGS( g_pDeferredLightingVS ) );
    m_LightDebugPSO.SetPixelShader( MY_SHADER_ARGS( g_pDeferredLightingDebugPS ) );
    m_LightDebugPSO.SetRasterizerState( RasterizerWireframe );
    m_LightDebugPSO.SetDepthStencilState( DepthStateReadWrite );
    m_LightDebugPSO.Finalize();

	// Pipeline for deferred Deferred (stage 1 to determine lit pixels)
    m_Deferred1PSO.SetInputLayout( _countof( PrimitiveUtility::Desc ), PrimitiveUtility::Desc );
    m_Deferred1PSO.SetVertexShader( MY_SHADER_ARGS( g_pDeferredLightingVS ) );
    m_Deferred1PSO.SetRasterizerState( RasterizerDefault );
    m_Deferred1PSO.SetDepthStencilState( DepthStateReadOnlyReversed );
    m_Deferred1PSO.Finalize();

    D3D11_RASTERIZER_DESC raster2 = RasterizerDefault;
    raster2.CullMode = D3D11_CULL_FRONT;
    raster2.DepthClipEnable = FALSE;

    // Setup depth mode
    // Disable depth writes
    D3D11_DEPTH_STENCIL_DESC depth2 = DepthStateReadOnly;
	depth2.DepthFunc = Math::g_ReverseZ ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_GREATER_EQUAL;

    // Pipeline for deferred Deferred (stage 2 to render lit pixels)
    m_Deferred2PSO = m_Deferred1PSO;
    m_Deferred2PSO.SetPixelShader( MY_SHADER_ARGS( g_pDeferredLightingPS ) );
    m_Deferred2PSO.SetRasterizerState( raster2 );
    // Perform additive blending if a pixel passes the lighting tests.
    m_Deferred2PSO.SetBlendState( BlendAdditive );
    m_Deferred2PSO.SetDepthStencilState( depth2 );
    m_Deferred2PSO.Finalize();

    m_DirectionalLightPSO = m_Deferred2PSO;
    m_DirectionalLightPSO.SetDepthStencilState( DepthStateDisabled );
    m_DirectionalLightPSO.SetRasterizerState( RasterizerDefaultCW );
    m_DirectionalLightPSO.Finalize();

    // Initialize forward pass
    m_ModelPSO.SetPrimitiveTopologyType(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_ModelPSO.SetInputLayout(_countof(vertElem), vertElem);
    m_ModelPSO.SetRasterizerState(RasterizerDefault);
    m_ModelPSO.SetBlendState(BlendDisable);
    m_ModelPSO.SetDepthStencilState(DepthStateReadWrite);
    m_ModelPSO.SetVertexShader( MY_SHADER_ARGS(g_pModelViewerVS) );
    m_ModelPSO.SetPixelShader( MY_SHADER_ARGS(g_pModelViewerPS) );
    m_ModelPSO.Finalize();

    m_CutoutModelPSO = m_ModelPSO;
    m_CutoutModelPSO.SetRasterizerState(RasterizerTwoSided);
    m_CutoutModelPSO.Finalize();

    m_GBufferPSO = m_ModelPSO;
    m_GBufferPSO.SetPixelShader( MY_SHADER_ARGS(g_pDeferredGBufferPS) );
    m_GBufferPSO.Finalize();

    m_GBuffer2PSO = m_GBufferPSO;
    m_GBuffer2PSO.SetRasterizerState(RasterizerTwoSided);
    m_GBuffer2PSO.Finalize();
}

Matrix4 Deferred::GetLightTransfrom(const LightData& Data, const Matrix4& WorldToClip)
{
    if ( LightType(Data.type) == LightType::Directional )
        return m_FullScreenProjMatrix;

    Quaternion quat = RotationBetweenVectors( Vector3( 0, -1, 0 ), XMFLOAT3(Data.coneDir) );
    OrthogonalTransform transform( quat, XMFLOAT3(Data.pos) );

    // Compute the scale depending on the light type.
    float scaleX, scaleY, scaleZ;
    float range = Sqrt( Data.radiusSq );
    // For point lights, we want to scale the geometry by the range of the light.
    scaleX = scaleY = scaleZ = range;
    if (LightType(Data.type) == LightType::Spot)
    {
        // For spotlights, we want to scale the base of the cone by the spotlight angle.
        scaleX = scaleZ = Tan(ACos(Data.coneAngles[1])) * range;
    }
    AffineTransform scale = AffineTransform::MakeScale( Vector3( scaleX, scaleY, scaleZ ) );
    return WorldToClip * AffineTransform( transform ) * scale;
}

void Deferred::RenderSubPass( GraphicsContext& gfxContext, LightType Type, const Matrix4& WorldToClip, GraphicsPSO& PSO )
{
    gfxContext.SetPipelineState( PSO );
    for (int32_t i = 0; i < MaxLights; i++)
    {
        LightData& light = m_LightData[i];
        if (Type != LightType(light.type))
            continue;
        __declspec(align(16)) int32_t idx = i;
        gfxContext.SetDynamicConstantBufferView( 4, sizeof(int32_t), &idx, { kBindPixel } );
        __declspec(align(16)) Matrix4 model = GetLightTransfrom( light, WorldToClip );
        gfxContext.SetDynamicConstantBufferView( 2, sizeof( Matrix4 ), &model, { kBindVertex } );

        using namespace PrimitiveUtility;
        PrimtiveMeshType mesh[] = { kSphereMesh, kConeMesh, kConeMesh, kFarClipMesh };
        PrimitiveUtility::Render( gfxContext, mesh[(int)light.type] );
    }
    if (Type == LightType::Directional)
    {
        __declspec(align(16)) int32_t idx = -1;
        gfxContext.SetDynamicConstantBufferView( 4, sizeof(int32_t), &idx, { kBindPixel } );
        __declspec(align(16)) Matrix4 model = m_FullScreenProjMatrix;
        gfxContext.SetDynamicConstantBufferView( 2, sizeof( Matrix4 ), &model, { kBindVertex } );
        using namespace PrimitiveUtility;
        PrimitiveUtility::Render( gfxContext, kFarClipMesh );
    }
}

void Deferred::Render( std::function<void(int)> render, GraphicsContext& gfxContext, Math::BaseCamera& camera )
{
    Matrix4 ViewMatrix = camera.GetViewMatrix();
    Matrix4 ProjMatrix = camera.GetProjMatrix();
    Matrix4 WorldToClipMatrix = camera.GetViewProjMatrix();

    gfxContext.SetDynamicDescriptor( 60, m_LightBuffer.GetSRV(), { kBindPixel } );

#define DEFERRED 1
#if DEFERRED
    { ScopedTimer _prof( L"Geometry Pass", gfxContext );
    gfxContext.ClearColor( m_NormalTexture );
    gfxContext.ClearColor( m_SpecularPowerTexture );
    gfxContext.ClearColor( m_DiffuseTexture );
    gfxContext.ClearColor( m_SpecularMask );
    D3D11_RTV_HANDLE rtvs[] = {
        m_NormalTexture.GetRTV(),
        m_SpecularPowerTexture.GetRTV(),
        m_DiffuseTexture.GetRTV(),
        m_SpecularMask.GetRTV(),
        g_SceneColorBuffer.GetRTV(),
    };
    gfxContext.SetRenderTargets( _countof( rtvs ), rtvs, g_SceneDepthBuffer.GetDSV() );
    gfxContext.SetPipelineState( m_GBufferPSO ); render(1);
    gfxContext.SetPipelineState( m_GBuffer2PSO ); render(2);
    D3D11_RTV_HANDLE nullrtvs[_countof(rtvs)] = { nullptr, };
    gfxContext.SetRenderTargets( _countof( rtvs ), nullrtvs, nullptr );
    } // Geometry

    { ScopedTimer _prof( L"Lighting Pass", gfxContext );
    D3D11_SRV_HANDLE srvs[] = {
        m_NormalTexture.GetSRV(),
        m_SpecularPowerTexture.GetSRV(),
        m_DiffuseTexture.GetSRV(),
        m_SpecularMask.GetSRV(),
        g_SceneDepthBuffer.GetDepthSRV(),
    };
    struct ScreenToViewParams
    {
        Matrix4 InverseProjectionMatrix;
        Vector4 ScreenDimensions;
    } psScreenToView;
    psScreenToView.InverseProjectionMatrix = Invert( ProjMatrix );
    psScreenToView.ScreenDimensions = Vector4( g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight(), 0, 0 );
    gfxContext.SetDynamicConstantBufferView( 3, sizeof( psScreenToView ), &psScreenToView, { kBindPixel } );
    gfxContext.SetDynamicDescriptors( 0, _countof(srvs), srvs, { kBindPixel } );
    gfxContext.SetRenderTarget( g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly() );
    RenderSubPass( gfxContext, LightType::Point, WorldToClipMatrix, m_Deferred1PSO );
    RenderSubPass( gfxContext, LightType::Spot, WorldToClipMatrix, m_Deferred1PSO );
    RenderSubPass( gfxContext, LightType::SpotShadow, WorldToClipMatrix, m_Deferred1PSO );
    RenderSubPass( gfxContext, LightType::Point, WorldToClipMatrix, m_Deferred2PSO );
    RenderSubPass( gfxContext, LightType::Spot, WorldToClipMatrix, m_Deferred2PSO );
    RenderSubPass( gfxContext, LightType::SpotShadow, WorldToClipMatrix, m_Deferred2PSO );
    RenderSubPass( gfxContext, LightType::Directional, WorldToClipMatrix, m_DirectionalLightPSO );
    D3D11_SRV_HANDLE nullsrvs[_countof(srvs)] = { nullptr, };
    gfxContext.SetDynamicDescriptors( 0, _countof(nullsrvs), nullsrvs, { kBindPixel } );
    } // lighting

    if (s_bLightBoundary)
    {
        gfxContext.SetRenderTarget( g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV() );
        RenderSubPass( gfxContext, LightType::Point, WorldToClipMatrix, m_LightDebugPSO );
        RenderSubPass( gfxContext, LightType::Spot, WorldToClipMatrix, m_LightDebugPSO );
    }
#endif
    { ScopedTimer _prof( L"Forward Pass", gfxContext );
    gfxContext.SetRenderTarget( g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV() );
#if !DEFERRED
    gfxContext.SetPipelineState( m_ModelPSO );
    render(1);
    gfxContext.SetPipelineState( m_CutoutModelPSO );
    render(2);
#endif
    }
}

void Deferred::Shutdown( void )
{
    m_DiffuseTexture.Destroy();
    m_SpecularMask.Destroy();
    m_NormalTexture.Destroy();
    m_SpecularPowerTexture.Destroy();
}