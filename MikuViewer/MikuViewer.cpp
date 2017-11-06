#include "GameCore.h"
#include "GraphicsCore.h"
#include "PipelineState.h"
#include "CommandContext.h"
#include "GpuBuffer.h"
#include "InputLayout.h"
#include "ColorBuffer.h"
#include "DepthBuffer.h"
#include "ShadowBuffer.h"
#include "ConstantBuffer.h"
#include "TemporalEffects.h"
#include "TextureManager.h"
#include "BufferManager.h"
#include "MotionBlur.h"
#include "DepthOfField.h"
#include "ShadowCamera.h"
#include "MikuCamera.h"
#include "MikuCameraController.h"
#include "CameraController.h"
#include "GameInput.h"
#include "Motion.h"
#include "FXAA.h"
#include "DebugHelper.h"
#include "MikuModel.h"
#include "GroundPlane.h"

#include "Math/BoundingBox.h"
#include "OrthographicCamera.h"

#include "CompiledShaders/MikuModelVS.h"
#include "CompiledShaders/MikuModelPS.h"
#include "CompiledShaders/DepthViewerVS.h"
#include "CompiledShaders/DepthViewerPS.h"
#include "CompiledShaders/GroundPlaneVS.h"
#include "CompiledShaders/GroundPlanePS.h"

using namespace DirectX;
using namespace GameCore;
using namespace Graphics;
using namespace Math;

#ifndef SAFE_DELETE
#define SAFE_DELETE(x) { delete x; x = nullptr; }
#endif

namespace Pmd {
	std::vector<InputDesc> InputDescriptor
	{
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXTURE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BONE_ID", 0, DXGI_FORMAT_R16G16_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BONE_WEIGHT", 0, DXGI_FORMAT_R8_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "EDGE_FLAT", 0, DXGI_FORMAT_R8_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
}

enum { kShadowSplit = 4 };

namespace Lighting
{
    enum { kMaxLights = 4 };
    enum { kShadowDim = 512 };
    ColorBuffer m_LightShadowArray;
    ShadowBuffer m_LightShadowTempBuffer;

    void Initialize();
    void Shutdown();
}

void Lighting::Initialize( void )
{
    m_LightShadowArray.CreateArray(L"m_LightShadowArray", kShadowDim, kShadowDim, kMaxLights, DXGI_FORMAT_R16_UNORM);
    m_LightShadowTempBuffer.Create(L"m_LightShadowTempBuffer", kShadowDim, kShadowDim);
}

void Lighting::Shutdown( void )
{
    m_LightShadowArray.Destroy();
    m_LightShadowTempBuffer.Destroy();
}

class MikuViewer : public GameCore::IGameApp
{
public:
    MikuViewer();

	virtual void Startup( void ) override;
	virtual void Cleanup( void ) override;

	virtual void Update( float deltaT ) override;
	virtual void RenderScene( void ) override;

    void RenderUI( GraphicsContext& Context ) override;

private:

    BoundingBox GetBoundingBox();
    void RenderObjects( GraphicsContext& gfxContext, const Matrix4& ViewProjMat, eObjectFilter Filter );
    void RenderObjects( GraphicsContext& gfxContext, const Matrix4 & ViewMat, const Matrix4 & ProjMat, eObjectFilter Filter );
    void RenderLightShadows(GraphicsContext& gfxContext);
    void RenderShadowMap(GraphicsContext& gfxContext);
    BaseCamera* SelectedCamera();

    Camera m_SecondCamera;
	CameraController* m_pCameraController;
	CameraController* m_pSecondCameraController;

    Matrix4 m_ViewMatrix;
    Matrix4 m_ProjMatrix;

	D3D11_VIEWPORT m_MainViewport;
	float m_JitterDelta[2];
	D3D11_RECT m_MainScissor;

    Vector3 m_SunDirection;
    Vector3 m_SunColor;
    ShadowCamera m_SunShadow;

    using IRenderObjectPtr = std::shared_ptr<Graphics::IRenderObject>;
    std::vector<IRenderObjectPtr> m_Models;
	Graphics::Motion m_Motion;

	GraphicsPSO m_DepthPSO;
    GraphicsPSO m_CutoutDepthPSO;
    GraphicsPSO m_ShadowPSO;
    GraphicsPSO m_CutoutShadowPSO;
	GraphicsPSO m_OpaquePSO;
	GraphicsPSO m_BlendPSO;
	GraphicsPSO m_GroundPlanePSO;

    std::vector<Matrix4> m_ShadowViewProj;
    std::vector<FrustumCorner> m_SplitFrustum;
    std::vector<FrustumCorner> m_ViewFrustum;
};

CREATE_APPLICATION( MikuViewer )

NumVar m_Frame( "Application/Animation/Frame", 0, 0, 1e5, 1 );

enum { kCameraMain, kCameraVirtual };
const char* CameraNames[] = { "CameraMain", "CameraVirtual" };
EnumVar m_CameraType("Application/Camera/Camera Type", kCameraVirtual, kCameraVirtual+1, CameraNames );
BoolVar m_bDebugTexture("Application/Camera/Debug Texture", true);
BoolVar m_bViewSplit("Application/Camera/View Split", false);
BoolVar m_bShadowSplit("Application/Camera/Shadow Split", false);
BoolVar m_bFixDepth("Application/Camera/Fix Depth", false);
BoolVar m_bLightFrustum("Application/Camera/Light Frustum", false);
BoolVar m_bStabilizeCascades("Application/Camera/Stabilize Cascades", false);

ExpVar m_SunLightIntensity("Application/Lighting/Sun Light Intensity", 4.0f, 0.0f, 16.0f, 0.1f);
ExpVar m_AmbientIntensity("Application/Lighting/Ambient Intensity", 0.1f, -16.0f, 16.0f, 0.1f);
NumVar m_ShadowDimX("Application/Lighting/Shadow Dim X", 10, 1, 100, 1 );
NumVar m_ShadowDimY("Application/Lighting/Shadow Dim Y", 10, 1, 100, 1 );
NumVar m_ShadowDimZ("Application/Lighting/Shadow Dim Z", 10, 1, 100, 1 );

NumVar m_SunDirX("Application/Lighting/Sun Dir X", -0.5f, -1.0f, 1.0f, 0.1f );
NumVar m_SunDirY("Application/Lighting/Sun Dir Y", -1.0f, -1.0f, 1.0f, 0.1f );
NumVar m_SunDirZ("Application/Lighting/Sun Dir Z", 0.5f, -1.0f, 1.0f, 0.1f );
NumVar m_SunColorR("Application/Lighting/Sun Color R", 157.f, 0.0f, 255.0f, 1.0f );
NumVar m_SunColorG("Application/Lighting/Sun Color G", 157.f, 0.0f, 255.0f, 1.0f );
NumVar m_SunColorB("Application/Lighting/Sun Color B", 157.f, 0.0f, 255.0f, 1.0f );

BoolVar ShowWaveTileCounts("Application/Forward+/Show Wave Tile Counts", false);
#ifdef _WAVE_OP
BoolVar EnableWaveOps("Application/Forward+/Enable Wave Ops", true);
#endif

MikuViewer::MikuViewer() : m_pCameraController( nullptr ), m_pSecondCameraController( nullptr )
{
}

void MikuViewer::Startup( void )
{
	TextureManager::Initialize( L"Textures" );
    Lighting::Initialize();

    MikuModel::Initialize();

    const Vector3 eye = Vector3(0.0f, 20.0f, -20.0f);
    m_SecondCamera.SetEyeAtUp( eye, Vector3(0.0, 20.f, 0.f), Vector3(kYUnitVector) );
    m_SecondCamera.SetPerspectiveMatrix( XM_PIDIV4, 9.0f/16.0f, 1.0f, 20000.0f );
    m_SecondCamera.Update();

    struct ModelInit
    {
        std::wstring Model;
        std::wstring Motion;
        XMFLOAT3 Position;
    };

    auto motionPath = L"";
    std::vector<ModelInit> list = {
        { L"Models/Lat0.pmd", motionPath, XMFLOAT3( 0.f, 0.f, 10.f ) },
        { L"Models/GUMIβ版修正.pmd", motionPath, XMFLOAT3( 0.f, 0.f, 0.f ) },
        // { L"Models/Library.pmd", L"", XMFLOAT3( 0.f, 1.f, 0.f ) },
#ifndef _DEBUG
        { L"Models/Library.pmd", L"", XMFLOAT3( 0.f, 1.f, 0.f ) },
        { L"Models/Library.pmd", L"", XMFLOAT3( 0.f, 1.f, 0.f ) },
        { L"Models/m_GUMI.zip", motionPath, XMFLOAT3( 10.f, 0.f, 0.f ) },
        { L"Models/Lat式ミクVer2.31_White.pmd", motionPath, XMFLOAT3( -11.f, 10.f, -19.f ) },
        { L"Models/Lat式ミクVer2.31_Normal.pmd", motionPath, XMFLOAT3( 0.f, 0.f, 10.f ) },
#endif
    };

    for (auto l : list)
    {
        auto model = std::make_shared<Graphics::MikuModel>(false);
        model->SetModel( l.Model );
        model->SetMotion( l.Motion );
        model->SetPosition( l.Position );
        model->Load();
        m_Models.push_back( model );
    }
#ifdef _DEBUG
    m_Models.emplace_back( std::make_shared<Graphics::GroundPlane>() );
#endif

	// Depth-only (2x rate)
	m_DepthPSO.SetRasterizerState( RasterizerDefaultCW );
	m_DepthPSO.SetBlendState( BlendNoColorWrite );
	m_DepthPSO.SetInputLayout( static_cast<UINT>(Pmd::InputDescriptor.size()), Pmd::InputDescriptor.data() );
	m_DepthPSO.SetPrimitiveTopologyType( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    m_DepthPSO.SetDepthStencilState( DepthStateReadWrite );
    m_DepthPSO.SetVertexShader( MY_SHADER_ARGS( g_pDepthViewerVS ) );
    m_DepthPSO.Finalize();

    // Depth-only shading but with alpha testing
    m_CutoutDepthPSO = m_DepthPSO;
    m_CutoutDepthPSO.SetPixelShader( MY_SHADER_ARGS( g_pDepthViewerPS ) );
    m_CutoutDepthPSO.SetRasterizerState(RasterizerTwoSided);
    m_CutoutDepthPSO.Finalize();

    // Depth-only but with a depth bias and/or render only backfaces
    m_ShadowPSO = m_DepthPSO;
    m_ShadowPSO.SetRasterizerState( RasterizerShadowTwoSided );
    m_ShadowPSO.Finalize();

    // Shadows with alpha testing
    m_CutoutShadowPSO = m_ShadowPSO;
    m_CutoutShadowPSO.SetPixelShader( MY_SHADER_ARGS( g_pDepthViewerPS ) );
    m_CutoutShadowPSO.SetRasterizerState(RasterizerShadowTwoSided);
    m_CutoutShadowPSO.Finalize();

	m_GroundPlanePSO = m_DepthPSO;
	m_GroundPlanePSO.SetInputLayout( static_cast<UINT>(Graphics::GroundPlanInputDesc.size()), Graphics::GroundPlanInputDesc.data() );
	m_GroundPlanePSO.SetBlendState( BlendDisable );
	m_GroundPlanePSO.SetRasterizerState( RasterizerTwoSided );
	m_GroundPlanePSO.SetVertexShader( MY_SHADER_ARGS( g_pGroundPlaneVS) );
	m_GroundPlanePSO.SetPixelShader( MY_SHADER_ARGS( g_pGroundPlanePS ) );
	m_GroundPlanePSO.Finalize();

	m_OpaquePSO = m_DepthPSO;
	m_OpaquePSO.SetBlendState( BlendDisable );
	m_OpaquePSO.SetRasterizerState( RasterizerDefaultCW );
	m_OpaquePSO.SetVertexShader( MY_SHADER_ARGS( g_pMikuModelVS ) );
	m_OpaquePSO.SetPixelShader( MY_SHADER_ARGS( g_pMikuModelPS ) );
	m_OpaquePSO.Finalize();

	m_BlendPSO = m_OpaquePSO;
	m_BlendPSO.SetBlendState( BlendTraditional );
	m_BlendPSO.Finalize();

	m_pCameraController = new CameraController(m_SunShadow, Vector3(kYUnitVector));
	m_pSecondCameraController = new CameraController(m_SecondCamera, Vector3(kYUnitVector));

    m_ShadowViewProj.resize( kShadowSplit );
    m_ViewFrustum.resize( kShadowSplit );
    m_SplitFrustum.resize( kShadowSplit );

    MotionBlur::Enable = true;
    TemporalEffects::EnableTAA = false;
    FXAA::Enable = true;
    // PostEffects::EnableHDR = true;
    // PostEffects::EnableAdaptation = true;
    // SSAO::Enable = true;
}

void MikuViewer::Cleanup( void )
{
    m_Models.clear();
    MikuModel::Shutdown();

    Lighting::Shutdown();

	m_DepthPSO.Destroy();
    m_CutoutDepthPSO.Destroy();
    m_ShadowPSO.Destroy();
    m_CutoutShadowPSO.Destroy();
	m_OpaquePSO.Destroy();
	m_BlendPSO.Destroy();
    m_GroundPlanePSO.Destroy();

    SAFE_DELETE( m_pCameraController );
    SAFE_DELETE( m_pSecondCameraController );
}

namespace Graphics
{
	extern EnumVar DebugZoom;
}

namespace GameCore
{
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
	extern HWND g_hWnd;
#else
	extern Platform::Agile<Windows::UI::Core::CoreWindow> g_window;
#endif
}

BaseCamera* MikuViewer::SelectedCamera()
{
    if (m_CameraType == kCameraVirtual)
        return &m_SecondCamera;
    else
        return &m_SunShadow;
}

void MikuViewer::Update( float deltaT )
{
    using namespace Lighting;

    ScopedTimer _prof( L"Update" );

	if (GameInput::IsFirstPressed(GameInput::kLShoulder))
		DebugZoom.Decrement();
	else if (GameInput::IsFirstPressed(GameInput::kRShoulder))
		DebugZoom.Increment();

	// We use viewport offsets to jitter our color samples from frame to frame (with TAA.)
	// D3D has a design quirk with fractional offsets such that the implicit scissor
	// region of a viewport is floor(TopLeftXY) and floor(TopLeftXY + WidthHeight), so
	// having a negative fractional top left, e.g. (-0.25, -0.25) would also shift the
	// BottomRight corner up by a whole integer.  One solution is to pad your viewport
	// dimensions with an extra pixel.  My solution is to only use positive fractional offsets,
	// but that means that the average sample position is +0.5, which I use when I disable
	// temporal AA.
    TemporalEffects::GetJitterOffset(m_MainViewport.TopLeftX, m_MainViewport.TopLeftY);

	m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
	m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;

	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
	m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();

    if (!EngineProfiling::IsPaused())
        m_Frame = m_Frame + deltaT * 30.f;

    m_pSecondCameraController->Update( deltaT );

	m_ViewMatrix = SelectedCamera()->GetViewMatrix();
    m_ProjMatrix = SelectedCamera()->GetProjMatrix();

    m_SunDirection = Vector3( m_SunDirX, m_SunDirY, m_SunDirZ );
    m_SunColor = Vector3( m_SunColorR, m_SunColorG, m_SunColorB );
}

void MikuViewer::RenderObjects( GraphicsContext& gfxContext, const Matrix4& ViewMat, const Matrix4& ProjMat, eObjectFilter Filter )
{
    struct VSConstants
    {
        Matrix4 view;
        Matrix4 projection;
        Matrix4 shadow;
    } vsConstants;
    vsConstants.view = ViewMat;
    vsConstants.projection = ProjMat;

    auto T = Matrix4( AffineTransform( Matrix3::MakeScale( 0.5f, -0.5f, 1.0f ), Vector3( 0.5f, 0.5f, 0.0f ) ) );
    vsConstants.shadow = T * m_SunShadow.GetViewProjMatrix();

	gfxContext.SetDynamicConstantBufferView( 0, sizeof(vsConstants), &vsConstants, { kBindVertex } );

    for (auto& model : m_Models)
        model->Draw( gfxContext, Filter );
}

BoundingBox MikuViewer::GetBoundingBox()
{
    Vector3 minVec( FLT_MAX ), maxVec( FLT_MIN );
    for (auto& model : m_Models)
    {
        auto bound = model->GetBoundingBox();
        minVec = Min(bound.GetMin(), minVec);
        maxVec = Max(bound.GetMax(), maxVec);
    }
    return BoundingBox( minVec, maxVec );
}

void MikuViewer::RenderObjects( GraphicsContext& gfxContext, const Matrix4& ViewProjMat, eObjectFilter Filter )
{
    RenderObjects( gfxContext, ViewProjMat, Matrix4(kIdentity), Filter );
}

void MikuViewer::RenderLightShadows( GraphicsContext& gfxContext )
{
    using namespace Lighting;

    ScopedTimer _prof(L"RenderLightShadows", gfxContext);

    static uint32_t LightIndex = 0;
    if (LightIndex >= kMaxLights)
        return;

    m_LightShadowTempBuffer.BeginRendering(gfxContext);
    {
        gfxContext.SetPipelineState(m_ShadowPSO);
        gfxContext.SetPipelineState(m_CutoutShadowPSO);
    }
    m_LightShadowTempBuffer.EndRendering(gfxContext);

    gfxContext.CopySubresource(m_LightShadowArray, LightIndex, m_LightShadowTempBuffer, 0);

    ++LightIndex;
}

// Makes the "global" shadow matrix used as the reference point for the cascades
Matrix4 MakeGlobalShadowMatrix(const BaseCamera& camera, Vector3 LightDirection)
{
    // Get the 8 points of the view frustum in world space
    Vector3 frustumCorners[8] =
    {
        Vector3(-1.0f, -1.0f, 1.0f),
        Vector3(-1.0f,  1.0f, 1.0f),
        Vector3( 1.0f, -1.0f, 1.0f),
        Vector3( 1.0f,  1.0f, 1.0f),
        Vector3(-1.0f, -1.0f, 0.0f),
        Vector3(-1.0f,  1.0f, 0.0f),
        Vector3( 1.0f, -1.0f, 0.0f),
        Vector3( 1.0f,  1.0f, 0.0f),
    };

    const Matrix4& invViewProj = camera.GetClipToWorld();
    Vector3 frustumCenter = 0.0f;
    for(uint32_t i = 0; i < 8; ++i)
    {
        frustumCorners[i] = invViewProj.Transform( frustumCorners[i] );
        frustumCenter += frustumCorners[i];
    }

    frustumCenter /= Scalar(8.0f);

    // Pick the up vector to use for the light camera
    Vector3 upDir = camera.GetRightVec();

    // This needs to be constant for it to be stable
    if (m_bStabilizeCascades)
        upDir = Vector3(0.0f, 1.0f, 0.0f);

    // Get position of the shadow camera
    Vector3 shadowCameraPos = frustumCenter + LightDirection * 0.5f;

    // Come up with a new orthographic camera for the shadow caster
    OrthographicCamera lightCamera;
    lightCamera.SetOrthographic( -0.5f, 0.5f, -0.5f, 0.5f, 0.0f, 1.0f );
    lightCamera.SetEyeAtUp( shadowCameraPos, frustumCenter, upDir );
    lightCamera.Update();

    Matrix4 Tex = AffineTransform( Matrix3::MakeScale( 0.5f, -0.5f, 1.0f ), Vector3( 0.5f, 0.5f, 0.0f ) );
    return Tex * lightCamera.GetViewProjMatrix();
}

BoolVar m_bShadowBound("Application/Camera/Shadow Bound", true);

void MikuViewer::RenderShadowMap( GraphicsContext& gfxContext )
{
    ScopedTimer _prof( L"Render Shadow Map", gfxContext );

    Vector3 minVec( FLT_MAX ), maxVec( FLT_MIN );
    if (m_bShadowBound)
    {
        for (auto& model : m_Models)
        {
            auto bound = model->GetBoundingBox();
            minVec = Min(bound.GetMin(), minVec);
            maxVec = Max(bound.GetMax(), maxVec);
        }
        m_ShadowDimX = maxVec.GetX(), m_ShadowDimY = maxVec.GetY(), m_ShadowDimZ = maxVec.GetZ();
    }
    else
    {
        minVec = -Vector3( m_ShadowDimX, m_ShadowDimY, m_ShadowDimZ );
        maxVec = -minVec;
    }

    // caster and reciver use same sized box
    BoundingBox frustumAABB( minVec, maxVec );
    BoundingBox casterAABB( minVec, maxVec );
    Vector3 m_lightDir = Normalize(-m_SunDirection);
    Vector3 eyeLightDir = m_ViewMatrix.Get3x3() * m_lightDir;

    frustumAABB = m_ViewMatrix * frustumAABB;
    casterAABB = m_ViewMatrix * casterAABB;

    //  light pt is "infinitely" far away from the view frustum.
    //  however, all that's really needed is to place it just outside of all shadow casters
    Vector3 frustumCenter = frustumAABB.GetCenter();
    float t;
    casterAABB.Intersect( &t, frustumCenter, eyeLightDir );

    Vector3 lightPt = frustumCenter + 2.f*t*eyeLightDir;
    const Vector3 yAxis(0.f, 1.f, 0.f);
    const Vector3 zAxis(0.f, 0.f, 1.f);
    Vector3 axis;
    if (fabsf( Dot( eyeLightDir, yAxis ) ) > 0.99f)
        axis = zAxis;
    else
        axis = yAxis;

    Matrix4 lightView = Matrix4(XMMatrixLookAtLH( lightPt, frustumCenter, axis ));
    frustumAABB = lightView * frustumAABB;
    casterAABB = lightView * casterAABB;

    //  use a small fudge factor for the near plane, to avoid some minor clipping artifacts
    Matrix4 lightProj = Matrix4( DirectX::XMMatrixOrthographicOffCenterLH(
        frustumAABB.m_Min.GetX(), frustumAABB.m_Max.GetX(),
        frustumAABB.m_Min.GetY(), frustumAABB.m_Max.GetY(),
        casterAABB.m_Min.GetZ(), frustumAABB.m_Max.GetZ() ) );
    lightView = lightView * m_ViewMatrix;
    m_SunShadow.SetViewProjectMatrix( lightView, lightProj );

    g_ShadowBuffer.BeginRendering( gfxContext );
    gfxContext.SetPipelineState( m_ShadowPSO );
    RenderObjects( gfxContext, m_SunShadow.GetViewProjMatrix(), kOpaque );
    RenderObjects( gfxContext, m_SunShadow.GetViewProjMatrix(), kTransparent );
    g_ShadowBuffer.EndRendering( gfxContext );
}

void MikuViewer::RenderScene( void )
{
    using namespace Lighting;

	GraphicsContext& gfxContext = GraphicsContext::Begin( L"Scene Render" );

    uint32_t FrameIndex = TemporalEffects::GetFrameIndexMod2();
    (FrameIndex);

    __declspec(align(16)) struct
    {
        Vector3 LightDirection;
        Vector3 LightColor;
        float ShadowTexelSize[4];
    } psConstants;

    psConstants.LightDirection = m_SecondCamera.GetViewMatrix().Get3x3() * m_SunDirection;
    psConstants.LightColor = m_SunColor / Vector3( 255.f, 255.f, 255.f );
    psConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
    psConstants.ShadowTexelSize[1] = 1.0f / g_ShadowBuffer.GetHeight();
	gfxContext.SetDynamicConstantBufferView( 1, sizeof(psConstants), &psConstants, { kBindPixel } );

    D3D11_SAMPLER_HANDLE Sampler[] = { SamplerLinearWrap, SamplerLinearClamp, SamplerShadow };
    gfxContext.SetDynamicSamplers( 0, _countof(Sampler), Sampler, { kBindPixel } );

    // RenderLightShadows(gfxContext);
    RenderShadowMap(gfxContext);

	gfxContext.SetDynamicConstantBufferView( 1, sizeof(psConstants), &psConstants, { kBindPixel } );

    gfxContext.ClearColor( g_SceneColorBuffer );
    gfxContext.ClearDepth( g_SceneDepthBuffer );
    {
        ScopedTimer _prof( L"Render Color", gfxContext );

        gfxContext.SetDynamicDescriptor( 4, g_ShadowBuffer.GetSRV(), { kBindPixel } );
        gfxContext.SetViewportAndScissor( m_MainViewport, m_MainScissor );
        gfxContext.SetRenderTarget( g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV() );
        gfxContext.SetPipelineState( m_OpaquePSO );
        RenderObjects( gfxContext, m_ViewMatrix, m_ProjMatrix, kOpaque );
        RenderObjects( gfxContext, m_ViewMatrix, m_ProjMatrix, kOverlay );
        gfxContext.SetPipelineState( m_BlendPSO );
        RenderObjects( gfxContext, m_ViewMatrix, m_ProjMatrix, kTransparent );
        gfxContext.SetPipelineState( m_GroundPlanePSO );
        RenderObjects( gfxContext, m_ViewMatrix, m_ProjMatrix, kGroundPlane );
    }
    {
        ScopedTimer _prof( L"Render Frustum", gfxContext );

        const std::vector<Color> SplitColor =
        {
            Color(1.f, 0.f, 0.f, 0.36f),
            Color(0.f, 1.f, 0.f, 0.36f),
            Color(0.f, 0.f, 1.f, 0.36f),
            Color(1.f, 1.f, 0.f, 0.36f)
        };

        auto WorldToClip = m_ProjMatrix * m_ViewMatrix;
        if (m_bShadowSplit)
        {
            for (auto i = 0; i < m_ShadowViewProj.size(); i++)
                Utility::DebugCube( gfxContext, WorldToClip, m_SplitFrustum[i].data(), SplitColor[i] );
        }
        if (m_bViewSplit)
        {
            for (auto i = 0; i < m_ViewFrustum.size(); i++)
                Utility::DebugCube( gfxContext, WorldToClip, m_ViewFrustum[i].data(), SplitColor[i] );
        }

        if (m_bLightFrustum)
        {
            Matrix4 FixDepth( kIdentity );
            FixDepth.SetZ( Vector4(kZero) );
            FixDepth.SetW( Vector4(0.f, 0.f, 0.0f, 1.f) );
            auto Corners  = m_SunShadow.GetWorldSpaceFrustum().GetFrustumCorners();
            Utility::DebugCube( gfxContext, FixDepth * WorldToClip, Corners.data(), Color( 0.f, 1.f, 1.f, 0.1f ) );
        }
    }
    if (m_bDebugTexture)
    {
        ScopedTimer _prof( L"DebugTexture", gfxContext );
        Utility::DebugTexture( gfxContext, g_ShadowBuffer.GetSRV() );
    }
    gfxContext.SetRenderTarget( nullptr );
    TemporalEffects::ResolveImage(gfxContext);

	gfxContext.Finish();
}

void MikuViewer::RenderUI( GraphicsContext& Context )
{
	auto Pos = SelectedCamera()->GetPosition();
	auto x = (float)g_SceneColorBuffer.GetWidth() - 400.f;
    float px = Pos.GetX(), py = Pos.GetY(), pz = Pos.GetZ();

	TextContext Text(Context);
	Text.Begin();
	Text.ResetCursor( x, 10.f );
    Text.DrawFormattedString( "Position: (%.1f, %.1f, %.1f)\n", px, py, pz );
	Text.End();
}