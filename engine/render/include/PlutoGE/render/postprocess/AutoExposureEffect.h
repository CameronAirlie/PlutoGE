#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    class AutoExposureEffect : public IPostProcessEffect
    {
    public:
        struct Settings
        {
            float keyValue = 0.18f;
            float minExposure = 0.35f;
            float maxExposure = 4.0f;
            float adaptationSpeedUp = 0.18f;
            float adaptationSpeedDown = 0.06f;
        };
        AutoExposureEffect(
            float keyValue = 0.2f,
            float minExposure = 0.1f,
            float maxExposure = 1.5f,
            float adaptationSpeedUp = 0.18f,
            float adaptationSpeedDown = 0.06f)
            : m_keyValue(keyValue),
              m_minExposure(minExposure),
              m_maxExposure(maxExposure),
              m_adaptationSpeedUp(adaptationSpeedUp),
              m_adaptationSpeedDown(adaptationSpeedDown)
        {
        }

        std::string GetTypeName() const override { return "AutoExposure"; }
        std::string GetDisplayName() const override { return "Auto Exposure"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        [[nodiscard]] Settings GetSettings() const noexcept
        {
            return {m_keyValue, m_minExposure, m_maxExposure,
                    m_adaptationSpeedUp, m_adaptationSpeedDown};
        }

    private:
        float m_keyValue = 0.18f;
        float m_minExposure = 0.35f;
        float m_maxExposure = 4.0f;
        float m_adaptationSpeedUp = 0.18f;
        float m_adaptationSpeedDown = 0.06f;
    };
}
