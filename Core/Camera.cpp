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

//
// 'forward' is inverse look direction (right hand coord)
// So in cross calcuration to calc right and up vector
// Element order is chaged to correct equation
//
void BaseCamera::SetLookDirection( Vector3 forward, Vector3 up )
{
	// Given, but ensure normalization
	Scalar forwardLenSq = LengthSquare(forward);
	forward = Select(forward * RecipSqrt(forwardLenSq), -Vector3(kZUnitVector), forwardLenSq < Scalar(0.000001f));

	// Deduce a valid, orthogonal right vector

	Vector3 right = Cross(forward, up); // forward = -look
	Scalar rightLenSq = LengthSquare(right);
	right = Select(right * RecipSqrt(rightLenSq), Quaternion(Vector3(kYUnitVector), -XM_PIDIV2) * forward, rightLenSq < Scalar(0.000001f));

	// Compute actual up vector
	up = Cross(right, forward); // forward = -look

	// Finish constructing basis
	m_Basis = Matrix3(right, up, -forward); // -forward = look
	m_CameraToWorld.SetRotation(Quaternion(m_Basis));
}

void BaseCamera::Update()
{
	m_PreviousViewProjMatrix = m_ViewProjMatrix;

	m_ViewMatrix = Matrix4(~m_CameraToWorld);
	m_ViewProjMatrix = m_ProjMatrix * m_ViewMatrix;
	m_ReprojectMatrix = m_PreviousViewProjMatrix * Invert(GetViewProjMatrix());

	m_FrustumVS = Frustum( m_ProjMatrix );
	m_FrustumWS = m_CameraToWorld * m_FrustumVS;
}


void Camera::UpdateProjMatrix( void )
{
	float Y = 1.0f / std::tanf( m_VerticalFOV * 0.5f );
	float X = Y * m_AspectRatio;

	float Q1, Q2;

	// ReverseZ puts far plane at Z=0 and near plane at Z=1.  This is never a bad idea, and it's
	// actually a great idea with F32 depth buffers to redistribute precision more evenly across
	// the entire range.  It requires clearing Z to 0.0f and using a GREATER variant depth test.
	// Some care must also be done to properly reconstruct linear W in a pixel shader from hyperbolic Z.
	if (m_ReverseZ)
	{
		Q1 = m_NearClip / (m_FarClip - m_NearClip);
		Q2 = Q1 * m_FarClip;
	}
	else
	{
		Q1 = m_FarClip / (m_NearClip - m_FarClip);
		Q2 = Q1 * m_NearClip;
	}

	SetProjMatrix( Matrix4(
		Vector4( X, 0.0f, 0.0f, 0.0f ),
		Vector4( 0.0f, Y, 0.0f, 0.0f ),
		Vector4( 0.0f, 0.0f, Q1, -1.0f ),
		Vector4( 0.0f, 0.0f, Q2, 0.0f )
		) );
}
