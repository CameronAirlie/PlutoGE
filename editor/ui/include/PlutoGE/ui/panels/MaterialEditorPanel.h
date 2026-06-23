#pragma once

#include "PlutoGE/render/Material.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <glm/glm.hpp>

#include <string>

namespace PlutoGE::ui
{
    class MaterialEditorPanel : public Panel
    {
    public:
        MaterialEditorPanel(const PanelConfig &config) : Panel(config) {}
        ~MaterialEditorPanel() override = default;

        void Render() override;

    private:
        void LoadActiveMaterial();

        std::string m_loadedReference;
        glm::vec4 m_color{1.0f};
        render::MaterialSurfaceType m_surfaceType = render::MaterialSurfaceType::Standard;
        render::AlphaMode m_alphaMode = render::AlphaMode::Opaque;
        float m_alphaCutoff = 0.5f;
        bool m_castsShadow = true;
        std::string m_albedoTexturePath;
        std::string m_normalTexturePath;
        float m_metallic = 0.0f;
        std::string m_metallicTexturePath;
        render::TextureChannel m_metallicTextureChannel = render::TextureChannel::Red;
        float m_roughness = 0.55f;
        std::string m_roughnessTexturePath;
        render::TextureChannel m_roughnessTextureChannel = render::TextureChannel::Red;
        float m_transmission = 0.0f;
        float m_ior = 1.45f;
        float m_thickness = 0.01f;
        glm::vec3 m_attenuationColor{1.0f};
        float m_attenuationDistance = 1.0f;
        bool m_flipNormalY = false;
        bool m_dirty = false;
    };
}
