#include "PlutoGE/ui/panels/ContentBrowserPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/ui/EditorShell.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        bool ContainsInsensitive(std::string_view text, std::string_view filter)
        {
            if (filter.empty())
            {
                return true;
            }

            auto lower = [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            };

            std::string haystack(text);
            std::string needle(filter);
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), lower);
            std::transform(needle.begin(), needle.end(), needle.begin(), lower);
            return haystack.find(needle) != std::string::npos;
        }

        std::string DisplayAssetReference(std::string reference)
        {
            if (reference.rfind(assets::Project::kProjectAssetScheme, 0) == 0)
            {
                reference.erase(0, assets::Project::kProjectAssetScheme.size());
            }
            return reference;
        }

        std::string SanitizeAssetFileName(std::string_view text)
        {
            std::string name;
            name.reserve(text.size());
            for (const char rawCharacter : text)
            {
                const unsigned char character = static_cast<unsigned char>(rawCharacter);
                if (std::isalnum(character) != 0 || rawCharacter == '_' || rawCharacter == '-')
                {
                    name.push_back(rawCharacter);
                }
                else if (!name.empty() && name.back() != '_')
                {
                    name.push_back('_');
                }
            }

            while (!name.empty() && name.back() == '_')
            {
                name.pop_back();
            }
            return name;
        }
    }

    void ContentBrowserPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        auto *project = editorShell.GetProject();
        if (!project)
        {
            ImGui::TextDisabled("No project loaded.");
            return;
        }

        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputText("Filter", m_filterBuffer.data(), m_filterBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
        {
            project->RefreshAssetRegistry();
            editorShell.Log(EditorShell::ConsoleSeverity::Info, "Refreshed project assets.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Create Material"))
        {
            m_newMaterialNameBuffer.fill('\0');
            ImGui::OpenPopup("Create Material Asset");
        }

        if (ImGui::BeginPopupModal("Create Material Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newMaterialNameBuffer.data(), m_newMaterialNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newMaterialNameBuffer.data());
            if (!sanitizedName.empty())
            {
                ImGui::TextDisabled("Creates Materials/%s.plutomaterial", sanitizedName.c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter a material name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto materialPath = project->GetAssetDirectoryPath() / "Materials" / (sanitizedName + ".plutomaterial");
                const std::string reference = project->MakeAssetReference(materialPath);
                render::MaterialConfig config;
                config.color = glm::vec4(0.82f, 0.84f, 0.88f, 1.0f);
                config.metallic = 0.0f;
                config.roughness = 0.55f;

                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveMaterialAsset(reference, config, &errorMessage))
                {
                    project->RefreshAssetRegistry();
                    editorShell.OpenMaterialAsset(reference);
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created material: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create material." : errorMessage);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::TextDisabled("Assets: %zu", project->GetManifest().assetEntries.size());
        ImGui::Separator();

        const auto &assets = project->GetManifest().assetEntries;
        ImGui::BeginChild("Assets", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 3.0f), true);
        for (int index = 0; index < static_cast<int>(assets.size()); ++index)
        {
            const auto &asset = assets[static_cast<std::size_t>(index)];
            const std::string displayName = std::string("[") + std::string(assets::Project::GetAssetTypeName(asset.type)) + "] " + DisplayAssetReference(asset.reference);
            if (!ContainsInsensitive(displayName, m_filterBuffer.data()))
            {
                continue;
            }

            const bool selected = m_selectedAssetIndex == index;
            if (ImGui::Selectable(displayName.c_str(), selected))
            {
                m_selectedAssetIndex = index;
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                const auto resolvedPath = project->ResolveAssetReference(asset.reference);
                if (resolvedPath.extension() == ".plutoscene")
                {
                    editorShell.OpenSceneFromPath(resolvedPath);
                }
                else if (asset.type == assets::ProjectAssetType::Material)
                {
                    editorShell.OpenMaterialAsset(asset.reference);
                }
            }
        }
        ImGui::EndChild();

        if (m_selectedAssetIndex >= 0 && m_selectedAssetIndex < static_cast<int>(assets.size()))
        {
            const auto &asset = assets[static_cast<std::size_t>(m_selectedAssetIndex)];
            const auto resolvedPath = project->ResolveAssetReference(asset.reference);
            const std::string typeName(assets::Project::GetAssetTypeName(asset.type));
            ImGui::Text("Type: %s", typeName.c_str());
            ImGui::TextWrapped("Reference: %s", asset.reference.c_str());
            if (assets::Project::IsEngineAssetReference(asset.reference))
            {
                ImGui::TextWrapped("Engine Asset");
            }
            else
            {
                ImGui::TextWrapped("Path: %s", resolvedPath.string().c_str());
            }

            if (asset.type == assets::ProjectAssetType::Material)
            {
                if (ImGui::Button("Open Material"))
                {
                    editorShell.OpenMaterialAsset(asset.reference);
                }
            }
        }
    }
}
