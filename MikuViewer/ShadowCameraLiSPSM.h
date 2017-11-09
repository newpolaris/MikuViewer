#pragma once

#include <vector>
#include <memory>
#include "Camera.h"
#include "IRenderObject.h"
#include "BaseShadowCamera.h"
#include "Clipping.h"

class ShadowCameraLiSPSM : public BaseShadowCamera
{
public:

    ShadowCameraLiSPSM() {}

    void UpdateMatrix( const RenderList& Model, Math::Vector3 LightDirection, const BaseCamera& Camera );

private:

    void CalcLispSMMtx( Math::Vector3 eyePos, Math::Vector3 lightDir, Math::Vector3 viewDir, Math::VecPoint& B );
    void CalcUniformShadowMtx( Math::Vector3 eyePos, Math::Vector3 lightDir, Math::Vector3 viewDir, Math::VecPoint& B );

    Math::Matrix4 m_ShadowMatrix;
};
