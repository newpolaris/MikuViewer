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

#define ReverseZ_
#ifdef ReverseZ_
const bool Math::g_ReverseZ = true;
const float Math::g_ClearDepth = 0.0f;
#else
const bool Math::g_ReverseZ = false;
const float Math::g_ClearDepth = 1.0f;
#endif

namespace Math
{
    NumVar ApertureWidth("Graphics/Camera/Aperture Width(mm)", 0.0f, 0.0f, 1.0f, 0.0f);

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
    NumVar FocalLength("Graphics/Camera/Focal Length(mm)", 35.0f, 0.0f, 100.0f, 1.0f);
    NumVar FocusDistance("Graphics/Camera/Focus Distance(m)", 10.0f, 1.0f, 100.0f, 0.1f);
    NumVar FilmSize("Graphics/Camera/Film Size(mm)", 35.0f, 1.0f, 100.0f, 0.1f);
}

using namespace Math;

const Vector3& BaseCamera::GetForwardZ() const
{
#if LEFT
    static const Vector3 forward( 0, 0, +1 );
#else
    static const Vector3 forward( 0, 0, -1 );
#endif
    return forward;
}

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
    m_ClipToWorld = Invert(m_ViewProjMatrix);
	m_ReprojectMatrix = m_PreviousViewProjMatrix * m_ClipToWorld;

	m_FrustumVS = Frustum( m_ProjMatrix );
	m_FrustumWS = m_CameraToWorld * m_FrustumVS;
}

void Camera::UpdateProjMatrix( void )
{
    SetProjMatrix(PerspectiveMatrix( m_VerticalFOV, m_AspectRatio, m_NearClip, m_FarClip, m_ReverseZ ));
}
