#include "PlutoGE/ui/panels/MaterialEditorPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <unordered_map>

#include <imgui.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace PlutoGE::ui
{
    namespace
    {
        constexpr std::size_t kTexturePathBufferSize = 512;

        std::array<char, kTexturePathBufferSize> &GetTexturePathBuffer(const std::string &materialReference, const char *slotName)
        {
            static std::unordered_map<std::string, std::array<char, kTexturePathBufferSize>> buffers;
            return buffers[materialReference + ":" + slotName];
        }

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

        std::optional<std::string> AcceptDroppedTextureAssetReference()
        {
            std::optional<std::string> droppedReference;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
                {
                    if (payload->Data && payload->DataSize > 0)
                    {
                        const auto *data = static_cast<const char *>(payload->Data);
                        const std::string reference(data, data + payload->DataSize - 1);
                        if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::Texture)
                        {
                            droppedReference = reference;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            return droppedReference;
        }

        bool BrowseTexturePath(std::array<char, kTexturePathBufferSize> &buffer)
        {
#ifdef _WIN32
            OPENFILENAMEA ofn = {};
            char fileName[MAX_PATH] = "";
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = nullptr;
            ofn.lpstrFilter = "Texture Files\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr\0All Files\0*.*\0";
            ofn.lpstrFile = fileName;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn))
            {
                strncpy_s(buffer.data(), buffer.size(), fileName, _TRUNCATE);
                return true;
            }
#else
            (void)buffer;
#endif
            return false;
        }

        bool RenderTexturePathControl(const std::string &materialReference,
                                      const char *slotName,
                                      const char *label,
                                      std::string &path)
        {
            bool changed = false;
            const std::string key = materialReference + ":" + slotName;
            auto &buffer = GetTexturePathBuffer(materialReference, slotName);
            static std::unordered_map<std::string, std::string> cachedPaths;
            auto &cachedPath = cachedPaths[key];
            if (cachedPath != path)
            {
                std::fill(buffer.begin(), buffer.end(), '\0');
                strncpy_s(buffer.data(), buffer.size(), path.c_str(), _TRUNCATE);
                cachedPath = path;
            }

            ImGui::InputText(label, buffer.data(), buffer.size());
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                path = buffer.data();
                cachedPath = path;
                changed = true;
            }
            if (auto droppedReference = AcceptDroppedTextureAssetReference())
            {
                path = *droppedReference;
                std::fill(buffer.begin(), buffer.end(), '\0');
                strncpy_s(buffer.data(), buffer.size(), path.c_str(), _TRUNCATE);
                cachedPath = path;
                changed = true;
            }

            ImGui::SameLine();
            if (ImGui::Button((std::string("...##") + slotName).c_str()))
            {
                if (BrowseTexturePath(buffer))
                {
                    path = buffer.data();
                    cachedPath = path;
                    changed = true;
                }
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(path.empty());
            if (ImGui::Button((std::string("Clear##") + slotName).c_str()))
            {
                path.clear();
                std::fill(buffer.begin(), buffer.end(), '\0');
                cachedPath = path;
                changed = true;
            }
            ImGui::EndDisabled();
            return changed;
        }

        render::Texture *LoadMaterialEditorTexture(const std::string &path)
        {
            if (path.empty())
            {
                return nullptr;
            }

            const std::string resolvedPath = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(path);
            return resolvedPath.empty() ? nullptr : render::Texture::LoadFromFile(resolvedPath.c_str());
        }
    }

    void MaterialEditorPanel::LoadActiveMaterial()
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto &reference = editorShell.GetActiveMaterialAssetReference();
        m_loadedReference = reference;
        m_dirty = false;

        auto *material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(reference);
        if (!material)
        {
            m_color = glm::vec4(1.0f);
            m_surfaceType = render::MaterialSurfaceType::Standard;
            m_alphaMode = render::AlphaMode::Opaque;
            m_alphaCutoff = 0.5f;
            m_castsShadow = true;
            m_albedoTexturePath.clear();
            m_normalTexturePath.clear();
            m_metallic = 0.0f;
            m_metallicTexturePath.clear();
            m_metallicTextureChannel = render::TextureChannel::Red;
            m_roughness = 0.55f;
            m_roughnessTexturePath.clear();
            m_roughnessTextureChannel = render::TextureChannel::Red;
            m_transmission = 0.0f;
            m_ior = 1.45f;
            m_thickness = 0.01f;
            m_attenuationColor = glm::vec3(1.0f);
            m_attenuationDistance = 1.0f;
            m_flipNormalY = false;
            return;
        }

        const auto &config = material->GetConfig();
        m_color = config.color;
        m_surfaceType = config.surfaceType;
        m_alphaMode = config.alphaMode;
        m_alphaCutoff = config.alphaCutoff;
        m_castsShadow = config.castsShadow;
        m_albedoTexturePath = config.albedoTexture ? config.albedoTexture->GetFilePath() : std::string{};
        m_normalTexturePath = config.normalTexture ? config.normalTexture->GetFilePath() : std::string{};
        m_metallic = config.metallic;
        m_metallicTexturePath = config.metallicTexture ? config.metallicTexture->GetFilePath() : std::string{};
        m_metallicTextureChannel = config.metallicTextureChannel;
        m_roughness = config.roughness;
        m_roughnessTexturePath = config.roughnessTexture ? config.roughnessTexture->GetFilePath() : std::string{};
        m_roughnessTextureChannel = config.roughnessTextureChannel;
        m_transmission = config.transmission;
        m_ior = config.ior;
        m_thickness = config.thickness;
        m_attenuationColor = config.attenuationColor;
        m_attenuationDistance = config.attenuationDistance;
        m_flipNormalY = config.flipNormalY;
    }

    void MaterialEditorPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto &reference = editorShell.GetActiveMaterialAssetReference();
        if (reference.empty())
        {
            ImGui::TextDisabled("No material selected.");
            return;
        }

        if (reference != m_loadedReference)
        {
            LoadActiveMaterial();
        }

        const bool engineMaterial = assets::Project::IsEngineAssetReference(reference);
        ImGui::TextWrapped("Material: %s", reference.c_str());
        if (engineMaterial)
        {
            ImGui::TextDisabled("Engine material assets are read-only.");
        }

        ImGui::Separator();
        if (engineMaterial)
        {
            ImGui::BeginDisabled();
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

        ImGui::SeparatorText("Textures");
        if (RenderTexturePathControl(reference, "Albedo", "Albedo", m_albedoTexturePath))
        {
            m_dirty = true;
        }
        if (RenderTexturePathControl(reference, "Normal", "Normal", m_normalTexturePath))
        {
            m_dirty = true;
        }

        if (ImGui::SliderFloat("Metallic", &m_metallic, 0.0f, 1.0f, "%.2f"))
        {
            m_dirty = true;
        }
        if (RenderTexturePathControl(reference, "Metallic", "Metallic Texture", m_metallicTexturePath))
        {
            m_dirty = true;
        }
        if (RenderTextureChannelCombo("Metallic Channel", m_metallicTextureChannel))
        {
            m_dirty = true;
        }

        if (ImGui::SliderFloat("Roughness", &m_roughness, 0.04f, 1.0f, "%.2f"))
        {
            m_dirty = true;
        }
        if (RenderTexturePathControl(reference, "Roughness", "Roughness Texture", m_roughnessTexturePath))
        {
            m_dirty = true;
        }
        if (RenderTextureChannelCombo("Roughness Channel", m_roughnessTextureChannel))
        {
            m_dirty = true;
        }

        if (m_surfaceType == render::MaterialSurfaceType::Glass)
        {
            ImGui::SeparatorText("Glass");
            if (ImGui::SliderFloat("Transmission", &m_transmission, 0.0f, 1.0f, "%.2f"))
            {
                m_dirty = true;
            }
            if (ImGui::SliderFloat("IOR", &m_ior, 1.0f, 2.5f, "%.2f"))
            {
                m_dirty = true;
            }
            if (ImGui::DragFloat("Thickness", &m_thickness, 0.001f, 0.0f, 100.0f, "%.3f"))
            {
                m_dirty = true;
            }
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
        ImGui::BeginDisabled(engineMaterial || !m_dirty);
        if (ImGui::Button("Save"))
        {
            render::MaterialConfig config;
            config.color = m_color;
            config.surfaceType = m_surfaceType;
            config.alphaMode = m_alphaMode;
            config.alphaCutoff = (std::clamp)(m_alphaCutoff, 0.0f, 1.0f);
            config.castsShadow = m_castsShadow;
            config.albedoTexture = LoadMaterialEditorTexture(m_albedoTexturePath);
            config.normalTexture = LoadMaterialEditorTexture(m_normalTexturePath);
            config.metallic = (std::clamp)(m_metallic, 0.0f, 1.0f);
            config.metallicTexture = LoadMaterialEditorTexture(m_metallicTexturePath);
            config.metallicTextureChannel = m_metallicTextureChannel;
            config.roughness = (std::clamp)(m_roughness, 0.04f, 1.0f);
            config.roughnessTexture = LoadMaterialEditorTexture(m_roughnessTexturePath);
            config.roughnessTextureChannel = m_roughnessTextureChannel;
            config.transmission = (std::clamp)(m_transmission, 0.0f, 1.0f);
            config.ior = (std::clamp)(m_ior, 1.0f, 2.5f);
            config.thickness = (std::max)(m_thickness, 0.0f);
            config.attenuationColor = glm::clamp(m_attenuationColor, glm::vec3(0.0f), glm::vec3(1.0f));
            config.attenuationDistance = (std::max)(m_attenuationDistance, 0.0001f);
            config.flipNormalY = m_flipNormalY;

            std::string errorMessage;
            if (core::Engine::GetInstance().GetAssetManager().SaveMaterialAsset(reference, config, &errorMessage))
            {
                m_dirty = false;
                editorShell.MarkSceneDirty();
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "Saved material: " + reference);
            }
            else
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to save material." : errorMessage);
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button("Revert"))
        {
            LoadActiveMaterial();
        }
        ImGui::EndDisabled();
    }
}
