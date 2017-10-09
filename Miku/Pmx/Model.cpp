﻿#include "Model.h"

#include <string>
#include <vector>
#include <map>
#include <DirectXMath.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

#include "Archive.h"
#include "InputLayout.h"
#include "TextureManager.h"
#include "FileUtility.h"
#include "Utility.h"
#include "Mapping.h"
#include "Encoding.h"
#include "ModelBase.h"
#include "CommandContext.h"
#include "..\Pmd\Model.h"

using namespace DirectX;
using namespace Graphics;
using namespace Graphics::Pmx;

template <typename T>
size_t GetVectorSize(const std::vector<T>& vec)
{
    return sizeof(T) * vec.size();
}

bool Mesh::SetTexture( GraphicsContext& gfxContext )
{
    D3D11_SRV_HANDLE SRV[kTextureMax] = { nullptr };
    for (auto i = 0; i < _countof( Texture ); i++)
    {
        if (Texture[i] == nullptr) continue;
        SRV[i] = Texture[i]->GetSRV();
    }
    gfxContext.SetDynamicDescriptors( 1, _countof(SRV), SRV, { kBindPixel } );
    return false;
}

Model::Model( bool bRightHand ) : m_bRightHand( bRightHand ), m_ModelTransform(kIdentity)
{
}

Model::~Model()
{
    Clear();
}

bool Model::LoadModel( ArchivePtr& Archive, Path& FilePath )
{
    using namespace ::Pmx;

	auto ba = Archive->GetFile( FilePath );
    Utility::ByteStream bs( ba );

	PMX pmx;
	pmx.Fill( bs, m_bRightHand );
    if (!pmx.IsValid())
        return false;

	//
	// raw: decode with system default.
	//      if system default is not shift-jis it will display with corrupted charactor.
	//      but, it will also happens when extract from zip archive. so, can find the file
	// unicode: decode with shift-jis
	//
	auto LoadTexture = []( ArchivePtr& archive, std::wstring unicode, bool bSRGB )
		-> const ManagedTexture*
	{
		//
		// zip format does not support unicode.
		// So, it is convenient to use unified ecoding, system default
        //
        const ManagedTexture* texture = nullptr;

		// Try raw name
        Path name = unicode;
        bool bExist = archive->IsExist( name );
        if (bExist)
        {
            auto ia = archive->GetFile( name );
            texture = TextureManager::LoadFromMemory( archive->GetKeyName(name).generic_wstring(), ia, bSRGB );
        }
        else
        {
            // If not, try default provided texture in MMD (toon01.bmp)
            auto toon = Path( "toon" ) / name;
            texture = TextureManager::LoadFromFile( toon.generic_wstring(), bSRGB );
        }
		return texture;
	};

	struct Attribute
	{
		XMFLOAT3 Normal;
		XMFLOAT2 UV;
        uint32_t BoneID[4] = {0, };
        float    Weight[4] = {0.f };
		float    EdgeSize;
	};

	std::vector<Attribute> attributes( pmx.m_Vertices.size() );
	m_VertexPos.resize( pmx.m_Vertices.size() );
	for (auto i = 0; i < pmx.m_Vertices.size(); i++)
	{
		m_VertexPos[i] = pmx.m_Vertices[i].Pos;
		attributes[i].Normal = pmx.m_Vertices[i].Normal;
		attributes[i].UV = pmx.m_Vertices[i].UV;

        ASSERT( pmx.m_Vertices[i].SkinningType != Vertex::kQdef );

        switch (pmx.m_Vertices[i].SkinningType)
        {
        case Vertex::kBdef1:
            attributes[i].BoneID[0] = pmx.m_Vertices[i].bdef1.BoneIndex;
            attributes[i].Weight[0] = 1.f;
            break;
        case Vertex::kSdef: // ignore rotation center, r0 and r1 treat as Bdef2
        case Vertex::kBdef2:
            attributes[i].BoneID[0] = pmx.m_Vertices[i].bdef2.BoneIndex[0];
            attributes[i].BoneID[1] = pmx.m_Vertices[i].bdef2.BoneIndex[1];
            attributes[i].Weight[0] = pmx.m_Vertices[i].bdef2.Weight;
            attributes[i].Weight[1] = 1 - pmx.m_Vertices[i].bdef2.Weight;
            break;
        case Vertex::kBdef4:
            for (int k = 0; k < 4; k++)
            {
                attributes[i].BoneID[k] = pmx.m_Vertices[i].bdef4.BoneIndex[k];
                attributes[i].Weight[k] = pmx.m_Vertices[i].bdef4.Weight[k];
            }
            break;
        }
		attributes[i].EdgeSize = pmx.m_Vertices[i].EdgeSize;
	}
	m_VertexMorphedPos = m_VertexPos;

	m_Name = pmx.m_Description.Name;
    std::copy(pmx.m_Indices.begin(), pmx.m_Indices.end(), std::back_inserter(m_Indices));

	m_AttributeBuffer.Create( m_Name + L"_AttrBuf",
		static_cast<uint32_t>(attributes.size()),
		sizeof( Attribute ),
		attributes.data() );

	m_PositionBuffer.Create( m_Name + L"_PosBuf",
		static_cast<uint32_t>(m_VertexPos.size()),
		sizeof( XMFLOAT3 ),
		m_VertexPos.data() );

	m_IndexBuffer.Create( m_Name + L"_IndexBuf",
		static_cast<uint32_t>(pmx.m_Indices.size()),
		sizeof( pmx.m_Indices[0] ),
		pmx.m_Indices.data() );

	uint32_t IndexOffset = 0;
	for (auto& material : pmx.m_Materials)
	{
		Mesh mesh = {};

		MaterialCB mat = {};
		mat.Diffuse = material.Diffuse;
		mat.SpecularPower = material.SpecularPower;
		mat.Ambient = material.Ambient;

		mesh.IndexCount = material.NumVertex;
		mesh.IndexOffset = IndexOffset;
		IndexOffset += material.NumVertex;

        if (material.DiffuseTexureIndex >= 0)
			mesh.Texture[kTextureDiffuse] = LoadTexture( Archive, pmx.m_Textures[material.DiffuseTexureIndex], true );

        //
        // https://learnmmd.com/http:/learnmmd.com/pmd-editor-basics-sph-and-spa-files-add-sparkle/
        //
        // spa adds to main texture and sph multiplies.
        // However, spa does not use sRGB because it is generally
        // used as a brightness image.
        //
        //    bool sRGB = true;
        //    if (std::string::npos != material.SphereRaw.rfind(".spa"))
        //        sRGB = false;
        //
        if (material.SphereTextureIndex >= 0)
			mesh.Texture[kTextureSphere] = LoadTexture( Archive, pmx.m_Textures[material.SphereTextureIndex], true );

        std::wstring ToonName;
        if (material.bDefaultToon)
            ToonName = std::wstring(L"toon") + std::to_wstring(material.DeafultToon) + std::wstring(L".bmp");
        else if (material.Toon >= 0)
            ToonName = pmx.m_Textures[material.Toon];
        if (!ToonName.empty())
            mesh.Texture[kTextureToon] = LoadTexture( Archive, ToonName, true );

		if (mesh.Texture[kTextureToon])
			mat.bUseToon = TRUE;
		if (mesh.Texture[kTextureDiffuse])
			mat.bUseTexture = TRUE;
		if (mesh.Texture[kTextureSphere])
			mat.SphereOperation = material.SphereOperation;

		mesh.Material = mat;
		mesh.EdgeSize = material.EdgeSize;
		mesh.EdgeColor = Color(Vector4(material.EdgeColor)).FromSRGB();

        // if motion is not registered, bounding box is used to viewpoint culling
        mesh.BoundSphere = ComputeBoundingSphereFromVertices(
            m_VertexPos, m_Indices, mesh.IndexCount, mesh.IndexOffset );

		m_Mesh.push_back(mesh);
	}

	size_t numBones = pmx.m_Bones.size();
	SetBoneNum( numBones );
    ASSERT( numBones > 0 );
	for (auto i = 0; i < numBones; i++)
	{
		auto& boneData = pmx.m_Bones[i];

		m_Bones[i].Name = boneData.Name;
		m_BoneParent[i] = boneData.ParentBoneIndex;
		if (boneData.ParentBoneIndex >= 0)
			m_BoneChild[boneData.ParentBoneIndex].push_back( i );

		Vector3 origin = boneData.Position;
		Vector3 parentOrigin = Vector3( 0.0f, 0.0f, 0.0f );

		if( boneData.ParentBoneIndex >= 0)
			parentOrigin = pmx.m_Bones[boneData.ParentBoneIndex].Position;

		m_Bones[i].Translate = origin - parentOrigin;
        m_Bones[i].Position = origin;
        m_Bones[i].DestinationIndex = boneData.DestinationOriginIndex;
        m_Bones[i].DestinationOffset = boneData.DestinationOriginOffset;

		m_BoneIndex[boneData.Name] = i;
	}

    m_Skinning.resize( numBones );
    m_SkinningDual.resize( numBones );
    for ( auto i = 0; i < numBones; i++)
        m_SkinningDual[i] = OrthogonalTransform();

    for (auto i = 0; i < numBones; i++)
    {
        if (!pmx.m_Bones[i].bIK)
            continue;
        auto& it = pmx.m_Bones[i].Ik;

        IKAttr attr;
        attr.BoneIndex = i;
        attr.TargetBoneIndex = it.BoneIndex;
        attr.LimitedRadian = it.LimitedRadian;
        attr.NumIteration = it.NumIteration;

        for (auto& ik : it.Link)
        {
            IKChild child;
            child.BoneIndex = ik.BoneIndex;
            child.bLimit = ik.bLimit;
            child.MinLimit = ik.MinLimit;
            child.MaxLimit = ik.MaxLimit;
            attr.Link.push_back( child );
        }
        m_IKs.push_back( attr );
    }

    /*

	m_MorphMotions.resize( pmx.m_Faces.size() );
	for ( auto i = 0; i < pmx.m_Faces.size(); i++ )
	{
		auto& morph = pmx.m_Faces[i];
		m_MorphIndex[morph.Name] = i;
        auto numVertices = morph.FaceVertices.size();

        auto& motion = m_MorphMotions[i];
        motion.m_MorphVertices.reserve( numVertices );
        motion.m_MorphVertices.reserve( numVertices );
		for (auto& vert : morph.FaceVertices)
        {
			motion.m_MorphIndices.push_back( vert.Index );
			motion.m_MorphVertices.push_back( vert.Position );
        }
	}
    if (m_MorphMotions.size() > 0)
        m_MorphDelta.resize( m_MorphMotions[kMorphBase].m_MorphIndices.size() );
    */

    SetVisualizeSkeleton();
    SetBoundingBox();
    SetBoundingSphere();

    return true;
}

bool Model::LoadMotion( const std::wstring& motionPath )
{
	using namespace std;
	using namespace Animation;

	Utility::ByteArray ba = Utility::ReadFileSync( motionPath );
	Utility::ByteStream bs(ba);

	Vmd::VMD vmd;
	vmd.Fill( bs, m_bRightHand );
	if (!vmd.IsValid())
        return false;

    LoadBoneMotion( vmd.BoneFrames );

	for (auto& frame : vmd.FaceFrames)
	{
		MorphKeyFrame key;
		key.Frame = frame.Frame;
		key.Weight = frame.Weight;
		key.Weight = frame.Weight;

        WARN_ONCE_IF(m_MorphIndex.count(frame.FaceName) <= 0, L"Can't find target morph on model: " + m_ModelPath);
        if (m_MorphIndex.count(frame.FaceName) > 0)
        {
            auto& motion = m_MorphMotions[m_MorphIndex[frame.FaceName]];
            motion.m_Name = frame.FaceName;
            motion.InsertKeyFrame( key );
        }
	}

	for (auto& face : m_MorphMotions )
		face.SortKeyFrame();

	for (auto& frame : vmd.CameraFrames)
	{
		CameraKeyFrame keyFrame;
		keyFrame.Frame = frame.Frame;
		keyFrame.Data.bPerspective = frame.TurnOffPerspective == 0;
		keyFrame.Data.Distance = frame.Distance;
		keyFrame.Data.FovY = frame.ViewAngle / XM_PI;
		keyFrame.Data.Rotation = Quaternion( frame.Rotation.y, frame.Rotation.x, frame.Rotation.z );
		keyFrame.Data.Position = frame.Position;

		//
		// http://harigane.at.webry.info/201103/article_1.html
		//
		auto interp = reinterpret_cast<const char*>(&frame.Interpolation[0]);
		float scale = 1.0f / 127.0f;

		for (auto i = 0; i < 6; i++)
			keyFrame.BezierCoeff[i] = Vector4( interp[i], interp[i+2], interp[i+1], interp[i+3] ) * scale;

		m_CameraMotion.InsertKeyFrame( keyFrame );
	}
    return true;
}

void Model::LoadBoneMotion( const std::vector<Vmd::BoneFrame>& frames )
{
    if (frames.size() <= 0)
        return;

    int32_t numBones = static_cast<int32_t>(m_Bones.size());

    m_BoneMotions.resize( numBones );
    m_Pose.resize( numBones );
    m_LocalPose.resize( numBones );
    m_toRoot.resize( numBones );
    m_Skinning.resize( numBones );
    m_SkinningDual.resize( numBones );

    for (auto i = 0; i < numBones; i++)
    {
        auto& bone = m_Bones[i];
        auto& meshBone = m_BoneMotions[i];

        meshBone.bLimitXAngle = false;

        //
        // In PMD Model minIK, maxIK is not manually given.
        // But, bone name that contains 'knee'('ひざ') has constraint
        // that can move only in x axis and outer angle (just like human knee)
        // If this constraint is not given, knee goes forward just like
        // the following vmd motion. http://www.nicovideo.jp/watch/sm18737664
        //
        if (std::string::npos != bone.Name.find( L"ひざ" ))
            meshBone.bLimitXAngle = true;
    }

    for (auto i = 0; i < m_Bones.size(); i++)
        m_LocalPose[i].SetTranslation( m_Bones[i].Translate );

    std::vector<OrthogonalTransform> RestPose( numBones );
    for (auto i = 0; i < numBones; i++)
    {
        auto& bone = m_Bones[i];
        auto& parent = m_BoneParent[i];

        RestPose[i].SetTranslation( bone.Translate );
        if (parent >= 0)
            RestPose[i] = RestPose[parent] * RestPose[i];
    }

    for (auto i = 0; i < numBones; i++)
        m_toRoot[i] = ~RestPose[i];

	for (auto& frame : frames)
	{
		if ( m_BoneIndex.count( frame.BoneName ) == 0)
			continue;

		Vector3 BoneTranslate(m_Bones[m_BoneIndex[frame.BoneName]].Translate);

		Animation::BoneKeyFrame key;
		key.Frame = frame.Frame;
		key.Local.SetTranslation( Vector3(frame.Offset) + BoneTranslate );
		key.Local.SetRotation( Quaternion( frame.Rotation ) );

		//
		// http://harigane.at.webry.info/201103/article_1.html
		//
		// X_x1, Y_x1, Z_x1, R_x1,
		// X_y1, Y_y1, Z_y1, R_y1,
		// X_x2, Y_x2, Z_x2, R_x2,
		// X_y2, Y_y2, Z_y2, R_y2,
		//
		// ... (duplicated values)
		//
		auto interp = reinterpret_cast<const char*>(&frame.Interpolation[0]);
		float scale = 1.0f / 127.0f;

		for (auto i = 0; i < 4; i++)
			key.BezierCoeff[i] = Vector4( interp[i], interp[i+4], interp[i+8], interp[i+12] ) * scale;

		m_BoneMotions[m_BoneIndex[frame.BoneName]].InsertKeyFrame( key );
	}

	for (auto& bone : m_BoneMotions )
		bone.SortKeyFrame();
}


void Model::SetBoneNum( size_t numBones )
{
	m_BoneParent.resize( numBones );
	m_BoneChild.resize( numBones );
	m_Bones.resize( numBones );
}

void Model::SetModel( const std::wstring& model )
{
    m_ModelPath = model;
}

void Model::SetMotion( const std::wstring& motion )
{
    m_MotionPath = motion;
}

void Model::SetBoundingSphere( void )
{
    ASSERT(m_Bones.size() > 0);

    auto it = std::find_if( m_Bones.begin(), m_Bones.end(), []( const Bone& Bone ) {
        return Bone.Name.compare( L"センター" ) == 0;
    } );
    if (it == m_Bones.end())
        it = m_Bones.begin();

    Vector3 Center = it->Translate;
    Scalar Radius( 0.f );

    for (auto& vert : m_VertexPos) {
        Scalar R = LengthSquare( Center - Vector3( vert ) );
        if (ModelBase::s_bExcludeSkyBox)
            if (R > ModelBase::s_ExcludeRange*ModelBase::s_ExcludeRange)
                continue;
        Radius = Max( Radius, R );
    }
    m_BoundingSphere = BoundingSphere( Center, Sqrt(Radius) );
    m_RootBoneIndex = static_cast<uint32_t>(std::distance( m_Bones.begin(), it ));
}

void Model::SetBoundingBox( void )
{
    ASSERT(m_Bones.size() > 0);

    auto it = std::find_if( m_Bones.begin(), m_Bones.end(), [](const Bone& Bone){
        return Bone.Name.compare( L"センター" ) == 0;
    });
    if (it == m_Bones.end())
        it = m_Bones.begin();

    Vector3 Center = it->Translate;

    Vector3 MinV( FLT_MAX ), MaxV( FLT_MIN );
    for (auto& vert : m_VertexPos)
    {
        if (ModelBase::s_bExcludeSkyBox)
        {
            Scalar R = Dot( Vector3( 1.f ), Abs( Center - Vector3( vert ) ) );
            if (R > ModelBase::s_ExcludeRange)
                continue;
        }
        MinV = Min( MinV, vert );
        MaxV = Max( MaxV, vert );
    }

    m_BoundingBox = BoundingBox( MinV, MaxV );
    m_RootBoneIndex = static_cast<uint32_t>(std::distance( m_Bones.begin(), it ));
}

void Model::SetVisualizeSkeleton()
{
	auto numBone = m_Bones.size();

	m_BoneAttribute.resize( numBone );

	for ( auto i = 0; i < numBone; i++ )
	{
		auto DestinationIndex = m_Bones[i].DestinationIndex;
		Vector3 DestinationOffset = m_Bones[i].DestinationOffset;
		if (DestinationIndex >= 0)
			DestinationOffset = m_Bones[DestinationIndex].Position - m_Bones[i].Position;

		Vector3 diff = DestinationOffset;
		Scalar length = Length( diff );
		Quaternion Q = RotationBetweenVectors( Vector3( 0.0f, 1.0f, 0.0f ), diff );
		AffineTransform scale = AffineTransform::MakeScale( Vector3(0.05f, length, 0.05f) );
        // Move primitive bottom to origin
		AffineTransform alignToOrigin = AffineTransform::MakeTranslation( Vector3(0.0f, 0.5f * length, 0.0f) );
		m_BoneAttribute[i] = AffineTransform(Q, m_Bones[i].Position) * alignToOrigin * scale;
	}
}

void Model::Clear()
{
	m_AttributeBuffer.Destroy();
	m_PositionBuffer.Destroy();
	m_IndexBuffer.Destroy();
}

void Model::UpdateChildPose( int32_t idx )
{
	auto parentIndex = m_BoneParent[idx];
	if (parentIndex >= 0)
		m_Pose[idx] = m_Pose[parentIndex] * m_LocalPose[idx];

	for (auto c : m_BoneChild[idx])
		UpdateChildPose( c );
}

void Model::Update( float kFrameTime )
{
	if (m_BoneMotions.size() > 0)
	{
		size_t numBones = m_BoneMotions.size();
		for (auto i = 0; i < numBones; i++)
			m_BoneMotions[i].Interpolate( kFrameTime, m_LocalPose[i] );

		for (auto i = 0; i < numBones; i++)
		{
			auto parentIndex = m_BoneParent[i];
			if (parentIndex < numBones)
				m_Pose[i] = m_Pose[parentIndex] * m_LocalPose[i];
			else
				m_Pose[i] = m_LocalPose[i];
		}

    #if 1
		for (auto& ik : m_IKs)
            UpdateIK( ik );
    #endif

		for (auto i = 0; i < numBones; i++)
			m_Skinning[i] = m_Pose[i] * m_toRoot[i];

		for (auto i = 0; i < numBones; i++)
            m_SkinningDual[i] = m_Skinning[i];
	}

    if (m_MorphMotions.size() > 0)
	{
		//
		// http://blog.goo.ne.jp/torisu_tetosuki/e/8553151c445d261e122a3a31b0f91110
		//
        auto elemByte = sizeof( decltype(m_MorphDelta)::value_type );
        memset( m_MorphDelta.data(), 0, elemByte*m_MorphDelta.size() );

		bool bUpdate = false;
		for (auto i = kMorphBase+1; i < m_MorphMotions.size(); i++)
		{
			auto& motion = m_MorphMotions[i];
			motion.Interpolate( kFrameTime );
			if (std::fabsf( motion.m_WeightPre - motion.m_Weight ) < 0.1e-2)
				continue;
			bUpdate = true;
			auto weight = motion.m_Weight;
			for (auto k = 0; k < motion.m_MorphVertices.size(); k++)
			{
                auto idx = motion.m_MorphIndices[k];
				m_MorphDelta[idx] += weight * motion.m_MorphVertices[k];
			}
		}
		if (bUpdate)
		{
            auto& baseFace = m_MorphMotions[kMorphBase];
			for (auto i = 0; i < m_MorphDelta.size(); i++)
                m_MorphDelta[i] += baseFace.m_MorphVertices[i];

			for (auto i = 0; i < m_MorphDelta.size(); i++)
				XMStoreFloat3( &m_VertexMorphedPos[baseFace.m_MorphIndices[i]], m_MorphDelta[i]);

			m_PositionBuffer.Create( m_Name + L"_PosBuf",
				static_cast<uint32_t>(m_VertexMorphedPos.size()),
				sizeof( XMFLOAT3 ),
				m_VertexMorphedPos.data() );
		}
	}
}

//
// Solve Constrainted IK
// Cyclic-Coordinate-Descent（CCD）
//
// http://d.hatena.ne.jp/edvakf/20111102/1320268602
// Game programming gems 3 Constrained Inverse Kinematics - Jason Weber
//
void Model::UpdateIK(const IKAttr& ik)
{
	auto GetPosition = [&]( int32_t index ) -> Vector3
	{
		return Vector3(m_Pose[index].GetTranslation());
	};

	// "effector" (Fixed)
	const auto ikBonePos = GetPosition( ik.BoneIndex );

	for (int n = 0; n < ik.NumIteration; n++)
	{
		// "effected" bone list in order
		for (auto k = 0; k < ik.Link.size(); k++)
		{
			auto childIndex = ik.Link[k].BoneIndex;
			auto ikTargetBonePos = GetPosition( ik.TargetBoneIndex );
			auto invLinkMtx = Invert( m_Pose[childIndex] );

			//
			// transform to child bone's local coordinate.
			// note that even if pos is vector3 type, it is calcurated by affine tranform.
			//
			auto ikTargetVec = Vector3( invLinkMtx * ikTargetBonePos );
			auto ikBoneVec = Vector3( invLinkMtx * ikBonePos );

			auto axis = Cross( ikBoneVec, ikTargetVec );
			auto axisLen = Length( axis );
			auto sinTheta = axisLen / Length( ikTargetVec ) / Length( ikBoneVec );
			if (sinTheta < 1.0e-3f)
				continue;

			// angle to move in one iteration
			auto maxAngle = (k + 1) * ik.LimitedRadian * 4;
			auto theta = ASin( sinTheta );
			if (Dot( ikTargetVec, ikBoneVec ) < 0.f)
				theta = XM_PI - theta;
			if (theta > maxAngle)
				theta = maxAngle;

			auto rotBase = m_LocalPose[childIndex].GetRotation();
			auto translate = m_LocalPose[childIndex].GetTranslation();

			// To apply base coordinate system which it is base on, inverted theta direction
			Quaternion rotNext( axis, -theta );
			auto rotFinish = rotBase * rotNext;

			// Constraint IK, restrict rotation angle
			if (m_BoneMotions[childIndex].bLimitXAngle)
			{
#ifndef EXPERIMENT_IK
				// c = cos(theta / 2)
				auto c = XMVectorGetW( rotFinish );
				// s = sin(theta / 2)
				auto s = Sqrt( 1.0f - c*c );
				rotFinish = Quaternion( Vector4( s, 0, 0, c ) );
				if (!m_bRightHand)
				{
					auto a = -std::asin( s );
					rotFinish = Quaternion( Vector4( std::sin( a ), 0, 0, std::cos( a ) ) );
				}
#else
				//
				// MMD-Agent PMDIK
				//
				// when this is the first iteration, we force rotating to the maximum angle toward limited direction
				// this will help convergence the whole IK step earlier for most of models, especially for legs
				if (n == 0)
				{
					if (theta < 0.0f)
						theta = -theta;
					rotFinish = rotBase * Quaternion( Vector3( 1.0f, 0.f, 0.f ), theta );
				}
				else
				{
					//
					// Needed to stable IK result (esp. Ankle)
					// The value obtained from the test
					//
					const Scalar PMDMinRotX = 0.10f;
					auto next = rotNext.toEuler();
					auto base = rotBase.toEuler();

					auto sum = Clamp( next.GetX() + base.GetX(), PMDMinRotX, Scalar(XM_PI) );
					next = Vector3( sum - base.GetX(), 0.f, 0.f );
					rotFinish = rotBase * Quaternion( next.GetX(), next.GetY(), next.GetZ() );
				}
#endif
			}
			m_LocalPose[childIndex] = OrthogonalTransform( rotFinish, translate );
			UpdateChildPose( childIndex );
		}
	}
}

void Model::Draw( GraphicsContext& gfxContext, eObjectFilter Filter )
{
    if (Filter & kOverlay)
    {
        DrawBone();
        DrawBoundingSphere();
        return;
    }

#define SKINNING_LBS
#ifdef SKINNING_LBS
    std::vector<Matrix4> SkinData;
    SkinData.reserve( m_Skinning.size() );
    for (auto& orth : m_Skinning)
        SkinData.emplace_back( orth );
#else // SKINNING_DLB
    auto& SkinData = m_SkinningDual;
#endif
    auto numByte = GetVectorSize(SkinData);

    gfxContext.SetDynamicConstantBufferView( 1, numByte, SkinData.data(), { kBindVertex } );
    gfxContext.SetDynamicConstantBufferView( 2, sizeof(m_ModelTransform), &m_ModelTransform, { kBindVertex } );
	gfxContext.SetVertexBuffer( 0, m_AttributeBuffer.VertexBufferView() );
	gfxContext.SetVertexBuffer( 1, m_PositionBuffer.VertexBufferView() );
	gfxContext.SetIndexBuffer( m_IndexBuffer.IndexBufferView() );

	for (auto& mesh: m_Mesh)
	{
		bool bOpaque = Filter & kOpaque && !mesh.isTransparent();
		bool bTransparent = Filter & kTransparent && mesh.isTransparent();
		if (!bOpaque && !bTransparent)
            continue;
        if (mesh.SetTexture( gfxContext ))
            continue;

		gfxContext.SetDynamicConstantBufferView( 0, sizeof(mesh.Material), &mesh.Material, { kBindPixel } );
		gfxContext.DrawIndexed( mesh.IndexCount, mesh.IndexOffset, 0 );
	}
}

void Model::DrawBone()
{
    if (!ModelBase::s_bEnableDrawBone)
        return;
	auto numBones = m_BoneAttribute.size();
	for (auto i = 0; i < numBones; i++)
        ModelBase::Append( ModelBase::kBoneMesh, m_ModelTransform * m_Skinning[i] * m_BoneAttribute[i] );
}

void Model::DrawBoundingSphere()
{
    if (!ModelBase::s_bEnableDrawBoundingSphere)
        return;

    BoundingSphere sphere = GetBoundingSphere();
    AffineTransform scale = AffineTransform::MakeScale( float(sphere.GetRadius()) );
    AffineTransform center = AffineTransform::MakeTranslation( sphere.GetCenter() );
    ModelBase::Append( ModelBase::kSphereMesh, center*scale );
}

BoundingSphere Model::GetBoundingSphere()
{
	if (m_BoneMotions.size() > 0)
        return m_ModelTransform * m_Skinning[m_RootBoneIndex] * m_BoundingSphere;
    return m_ModelTransform * m_BoundingSphere;
}

BoundingBox Model::GetBoundingBox()
{
	if (m_BoneMotions.size() > 0)
        return m_ModelTransform * m_Skinning[m_RootBoneIndex] * m_BoundingBox;
    return m_ModelTransform * m_BoundingBox;
}