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

#pragma once

#include <DirectXMath.h>
#include "Math/Vector.h"

class Color
{
public:
	Color( ) : m_value(DirectX::g_XMOne) {}
	Color( DirectX::FXMVECTOR vec );
	Color( const DirectX::XMVECTORF32& vec );
	Color( float r, float g, float b, float a = 1.0f );
	Color( uint16_t r, uint16_t g, uint16_t b, uint16_t a = 255, uint16_t bitDepth = 8 );
    Color( BYTE r, BYTE g, BYTE b );
	explicit Color( uint32_t rgbaLittleEndian );

	float R() const { return DirectX::XMVectorGetX(m_value); }
	float G() const { return DirectX::XMVectorGetY(m_value); }
	float B() const { return DirectX::XMVectorGetZ(m_value); }
	float A() const { return DirectX::XMVectorGetW(m_value); }

	bool operator==( const Color& rhs ) const { return DirectX::XMVector4Equal(m_value, rhs.m_value); }
	bool operator!=( const Color& rhs ) const { return !DirectX::XMVector4Equal(m_value, rhs.m_value); }
	void SetR( float r ) { m_value = DirectX::XMVectorSetX(m_value, r); }
	void SetG( float g ) { m_value = DirectX::XMVectorSetY(m_value, g); }
	void SetB( float b ) { m_value = DirectX::XMVectorSetZ(m_value, b); }
	void SetA( float a ) { m_value = DirectX::XMVectorSetW(m_value, a); }

	float* GetPtr( void ) { return reinterpret_cast<float*>(this); }
	float& operator[]( int idx ) { return GetPtr()[idx]; }

	void SetRGB( float r, float g, float b ) { m_value = DirectX::XMVectorSelect( m_value, DirectX::XMVectorSet(r, g, b, b), DirectX::g_XMMask3 ); }

	Color ToSRGB() const;
	Color FromSRGB() const;
	Color ToREC709() const;
	Color FromREC709() const;

	// Probably want to convert to sRGB or Rec709 first
	uint32_t R10G10B10A2() const;
	uint32_t R8G8B8A8() const;

	// Pack an HDR color into 32-bits
	uint32_t R11G11B10F(bool RoundToEven=false) const;

	operator DirectX::XMVECTOR() const { return m_value; }

private:
	DirectX::XMVECTOR m_value;
};

inline Color::Color( DirectX::FXMVECTOR vec )
{
	m_value = vec;
}

inline Color::Color( const DirectX::XMVECTORF32& vec )
{
	m_value = (DirectX::XMVECTOR)vec;
}

inline Color::Color( float r, float g, float b, float a )
{
	m_value = DirectX::XMVectorSet(r, g, b, a);
}

inline Color Color::ToSRGB( void ) const
{
    using namespace DirectX;
	XMVECTOR T = XMVectorSaturate(m_value);
	XMVECTOR result = XMVectorSubtract(XMVectorScale(XMVectorPow(T, XMVectorReplicate(1.0f / 2.4f)), 1.055f), XMVectorReplicate(0.055f));
	result = XMVectorSelect(result, XMVectorScale(T, 12.92f), XMVectorLess(T, XMVectorReplicate(0.0031308f)));
	return XMVectorSelect(T, result, g_XMSelect1110);
}

inline Color Color::FromSRGB( void ) const
{
    using namespace DirectX;
	XMVECTOR T = XMVectorSaturate(m_value);
	XMVECTOR result = XMVectorPow(XMVectorScale(XMVectorAdd(T, XMVectorReplicate(0.055f)), 1.0f / 1.055f), XMVectorReplicate(2.4f));
	result = XMVectorSelect(result, XMVectorScale(T, 1.0f / 12.92f), XMVectorLess(T, XMVectorReplicate(0.0031308f)));
	return XMVectorSelect(T, result, g_XMSelect1110);
}

inline Color Color::ToREC709( void ) const
{
    using namespace DirectX;
	XMVECTOR T = XMVectorSaturate(m_value);
	XMVECTOR result = XMVectorSubtract(XMVectorScale(XMVectorPow(T, XMVectorReplicate(0.45f)), 1.099f), XMVectorReplicate(0.099f));
	result = XMVectorSelect(result, XMVectorScale(T, 4.5f), XMVectorLess(T, XMVectorReplicate(0.0018f)));
	return XMVectorSelect(T, result, g_XMSelect1110);
}

inline Color Color::FromREC709( void ) const
{
    using namespace DirectX;
	XMVECTOR T = XMVectorSaturate(m_value);
	XMVECTOR result = XMVectorPow(XMVectorScale(XMVectorAdd(T, XMVectorReplicate(0.099f)), 1.0f / 1.099f), XMVectorReplicate(1.0f / 0.45f));
	result = XMVectorSelect(result, XMVectorScale(T, 1.0f / 4.5f), XMVectorLess(T, XMVectorReplicate(0.0081f)));
	return XMVectorSelect(T, result, g_XMSelect1110);
}

inline uint32_t Color::R10G10B10A2( void ) const
{
    using namespace DirectX;
	XMVECTOR result = XMVectorRound(XMVectorMultiply(XMVectorSaturate(m_value), XMVectorSet(1023.0f, 1023.0f, 1023.0f, 3.0f)));
	result = _mm_castsi128_ps(_mm_cvttps_epi32(result));
	uint32_t r = XMVectorGetIntX(result);
	uint32_t g = XMVectorGetIntY(result);
	uint32_t b = XMVectorGetIntZ(result);
	uint32_t a = XMVectorGetIntW(result) >> 8;
	return a << 30 | b << 20 | g << 10 | r;
}

inline uint32_t Color::R8G8B8A8( void ) const
{
    using namespace DirectX;
	XMVECTOR result = XMVectorRound(XMVectorMultiply(XMVectorSaturate(m_value), XMVectorReplicate(255.0f)));
	result = _mm_castsi128_ps(_mm_cvttps_epi32(result));
	uint32_t r = XMVectorGetIntX(result);
	uint32_t g = XMVectorGetIntY(result);
	uint32_t b = XMVectorGetIntZ(result);
	uint32_t a = XMVectorGetIntW(result);
	return a << 24 | b << 16 | g << 8 | r;
}

inline DirectX::XMFLOAT4 FromSRGB(DirectX::XMFLOAT4 c)
{
    using namespace DirectX;
    FXMVECTOR C = XMLoadFloat4(&c);
    XMVECTOR V = Color(C).FromSRGB();
    XMStoreFloat4( &c, V );
    return c;
};

inline DirectX::XMFLOAT3 FromSRGB(DirectX::XMFLOAT3 c)
{
    using namespace DirectX;
    FXMVECTOR C = XMLoadFloat3(&c);
    XMVECTOR V = Color(C).FromSRGB();
    XMStoreFloat3( &c, V );
    return c;
};
