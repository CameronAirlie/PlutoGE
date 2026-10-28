#pragma once

#include "PlutoGE/render/DdgiSettings.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <optional>

namespace PlutoGE::render
{
    // Dynamic diffuse global illumination: a camera-centred probe volume ray-marched through the voxel scene and
    // applied to the G-buffer (docs/DDGI.md). Vulkan and OpenGL RHI only; the legacy OpenGL chain skips it.
    //
    // "Quality" picks probe density, rays, update rate and voxel detail together; editing any of those afterwards
    // shows "Custom".
    class DDGIEffect : public IPostProcessEffect
    {
    public:
        void Initialize() override {}
        void Apply(const PostProcessContext &) override {}
        std::string GetTypeName() const override { return "DDGI"; }
        std::string GetDisplayName() const override { return "Dynamic Diffuse GI (Probes)"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        std::vector<std::string> GetSummary() const override;

        [[nodiscard]] const DdgiSettings &GetSettings() const noexcept { return m_settings; }
        void SetSettings(const DdgiSettings &settings) { m_settings = settings.Sanitized(); }
        // The quality level the settings match; std::nullopt when customised.
        [[nodiscard]] std::optional<DdgiQuality> GetQuality() const;

    private:
        DdgiSettings m_settings;
    };
}
