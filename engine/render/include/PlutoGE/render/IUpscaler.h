#pragma once

namespace PlutoGE::render
{
    class RenderTarget;

    struct UpscalerConfig
    {
        float sharpness = 0.2f;
    };

    class IUpscaler
    {
    public:
        virtual ~IUpscaler() = default;

        virtual bool Initialize() = 0;
        virtual bool IsInitialized() const = 0;
        virtual bool Upscale(const RenderTarget &source, RenderTarget &destination,
                             const UpscalerConfig &config = {}) = 0;
        virtual bool UpscaleToFramebuffer(const RenderTarget &source, int outputWidth, int outputHeight,
                                          const UpscalerConfig &config = {}) = 0;
        virtual void Shutdown() = 0;
    };
}
