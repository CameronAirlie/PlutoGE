#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    struct AutoExposureSettings
    {
        float keyValue = 0.2f;
        float minExposure = 0.1f;
        float maxExposure = 1.5f;
        float adaptationSpeedUp = 0.18f;
        float adaptationSpeedDown = 0.06f;
        // Metered EV100 below which low-light compensation starts.
        float lowLightEv = 0.0f;
        // Stops of exposure removed per stop the scene is darker than
        // lowLightEv: 0 adapts fully, 1 holds exposure at the lowLightEv
        // level, above 1 lets darker scenes get less exposure.
        float lowLightDarkening = 0.0f;
    };

    // Meters the scene's average luminance and scales it toward Key Value,
    // adapting over time. Scenes darker than Low Light EV are held back by
    // Low Light Darkening, so night and interiors can stay dark while bright
    // scenes still adapt fully. AutoExposure.slang evaluates the same curve as
    // ComputeTargetExposure.
    class AutoExposureEffect : public IPostProcessEffect
    {
    public:
        using Settings = AutoExposureSettings;

        explicit AutoExposureEffect(Settings settings = {});

        std::string GetTypeName() const override { return "AutoExposure"; }
        std::string GetDisplayName() const override { return "Auto Exposure"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        std::vector<std::string> GetSummary() const override;

        [[nodiscard]] const Settings &GetSettings() const noexcept { return m_settings; }

        // EV100 for an average luminance, and back (L = 0.125 * 2^EV).
        [[nodiscard]] static float LuminanceToEv100(float luminance) noexcept;
        [[nodiscard]] static float Ev100ToLuminance(float ev100) noexcept;

        // The exposure the meter settles at for a scene of this average
        // luminance, before temporal adaptation.
        [[nodiscard]] static float ComputeTargetExposure(const Settings &settings, float averageLuminance) noexcept;

    private:
        Settings m_settings;
    };
}
