#pragma once

#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/MaterialTextureSlots.h"
#include "PlutoGE/ui/EditorDocument.h"
#include "PlutoGE/ui/UnsavedSwitchGuard.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <glm/glm.hpp>

#include <array>
#include <string>

namespace PlutoGE::ui
{
    class MaterialEditorPanel : public Panel, public IEditorDocument
    {
    public:
        MaterialEditorPanel(const PanelConfig &config) : Panel(config) {}
        ~MaterialEditorPanel() override = default;

        void Render() override;
        void OnProjectChanged() override;

        // IEditorDocument
        [[nodiscard]] std::string GetDocumentName() const override;
        [[nodiscard]] bool HasUnsavedChanges() const override;
        bool SaveDocument(std::string &error) override;
        void DiscardChanges() override;
        void FocusDocument() override { Focus(); }

    private:
        void LoadMaterial(const std::string &reference);
        // Engine materials and materials owned by a model import cannot be saved here.
        [[nodiscard]] bool IsReadOnly() const;
        // The edited texture reference and sampled channel of each material texture slot.
        std::string &TexturePath(render::MaterialTextureSlot slot) { return m_texturePaths[static_cast<std::size_t>(slot)]; }
        render::TextureChannel &TextureChannel(render::MaterialTextureSlot slot)
        {
            return m_textureChannels[static_cast<std::size_t>(slot)];
        }
        // Fills `config`'s texture slots from the edited references (after emissionChannelMask is set).
        void ApplyTextures(render::MaterialConfig &config);
        // The clear coat and sheen controls; true when one changed.
        bool RenderLayerControls();
        // A slot's texture reference and, when it has one, channel selector; true when either changed.
        bool RenderSlotTextureControls(render::MaterialTextureSlot slot);

        std::string m_loadedReference;
        std::string m_shaderGraphReference;
        std::vector<render::ShaderGraphVariable> m_shaderGraphVariables;
        std::vector<render::ShaderGraphTextureParameter> m_shaderGraphTextures;
        glm::vec4 m_color{1.0f};
        render::MaterialSurfaceType m_surfaceType = render::MaterialSurfaceType::Standard;
        render::AlphaMode m_alphaMode = render::AlphaMode::Opaque;
        float m_alphaCutoff = 0.5f;
        bool m_castsShadow = true;
        bool m_twoSided = false;
        std::array<std::string, render::MaterialTextureSlotCount> m_texturePaths;
        std::array<render::TextureChannel, render::MaterialTextureSlotCount> m_textureChannels{};
        float m_metallic = 0.0f;
        float m_roughness = 0.55f;
        float m_occlusionStrength = 1.0f;
        float m_clearCoat = 0.0f;
        float m_clearCoatRoughness = 0.0f;
        glm::vec3 m_sheenColor{0.0f};
        float m_sheenRoughness = 0.5f;
        glm::vec3 m_emission{0.0f};
        int m_emissionTexCoord = 0;
        bool m_emissionChannelMask = false;
        std::array<glm::vec4,3> m_emissionChannels{{{1,0,0,1},{0,1,0,1},{0,0,1,1}}};
        float m_subsurface = 0.0f;
        glm::vec3 m_subsurfaceColor{1.0f, 0.35f, 0.2f};
        float m_subsurfaceRadius = 1.0f;
        float m_diffuseTransmission = 0.0f;
        glm::vec3 m_diffuseTransmissionColor{1.0f};
        float m_diffuseTransmissionAlbedoTint = 0.0f;
        float m_transmission = 0.0f;
        float m_ior = 1.45f;
        float m_thickness = 0.01f;
        glm::vec3 m_attenuationColor{1.0f};
        float m_attenuationDistance = 1.0f;
        bool m_flipNormalY = false;
        glm::vec2 m_uvScale{1.0f, 1.0f};
        bool m_dirty = false;
        UnsavedSwitchGuard m_switchGuard;
    };
}
