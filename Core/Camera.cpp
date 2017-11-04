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
#include "Camera.h"
#include <cmath>

using namespace Math;

// #define ReverseZ_
#ifdef ReverseZ_
const bool Math::g_bReverseZ = true;
const float Math::g_ClearDepth = 0.0f;
#else
const bool Math::g_ReverseZ = false;
const float Math::g_ClearDepth = 1.0f;
#endif

void BaseCamera::SetLookDirection( Vector3 look, Vector3 up )
{
	// Given, but ensure normalization
	Scalar lookLenSq = LengthSquare(look);
	look = Select(look * RecipSqrt(lookLenSq), -Vector3(kZUnitVector), lookLenSq < Scalar(0.000001f));

	Vector3 left = Cross(look, up); // forward == look
	Scalar leftLenSq = LengthSquare(left);
	left = Select(left * RecipSqrt(leftLenSq), Quaternion(Vector3(kYUnitVector), -XM_PIDIV2) * look, leftLenSq < Scalar(0.000001f));

	// Compute actual up vector
	up = Cross(left, look);

	// Finish constructing basis
	m_Basis = Matrix3(-left, up, look);
	m_CameraToWorld.SetRotation(Quaternion(m_Basis));
}

void BaseCamera::Update()
{
	m_PreviousViewProjMatrix = m_ViewProjMatrix;

	m_ViewMatrix = Matrix4(~m_CameraToWorld);
	m_ViewProjMatrix = m_ProjMatrix * m_ViewMatrix;
    m_ClipToWorld = Invert(m_ViewProjMatrix);
	m_ReprojectMatrix = m_PreviousViewProjMatrix * m_ClipToWorld;

	m_FrustumVS = Frustum( m_ProjMatrix );
	m_FrustumWS = m_CameraToWorld * m_FrustumVS;
}

void Camera::UpdateProjMatrix( void )
{
    SetProjMatrix(PerspectiveMatrix( m_VerticalFOV, m_AspectRatio, m_NearClip, m_FarClip, m_ReverseZ ));
}
