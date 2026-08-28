#pragma once

#include "PlutoGE/render/IUpscaler.h"

#include <memory>

namespace PlutoGE::render
{
    class Shader;

    class SpatialUpscaler final : public IUpscaler
    {
    public:
        SpatialUpscaler();
        ~SpatialUpscaler() override;

        bool Initialize() override;
        bool IsInitialized() const override { return m_shader != nullptr; }
        bool Upscale(const RenderTarget &source, RenderTarget &destination,
                     const UpscalerConfig &config = {}) override;
        bool UpscaleToFramebuffer(const RenderTarget &source, int outputWidth, int outputHeight,
                                  const UpscalerConfig &config = {}) override;
        void Shutdown() override;

    private:
        bool Render(const RenderTarget &source, RenderTarget *destination,
                    int outputWidth, int outputHeight, const UpscalerConfig &config);
        std::unique_ptr<Shader> m_shader;
    };
}
