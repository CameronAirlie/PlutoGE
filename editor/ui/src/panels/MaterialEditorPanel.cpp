#include "PlutoGE/ui/panels/MaterialEditorPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"

#include <algorithm>
#include <filesystem>
#include <array>
#include <cstring>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        const char *TextureChannelLabel(render::TextureChannel channel)
        {
            switch (channel)
            {
            case render::TextureChannel::Green:
                return "Green";
            case render::TextureChannel::Blue:
                return "Blue";
            case render::TextureChannel::Alpha:
                return "Alpha";
            case render::TextureChannel::Red:
            default:
                return "Red";
            }
        }

        bool RenderTextureChannelCombo(const char *label, render::TextureChannel &channel)
        {
            bool changed = false;
            if (ImGui::BeginCombo(label, TextureChannelLabel(channel)))
            {
                constexpr render::TextureChannel channels[] = {
                    render::TextureChannel::Red,
                    render::TextureChannel::Green,
                    render::TextureChannel::Blue,
                    render::TextureChannel::Alpha,
                };
                for (auto option : channels)
                {
                    const bool selected = option == channel;
                    if (ImGui::Selectable(TextureChannelLabel(option), selected))
                    {
                        channel = option;
                        changed = true;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        // Engine defaults are the only shader graphs that are not project assets.
        bool RenderShaderGraphReferenceControl(std::string &reference)
        {
            static const std::array builtins{
                AssetReferenceOption{std::string(assets::Project::kBuiltinDefaultShaderGraphReference), "Default Lit"},
                AssetReferenceOption{std::string(assets::Project::kBuiltinDefaultUnlitShaderGraphReference), "Default Unlit"}};
            std::string effective = reference.empty()
                                        ? std::string(assets::Project::kBuiltinDefaultShaderGraphReference)
                                        : reference;
            if (!RenderProjectAssetPicker("Shader Graph", EditorShell::GetInstance().GetProject(),
                                          assets::ProjectAssetType::ShaderGraph, effective,
                                          {.noneLabel = nullptr, .builtinOptions = builtins}))
                return false;
            reference = std::move(effective);
            return true;
        }

        // Texture slots accept only project textures, including render textures.
        bool RenderTextureReferenceControl(const char *label, std::string &reference)
        {
            return RenderProjectAssetPicker(label, EditorShell::GetInstance().GetProject(),
                                            assets::ProjectAssetType::Texture, reference);
        }

        render::Texture *LoadMaterialEditorTexture(const std::string &path, render::TextureColorSpace colorSpace)
        {
            if (path.empty())
            {
                return nullptr;
            }

            const std::string resolvedPath = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(path);
            return resolvedPath.empty() ? nullptr : render::Texture::LoadFromFile(resolvedPath.c_str(), colorSpace);
        }

        void HashCombine(std::size_t &seed, std::size_t value)
        {
            seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        }

        template <typename T>
        void HashPreviewValue(std::size_t &seed, const T &value)
        {
            HashCombine(seed, std::hash<T>{}(value));
        }
    }

    bool RenderVector2Control(const char *label, glm::vec2 &value, float resetValue = 0.0f, float columnWidth = 100.0f)
    {
        bool changed = false;
        ImGuiStyle &style = ImGui::GetStyle();

        ImGui::PushID(label);

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);

        ImGui::TextUnformatted(label);
        ImGui::NextColumn();

        ImVec2 buttonSize = {28.0f, 28.0f};
        float spacing = style.ItemSpacing.x;

        float availableWidth = ImGui::GetContentRegionAvail().x;

        // Layout:
        // [X button] spacing [X drag] spacing [Y button] spacing [Y drag]
        float dragWidth = (availableWidth - buttonSize.x * 2.0f - spacing * 3.0f) / 2.0f;

        // Avoid negative/silly widths in narrow panels
        dragWidth = std::max(dragWidth, 20.0f);

        // X component
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.15f, 1.0f));
        if (ImGui::Button("X", buttonSize))
        {
            value.x = resetValue;
            changed = true;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(dragWidth);
        if (ImGui::DragFloat("##X", &value.x, 0.1f))
        {
            changed = true;
        }

        // Y component
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        if (ImGui::Button("Y", buttonSize))
        {
            value.y = resetValue;
            changed = true;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(dragWidth);
        if (ImGui::DragFloat("##Y", &value.y, 0.1f))
        {
            changed = true;
        }

        ImGui::Columns(1);
        ImGui::PopID();

        return changed;
    }

    void MaterialEditorPanel::OnProjectChanged()
    {
        m_loadedReference.clear();
        m_dirty = false;
        m_switchGuard.Reset();
    }

    void MaterialEditorPanel::ApplyTextures(render::MaterialConfig &config)
    {
        for (const auto &slot : render::MaterialTextureSlots())
        {
            config.*slot.texture = LoadMaterialEditorTexture(TexturePath(slot.slot),
                                                             render::GetMaterialTextureColorSpace(config, slot.slot));
            if (slot.channel) config.*slot.channel = TextureChannel(slot.slot);
        }
    }

    bool MaterialEditorPanel::RenderSlotTextureControls(render::MaterialTextureSlot slot)
    {
        const auto &info = render::GetMaterialTextureSlotInfo(slot);
        bool changed = RenderTextureReferenceControl((std::string(info.displayName) + " Texture").c_str(), TexturePath(slot));
        if (info.channel && !TexturePath(slot).empty())
            changed |= RenderTextureChannelCombo((std::string(info.displayName) + " Channel").c_str(), TextureChannel(slot));
        return changed;
    }

    bool MaterialEditorPanel::RenderLayerControls()
    {
        bool changed = false;
        ImGui::SeparatorText("Clear Coat");
        changed |= ImGui::SliderFloat("Strength##ClearCoat", &m_clearCoat, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A thin, smooth transparent layer over the surface, such as lacquer, varnish or car paint.");
        if (m_clearCoat > 0.0f)
        {
            changed |= ImGui::SliderFloat("Roughness##ClearCoat", &m_clearCoatRoughness, 0.0f, 1.0f, "%.2f");
            changed |= RenderSlotTextureControls(render::MaterialTextureSlot::ClearCoat);
            changed |= RenderSlotTextureControls(render::MaterialTextureSlot::ClearCoatRoughness);
        }
        ImGui::SeparatorText("Sheen");
        changed |= ImGui::ColorEdit3("Color##Sheen", &m_sheenColor.x, ImGuiColorEditFlags_Float);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Soft light scattered back by fibres at grazing angles, such as cloth, velvet or dust.\n"
                              "Black turns sheen off.");
        if (m_sheenColor != glm::vec3(0.0f))
        {
            changed |= ImGui::SliderFloat("Roughness##Sheen", &m_sheenRoughness, 0.0f, 1.0f, "%.2f");
            changed |= RenderSlotTextureControls(render::MaterialTextureSlot::SheenColor);
            changed |= RenderSlotTextureControls(render::MaterialTextureSlot::SheenRoughness);
        }
        return changed;
    }

    void MaterialEditorPanel::LoadMaterial(const std::string &reference)
    {
        auto &editorShell = EditorShell::GetInstance();
        m_loadedReference = reference;
        m_dirty = false;

        auto *material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(reference);
        if (!material)
        {
            m_color = glm::vec4(1.0f);
            m_shaderGraphVariables.clear();m_shaderGraphTextures.clear();
            m_shaderGraphReference = std::string(assets::Project::kBuiltinDefaultShaderGraphReference);
            m_surfaceType = render::MaterialSurfaceType::Standard;
            m_alphaMode = render::AlphaMode::Opaque;
            m_alphaCutoff = 0.5f;
            m_castsShadow = true;
            m_twoSided = false;
            m_texturePaths = {};
            m_textureChannels = {};
            m_metallic = 0.0f;
            m_roughness = 0.55f;
            m_occlusionStrength = 1.0f;
            m_clearCoat = 0.0f;
            m_clearCoatRoughness = 0.0f;
            m_sheenColor = glm::vec3(0.0f);
            m_sheenRoughness = 0.5f;
            m_emission = glm::vec3(0.0f);
            m_emissionTexCoord = 0;
            m_emissionChannelMask = false;
            m_emissionChannels = {{{1,0,0,1},{0,1,0,1},{0,0,1,1}}};
            m_transmission = 0.0f;
            m_ior = 1.45f;
            m_thickness = 0.01f;
            m_attenuationColor = glm::vec3(1.0f);
            m_attenuationDistance = 1.0f;
            m_subsurface = 0.0f;
            m_subsurfaceColor = glm::vec3(1.0f, 0.35f, 0.2f);
            m_subsurfaceRadius = 1.0f;
            m_diffuseTransmission = 0.0f;
            m_diffuseTransmissionColor = glm::vec3(1.0f);
            m_diffuseTransmissionAlbedoTint = 0.0f;
            m_uvScale = glm::vec2(1.0f, 1.0f);
            m_flipNormalY = false;
            return;
        }

        const auto &config = material->GetConfig();
        // Loaded textures report resolved file paths; edit them as project
        // references. Paths outside the project stay visible as invalid.
        const auto *project = editorShell.GetProject();
        const auto toReference = [project](const std::string &path) {
            return project ? ToProjectAssetReference(*project, path) : path;
        };
        const auto textureReference = [&](const render::Texture *texture) {
            return texture ? toReference(texture->GetFilePath()) : std::string{};
        };
        m_shaderGraphVariables = config.shaderGraphVariables;
        m_shaderGraphTextures = config.shaderGraphTextures;
        for (auto &texture : m_shaderGraphTextures)
            texture.reference = toReference(texture.reference);
        m_shaderGraphReference = config.shaderGraphReference.empty()
                                     ? std::string(assets::Project::kBuiltinDefaultShaderGraphReference)
                                     : toReference(config.shaderGraphReference);
        m_color = config.color;
        m_surfaceType = config.surfaceType;
        m_alphaMode = config.alphaMode;
        m_alphaCutoff = config.alphaCutoff;
        m_castsShadow = config.castsShadow;
        m_twoSided = config.twoSided;
        for (const auto &slot : render::MaterialTextureSlots())
        {
            TexturePath(slot.slot) = textureReference(config.*slot.texture);
            TextureChannel(slot.slot) = slot.channel ? config.*slot.channel : render::TextureChannel::Red;
        }
        m_metallic = config.metallic;
        m_roughness = config.roughness;
        m_occlusionStrength = config.occlusionStrength;
        m_clearCoat = config.clearCoat;
        m_clearCoatRoughness = config.clearCoatRoughness;
        m_sheenColor = config.sheenColor;
        m_sheenRoughness = config.sheenRoughness;
        m_emission = config.emission;
        m_emissionTexCoord = config.emissionTexCoord;
        m_emissionChannelMask = config.emissionChannelMask;
        m_emissionChannels = config.emissionChannels;
        m_transmission = config.transmission;
        m_ior = config.ior;
        m_thickness = config.thickness;
        m_attenuationColor = config.attenuationColor;
        m_attenuationDistance = config.attenuationDistance;
        m_subsurface = config.subsurface;
        m_subsurfaceColor = config.subsurfaceColor;
        m_subsurfaceRadius = config.subsurfaceRadius;
        m_diffuseTransmission = config.diffuseTransmission;
        m_diffuseTransmissionColor = config.diffuseTransmissionColor;
        m_diffuseTransmissionAlbedoTint = config.diffuseTransmissionAlbedoTint;
        m_uvScale = config.uvScale;
        m_flipNormalY = config.flipNormalY;
    }

    std::string MaterialEditorPanel::GetDocumentName() const
    {
        return "Material " + std::filesystem::path(m_loadedReference).filename().string();
    }

    bool MaterialEditorPanel::IsReadOnly() const
    {
        return assets::Project::IsEngineAssetReference(m_loadedReference) ||
               core::Engine::GetInstance().GetAssetManager().IsImportedAsset(m_loadedReference);
    }

    bool MaterialEditorPanel::HasUnsavedChanges() const
    {
        return m_dirty && !m_loadedReference.empty() && !IsReadOnly();
    }

    void MaterialEditorPanel::DiscardChanges()
    {
        LoadMaterial(m_loadedReference);
    }

    bool MaterialEditorPanel::SaveDocument(std::string &errorMessage)
    {
        auto &editorShell = EditorShell::GetInstance();
        const std::string reference = m_loadedReference;
        if (IsReadOnly())
        {
            errorMessage = "Engine and imported materials are read-only; extract an authored copy to edit it.";
            return false;
        }
        render::MaterialConfig config;
        config.shaderGraphReference = m_shaderGraphReference.empty()
                                          ? std::string(assets::Project::kBuiltinDefaultShaderGraphReference)
                                          : m_shaderGraphReference;
        config.shaderGraphVariables = m_shaderGraphVariables;
        config.shaderGraphTextures=m_shaderGraphTextures;
        config.color = m_color;
        config.surfaceType = m_surfaceType;
        config.alphaMode = m_alphaMode;
        config.alphaCutoff = (std::clamp)(m_alphaCutoff, 0.0f, 1.0f);
        config.castsShadow = m_castsShadow;
        config.twoSided = m_twoSided;
        config.metallic = (std::clamp)(m_metallic, 0.0f, 1.0f);
        config.roughness = (std::clamp)(m_roughness, 0.04f, 1.0f);
        config.occlusionStrength = (std::clamp)(m_occlusionStrength, 0.0f, 1.0f);
        config.clearCoat = (std::clamp)(m_clearCoat, 0.0f, 1.0f);
        config.clearCoatRoughness = (std::clamp)(m_clearCoatRoughness, 0.0f, 1.0f);
        config.sheenColor = glm::clamp(m_sheenColor, glm::vec3(0.0f), glm::vec3(1.0f));
        config.sheenRoughness = (std::clamp)(m_sheenRoughness, 0.0f, 1.0f);
        config.emission = glm::max(m_emission, glm::vec3(0.0f));
        config.emissionTexCoord = m_emissionTexCoord;
        config.emissionChannelMask = m_emissionChannelMask;
        ApplyTextures(config);
        config.emissionChannels = m_emissionChannels;
        config.subsurface = (std::clamp)(m_subsurface, 0.0f, 1.0f);
        config.subsurfaceColor = glm::max(m_subsurfaceColor, glm::vec3(0.0f));
        config.subsurfaceRadius = (std::max)(m_subsurfaceRadius, 0.001f);
        config.diffuseTransmission = std::clamp(m_diffuseTransmission, 0.0f, 1.0f);
        config.diffuseTransmissionColor = m_diffuseTransmissionColor;
        config.diffuseTransmissionAlbedoTint = std::clamp(m_diffuseTransmissionAlbedoTint, 0.0f, 1.0f);
        config.transmission = (std::clamp)(m_transmission, 0.0f, 1.0f);
        config.ior = (std::clamp)(m_ior, 1.0f, 2.5f);
        config.thickness = (std::max)(m_thickness, 0.0f);
        config.attenuationColor = glm::clamp(m_attenuationColor, glm::vec3(0.0f), glm::vec3(1.0f));
        config.attenuationDistance = (std::max)(m_attenuationDistance, 0.0001f);
        config.uvScale = glm::max(m_uvScale, glm::vec2(0.0001f));
        config.flipNormalY = m_flipNormalY;

        if (core::Engine::GetInstance().GetAssetManager().SaveMaterialAsset(reference, config, &errorMessage))
        {
            auto &engine = core::Engine::GetInstance();
            if (auto *scene = engine.GetScene())
            {
                scene->RefreshMaterialAsset(reference, engine.GetAssetManager().LoadMaterialAsset(reference));
            }
            m_dirty = false;
            editorShell.MarkSceneDirty();
            editorShell.Log(EditorShell::ConsoleSeverity::Info, "Saved material: " + reference);
        }
        return !m_dirty;
    }

    void MaterialEditorPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        // Copy: asset creation refreshes the project registry immediately before opening this panel.
        const std::string selected = editorShell.GetActiveMaterialAssetReference();
        if (auto next = m_switchGuard.Follow(selected, m_loadedReference, HasUnsavedChanges()))
            LoadMaterial(*next);
        else if (!selected.empty() && m_loadedReference.empty())
            LoadMaterial(selected);
        if (auto next = m_switchGuard.DrawPrompt(*this, "Unsaved Material"))
            LoadMaterial(*next);
        const std::string reference = m_loadedReference;
        if (reference.empty())
        {
            ImGui::TextDisabled("No material selected.");
            return;
        }

        const bool engineMaterial = assets::Project::IsEngineAssetReference(reference);
        ImGui::TextWrapped("Material: %s", reference.c_str());
        if (engineMaterial)
        {
            ImGui::TextDisabled("Engine material assets are read-only.");
        }

        ImGui::SeparatorText("Preview");
        render::MaterialConfig previewConfig;
        previewConfig.color = m_color;
        previewConfig.surfaceType = m_surfaceType;
        previewConfig.alphaMode = m_alphaMode;
        previewConfig.twoSided = m_twoSided;
        previewConfig.metallic = m_metallic;
        previewConfig.roughness = m_roughness;
        previewConfig.occlusionStrength = m_occlusionStrength;
        previewConfig.clearCoat = m_clearCoat;
        previewConfig.clearCoatRoughness = m_clearCoatRoughness;
        previewConfig.sheenColor = m_sheenColor;
        previewConfig.sheenRoughness = m_sheenRoughness;
        previewConfig.emission = m_emission;
        previewConfig.emissionTexCoord = m_emissionTexCoord;
        previewConfig.emissionChannelMask = m_emissionChannelMask;
        ApplyTextures(previewConfig);
        previewConfig.emissionChannels = m_emissionChannels;
        previewConfig.transmission = m_transmission;
        previewConfig.subsurface = m_subsurface;
        previewConfig.subsurfaceColor = m_subsurfaceColor;
        previewConfig.subsurfaceRadius = m_subsurfaceRadius;
        previewConfig.diffuseTransmission = m_diffuseTransmission;
        previewConfig.diffuseTransmissionColor = m_diffuseTransmissionColor;
        previewConfig.diffuseTransmissionAlbedoTint = m_diffuseTransmissionAlbedoTint;
        previewConfig.ior = m_ior;
        previewConfig.attenuationColor = m_attenuationColor;
        previewConfig.uvScale = m_uvScale;
        previewConfig.flipNormalY = m_flipNormalY;
        previewConfig.shaderGraphReference = m_shaderGraphReference;
        previewConfig.shaderGraphVariables = m_shaderGraphVariables;
        previewConfig.shaderGraphTextures=m_shaderGraphTextures;
        editorShell.GetEngine().GetAssetManager().ResolveMaterialShaderGraph(previewConfig);
        std::size_t previewRevision = 0;
        for (const float value : {m_color.r, m_color.g, m_color.b, m_color.a, m_metallic, m_roughness,
                                  m_emission.r, m_emission.g, m_emission.b, m_uvScale.x, m_uvScale.y,
                                  m_subsurface, m_subsurfaceColor.r, m_subsurfaceColor.g, m_subsurfaceColor.b, m_subsurfaceRadius,
                                  m_transmission, m_ior, m_attenuationColor.r, m_attenuationColor.g, m_attenuationColor.b,
                                  m_occlusionStrength, m_clearCoat, m_clearCoatRoughness, m_sheenColor.r, m_sheenColor.g,
                                  m_sheenColor.b, m_sheenRoughness})
            HashPreviewValue(previewRevision, value);
        for (std::size_t slot = 0; slot < render::MaterialTextureSlotCount; ++slot)
        {
            HashPreviewValue(previewRevision, m_texturePaths[slot]);
            HashPreviewValue(previewRevision, static_cast<int>(m_textureChannels[slot]));
        }
        HashPreviewValue(previewRevision, m_emissionTexCoord);
        HashPreviewValue(previewRevision, m_emissionChannelMask);
        for (const auto &channel : m_emissionChannels)
            for (int i=0;i<4;++i) HashPreviewValue(previewRevision, channel[i]);
        HashPreviewValue(previewRevision, m_flipNormalY);
        HashPreviewValue(previewRevision, m_twoSided);
        if (previewConfig.shaderGraphProgram)
        {
            HashPreviewValue(previewRevision, previewConfig.shaderGraphProgram->hash);
            HashPreviewValue(previewRevision, static_cast<std::uint64_t>(ImGui::GetTime() * 30.0));
        }

        const float previewExtent = (std::min)(ImGui::GetContentRegionAvail().x, 280.0f);
        const ImVec2 previewSize(previewExtent, previewExtent);
        const std::uint64_t previewTexture =
            GetCachedMaterialPreview(editorShell.GetEngine(), "material-editor:" + reference, previewConfig,
                                     static_cast<std::uint64_t>(previewRevision));
        if (previewTexture != 0)
            ImGui::Image(static_cast<ImTextureID>(previewTexture), previewSize, ImVec2(0, 1), ImVec2(1, 0));
        else
        {
            const ImVec2 previewMin = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(previewMin,
                                                       ImVec2(previewMin.x + previewSize.x, previewMin.y + previewSize.y),
                                                       ImGui::GetColorU32(ImGuiCol_FrameBg), 6.0f);
            ImGui::Dummy(previewSize);
        }
        ImGui::TextDisabled("%s | %s | Metallic %.2f | Roughness %.2f",
                            m_surfaceType == render::MaterialSurfaceType::Glass ? "Glass" : "Standard",
                            m_alphaMode == render::AlphaMode::Blend ? "Blend" : m_alphaMode == render::AlphaMode::Mask ? "Mask"
                                                                                                                       : "Opaque",
                            m_metallic,
                            m_roughness);
        std::string textureSummary;
        for (const auto &slot : render::MaterialTextureSlots())
            textureSummary += (textureSummary.empty() ? "" : " | ") + std::string(slot.displayName) +
                              (TexturePath(slot.slot).empty() ? " none" : " set");
        ImGui::TextDisabled("Textures: %s", textureSummary.c_str());

        ImGui::Separator();
        if (engineMaterial)
        {
            ImGui::BeginDisabled();
        }

        if (RenderShaderGraphReferenceControl(m_shaderGraphReference))
        {
            m_dirty = true;
        }

        const auto shaderGraph = core::Engine::GetInstance().GetAssetManager().LoadShaderGraphAsset(m_shaderGraphReference);
        if (!shaderGraph.variables.empty()) ImGui::SeparatorText("Shader parameters");
        for (const auto &parameter : shaderGraph.variables)
        {
            ImGui::PushID(parameter.name.c_str());
            auto overrideValue = std::find_if(m_shaderGraphVariables.begin(), m_shaderGraphVariables.end(), [&](const auto &v) { return v.name == parameter.name; });
            bool overridden = overrideValue != m_shaderGraphVariables.end();
            if (ImGui::Checkbox("Override", &overridden))
            {
                if (overridden) m_shaderGraphVariables.push_back(parameter);
                else m_shaderGraphVariables.erase(overrideValue);
                m_dirty = true;
                overrideValue = std::find_if(m_shaderGraphVariables.begin(), m_shaderGraphVariables.end(), [&](const auto &v) { return v.name == parameter.name; });
            }
            auto value = overridden ? overrideValue->value : parameter.value;
            ImGui::BeginDisabled(!overridden);
            int type=int(parameter.type);
            bool changed=type==0?ImGui::DragFloat(parameter.name.c_str(),&value.x,.01f):type==1?ImGui::DragFloat2(parameter.name.c_str(),&value.x,.01f):type==2?ImGui::DragFloat3(parameter.name.c_str(),&value.x,.01f):ImGui::ColorEdit4(parameter.name.c_str(),&value.x);
            if(changed) { overrideValue->type=parameter.type;overrideValue->value=value;m_dirty=true; }
            if(overridden && overrideValue->type!=parameter.type) ImGui::TextWrapped("Parameter type changed; disable Override to reset it.");
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        const auto textureProgram=render::BuildShaderGraphProgram(shaderGraph);
        if(textureProgram)for(const auto &parameter:textureProgram->textures) {
            ImGui::PushID(parameter.name.c_str());
            auto replacement=std::find_if(m_shaderGraphTextures.begin(),m_shaderGraphTextures.end(),[&](const auto &t){return t.name==parameter.name;});
            bool overridden=replacement!=m_shaderGraphTextures.end();
            if(ImGui::Checkbox("Override texture",&overridden)) {
                if(overridden)m_shaderGraphTextures.push_back(parameter);else m_shaderGraphTextures.erase(replacement);
                replacement=std::find_if(m_shaderGraphTextures.begin(),m_shaderGraphTextures.end(),[&](const auto &t){return t.name==parameter.name;});m_dirty=true;
            }
            auto path=overridden?replacement->reference:parameter.reference;
            ImGui::BeginDisabled(!overridden);
            if(RenderTextureReferenceControl(parameter.name.c_str(),path)) {replacement->reference=path;m_dirty=true;}
            ImGui::EndDisabled();ImGui::PopID();
        }
        float color[4] = {m_color.r, m_color.g, m_color.b, m_color.a};
        if (ImGui::ColorEdit4("Color", color))
        {
            m_color = glm::vec4(color[0], color[1], color[2], color[3]);
            m_dirty = true;
        }

        int surfaceType = static_cast<int>(m_surfaceType);
        const char *surfaceTypeItems[] = {"Standard", "Glass"};
        if (ImGui::Combo("Surface Type", &surfaceType, surfaceTypeItems, IM_ARRAYSIZE(surfaceTypeItems)))
        {
            m_surfaceType = static_cast<render::MaterialSurfaceType>(surfaceType);
            if (m_surfaceType == render::MaterialSurfaceType::Glass)
            {
                m_alphaMode = render::AlphaMode::Blend;
                m_castsShadow = false;
                m_metallic = 0.0f;
                m_transmission = 1.0f;
                m_roughness = (std::min)(m_roughness, 0.15f);
            }
            m_dirty = true;
        }

        int alphaMode = static_cast<int>(m_alphaMode);
        const char *alphaModeItems[] = {"Opaque", "Mask", "Blend"};
        if (ImGui::Combo("Alpha Mode", &alphaMode, alphaModeItems, IM_ARRAYSIZE(alphaModeItems)))
        {
            m_alphaMode = static_cast<render::AlphaMode>(alphaMode);
            m_dirty = true;
        }

        if (m_alphaMode == render::AlphaMode::Mask)
        {
            if (ImGui::SliderFloat("Alpha Cutoff", &m_alphaCutoff, 0.0f, 1.0f, "%.2f"))
            {
                m_dirty = true;
            }
        }

        if (ImGui::Checkbox("Casts Shadow", &m_castsShadow))
        {
            m_dirty = true;
        }
        if (ImGui::Checkbox("Two Sided", &m_twoSided))
        {
            m_dirty = true;
        }

        ImGui::SeparatorText("Textures");
        if (RenderTextureReferenceControl("Albedo", TexturePath(render::MaterialTextureSlot::Albedo)))
        {
            m_dirty = true;
        }
        if (RenderTextureReferenceControl("Normal", TexturePath(render::MaterialTextureSlot::Normal)))
        {
            m_dirty = true;
        }

        if (ImGui::SliderFloat("Metallic", &m_metallic, 0.0f, 1.0f, "%.2f"))
        {
            m_dirty = true;
        }
        if (RenderTextureReferenceControl("Metallic Texture", TexturePath(render::MaterialTextureSlot::Metallic)))
        {
            m_dirty = true;
        }
        if (RenderTextureChannelCombo("Metallic Channel", TextureChannel(render::MaterialTextureSlot::Metallic)))
        {
            m_dirty = true;
        }

        if (ImGui::SliderFloat("Roughness", &m_roughness, 0.04f, 1.0f, "%.2f"))
        {
            m_dirty = true;
        }
        if (RenderTextureReferenceControl("Emission Texture", TexturePath(render::MaterialTextureSlot::Emission)))
            m_dirty = true;
        const char *emissionUvs[] = {"UV 0 (primary)", "UV 1 (secondary)"};
        if (ImGui::Combo("Emission UV Set", &m_emissionTexCoord, emissionUvs, 2))
            m_dirty = true;
        if (ImGui::Checkbox("Emission RGB Channel Masks", &m_emissionChannelMask)) m_dirty = true;
        if (m_emissionChannelMask)
        {
            ImGui::TextWrapped("Each texture channel controls a separate emission colour and intensity.");
            const char *names[]{"Red mask", "Green mask", "Blue mask"};
            for (int i=0;i<3;++i)
            {
                ImGui::PushID(i);
                auto &channel = m_emissionChannels[i];
                if (ImGui::ColorEdit3(names[i], &channel.x, ImGuiColorEditFlags_Float)) m_dirty = true;
                if (ImGui::DragFloat("Intensity", &channel.w, .05f, 0.0f, 10000.0f)) {
                    channel.w = std::max(channel.w, 0.0f); m_dirty = true;
                }
                ImGui::PopID();
            }
        }
        float emission[3] = {m_emission.r, m_emission.g, m_emission.b};
        if (ImGui::ColorEdit3("Emission", emission, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float))
        {
            m_emission = glm::max(glm::vec3(emission[0], emission[1], emission[2]), glm::vec3(0.0f));
            m_dirty = true;
        }
        if (RenderTextureReferenceControl("Roughness Texture", TexturePath(render::MaterialTextureSlot::Roughness)))
        {
            m_dirty = true;
        }
        if (RenderTextureChannelCombo("Roughness Channel", TextureChannel(render::MaterialTextureSlot::Roughness)))
        {
            m_dirty = true;
        }
        if (RenderTextureReferenceControl("Occlusion Texture", TexturePath(render::MaterialTextureSlot::Occlusion)))
            m_dirty = true;
        if (!TexturePath(render::MaterialTextureSlot::Occlusion).empty())
        {
            if (RenderTextureChannelCombo("Occlusion Channel", TextureChannel(render::MaterialTextureSlot::Occlusion)))
                m_dirty = true;
            if (ImGui::SliderFloat("Occlusion Strength", &m_occlusionStrength, 0.0f, 1.0f, "%.2f"))
                m_dirty = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Darkens ambient light and reflections in cavities, and shadows direct light\n"
                                  "arriving from outside a cavity's opening. glTF packs it in the red channel.");
        }
        if (RenderVector2Control("UV Scale", m_uvScale, 0.2f))
        {
            m_dirty = true;
        }

        if (RenderLayerControls())
            m_dirty = true;

        ImGui::SeparatorText("Subsurface Scattering");
        if (ImGui::SliderFloat("Strength##Subsurface", &m_subsurface, 0.0f, 1.0f, "%.2f"))
        {
            m_dirty = true;
        }
        if (m_subsurface > 0.001f)
        {
            float subsurfaceColor[3] = {m_subsurfaceColor.r, m_subsurfaceColor.g, m_subsurfaceColor.b};
            if (ImGui::ColorEdit3("Scattering Color", subsurfaceColor))
            {
                m_subsurfaceColor = glm::vec3(subsurfaceColor[0], subsurfaceColor[1], subsurfaceColor[2]);
                m_dirty = true;
            }
            if (ImGui::DragFloat("Scattering Radius", &m_subsurfaceRadius, 0.01f, 0.001f, 100.0f, "%.3f"))
            {
                m_dirty = true;
            }
        }

        ImGui::SeparatorText("Diffuse Transmission");
        ImGui::TextWrapped("For thin surfaces such as leaves: the share of diffuse light that leaves the far side. GI also treats these surfaces as partly see-through.");
        if (ImGui::SliderFloat("Diffuse Transmission", &m_diffuseTransmission, 0.0f, 1.0f, "%.2f"))
        {
            m_dirty = true;
        }
        if (m_diffuseTransmission > 0.001f)
        {
            float transmissionColor[3] = {m_diffuseTransmissionColor.r, m_diffuseTransmissionColor.g, m_diffuseTransmissionColor.b};
            if (ImGui::ColorEdit3("Transmission Color", transmissionColor))
            {
                m_diffuseTransmissionColor = glm::vec3(transmissionColor[0], transmissionColor[1], transmissionColor[2]);
                m_dirty = true;
            }
            if (ImGui::SliderFloat("Follow Albedo", &m_diffuseTransmissionAlbedoTint, 0.0f, 1.0f, "%.2f"))
            {
                m_dirty = true;
            }
        }

        if (m_surfaceType == render::MaterialSurfaceType::Glass)
        {
            ImGui::SeparatorText("Glass");
            ImGui::TextWrapped("Transmission controls light through the glass. Base color alpha controls surface coverage; use 1 for a solid pane.");
            if (ImGui::SliderFloat("Transmission", &m_transmission, 0.0f, 1.0f, "%.2f"))
            {
                m_dirty = true;
            }
            if (ImGui::SliderFloat("IOR", &m_ior, 1.0f, 2.5f, "%.2f"))
            {
                m_dirty = true;
            }
            ImGui::SetItemTooltip("1.0 disables refraction and normal-incidence reflection. Window glass is typically around 1.5.");
            if (ImGui::DragFloat("Thickness", &m_thickness, 0.001f, 0.0f, 100.0f, "%.3f"))
            {
                m_dirty = true;
            }
            ImGui::SetItemTooltip("Thickness in scene units controls refraction and tint absorption. Roughness adds a frosted appearance.");
            float attenuationColor[3] = {m_attenuationColor.r, m_attenuationColor.g, m_attenuationColor.b};
            if (ImGui::ColorEdit3("Attenuation Color", attenuationColor))
            {
                m_attenuationColor = glm::vec3(attenuationColor[0], attenuationColor[1], attenuationColor[2]);
                m_dirty = true;
            }
            if (ImGui::DragFloat("Attenuation Distance", &m_attenuationDistance, 0.01f, 0.0001f, 1000.0f, "%.3f"))
            {
                m_dirty = true;
            }
        }

        if (ImGui::Checkbox("Flip Normal Y", &m_flipNormalY))
        {
            m_dirty = true;
        }

        if (engineMaterial)
        {
            ImGui::EndDisabled();
        }

        ImGui::Separator();
        const bool importedMaterial = core::Engine::GetInstance().GetAssetManager().IsImportedAsset(reference);
        if (importedMaterial) ImGui::TextWrapped("Imported material: extract an authored copy to edit it.");
        ImGui::BeginDisabled(engineMaterial || importedMaterial || !m_dirty);
        if (ImGui::Button("Save"))
        {
            std::string errorMessage;
            if (!SaveDocument(errorMessage))
                editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to save material." : errorMessage);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button("Revert"))
        {
            DiscardChanges();
        }
        ImGui::EndDisabled();
    }
}
