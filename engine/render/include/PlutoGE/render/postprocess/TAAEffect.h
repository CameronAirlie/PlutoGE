#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

namespace PlutoGE::render
{
    // See docs/TAA.md. Sharpening (0-1) is the RCAS strength applied to the
    // resolved output, never to history. Quality selects the history filter:
    // 0 Balanced (5-tap Catmull-Rom), 1 High (9-tap).
    struct TAAEffectConfig
    {
        float historyWeight = 0.90f;
        float stationaryHistoryWeight = 0.94f;
        float motionHistoryWeight = 0.55f;
        float sharpening = 0.18f;
        float depthRejectionThreshold = 0.0035f;
        float normalRejectionThreshold = 0.82f;
        float velocityRejectionScale = 80.0f;
        float jitterStrength = 1.0f;
        int quality = 0;
        bool jitterEnabled = true;
        bool jitterDebug = false;
    };

    class TAAEffect : public IPostProcessEffect
    {
    public:
        explicit TAAEffect(TAAEffectConfig config = {});

        std::string GetTypeName() const override { return "TAA"; }
        std::string GetDisplayName() const override { return "Temporal Anti-Aliasing"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;

        [[nodiscard]] const TAAEffectConfig &GetConfig() const noexcept { return m_config; }

    private:
        TAAEffectConfig m_config;
    };
}
