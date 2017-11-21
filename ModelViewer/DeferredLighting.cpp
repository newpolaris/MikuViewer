/*
#include <random>
#pragma warning(push)
#pragma warning(disable: 4201)
#include <glm/glm.hpp>
#include <glm/gtc/random.hpp>
#pragma warning(pop)
#include "DeferredLighting.h"
#include "BufferManager.h"
#include "CommandContext.h"
#include "PipelineState.h"
#include "Camera.h"
#include "GraphicsCore.h"
#include "Scene.h"
#include "VectorMath.h"
#include "Color.h"
#include "OpaquePass.h"
#include "TransparentPass.h"
#include "OutlinePass.h"
#include "PrimitiveUtility.h"
#include "DebugHelper.h"
#include "RenderArgs.h"

#include "CompiledShaders/ScreenQuadVS.h"
#include "CompiledShaders/DeferredLightingVS.h"
#include "CompiledShaders/DeferredLightingPS.h"

using namespace Math;
using namespace Graphics;

namespace Lighting
{
    LightData m_LightData[MaxLights];
    Matrix4 m_FullScreenProjMatrix;

    StructuredBuffer m_LightBuffer;
    ColorBuffer m_DiffuseTexture;
    ColorBuffer m_SpecularTexture;
    ColorBuffer m_SpecularPowerTexture;
    ColorBuffer m_NormalTexture;

    GraphicsPSO m_Lighting1PSO;
    GraphicsPSO m_Lighting2PSO;
    GraphicsPSO m_DirectionalLightPSO;
    GraphicsPSO m_LightDebugPSO;
    OutlinePass m_OutlinePass;
    TransparentPass m_TransparentPass;

    Matrix4 GetLightTransfrom( const LightData& Data, const Matrix4& ViewToProj );
    void RenderSubPass( GraphicsContext& gfxContext, LightType Type, const Matrix4& ViewToClip, GraphicsPSO& PSO );
}

BoolVar s_bLightBoundary( "Application/Deferred/Light Boundary", false );

inline Vector3 Convert(const glm::vec3& v )
{
    return Vector3( v.x, v.y, v.z );
}

inline glm::vec3 Convert(const Vector3& v )
{
    return glm::vec3( v.GetX(), v.GetY(), v.GetZ() );
}

void Lighting::CreateRandomLights( const Vector3 minBound, const Vector3 maxBound )
{
    Vector3 posScale = maxBound - minBound;
    Vector3 posBias = minBound;

    std::random_device rd;
    std::mt19937 mt(rd());
#if 1
    mt.seed( 0 );
#endif
    std::uniform_real_distribution<float> dist( 0, 1 );
    std::normal_distribution<float> gaussian( -1, 1 );
    auto randFloat = [&]() {
        return dist( mt );
    };
    auto randNormalFloat = [&]() {
        return gaussian( mt );
    };
    auto randVecUniform = [&]() {
        return Vector3( randFloat(), randFloat(), randFloat() );
    };
    auto randVecNormal = [&]() {
        return Normalize(Vector3( randNormalFloat(), randNormalFloat(), randNormalFloat() ));
    };

    for (uint32_t n = 0; n < MaxLights; n++)
    {
        auto& light = m_LightData[n];

        light.Intensity = 1.f;
        light.PositionWS = Vector4(randVecUniform() * posScale + posBias, 1.f);
        light.DirectionWS = randVecNormal();

        Vector3 color = randVecUniform();
        float colorScale = randFloat() * 0.3f + 0.3f;
        color = color * colorScale;
        light.Color = Color(color.GetX(), color.GetY(), color.GetZ());

        light.Range = randFloat() * 100.f;
        light.SpotlightAngle = randFloat() * (60.f - 1.f) + 1.f;
        light.Type = LightType(randFloat() > 0.5);
    }

#if 1
    m_LightData[0].Color = Color(1.f, 0.f, 0.f);
    m_LightData[0].Range = 40;
    m_LightData[0].Type = LightType(1);
    m_LightData[0].PositionWS = Vector4(0, 15, -5, 1);
    m_LightData[0].DirectionWS = Normalize(Vector3(0, -1, -1));
    m_LightData[0].SpotlightAngle = 45;

    m_LightData[1].Color = Color(1.f, 1.f, 1.f);
    m_LightData[1].Range = 80;
    m_LightData[1].Type = LightType(0);
    m_LightData[1].PositionWS = Vector4(0, 5, 5, 1);
    m_LightData[1].Intensity = 1.f;
#endif
}

void Lighting::Initialize( void )
{
    m_FullScreenProjMatrix = Math::OrthographicMatrix( 2, 2, 0, 1, Math::g_ReverseZ );
    const uint32_t width = g_NativeWidth, height = g_NativeHeight;

    m_NormalTexture.Create( L"Normal Buffer", width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT );
    m_SpecularPowerTexture.Create( L"SpecularPower Buffer", width, height, 1, DXGI_FORMAT_R8_UNORM );
    m_DiffuseTexture.Create( L"Diffuse Buffer", width, height, 1, DXGI_FORMAT_R11G11B10_FLOAT );
    m_SpecularTexture.Create( L"Specular Buffer", width, height, 1, DXGI_FORMAT_R11G11B10_FLOAT );

    m_LightDebugPSO.SetInputLayout( _countof( PrimitiveUtility::Desc ), PrimitiveUtility::Desc );
    m_LightDebugPSO.SetVertexShader( MY_SHADER_ARGS( g_pDeferredLightingVS ) );
    m_LightDebugPSO.SetPixelShader( MY_SHADER_ARGS( g_pDeferredLightingDebugPS ) );
    m_LightDebugPSO.SetRasterizerState( RasterizerWireframe );
    m_LightDebugPSO.SetDepthStencilState( DepthStateReadWrite );
    m_LightDebugPSO.Finalize();

	// Pipeline for deferred lighting (stage 1 to determine lit pixels)
    m_Lighting1PSO.SetInputLayout( _countof( PrimitiveUtility::Desc ), PrimitiveUtility::Desc );
    m_Lighting1PSO.SetVertexShader( MY_SHADER_ARGS( g_pDeferredLightingVS ) );
    m_Lighting1PSO.SetRasterizerState( RasterizerDefault );
    m_Lighting1PSO.SetDepthStencilState( DepthStateReadOnlyReversed );
    m_Lighting1PSO.Finalize();

    D3D11_RASTERIZER_DESC raster2 = RasterizerDefault;
    raster2.CullMode = D3D11_CULL_FRONT;
    raster2.DepthClipEnable = FALSE;

    // Setup depth mode
    // Disable depth writes
    D3D11_DEPTH_STENCIL_DESC depth2 = DepthStateReadOnly;
	depth2.DepthFunc = Math::g_ReverseZ ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_GREATER_EQUAL;

    // Pipeline for deferred lighting (stage 2 to render lit pixels)
    m_Lighting2PSO = m_Lighting1PSO;
    m_Lighting2PSO.SetPixelShader( MY_SHADER_ARGS( g_pDeferredLightingPS ) );
    m_Lighting2PSO.SetRasterizerState( raster2 );
    // Perform additive blending if a pixel passes the depth/stencil tests.
    m_Lighting2PSO.SetBlendState( BlendAdditive );
    m_Lighting2PSO.SetDepthStencilState( depth2 );
    m_Lighting2PSO.Finalize();

    m_DirectionalLightPSO = m_Lighting2PSO;
    m_DirectionalLightPSO.SetDepthStencilState( DepthStateDisabled );
    m_DirectionalLightPSO.SetRasterizerState( RasterizerDefaultCW );
    m_DirectionalLightPSO.Finalize();
}

Matrix4 Lighting::GetLightTransfrom(const LightData& Data, const Matrix4& ViewToProj)
{
    if ( Data.Type == LightType::Directional )
        return m_FullScreenProjMatrix;

    Quaternion quat = RotationBetweenVectors( Vector3( 0, -1, 0 ), Data.DirectionWS );
    OrthogonalTransform transform( quat, Vector3( Data.PositionWS ) );

    // Compute the scale depending on the light type.
    float scaleX, scaleY, scaleZ;
    // For point lights, we want to scale the geometry by the range of the light.
    scaleX = scaleY = scaleZ = Data.Range;
    if (Data.Type == LightType::Spot)
    {
        // For spotlights, we want to scale the base of the cone by the spotlight angle.
        scaleX = scaleZ = glm::tan( glm::radians( Data.SpotlightAngle ) ) * Data.Range;
    }
    AffineTransform scale = AffineTransform::MakeScale( Vector3( scaleX, scaleY, scaleZ ) );
    return ViewToProj * AffineTransform( transform ) * scale;
}

void Lighting::RenderSubPass( GraphicsContext& gfxContext, LightType Type, const Matrix4& ViewToClip, GraphicsPSO& PSO )
{
    gfxContext.SetPipelineState( PSO );
    for (uint32_t i = 0; i < MaxLights; i++)
    {
        LightData& light = m_LightData[i];
        if (Type != light.Type)
            continue;
        __declspec(align(16)) uint32_t idx = i;
        gfxContext.SetDynamicConstantBufferView( 4, sizeof( uint32_t ), &idx, { kBindPixel } );
        __declspec(align(16)) Matrix4 model = GetLightTransfrom( light, ViewToClip );
        gfxContext.SetDynamicConstantBufferView( 2, sizeof( Matrix4 ), &model, { kBindVertex } );

        using namespace PrimitiveUtility;
        PrimtiveMeshType mesh[] = { kSphereMesh, kConeMesh, kFarClipMesh };
        PrimitiveUtility::Render( gfxContext, mesh[(int)light.Type] );
    }
}


void Lighting::Render( std::shared_ptr<Scene>& scene, RenderArgs& args )
{
    GraphicsContext& gfxContext = args.gfxContext;

    gfxContext.SetDynamicDescriptor( 60, Lighting::m_LightBuffer.GetSRV(), { kBindPixel } );

#define DEFERRED 0
#if DEFERRED
    {
        ScopedTimer _prof( L"Geometry Pass", gfxContext );
        gfxContext.ClearColor( m_NormalTexture );
        gfxContext.ClearColor( m_SpecularPowerTexture );
        D3D11_RTV_HANDLE rtvs[] = {
            m_NormalTexture.GetRTV(),
            m_SpecularPowerTexture.GetRTV(),
        };
        gfxContext.SetRenderTargets( _countof( rtvs ), rtvs, g_SceneDepthBuffer.GetDSV() );
        OpaquePass gbuffer( kRenderQueueDeferredGBuffer );
        scene->Render( gbuffer, args );
        D3D11_RTV_HANDLE nullrtvs[_countof( rtvs )] = { nullptr, };
        gfxContext.SetRenderTargets( _countof( rtvs ), nullrtvs, nullptr );
    }
    {
        ScopedTimer _prof( L"Lighting Pass", gfxContext );
	    gfxContext.ClearColor( m_DiffuseTexture );
	    gfxContext.ClearColor( m_SpecularTexture );
        struct ScreenToViewParams
        {
            Matrix4 InverseProjectionMatrix;
            Vector4 ScreenDimensions;
        } psScreenToView;
        psScreenToView.InverseProjectionMatrix = Invert( args.m_ProjMatrix );
        psScreenToView.ScreenDimensions = Vector4( args.m_MainViewport.Width, args.m_MainViewport.Height, 0, 0 );
        gfxContext.SetDynamicConstantBufferView( 3, sizeof( psScreenToView ), &psScreenToView, { kBindPixel } );
        D3D11_SRV_HANDLE srvs[] = {
            m_NormalTexture.GetSRV(),
            m_SpecularPowerTexture.GetSRV(),
            g_SceneDepthBuffer.GetDepthSRV(),
        };
        gfxContext.SetDynamicDescriptors( 0, _countof( srvs ), srvs, { kBindPixel } );
        D3D11_RTV_HANDLE rtvs[] = {
            m_DiffuseTexture.GetRTV(),
            m_SpecularTexture.GetRTV(),
        };
        gfxContext.SetRenderTargets( _countof( rtvs ), rtvs, g_SceneDepthBuffer.GetDSV_DepthReadOnly() );
        Matrix4 ViewToClip = args.m_ProjMatrix*args.m_ViewMatrix;
        RenderSubPass( gfxContext, LightType::Point, ViewToClip, m_Lighting1PSO );
        RenderSubPass( gfxContext, LightType::Spot, ViewToClip, m_Lighting1PSO );
        RenderSubPass( gfxContext, LightType::Point, ViewToClip, m_Lighting2PSO );
        RenderSubPass( gfxContext, LightType::Spot, ViewToClip, m_Lighting2PSO );
        RenderSubPass( gfxContext, LightType::Directional, ViewToClip, m_DirectionalLightPSO );

        D3D11_RTV_HANDLE nullrtvs[_countof( rtvs )] = { nullptr, };
        gfxContext.SetRenderTargets( _countof( rtvs ), nullrtvs, nullptr );
        gfxContext.SetDynamicDescriptor( 2, nullptr, { kBindPixel } );
    }
    {
        ScopedTimer _prof( L"Final Pass", gfxContext );
        D3D11_SRV_HANDLE srvs[] = {
            m_DiffuseTexture.GetSRV(),
            m_SpecularTexture.GetSRV(),
            m_NormalTexture.GetSRV(),
            g_ShadowBuffer.GetSRV(),
        };
        gfxContext.SetDynamicDescriptors( 4, _countof( srvs ), srvs, { kBindPixel } );
        gfxContext.SetRenderTarget( g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly() );
        OpaquePass deferredFinal( kRenderQueueDeferredFinal );
        scene->Render( deferredFinal, args );

        if (s_bLightBoundary)
        {
            gfxContext.SetRenderTarget( g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV() );
            Matrix4 ViewToClip = args.m_ProjMatrix*args.m_ViewMatrix;
            RenderSubPass( gfxContext, LightType::Point, ViewToClip, m_LightDebugPSO );
            RenderSubPass( gfxContext, LightType::Spot, ViewToClip, m_LightDebugPSO );
        }
    }
#endif
    {
        ScopedTimer _prof( L"Forward Pass", gfxContext );
        gfxContext.SetRenderTarget( g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV() );
    #if !DEFERRED
        OpaquePass opaque( kRenderQueueOpaque );
        scene->Render( opaque, args );
    #endif
        scene->Render( m_TransparentPass, args );
    }
    {
        ScopedTimer _prof( L"Outline Pass", gfxContext );
        scene->Render( m_OutlinePass, args );
    }
}

void Lighting::Shutdown( void )
{
    m_LightBuffer.Destroy();

    m_DiffuseTexture.Destroy();
    m_SpecularTexture.Destroy();
    m_NormalTexture.Destroy();
    m_SpecularPowerTexture.Destroy();
}

void Lighting::UpdateLights( const BaseCamera& C )
{
    const Matrix4& View = C.GetViewMatrix();
    for (uint32_t n = 0; n < MaxLights; n++)
    {
        auto& light = m_LightData[n];
        light.PositionVS = View * light.PositionWS;
        light.DirectionVS = View.Get3x3() * light.DirectionWS;
    }
    m_LightBuffer.Create(L"m_LightBuffer", MaxLights, sizeof(LightData), m_LightData);
}
*/
