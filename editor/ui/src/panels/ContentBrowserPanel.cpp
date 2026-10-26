#include "PlutoGE/asset_import/AssetMoveService.h"
#include "PlutoGE/asset_import/ModelSourcePackageCopy.h"
#include "PlutoGE/asset_import/ModelObjectExtractionService.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"
#include "PlutoGE/scene/ui/UITheme.h"
#include "PlutoGE/ui/theme/UIThemeDocument.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/ScriptAssetFieldPicker.h"
#include "PlutoGE/ui/ModelImportDependenciesInspector.h"
#include "PlutoGE/ui/ModelHierarchyInspector.h"
#include "PlutoGE/ui/StaticModelHierarchyDialog.h"
#include "PlutoGE/ui/FileDialogs.h"
#include "PlutoGE/ui/SystemShell.h"
#include "PlutoGE/ui/contentbrowser/ContentFormatting.h"
#include "PlutoGE/ui/contentbrowser/ContentPaths.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/assets/AssetPathPolicy.h"

#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/assets/ModelSourcePackage.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/asset_import/ModelImportService.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RenderTexture.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/ShaderGraph.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/PrefabMeshExporter.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/ui/AssetReferenceSearchPanel.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/SurfacePlacement.h"
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <sstream>

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace PlutoGE::ui
{
    class AssetThumbnailCache
    {
    public:
      ~AssetThumbnailCache()
      {
          Clear();
      }
      void BeginFrame()
      {
          m_generationBudget = 2;
          ++m_frameSequence;
      }
      std::uint64_t Get(const assets::Project &project, core::Engine &engine, const assets::ProjectAssetEntry &asset)
      {
          if (!engine.GetRenderDevice())
              return 0;
          if (asset.type != assets::ProjectAssetType::Texture && asset.type != assets::ProjectAssetType::Model &&
              asset.type != assets::ProjectAssetType::Mesh && asset.type != assets::ProjectAssetType::Material)
          {
              return 0;
          }

          const auto found = m_entries.find(asset.reference);
          if (found != m_entries.end() &&
              m_frameSequence - found->second.lastValidatedFrame < kValidationIntervalFrames)
          {
              return found->second.texture;
          }

          auto stamp = GetStamp(project, asset.reference);
          render::Material *preloadedMaterial = nullptr;
          if (asset.type == assets::ProjectAssetType::Material)
          {
              preloadedMaterial = engine.GetAssetManager().LoadMaterialAsset(asset.reference);
              if (preloadedMaterial)
              {
                  const auto &config = preloadedMaterial->GetConfig();
                  for (const render::Texture *texture :
                       {config.albedoTexture, config.normalTexture, config.metallicTexture, config.roughnessTexture, config.emissionTexture})
                  {
                      if (!texture)
                          continue;
                      std::error_code error;
                      const auto time = std::filesystem::last_write_time(texture->GetFilePath(), error);
                      if (!error)
                          stamp ^= static_cast<std::uint64_t>(time.time_since_epoch().count()) + 0x9e3779b97f4a7c15ull +
                                   (stamp << 6u) + (stamp >> 2u);
                  }
              }
          }
          if (found != m_entries.end() && found->second.stamp == stamp)
          {
              found->second.lastValidatedFrame = m_frameSequence;
              return found->second.texture;
          }
          if (m_generationBudget <= 0)
              return 0;
          --m_generationBudget;

          if (asset.type == assets::ProjectAssetType::Texture)
          {
              const auto path = project.ResolveAssetReference(asset.reference).string();
              auto *texture = engine.GetAssetManager().LoadTexture(path.c_str());
              if (!texture)
                  return 0;

              auto &entry = m_entries[asset.reference];
              const auto pixels = ReadPixels(*texture);
              if (pixels.empty())
                  return 0;
              auto &device = *engine.GetRenderDevice();
              entry.image = render::rhi::Texture(
                  device, device.CreateTexture({static_cast<std::uint32_t>(texture->GetWidth()),
                                                static_cast<std::uint32_t>(texture->GetHeight()),
                                                render::rhi::Format::R8G8B8A8Unorm, render::rhi::TextureUsage::Sampled,
                                                "Asset thumbnail"},
                                               pixels));
              Publish(entry, device, entry.image.Get());
              entry.stamp = stamp;
              entry.lastValidatedFrame = m_frameSequence;
              return entry.texture;
          }

          std::string renderReference = asset.reference;
          if (asset.type == assets::ProjectAssetType::Model)
          {
              assets::ModelAsset model;
              if (!assets::LoadModelSourcePackage(project, asset.reference, model))
                  return 0;
              const auto mesh = std::find_if(model.objects.begin(), model.objects.end(), [](const auto &object) {
                  return object.type == assets::ProjectAssetType::Mesh;
              });
              if (mesh == model.objects.end())
                  return 0;
              renderReference = mesh->reference;
          }

          render::Mesh *mesh = nullptr;
          render::Material *material = nullptr;
          if (asset.type == assets::ProjectAssetType::Material)
          {
              material = preloadedMaterial;
              mesh = engine.GetAssetManager().LoadMeshAsset(std::string(assets::Project::kBuiltinSphereMeshReference));
          }
          else
          {
              mesh = engine.GetAssetManager().LoadMeshAsset(renderReference);
              const auto &materials = engine.GetAssetManager().GetMeshAssetMaterialReferences(renderReference);
              if (!materials.empty())
                  material = engine.GetAssetManager().LoadMaterialAsset(materials.front());
          }
          if (!mesh)
              return 0;

          auto &entry = m_entries[asset.reference];
          if (!Render(entry, engine, *mesh, material))
              return 0;
          entry.stamp = stamp;
          entry.lastValidatedFrame = m_frameSequence;
          return entry.texture;
        }

        std::uint64_t GetMaterial(core::Engine &engine, const std::string &key, const render::MaterialConfig &config,
                                  std::uint64_t revision)
        {
            if (!engine.GetRenderDevice())
                return 0;

            const auto found = m_entries.find(key);
            if (found != m_entries.end() && found->second.stamp == revision)
                return found->second.texture;
            auto *mesh = engine.GetAssetManager().LoadMeshAsset(std::string(assets::Project::kBuiltinSphereMeshReference));
            if (!mesh) return 0;
            auto &entry = m_entries[key];
            render::Material material(config);
            if (!Render(entry, engine, *mesh, &material))
                return 0;
            entry.stamp = revision;
            return entry.texture;
        }

        void Clear()
        {
            for (auto &[key, entry] : m_entries)
                EditorShell::GetInstance().GetPanelManager().UnregisterTexture(entry.registration);
            m_entries.clear();
        }
    private:
        struct Entry
        {
            std::uint64_t texture = 0;
            std::uint64_t stamp = 0;
            std::uint64_t lastValidatedFrame = 0;
            EditorTextureHandle registration;
            render::rhi::Texture image;
        };
        static constexpr int kSize = 128;
        static constexpr std::uint64_t kValidationIntervalFrames = 60;
        std::unique_ptr<render::RhiSceneRenderer> m_renderer;
        render::rhi::GraphicsPipeline m_copyPipeline;
        render::rhi::Sampler m_copySampler;
        int m_generationBudget = 2;
        std::uint64_t m_frameSequence = 0;
        std::unordered_map<std::string, Entry> m_entries;
        static std::uint64_t GetStamp(const assets::Project &project, const std::string &reference)
        {
            std::error_code error;
            const auto stamp = std::filesystem::last_write_time(project.ResolveAssetReference(reference), error);
            return error ? 0 : static_cast<std::uint64_t>(stamp.time_since_epoch().count());
        }
        static std::vector<std::byte> ReadPixels(const render::Texture &texture)
        {
            const auto pixels = texture.GetRgba8Pixels();
            const auto *begin = reinterpret_cast<const std::byte *>(pixels.data());
            return pixels.empty() ? std::vector<std::byte>{} : std::vector<std::byte>(begin, begin + pixels.size());
        }
        static void Publish(Entry &entry, render::rhi::IRenderDevice &device, render::rhi::TextureHandle texture)
        {
            auto &panels = EditorShell::GetInstance().GetPanelManager();
            EditorTextureDescriptor descriptor{&device, texture, kSize, kSize};
            if (entry.registration.IsValid())
                panels.UpdateTexture(entry.registration, descriptor);
            else
                entry.registration = panels.RegisterTexture(descriptor);
            entry.texture = panels.GetImGuiTextureId(entry.registration);
        }
        bool Render(Entry &entry, core::Engine &engine, render::Mesh &mesh, render::Material *material)
        {
            auto &device = *engine.GetRenderDevice();
            if (!m_renderer)
            {
                m_renderer = std::make_unique<render::RhiSceneRenderer>();
                auto shaders = render::ShaderArtifactLibrary{}.LoadBasicRendererPackage();
                shaders.virtualShadows = {}; // Preview lighting does not cast shadows.
                if (!m_renderer->Initialize(device, shaders))
                {
                    m_renderer.reset();
                    return false;
                }
            }
            m_renderer->SetImmediateTextureUploads(true);

            if (!m_copyPipeline)
            {
                render::rhi::GraphicsPipelineDescriptor copy;
                const render::ShaderArtifactLibrary artifacts;
                copy.vertexShader = artifacts.Load("ThumbnailCopy", "vertex");
                copy.fragmentShader = artifacts.Load("ThumbnailCopy", "fragment");
                copy.colorFormat = render::rhi::Format::R8G8B8A8Unorm;
                copy.depthFormat = render::rhi::Format::Undefined;
                copy.depthTest = copy.depthWrite = false;
                copy.cullMode = render::rhi::CullMode::None;
                copy.resourceBindings = {{1, 0, 1, render::rhi::ResourceBindingType::SampledTexture,
                                          render::rhi::ShaderStageMask::Fragment}};
                copy.debugName = "Thumbnail cache copy";
                m_copyPipeline = render::rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(copy));
                m_copySampler = render::rhi::Sampler(device, device.CreateSampler({}));
            }
            const auto bounds = mesh.GetBounds();
            render::RenderCommand command;
            command.mesh = material&&material->GetConfig().shaderGraphProgram?mesh.GetTessellated(unsigned(material->GetConfig().shaderGraphProgram->data.header.w)):&mesh;
            command.material = material;
            command.castsShadow = false;
            command.model = glm::rotate(glm::mat4(1), glm::radians(-18.0f), glm::vec3(1, 0, 0));
            command.model = glm::rotate(command.model, glm::radians(32.0f), glm::vec3(0, 1, 0));
            command.model = glm::scale(command.model, glm::vec3(0.66f / std::max(bounds.radius, 0.001f)));
            command.model = glm::translate(command.model, -bounds.center);
            render::CameraData camera;
            camera.view = glm::lookAt(glm::vec3(0, 0, 2.4f), glm::vec3(0), glm::vec3(0, 1, 0));
            camera.projection = glm::perspective(glm::radians(32.0f), 1.0f, 10.0f, 0.01f);
            camera.nearPlane = 0.01f;
            camera.farPlane = 10.0f;
            render::BasicLighting lighting;
            const std::array effects{
                render::BasicPostProcessEffect{render::BasicPostProcessEffectType::ToneMapping},
                render::BasicPostProcessEffect{render::BasicPostProcessEffectType::GammaCorrection}};
            std::vector<render::RenderCommand> previewCommands{command};
            for(size_t i=0;i<previewCommands.size()&&previewCommands.size()<32;++i)if(previewCommands[i].material)
                for(const auto &pass:previewCommands[i].material->GetConfig().additionalPasses){
                    auto overlay=command;overlay.material=pass.get();overlay.mesh=mesh.GetTessellated(pass->GetConfig().shaderGraphProgram?unsigned(pass->GetConfig().shaderGraphProgram->data.header.w):0);
                    previewCommands.push_back(overlay);
                }
            if (!m_renderer->Render(kSize, kSize, camera, lighting, previewCommands, {}, {}, effects, ReadPixels))
                return false;
            if (!entry.image)
                entry.image =
                    render::rhi::Texture(device, device.CreateTexture({kSize, kSize, render::rhi::Format::R8G8B8A8Unorm,
                                                                       render::rhi::TextureUsage::ColorAttachment,
                                                                       "Cached thumbnail", true}));
            auto &commands = device.GetImmediateContext();
            commands.BeginFrame("Asset thumbnail");
            render::rhi::RenderingInfo info;
            info.colorAttachments = {entry.image.Get()};
            info.width = info.height = kSize;
            commands.BeginRendering(info);
            commands.BindPipeline(m_copyPipeline.Get());
            commands.BindTexture(1, m_renderer->GetColorTexture(), m_copySampler.Get());
            commands.Draw(3);
            commands.EndRendering();
            commands.Submit();
            Publish(entry, device, entry.image.Get());
            return entry.texture != 0;
        }
    };

    namespace
    {
        std::unique_ptr<AssetThumbnailCache> materialPreviewCache;
    }

    void ClearCachedMaterialPreviews()
    {
        materialPreviewCache.reset();
    }

    std::uint64_t GetCachedMaterialPreview(core::Engine &engine, const std::string &cacheKey,
                                           const render::MaterialConfig &config, std::uint64_t revision)
    {
        if (!materialPreviewCache)
            materialPreviewCache = std::make_unique<AssetThumbnailCache>();
        return materialPreviewCache->GetMaterial(engine, cacheKey, config, revision);
    }

    ContentBrowserPanel::ContentBrowserPanel(const PanelConfig &config) : Panel(config) {}
    ContentBrowserPanel::~ContentBrowserPanel() = default;

    void ContentBrowserPanel::OnProjectChanged()
    {
        m_referenceSearch.reset();
        m_importDependenciesInspector.reset();
        m_modelHierarchyInspector.reset();
        m_staticHierarchyDialog.reset();
        m_thumbnailCache.reset();
        m_filterBuffer.fill(0);
        m_selectedAssetIndex = -1;
        m_selectedFolder.clear();
        m_modelSettingsAwaitingImport = false;
        m_modelSettingsReference.clear();
        m_modelSettingsError.clear();
        m_openModelReference.clear();
        m_openModelName.clear();
        m_openModelObjects.clear();
        m_surfaceEditorReference.clear();
        m_surfaceDraft = {};
        m_surfaceError.clear();
        m_pendingMenuAction = PendingMenuAction::None;

        m_assetCacheDirty = true;
        m_cachedProject = nullptr;
        m_cachedCatalog.reset();
        m_cachedAssetReferences.clear();
        m_cachedAssetFolders.clear();
        m_cachedAssetFileNames.clear();
        m_cachedAssetRelativePaths.clear();
        m_cachedFolders.clear();
        m_cachedFolderLabels.clear();
        m_cachedFolderChildIndices.clear();
        m_cachedRootFolderIndices.clear();

        m_itemsDirty = true;
        m_items.clear();
        m_selection.Clear();
        m_history.Reset();
        m_scrollToKey.clear();
        m_releaseSelectKey.clear();
        m_deferredEdits.clear();
        m_clipboardKeys.clear();
        m_clipboardCut = false;
        m_rename = {};
        m_renameWhenShown.clear();
        m_pendingDeletePaths.clear();
        m_openDeletePopup = false;
    }

    namespace
    {
        struct AssetTypeIcon
        {
            const char *glyph;
            ImU32 color;
        };

        AssetTypeIcon GetAssetTypeIcon(assets::ProjectAssetType type)
        {
            using Type = assets::ProjectAssetType;
            switch (type)
            {
                case Type::Scene: return {icons::SceneAsset, IM_COL32(112, 196, 132, 255)};
                case Type::Prefab: return {icons::PrefabAsset, IM_COL32(92, 170, 230, 255)};
                case Type::Script: return {icons::ScriptAsset, IM_COL32(124, 206, 108, 255)};
                case Type::Mesh: return {icons::MeshAsset, IM_COL32(236, 160, 84, 255)};
                case Type::Animation: return {icons::AnimationAsset, IM_COL32(104, 204, 214, 255)};
                case Type::AnimationClip: return {icons::AnimationClipAsset, IM_COL32(104, 204, 214, 255)};
                case Type::Model: return {icons::ModelAsset, IM_COL32(236, 132, 84, 255)};
                case Type::Material: return {icons::MaterialAsset, IM_COL32(126, 210, 120, 255)};
                case Type::ShaderGraph: return {icons::ShaderGraphAsset, IM_COL32(150, 128, 232, 255)};
                case Type::AnimationGraph: return {icons::AnimationGraphAsset, IM_COL32(72, 182, 196, 255)};
                case Type::ParticleSystem: return {icons::ParticleSystemAsset, IM_COL32(240, 200, 88, 255)};
                case Type::PostProcessPreset: return {icons::Quality, IM_COL32(186, 140, 226, 255)};
                case Type::Audio: return {icons::AudioAsset, IM_COL32(226, 98, 120, 255)};
                case Type::Texture: return {icons::TextureAsset, IM_COL32(214, 92, 92, 255)};
                case Type::Assembly: return {icons::AssemblyAsset, IM_COL32(150, 160, 176, 255)};
                case Type::ScriptableObject: return {icons::ScriptableObjectAsset, IM_COL32(88, 196, 172, 255)};
                case Type::RmlDocument: return {icons::RmlDocumentAsset, IM_COL32(96, 150, 236, 255)};
                case Type::InputMapping: return {icons::InputMappingAsset, IM_COL32(232, 116, 180, 255)};
                case Type::SurfaceResponse: return {icons::SurfaceResponseAsset, IM_COL32(196, 160, 112, 255)};
                case Type::LoadingScreen: return {icons::LoadingScreenAsset, IM_COL32(214, 176, 92, 255)};
                case Type::UITheme: return {icons::Palette, IM_COL32(232, 128, 196, 255)};
                default: return {icons::File, IM_COL32(150, 156, 166, 255)};
            }
        }

        // Shortens text with an ellipsis so it fits within maxWidth at the current font size.
        std::string FitTextToWidth(const std::string &text, float maxWidth)
        {
            if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
            const float ellipsisWidth = ImGui::CalcTextSize("...").x;
            std::size_t length = text.size();
            while (length > 0)
            {
                --length;
                // Avoid cutting through a UTF-8 multi-byte sequence.
                while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) --length;
                if (ImGui::CalcTextSize(text.data(), text.data() + length).x + ellipsisWidth <= maxWidth) break;
            }
            return text.substr(0, length) + "...";
        }

        void DrawCenteredCardText(ImDrawList *drawList, const std::string &text, float left, float width, float y, ImU32 color)
        {
            const std::string shown = FitTextToWidth(text, width);
            const float textWidth = ImGui::CalcTextSize(shown.c_str()).x;
            drawList->AddText(ImVec2(left + std::max(0.0f, (width - textWidth) * 0.5f), y), color, shown.c_str());
        }

        struct FittedIcon
        {
            ImVec2 size;       // Visible glyph size once fitted.
            ImVec2 inkOffset;  // Offset from the text draw position to the glyph's visible top-left.
            float fontSize = 0.0f;
        };

        // Sizes a glyph by its visible bounds rather than the font's line box, so it can fill a box without
        // the empty ascent/descent space a plain AddText leaves around icons.
        FittedIcon FitIcon(const char *glyph, ImVec2 boxSize)
        {
            unsigned int codepoint = 0;
            ImTextCharFromUtf8(&codepoint, glyph, nullptr);
            ImFontBaked *baked = ImGui::GetFont()->GetFontBaked(std::max(1.0f, boxSize.y));
            const ImFontGlyph *fontGlyph = baked->FindGlyph(static_cast<ImWchar>(codepoint));
            const float glyphWidth = fontGlyph ? fontGlyph->X1 - fontGlyph->X0 : 0.0f;
            const float glyphHeight = fontGlyph ? fontGlyph->Y1 - fontGlyph->Y0 : 0.0f;
            if (glyphWidth <= 0.0f || glyphHeight <= 0.0f) return {};
            const float scale = std::min(boxSize.x / glyphWidth, boxSize.y / glyphHeight);
            return {ImVec2(glyphWidth * scale, glyphHeight * scale),
                    ImVec2(fontGlyph->X0 * scale, fontGlyph->Y0 * scale), baked->Size * scale};
        }

        void DrawFittedIcon(ImDrawList *drawList, const char *glyph, const FittedIcon &fitted, ImVec2 visibleMin, ImU32 color)
        {
            if (fitted.fontSize <= 0.0f) return;
            drawList->AddText(ImGui::GetFont(), fitted.fontSize,
                              ImVec2(std::floor(visibleMin.x) - fitted.inkOffset.x, std::floor(visibleMin.y) - fitted.inkOffset.y),
                              color, glyph);
        }

        void DrawAssetTypeIcon(ImDrawList *drawList, assets::ProjectAssetType type, ImVec2 previewMin, ImVec2 previewMax)
        {
            const AssetTypeIcon icon = GetAssetTypeIcon(type);
            const ImVec2 previewSize(previewMax.x - previewMin.x, previewMax.y - previewMin.y);
            const FittedIcon fitted = FitIcon(icon.glyph, ImVec2(previewSize.x * 0.5f, previewSize.y * 0.5f));
            DrawFittedIcon(drawList, icon.glyph, fitted,
                           ImVec2(previewMin.x + (previewSize.x - fitted.size.x) * 0.5f, previewMin.y + (previewSize.y - fitted.size.y) * 0.5f),
                           icon.color);
        }

        void DrawAssetTypeAccent(ImDrawList *drawList, assets::ProjectAssetType type, ImVec2 previewMin, ImVec2 previewMax, float thickness)
        {
            drawList->AddRectFilled(ImVec2(previewMin.x, previewMax.y - thickness), previewMax,
                                    GetAssetTypeIcon(type).color, thickness, ImDrawFlags_RoundCornersBottom);
        }

        int ResizeStringInput(ImGuiInputTextCallbackData *data)
        {
            if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
            {
                return 0;
            }

            auto &value = *static_cast<std::string *>(data->UserData);
            value.resize(static_cast<std::size_t>(data->BufTextLen));
            data->Buf = value.data();
            return 0;
        }

        bool InputTextString(const char *label, std::string &value)
        {
            return ImGui::InputText(
                label,
                value.data(),
                value.capacity() + 1,
                ImGuiInputTextFlags_CallbackResize,
                ResizeStringInput,
                &value);
        }

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

        std::string GetReferenceRelativePath(const assets::Project &project, const assets::ProjectAssetEntry &asset)
        {
            if (assets::Project::IsProjectAssetReference(asset.reference))
            {
                return NormalizeContentFolder(DisplayAssetReference(asset.reference));
            }

            const auto resolvedPath = project.ResolveAssetReference(asset.reference);
            if (!resolvedPath.empty() && project.IsInAssetDirectory(resolvedPath))
            {
                std::error_code errorCode;
                return NormalizeContentFolder(std::filesystem::relative(resolvedPath, project.GetAssetDirectoryPath(), errorCode));
            }

            return NormalizeContentFolder(DisplayAssetReference(asset.reference));
        }

        std::filesystem::path GetCreateDirectory(const assets::Project &project, std::string_view selectedFolder, std::string_view fallbackFolder)
        {
            if (!selectedFolder.empty())
            {
                return project.GetAssetDirectoryPath() / std::filesystem::path(std::string(selectedFolder));
            }
            return project.GetAssetDirectoryPath() / std::filesystem::path(std::string(fallbackFolder));
        }

        std::filesystem::path MakeUniqueAssetPath(const std::filesystem::path &directory,
                                                  const std::filesystem::path &sourcePath,
                                                  std::string_view preferredName)
        {
            std::string fileName(preferredName);
            for (char &character : fileName)
            {
                if (character == '<' || character == '>' || character == ':' || character == '"' ||
                    character == '/' || character == '\\' || character == '|' || character == '?' || character == '*')
                {
                    character = '_';
                }
            }
            if (fileName.empty()) fileName = sourcePath.stem().string();

            const auto extension = sourcePath.extension();
            auto candidate = directory / (fileName + extension.string());
            for (unsigned int suffix = 1; std::filesystem::exists(candidate); ++suffix)
            {
                candidate = directory / (fileName + "_" + std::to_string(suffix) + extension.string());
            }
            return candidate;
        }

        bool ExportPrefabMesh(const assets::Project &project,
                              const assets::ProjectAssetEntry &prefab,
                              std::string_view selectedFolder,
                              std::string *outputReference,
                              std::string *errorMessage)
        {
            const auto directory = GetCreateDirectory(project, selectedFolder, {});
            std::error_code directoryError;
            std::filesystem::create_directories(directory, directoryError);
            if (directoryError)
            {
                if (errorMessage) *errorMessage = "Failed to create mesh export directory: " + directoryError.message();
                return false;
            }
            const auto prefabPath = project.ResolveAssetReference(prefab.reference);
            const auto meshPath = MakeUniqueAssetPath(
                directory,
                std::filesystem::path(prefabPath.stem().string() + ".plutomesh"),
                prefabPath.stem().string() + "_Static");
            const std::string meshReference = project.MakeAssetReference(meshPath);
            if (!scene::ExportPrefabToStaticMeshAsset(
                    prefab.reference,
                    meshReference,
                    core::Engine::GetInstance().GetAssetManager(),
                    errorMessage))
                return false;
            if (outputReference) *outputReference = meshReference;
            return true;
        }

        bool ExtractModelSubAsset(assets::Project &project,
                                  const assets::ModelSubAsset &object,
                                  std::string_view selectedFolder,
                                  std::string *extractedReference,
                                  std::string *errorMessage)
        {
            auto &engine = core::Engine::GetInstance();
            auto &assetManager = engine.GetAssetManager();
            // Imported locations are virtual in pipeline v3; their bytes live in Library.
            const std::filesystem::path sourcePath = assetManager.ResolveAssetPath(object.reference);
            std::error_code sourceError;
            if (sourcePath.empty() || !std::filesystem::is_regular_file(sourcePath, sourceError))
            {
                if (errorMessage) *errorMessage = "Imported object is missing: " + object.reference;
                return false;
            }

            const auto destinationDirectory = GetCreateDirectory(project, selectedFolder, {});
            const auto catalog = assetManager.GetAssetCatalog();
            if (!catalog)
            {
                if (errorMessage) *errorMessage = "The asset catalog is unavailable; refresh project assets before extracting.";
                return false;
            }
            const auto destinationReference = assetimport::FindAvailableModelExtractionReference(project, *catalog,
                MakeUniqueAssetPath(destinationDirectory, sourcePath, object.name));
            assetimport::ModelObjectExtractionResult result;
            if (!assetimport::ModelObjectExtractionService{}.Extract(project, object.reference,
                    destinationReference, result, errorMessage,
                    object.type == assets::ProjectAssetType::Material)) return false;
            std::string oldLogicalReference;
            if (const auto previousCatalog = assetManager.GetAssetCatalog())
                if (const auto identity = previousCatalog->FindIdentityByLocation(object.reference))
                    assets::SerializeAssetReference(*identity, oldLogicalReference);
            assetManager.SetAssetSnapshot(result.catalog, result.storage);
            assetManager.RefreshImportedAssets(result.changedAssets);
            if (object.type == assets::ProjectAssetType::Material)
            {
                auto *material = assetManager.LoadMaterialAsset(result.projectReference);
                if (auto *scene = engine.GetScene())
                {
                    scene->RemapMaterialAsset(object.reference, result.projectReference, material);
                    if (!oldLogicalReference.empty())
                        scene->RemapMaterialAsset(oldLogicalReference, result.projectReference, material);
                }
            }
            if (extractedReference) *extractedReference = result.projectReference;
            return true;
        }

        std::string DisplayCreatePath(const assets::Project &project, const std::filesystem::path &path, std::string_view fileName)
        {
            std::error_code errorCode;
            auto relativePath = std::filesystem::relative(path / std::filesystem::path(std::string(fileName)), project.GetAssetDirectoryPath(), errorCode);
            return NormalizeContentFolder(errorCode ? path.filename() / std::filesystem::path(std::string(fileName)) : relativePath);
        }

        void OpenAsset(EditorShell &editorShell, const assets::Project &project, const assets::ProjectAssetEntry &asset)
        {
            const auto resolvedPath = project.ResolveAssetReference(asset.reference);
            if (resolvedPath.extension() == ".rml")
            {
                editorShell.OpenRmlDocument(asset.reference);
            }
            else if (resolvedPath.extension() == ".plutoscene")
            {
                editorShell.OpenSceneFromPath(resolvedPath);
            }
            else if (resolvedPath.extension() == ".plutoprefab")
            {
                editorShell.OpenSceneFromPath(resolvedPath);
            }
            else if (resolvedPath.extension() == ".cs")
            {
                editorShell.OpenScriptSource(resolvedPath);
            }
            else if (asset.type == assets::ProjectAssetType::Material)
            {
                editorShell.OpenMaterialAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::Mesh)
            {
                editorShell.OpenMeshAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::ShaderGraph)
            {
                editorShell.OpenShaderGraphAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::AnimationGraph)
            {
                editorShell.OpenAnimationGraphAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::AnimationClip)
            {
                editorShell.OpenAnimationClipAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::ParticleSystem)
            {
                editorShell.OpenParticleSystemAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::LoadingScreen)
            {
                editorShell.OpenLoadingScreenAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::UITheme)
            {
                editorShell.OpenUIThemeEditor(UIThemeTarget::ForAsset(asset.reference));
            }
            else if (asset.type == assets::ProjectAssetType::InputMapping)
            {
                editorShell.OpenInputMappingAsset(asset.reference);
            }
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

        std::string EscapeScriptableText(std::string_view text)
        {
            std::string result;
            for (const char character : text)
            {
                switch (character)
                {
                case '\\': result += "\\\\"; break;
                case '\t': result += "\\t"; break;
                case '\n': result += "\\n"; break;
                default: result += character; break;
                }
            }
            return result;
        }

        std::vector<std::string> SplitScriptableLine(std::string_view line)
        {
            std::vector<std::string> tokens(1);
            bool escaping = false;
            for (const char character : line)
            {
                if (escaping)
                {
                    tokens.back() += character == 't' ? '\t' : character == 'n' ? '\n' : character;
                    escaping = false;
                }
                else if (character == '\\')
                {
                    escaping = true;
                }
                else if (character == '\t')
                {
                    tokens.emplace_back();
                }
                else
                {
                    tokens.back() += character;
                }
            }
            return tokens;
        }

        std::string SerializeScriptableValue(const scripting::ScriptFieldValue &value)
        {
            return std::visit([](const auto &typedValue) -> std::string
            {
                using T = std::decay_t<decltype(typedValue)>;
                if constexpr (std::is_same_v<T, std::monostate>) return {};
                else if constexpr (std::is_same_v<T, bool>) return typedValue ? "true" : "false";
                else if constexpr (std::is_same_v<T, std::string>) return typedValue;
                else if constexpr (std::is_same_v<T, scripting::ScriptReferenceArray>) return scripting::EncodeReferenceArray(typedValue);
                else if constexpr (std::is_same_v<T, glm::vec2>) return std::to_string(typedValue.x) + "," + std::to_string(typedValue.y);
                else if constexpr (std::is_same_v<T, glm::vec3>) return std::to_string(typedValue.x) + "," + std::to_string(typedValue.y) + "," + std::to_string(typedValue.z);
                else return std::to_string(typedValue);
            }, value);
        }

        scripting::ScriptFieldValue ParseScriptableValue(scripting::ScriptFieldType type, const std::string &text)
        {
            try
            {
                if (scripting::IsAssetFieldType(type)) return text;
                switch (type)
                {
                case scripting::ScriptFieldType::Boolean: return text == "true" || text == "1";
                case scripting::ScriptFieldType::Int32: return static_cast<int32_t>(std::stoi(text));
                case scripting::ScriptFieldType::Float: return std::stof(text);
                case scripting::ScriptFieldType::Double: return std::stod(text);
                case scripting::ScriptFieldType::String: return text;
                case scripting::ScriptFieldType::Vector2:
                {
                    glm::vec2 value{};
                    char comma{};
                    std::istringstream(text) >> value.x >> comma >> value.y;
                    return value;
                }
                case scripting::ScriptFieldType::Vector3:
                {
                    glm::vec3 value{};
                    char comma1{}, comma2{};
                    std::istringstream(text) >> value.x >> comma1 >> value.y >> comma2 >> value.z;
                    return value;
                }
                default: return scripting::MakeDefaultFieldValue(type);
                }
            }
            catch (...)
            {
                return scripting::MakeDefaultFieldValue(type);
            }
        }

        bool SaveScriptableObjectAsset(const std::filesystem::path &path,
                                       std::string_view className,
                                       const std::vector<scripting::ScriptFieldDefinition> &fields,
                                       const std::unordered_map<std::string, scripting::ScriptFieldValue> &values)
        {
            std::error_code errorCode;
            std::filesystem::create_directories(path.parent_path(), errorCode);
            std::ofstream output(path, std::ios::out | std::ios::trunc);
            if (!output.is_open()) return false;
            output << "SCRIPTABLE\t" << EscapeScriptableText(className) << '\n';
            for (const auto &field : fields)
            {
                const auto iterator = values.find(field.name);
                const auto &value = iterator != values.end() ? iterator->second : field.defaultValue;
                output << "FIELD\t" << EscapeScriptableText(field.name) << '\t'
                       << static_cast<int>(field.type) << '\t'
                       << EscapeScriptableText(SerializeScriptableValue(value)) << '\n';
            }
            return output.good();
        }

        bool LoadScriptableObjectAsset(const std::filesystem::path &path,
                                       std::string &className,
                                       std::unordered_map<std::string, scripting::ScriptFieldValue> &values)
        {
            std::ifstream input(path);
            std::string line;
            if (!std::getline(input, line)) return false;
            auto header = SplitScriptableLine(line);
            if (header.size() < 2 || header[0] != "SCRIPTABLE") return false;
            className = header[1];
            values.clear();
            while (std::getline(input, line))
            {
                auto tokens = SplitScriptableLine(line);
                if (tokens.size() >= 4 && tokens[0] == "FIELD")
                {
                    values[tokens[1]] = ParseScriptableValue(static_cast<scripting::ScriptFieldType>(std::stoi(tokens[2])), tokens[3]);
                }
            }
            return true;
        }

        std::string BuildEntityNameForMeshReference(const std::string &reference)
        {
            std::string displayName = DisplayAssetReference(reference);
            if (displayName.rfind("engine://", 0) == 0)
            {
                const auto separator = displayName.find_last_of('/');
                return separator == std::string::npos ? displayName : displayName.substr(separator + 1);
            }

            std::filesystem::path path(displayName);
            const auto stem = path.stem().string();
            return stem.empty() ? displayName : stem;
        }

        bool IsSupportedImportedModelReference(const std::string &reference)
        {
            const auto extension = std::filesystem::path(reference).extension().string();
            return extension == ".gltf" || extension == ".glb" || extension == ".fbx" ||
                   extension == ".GLTF" || extension == ".GLB" || extension == ".FBX";
        }

        std::string BrowseSourceModelPath()
        {
            constexpr std::array kModelFilters{FileDialogFilter{"3D Models", "*.gltf;*.glb;*.fbx"},
                                               FileDialogFilter{"glTF", "*.gltf;*.glb"}, FileDialogFilter{"FBX", "*.fbx"}};
            std::string error;
            const auto path = ShowOpenFileDialog(kModelFilters, &error);
            if (!error.empty()) EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Error, error);
            return path.empty() ? std::string{} : path.lexically_normal().string();
        }

        bool CopySourceModelPackageToAssets(const assets::Project &project,
                                            const std::filesystem::path &sourcePath,
                                            std::filesystem::path &importedSourcePath,
                                            std::string *errorMessage)
        {
            std::error_code errorCode;
            const auto normalizedSourcePath = std::filesystem::weakly_canonical(sourcePath, errorCode);
            const auto sourceFilePath = errorCode ? sourcePath.lexically_normal() : normalizedSourcePath;
            if (!std::filesystem::exists(sourceFilePath))
            {
                if (errorMessage)
                {
                    *errorMessage = "Selected model does not exist.";
                }
                return false;
            }

            if (!IsSupportedImportedModelReference(sourceFilePath.string()))
            {
                if (errorMessage)
                {
                    *errorMessage = "Selected file is not a supported source model. Use .gltf, .glb, or .fbx.";
                }
                return false;
            }

            if (project.IsInAssetDirectory(sourceFilePath))
            {
                importedSourcePath = sourceFilePath;
                return true;
            }

            std::string packageName = SanitizeAssetFileName(sourceFilePath.stem().string());
            if (packageName.empty())
            {
                packageName = "ImportedModel";
            }
            const auto modelsDirectory = project.GetAssetDirectoryPath() / "SourceModels";
            std::filesystem::create_directories(modelsDirectory, errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to create source model directory: " + errorCode.message();
                }
                return false;
            }

            // Copy only the files the model references, never the rest of the
            // folder it was picked from. A fresh folder per import keeps
            // previously imported copies intact.
            assetimport::ModelPackageCopyPlan plan;
            if (!assetimport::CopyModelSourcePackage(sourceFilePath, MakeUniqueContentPath(modelsDirectory, packageName, {}), plan, errorMessage))
            {
                return false;
            }
            for (const auto &file : plan.flattened)
            {
                EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Warning,
                                               "'" + file.filename().string() + "' is outside the model's folder and was placed beside it.");
            }
            importedSourcePath = plan.copiedSource;
            return true;
        }

        std::vector<render::Material *> LoadMaterialReferences(core::Engine &engine, const std::vector<std::string> &materialReferences)
        {
            std::vector<render::Material *> materials;
            materials.reserve(materialReferences.size());
            for (const auto &materialReference : materialReferences)
            {
                auto *material = materialReference.empty()
                                     ? engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference))
                                     : engine.GetAssetManager().LoadMaterialAsset(materialReference);
                materials.push_back(material);
            }
            if (materials.empty())
            {
                materials.push_back(engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)));
            }
            return materials;
        }

        std::string FindSiblingAnimationAssetReference(const assets::Project *project, const std::string &meshReference)
        {
            if (!project || meshReference.empty() || assets::Project::IsEngineAssetReference(meshReference))
            {
                return {};
            }

            const auto meshPath = project->ResolveAssetReference(meshReference);
            const auto expectedAnimationPath = meshPath.parent_path() / (meshPath.stem().string() + ".plutoanim");
            if (std::filesystem::exists(expectedAnimationPath))
            {
                return project->MakeAssetReference(expectedAnimationPath);
            }

            for (const auto &asset : project->GetManifest().assetEntries)
            {
                if (asset.type != assets::ProjectAssetType::Animation)
                {
                    continue;
                }

                const auto animationPath = project->ResolveAssetReference(asset.reference);
                if (animationPath.parent_path() == meshPath.parent_path())
                {
                    return asset.reference;
                }
            }
            return {};
        }

        void AttachAnimationAsset(scene::Entity &entity, const std::string &animationReference)
        {
            if (animationReference.empty())
            {
                return;
            }

            auto *animationComponent = entity.GetComponent<scene::AnimationComponent>();
            if (!animationComponent)
            {
                animationComponent = entity.CreateComponent<scene::AnimationComponent>();
            }

            animationComponent->SetAnimationAssetReference(animationReference);
        }

        std::string MakeClipAssetFileName(const std::filesystem::path &sourcePath, const render::AnimationClip &clip, std::size_t clipIndex)
        {
            std::string clipName = SanitizeAssetFileName(clip.name);
            if (clipName.empty())
            {
                clipName = "Clip_" + std::to_string(clipIndex);
            }
            return sourcePath.stem().string() + "_" + clipName + "_" + std::to_string(clipIndex) + ".plutoclip";
        }

        bool SaveImportedAnimationClips(const assets::Project &project,
                                        const std::filesystem::path &clipDirectory,
                                        const std::filesystem::path &sourcePath,
                                        const std::vector<render::AnimationClip> &clips,
                                        std::vector<std::string> &clipReferences,
                                        std::string *errorMessage)
        {
            auto &assetManager = core::Engine::GetInstance().GetAssetManager();
            for (std::size_t clipIndex = 0; clipIndex < clips.size(); ++clipIndex)
            {
                const auto clipPath = clipDirectory / MakeClipAssetFileName(sourcePath, clips[clipIndex], clipIndex);
                const std::string clipReference = project.MakeAssetReference(clipPath);
                if (!assetManager.SaveAnimationClipAsset(clipReference, clips[clipIndex], errorMessage))
                {
                    return false;
                }
                if (std::find(clipReferences.begin(), clipReferences.end(), clipReference) == clipReferences.end())
                {
                    clipReferences.push_back(clipReference);
                }
            }
            return true;
        }

        bool EnsureAnimationAssetUsesClipReferences(const assets::Project &project,
                                                    const std::string &animationReference,
                                                    std::vector<std::string> &clipReferences,
                                                    std::string *errorMessage)
        {
            auto &assetManager = core::Engine::GetInstance().GetAssetManager();
            if (assetManager.LoadAnimationClipReferences(animationReference, clipReferences))
            {
                return true;
            }

            std::vector<render::AnimationClip> embeddedClips;
            if (!assetManager.LoadAnimationAsset(animationReference, embeddedClips))
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to load animation asset: " + animationReference;
                }
                return false;
            }

            const auto animationPath = project.ResolveAssetReference(animationReference);
            const auto clipDirectory = animationPath.parent_path() / "Clips";
            if (!SaveImportedAnimationClips(project, clipDirectory, animationPath, embeddedClips, clipReferences, errorMessage))
            {
                return false;
            }

            return assetManager.SaveAnimationAssetReferences(animationReference, clipReferences, errorMessage);
        }

        bool AddClipsFromSourceModelToAnimationAsset(const assets::Project &project,
                                                     const std::string &animationReference,
                                                     const std::filesystem::path &sourcePath,
                                                     std::string *errorMessage)
        {
            auto &engine = core::Engine::GetInstance();
            assetimport::ImportedMeshSourceAsset importedSourceAsset;
            try
            {
                importedSourceAsset = engine.GetMeshImporter().ImportMeshSourceAsset(sourcePath.string());
            }
            catch (const std::exception &exception)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to import animation source '" + sourcePath.string() + "': " + exception.what();
                }
                return false;
            }

            if (importedSourceAsset.animations.empty())
            {
                if (errorMessage)
                {
                    *errorMessage = "Source model contained no animation clips: " + sourcePath.string();
                }
                return false;
            }

            std::vector<std::string> clipReferences;
            if (!EnsureAnimationAssetUsesClipReferences(project, animationReference, clipReferences, errorMessage))
            {
                return false;
            }

            const auto animationPath = project.ResolveAssetReference(animationReference);
            const auto clipDirectory = animationPath.parent_path() / "Clips";
            if (!SaveImportedAnimationClips(project, clipDirectory, sourcePath, importedSourceAsset.animations, clipReferences, errorMessage))
            {
                return false;
            }

            return engine.GetAssetManager().SaveAnimationAssetReferences(animationReference, clipReferences, errorMessage);
        }

        bool ConfigureMeshComponentForReference(scene::Entity &entity,
                                                const std::string &reference,
                                                std::string *errorMessage,
                                                bool attachAnimation = true,
                                                const std::string &materialBindingReference = {})
        {
            auto &engine = core::Engine::GetInstance();
            auto *meshComponent = entity.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{});

            if (auto *mesh = engine.GetAssetManager().LoadMeshAsset(reference))
            {
                meshComponent->SetMesh(mesh);
                meshComponent->SetSourceMeshPath(reference);
                if (assets::Project::IsEngineAssetReference(reference))
                {
                    meshComponent->SetModelObjectIdentity(reference, 1);
                }
                else
                {
                    const auto &metadata = engine.GetAssetManager().GetMeshAssetMetadata(reference);
                    if (!metadata.sourceAssetId.empty() && metadata.sourceObjectId != 0)
                        meshComponent->SetModelObjectIdentity(metadata.sourceAssetId, metadata.sourceObjectId);
                    else
                        meshComponent->SetModelObjectIdentity({}, 0);
                }
                const auto &materialReferences = engine.GetAssetManager().GetMeshAssetMaterialReferences(
                    materialBindingReference.empty() ? reference : materialBindingReference);
                const auto materials = LoadMaterialReferences(engine, materialReferences);
                meshComponent->SetMaterials(materials);
                for (size_t materialSlotIndex = 0; materialSlotIndex < materialReferences.size(); ++materialSlotIndex)
                {
                    meshComponent->SetMaterialAssetForMaterialSlot(materialSlotIndex, materialReferences[materialSlotIndex]);
                }
                if (attachAnimation)
                {
                    AttachAnimationAsset(entity, FindSiblingAnimationAssetReference(EditorShell::GetInstance().GetProject(), reference));
                }
                return true;
            }
            if (errorMessage) *errorMessage = "Mesh object is not a valid imported model artifact: " + reference;
            return false;
        }

        bool ImportSourceModelAsset(assets::Project &project, const assets::ProjectAssetEntry &asset, std::string *errorMessage,
                                    std::optional<assetimport::MeshImportOptions> options = std::nullopt)
        {
            if (asset.type != assets::ProjectAssetType::Model) return false;
            if (core::Engine::GetInstance().IsRuntimeRunning())
            {
                if (errorMessage) *errorMessage = "Stop Play before importing a model.";
                return false;
            }
            const auto sourceReference = asset.reference;
            const auto started = std::chrono::steady_clock::now();
            assetimport::ModelImportRequest request;
            request.sourceReference = sourceReference;
            request.options = options;
            request.progress = [&](std::string_view stage)
            {
                const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                std::clog << "[Model import] " << stage << " (" << elapsed << " ms): " << sourceReference << std::endl;
            };
            auto &assetManager = core::Engine::GetInstance().GetAssetManager();
            assets::AssetMetadata sourceMetadata;
            const auto status = assets::LoadAssetMetadata(assets::GetAssetMetadataPath(project.ResolveAssetReference(sourceReference)), sourceMetadata);
            const auto previous = scene::CaptureModelAssetSnapshot(assetManager, status == assets::AssetMetadataStatus::Success ? sourceMetadata.id : std::string{});
            assetimport::ModelImportResult result;
            if (!assetimport::ModelImportService{}.Import(project, request, result, errorMessage)) return false;
            return EditorShell::GetInstance().PublishModelImportResult(sourceReference, result, previous, errorMessage);
        }

        bool ImportExternalSourceModelIntoAssets(assets::Project &project, std::string *importedReference, std::string *errorMessage)
        {
            if (core::Engine::GetInstance().IsRuntimeRunning())
            {
                if (errorMessage) *errorMessage = "Stop Play before importing a model.";
                return false;
            }
            if (EditorShell::GetInstance().IsModelImportRunning())
            {
                if (errorMessage) *errorMessage = "Finish or cancel the active model import first.";
                return false;
            }
            const std::string selectedPath = BrowseSourceModelPath();
            if (selectedPath.empty())
            {
                return false;
            }

            std::filesystem::path importedSourcePath;
            if (!CopySourceModelPackageToAssets(project, selectedPath, importedSourcePath, errorMessage))
            {
                return false;
            }

            const std::string sourceReference = project.MakeAssetReference(importedSourcePath);
            assets::ProjectAssetEntry sourceAsset{};
            sourceAsset.reference = sourceReference;
            sourceAsset.type = assets::ProjectAssetType::Model;
            std::error_code errorCode;
            sourceAsset.size = std::filesystem::file_size(importedSourcePath, errorCode);

            if (!ImportSourceModelAsset(project, sourceAsset, errorMessage))
            {
                return false;
            }

            if (importedReference)
            {
                *importedReference = sourceReference;
            }
            return true;
        }

    }

    bool InstantiateMeshAssetIntoScene(std::string reference,
                                       scene::Entity *parent,
                                       std::string materialBindingReference)
    {
        if (reference.empty() || assets::Project::GetAssetTypeForReference(reference) != assets::ProjectAssetType::Mesh)
        {
            return false;
        }

        auto &editorShell = EditorShell::GetInstance();
        auto *scene = editorShell.GetEngine().GetScene();
        if (!scene)
        {
            return false;
        }

        scene::Entity *createdEntity = nullptr;
        std::string errorMessage;
        editorShell.ExecuteSceneEdit("Instantiate Model Mesh Object",
                                     [scene, parent, reference, materialBindingReference, &createdEntity, &errorMessage]()
                                     {
                                         auto &engine = core::Engine::GetInstance();
                                         auto *mesh = engine.GetAssetManager().LoadMeshAsset(reference);
                                         if (!mesh)
                                         {
                                             errorMessage = "Mesh object is not a valid imported model artifact: " + reference;
                                             return;
                                         }

                                         auto root = std::make_unique<scene::Entity>(scene::EntityConfig{
                                             .name = BuildEntityNameForMeshReference(reference),
                                         });
                                         createdEntity = scene->AddEntity(std::move(root), parent);
                                         if (!createdEntity)
                                         {
                                             return;
                                         }

                                         if (mesh->GetSubmeshCount() <= 1)
                                         {
                                             if (ConfigureMeshComponentForReference(
                                                     *createdEntity, reference, &errorMessage, true, materialBindingReference))
                                             {
                                                 return;
                                             }
                                         }
                                         else
                                         {
                                             AttachAnimationAsset(
                                                 *createdEntity,
                                                 FindSiblingAnimationAssetReference(EditorShell::GetInstance().GetProject(), reference));

                                             bool allChildrenCreated = true;
                                             for (size_t submeshIndex = 0; submeshIndex < mesh->GetSubmeshCount(); ++submeshIndex)
                                             {
                                                 const auto &submesh = mesh->GetSubmesh(submeshIndex);
                                                 auto child = std::make_unique<scene::Entity>(scene::EntityConfig{
                                                     .name = submesh.name.empty()
                                                                 ? "Submesh " + std::to_string(submeshIndex)
                                                                 : submesh.name,
                                                 });
                                                 auto *childEntity = scene->AddEntity(std::move(child), createdEntity);
                                                 if (!childEntity ||
                                                     !ConfigureMeshComponentForReference(
                                                         *childEntity, reference, &errorMessage, false, materialBindingReference))
                                                 {
                                                     allChildrenCreated = false;
                                                     break;
                                                 }
                                                 childEntity->GetComponent<scene::MeshComponent>()->SetSubmeshRange(
                                                     static_cast<int>(submeshIndex), 1);
                                             }
                                             if (allChildrenCreated)
                                             {
                                                 return;
                                             }
                                         }

                                         if (createdEntity)
                                         {
                                             scene->RemoveEntity(createdEntity);
                                             createdEntity = nullptr;
                                         }
                                     });

        if (createdEntity)
        {
            editorShell.SetSelectedEntity(createdEntity);
            editorShell.MarkSceneDirty();
            return true;
        }

        if (!errorMessage.empty())
        {
            editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage);
        }
        return false;
    }

    bool InstantiateModelAssetIntoScene(const std::string &reference, scene::Entity *parent)
    {
        auto &editorShell = EditorShell::GetInstance();
        auto *project = editorShell.GetProject();
        if (!project || assets::Project::GetAssetTypeForReference(reference) != assets::ProjectAssetType::Model)
        {
            return false;
        }

        std::string meshReference, bindings, error;
        if (!assets::ResolveModelPlacementMesh(*project, reference, meshReference, bindings, &error))
        {
            editorShell.Log(EditorShell::ConsoleSeverity::Error, error);
            return false;
        }
        return InstantiateMeshAssetIntoScene(std::move(meshReference), parent, std::move(bindings));
    }

    namespace
    {
        bool AddAssetInFrontOfCamera(const std::string &reference)
        {
            auto &shell = EditorShell::GetInstance();
            auto *scene = shell.GetScene();
            if (!scene || shell.GetEngine().IsRuntimeRunning()) return false;
            SurfacePlacementSession placement;
            std::string error;
            if (!placement.Begin(reference, shell.GetEngine().GetAssetManager(), error, shell.GetProject()) ||
                !placement.Update(PlacementSurfaceHit{}, nullptr, {}, error))
            {
                shell.Log(EditorShell::ConsoleSeverity::Error, error);
                return false;
            }
            const auto &camera = shell.GetEditorCamera();
            auto rotation = glm::rotate(glm::mat4(1), glm::radians(camera.yawDegrees), glm::vec3(0, 1, 0));
            rotation = glm::rotate(rotation, glm::radians(camera.pitchDegrees), glm::vec3(1, 0, 0));
            const ViewportPickRay ray{camera.position, -glm::vec3(rotation[2])};
            const auto target = MakeCameraPlacementHit(ray, placement.GetPreviewSize());
            if (!target || !placement.Update(*target, nullptr, {}, error))
            {
                shell.Log(EditorShell::ConsoleSeverity::Error, error.empty() ? "Invalid editor camera placement target." : error);
                return false;
            }
            scene::Entity *created = nullptr;
            shell.ExecuteSceneEdit("Add Asset to Scene", [&] { created = placement.Stamp(*scene, nullptr, error); });
            if (!created)
            {
                shell.Log(EditorShell::ConsoleSeverity::Error, error);
                return false;
            }
            shell.SetSelectedEntity(created);
            return true;
        }
    }

    std::string ContentBrowserPanel::RevealAsset(std::string reference)
    {
        auto *project = EditorShell::GetInstance().GetProject();
        if (!project) return reference;
        const auto path = project->ResolveAssetReference(reference);
        if (path.extension() == ".plutomodel")
        {
            assets::ModelAsset model;
            if (assets::LoadModelAsset(path.string(), model) && !model.sourceReference.empty()) reference = model.sourceReference;
        }
        EditorShell::GetInstance().RefreshProjectAssets();
        m_filterBuffer.fill(0);
        m_assetCacheDirty = true;
        NavigateTo(NormalizeContentFolder(project->ResolveAssetReference(reference).parent_path()
                                              .lexically_relative(project->GetAssetDirectoryPath())));
        m_selection.Select(reference);
        m_scrollToKey = reference;
        const auto &entries = project->GetManifest().assetEntries;
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) { return entry.reference == reference; });
        m_selectedAssetIndex = found == entries.end() ? -1 : static_cast<int>(found - entries.begin());
        SetOpen(true);
        return reference;
    }

    void ContentBrowserPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        auto *project = editorShell.GetProject();
        if (!project)
        {
            if (m_referenceSearch) m_referenceSearch->Close();
            ImGui::TextDisabled("No project loaded.");
            return;
        }

        if (m_referenceSearch)
        {
            const auto reveal = [&](std::string reference) { return RevealAsset(std::move(reference)); };
            m_referenceSearch->Render(project->GetAssetDirectoryPath(), reveal, [&](const std::string &reference) {
                const auto owner = reveal(reference);
                OpenAsset(editorShell, *project, assets::ProjectAssetEntry{owner, 0, assets::Project::GetAssetTypeForReference(owner)});
            });
        }

        RenderToolbar(*project);

        const auto pendingMenuAction = m_pendingMenuAction;
        m_pendingMenuAction = PendingMenuAction::None;
        switch (pendingMenuAction)
        {
        case PendingMenuAction::ImportModel:
        {
            std::string importedReference;
            std::string errorMessage;
            if (ImportExternalSourceModelIntoAssets(*project, &importedReference, &errorMessage))
            {
                EditorShell::GetInstance().RefreshProjectAssets();
                m_assetCacheDirty = true;
                editorShell.MarkProjectDirty();
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "Imported model into assets: " + importedReference);
            }
            else if (!errorMessage.empty())
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage);
            }
            break;
        }
        case PendingMenuAction::ImportFiles:
        {
            constexpr std::array kAnyFile{FileDialogFilter{"All Files", "*"}};
            std::string error;
            const auto paths = ShowOpenFilesDialog(kAnyFile, &error);
            if (!error.empty()) editorShell.Log(EditorShell::ConsoleSeverity::Error, error);
            if (!paths.empty()) ImportExternalItems(*project, paths, m_selectedFolder);
            break;
        }
        case PendingMenuAction::CreateMaterial:
            ImGui::OpenPopup("Create Material Asset");
            break;
        case PendingMenuAction::CreateSurfaceResponse:
            ImGui::OpenPopup("Create Surface Response Asset");
            break;
        case PendingMenuAction::CreateParticleSystem:
            ImGui::OpenPopup("Create Particle System Asset");
            break;
        case PendingMenuAction::CreatePostProcessPreset:
            ImGui::OpenPopup("Create Post Process Preset Asset");
            break;
        case PendingMenuAction::CreateRenderTexture:
            ImGui::OpenPopup("Create Render Texture Asset");
            break;
        case PendingMenuAction::CreateShaderGraph:
            ImGui::OpenPopup("Create Shader Graph Asset");
            break;
        case PendingMenuAction::CreateAnimationGraph:
            ImGui::OpenPopup("Create Animation Graph Asset");
            break;
        case PendingMenuAction::CreateScriptableObject:
            ImGui::OpenPopup("Create Scriptable Object Asset");
            break;
        case PendingMenuAction::CreateRmlDocument:
            ImGui::OpenPopup("Create RmlUi Document");
            break;
        case PendingMenuAction::CreateInputMapping:
            ImGui::OpenPopup("Create Input Mapping Asset");
            break;
        case PendingMenuAction::CreateUITheme:
            ImGui::OpenPopup("Create UI Theme");
            break;
        case PendingMenuAction::None:
        default:
            break;
        }

        if (ImGui::BeginPopupModal("Create UI Theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newUIThemeNameBuffer.data(), m_newUIThemeNameBuffer.size());
            ImGui::Checkbox("Start with ready-made styles", &m_newUIThemeStarter);
            ImGui::SetItemTooltip("Panel, Label, Heading, Button, Primary Button, Input and more, with shared colours");
            const std::string name = SanitizeAssetFileName(m_newUIThemeNameBuffer.data());
            if (ImGui::Button("Create") && !name.empty())
            {
                std::string error;
                const auto reference = CreateUIThemeAsset(GetCreateDirectory(*project, m_selectedFolder, "UI/Themes"), name,
                                                          m_newUIThemeStarter ? scene::MakeStarterUITheme() : scene::UITheme{}, error);
                if (!reference.empty())
                {
                    m_assetCacheDirty = true;
                    editorShell.MarkProjectDirty();
                    editorShell.OpenUIThemeEditor(UIThemeTarget::ForAsset(reference));
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Input Mapping Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newInputMappingNameBuffer.data(), m_newInputMappingNameBuffer.size());
            const std::string name = SanitizeAssetFileName(m_newInputMappingNameBuffer.data());
            if (ImGui::Button("Create") && !name.empty())
            {
                const auto directory = GetCreateDirectory(*project, m_selectedFolder, "Input");
                std::error_code ec;
                std::filesystem::create_directories(directory, ec);
                const auto path = directory / (name + ".plutoinput");
                std::ofstream output(path, std::ios::binary | std::ios::trunc);
                output << "{\n  \"Actions\": []\n}\n";
                if (output)
                {
                    const auto reference = project->MakeAssetReference(path);
                    EditorShell::GetInstance().RefreshProjectAssets();
                    m_assetCacheDirty = true;
                    editorShell.MarkProjectDirty();
                    editorShell.OpenInputMappingAsset(reference);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Material Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newMaterialNameBuffer.data(), m_newMaterialNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newMaterialNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "Materials");
            if (!sanitizedName.empty())
            {
                const std::string fileName = sanitizedName + ".plutomaterial";
                ImGui::TextDisabled("Creates %s", DisplayCreatePath(*project, createDirectory, fileName).c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter a material name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto materialPath = createDirectory / (sanitizedName + ".plutomaterial");
                const std::string reference = project->MakeAssetReference(materialPath);
                render::MaterialConfig config;
                config.color = glm::vec4(0.82f, 0.84f, 0.88f, 1.0f);
                config.metallic = 0.0f;
                config.roughness = 0.55f;

                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveMaterialAsset(reference, config, &errorMessage))
                {
                    EditorShell::GetInstance().RefreshProjectAssets();
                    m_assetCacheDirty = true;
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

        if (ImGui::BeginPopupModal("Create RmlUi Document", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newRmlDocumentNameBuffer.data(), m_newRmlDocumentNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newRmlDocumentNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "UI");
            ImGui::TextDisabled("Creates an .rml document and matching .rcss stylesheet.");
            if (!sanitizedName.empty())
            {
                ImGui::TextDisabled(
                    "Creates %s and %s",
                    DisplayCreatePath(*project, createDirectory, sanitizedName + ".rml").c_str(),
                    DisplayCreatePath(*project, createDirectory, sanitizedName + ".rcss").c_str());
            }
            if (!m_rmlDocumentCreateError.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_rmlDocumentCreateError.c_str());
            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                m_rmlDocumentCreateError.clear();
                std::error_code errorCode;
                std::filesystem::create_directories(createDirectory, errorCode);
                const auto rmlPath = createDirectory / (sanitizedName + ".rml");
                const auto rcssPath = createDirectory / (sanitizedName + ".rcss");

                if (errorCode)
                {
                    m_rmlDocumentCreateError = "Could not create the destination folder: " + errorCode.message();
                }
                else
                {
                    const bool rmlExists = std::filesystem::exists(rmlPath, errorCode);
                    const bool rcssExists = !errorCode && std::filesystem::exists(rcssPath, errorCode);
                    if (errorCode)
                    {
                        m_rmlDocumentCreateError = "Could not inspect the destination: " + errorCode.message();
                    }
                    else if (rmlExists || rcssExists)
                        m_rmlDocumentCreateError = "An RML or RCSS asset with that name already exists.";
                    else
                    {
                        {
                            std::ofstream rml(rmlPath, std::ios::out | std::ios::trunc);
                            rml << "<rml>\n<head>\n  <title>" << sanitizedName
                                << "</title>\n  <link type=\"text/rcss\" href=\"" << sanitizedName
                                << ".rcss\"/>\n</head>\n<body>\n  <div id=\"root\">"
                                << sanitizedName << "</div>\n</body>\n</rml>\n";
                            rml.flush();
                            if (!rml)
                                m_rmlDocumentCreateError = "Failed to write " + rmlPath.filename().string() + ".";
                        }

                        if (m_rmlDocumentCreateError.empty())
                        {
                            std::ofstream rcss(rcssPath, std::ios::out | std::ios::trunc);
                            rcss << "body { width: 100%; height: 100%; margin: 0; color: #ffffff; }\n"
                                    "#root { padding: 24px; }\n";
                            rcss.flush();
                            if (!rcss)
                                m_rmlDocumentCreateError = "Failed to write " + rcssPath.filename().string() + ".";
                        }

                        if (m_rmlDocumentCreateError.empty())
                        {
                            EditorShell::GetInstance().RefreshProjectAssets();
                            m_assetCacheDirty = true;
                            editorShell.MarkProjectDirty();
                            editorShell.Log(EditorShell::ConsoleSeverity::Info,
                                            "Created RmlUi document: " + project->MakeAssetReference(rmlPath));
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }

                if (!m_rmlDocumentCreateError.empty())
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, m_rmlDocumentCreateError);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Scriptable Object Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            auto &scriptEngine = editorShell.GetEngine().GetScriptEngine();
            const auto classNames = scriptEngine.GetScriptableObjectClassNames();
            ImGui::InputText("Name", m_newScriptableObjectNameBuffer.data(), m_newScriptableObjectNameBuffer.size());
            if (classNames.empty())
            {
                ImGui::TextDisabled("Build a concrete ScriptableObject subclass first.");
            }
            else
            {
                m_newScriptableObjectClassIndex = std::clamp(m_newScriptableObjectClassIndex, 0, static_cast<int>(classNames.size()) - 1);
                if (ImGui::BeginCombo("Type", classNames[static_cast<std::size_t>(m_newScriptableObjectClassIndex)].c_str()))
                {
                    for (int index = 0; index < static_cast<int>(classNames.size()); ++index)
                    {
                        if (ImGui::Selectable(classNames[static_cast<std::size_t>(index)].c_str(), index == m_newScriptableObjectClassIndex))
                        {
                            m_newScriptableObjectClassIndex = index;
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            const std::string sanitizedName = SanitizeAssetFileName(m_newScriptableObjectNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "Data");
            ImGui::BeginDisabled(sanitizedName.empty() || classNames.empty());
            if (ImGui::Button("Create"))
            {
                const auto &className = classNames[static_cast<std::size_t>(m_newScriptableObjectClassIndex)];
                const auto *definition = scriptEngine.FindClass(className);
                std::unordered_map<std::string, scripting::ScriptFieldValue> values;
                if (definition)
                {
                    for (const auto &field : definition->fields) values[field.name] = field.defaultValue;
                }
                const auto assetPath = createDirectory / (sanitizedName + ".plutoscriptable");
                if (definition && SaveScriptableObjectAsset(assetPath, className, definition->fields, values))
                {
                    EditorShell::GetInstance().RefreshProjectAssets();
                    m_assetCacheDirty = true;
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created scriptable object: " + project->MakeAssetReference(assetPath));
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, "Failed to create scriptable object asset.");
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Surface Response Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newSurfaceNameBuffer.data(), m_newSurfaceNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newSurfaceNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "Surfaces");
            if (!sanitizedName.empty())
            {
                const std::string fileName = sanitizedName + ".plutosurface";
                ImGui::TextDisabled("Creates %s", DisplayCreatePath(*project, createDirectory, fileName).c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter a surface response name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty() || std::filesystem::exists(createDirectory / (sanitizedName + ".plutosurface")));
            if (ImGui::Button("Create"))
            {
                const auto particlePath = createDirectory / (sanitizedName + ".plutosurface");
                const std::string reference = project->MakeAssetReference(particlePath);
                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveSurfaceResponseAsset(reference, assets::SurfaceResponseAsset{}, &errorMessage))
                {
                    EditorShell::GetInstance().RefreshProjectAssets();
                    m_assetCacheDirty = true;
                    m_surfaceEditorReference.clear();
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created surface response: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create surface response." : errorMessage);
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

        if (ImGui::BeginPopupModal("Create Particle System Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newParticleSystemNameBuffer.data(), m_newParticleSystemNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newParticleSystemNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "Particles");
            if (!sanitizedName.empty())
            {
                const std::string fileName = sanitizedName + ".plutoparticles";
                ImGui::TextDisabled("Creates %s", DisplayCreatePath(*project, createDirectory, fileName).c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter a particle system name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto particlePath = createDirectory / (sanitizedName + ".plutoparticles");
                const std::string reference = project->MakeAssetReference(particlePath);
                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveParticleSystemAsset(reference, assets::CreateDefaultParticleSystemAsset(), &errorMessage))
                {
                    EditorShell::GetInstance().RefreshProjectAssets();
                    m_assetCacheDirty = true;
                    editorShell.OpenParticleSystemAsset(reference);
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created particle system: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create particle system." : errorMessage);
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

        if (ImGui::BeginPopupModal("Create Post Process Preset Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newPostProcessPresetNameBuffer.data(), m_newPostProcessPresetNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newPostProcessPresetNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "PostProcessing");
            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto path = createDirectory / (sanitizedName + ".plutopostprocess");
                const std::string reference = project->MakeAssetReference(path);
                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SavePostProcessPresetAsset(reference, assets::CreateDefaultPostProcessPresetAsset(), &errorMessage))
                {
                    EditorShell::GetInstance().RefreshProjectAssets();
                    m_assetCacheDirty = true;
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created post process preset: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Render Texture Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newRenderTextureNameBuffer.data(), m_newRenderTextureNameBuffer.size());
            ImGui::DragInt2("Size", m_newRenderTextureSize, 1.0f, render::RenderTextureDescriptor::kMinSize,
                            render::RenderTextureDescriptor::kMaxSize);
            const std::string sanitizedName = SanitizeAssetFileName(m_newRenderTextureNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "Textures");
            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto path = createDirectory / (sanitizedName + std::string(render::RenderTexture::kAssetExtension));
                std::error_code directoryError;
                std::filesystem::create_directories(createDirectory, directoryError);
                std::string errorMessage;
                if (render::RenderTexture::SaveDescriptor(
                        path, {.width = m_newRenderTextureSize[0], .height = m_newRenderTextureSize[1]}, &errorMessage))
                {
                    EditorShell::GetInstance().RefreshProjectAssets();
                    m_assetCacheDirty = true;
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info,
                                    "Created render texture: " + project->MakeAssetReference(path));
                    ImGui::CloseCurrentPopup();
                }
                else
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Shader Graph Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newShaderGraphNameBuffer.data(), m_newShaderGraphNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newShaderGraphNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "Shaders");
            if (!sanitizedName.empty())
            {
                const std::string fileName = sanitizedName + ".plutoshadergraph";
                ImGui::TextDisabled("Creates %s", DisplayCreatePath(*project, createDirectory, fileName).c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter a shader graph name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto graphPath = createDirectory / (sanitizedName + ".plutoshadergraph");
                const std::string reference = project->MakeAssetReference(graphPath);
                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveShaderGraphAsset(reference, render::CreateDefaultShaderGraph(), &errorMessage))
                {
                    EditorShell::GetInstance().RefreshProjectAssets();
                    m_assetCacheDirty = true;
                    editorShell.OpenShaderGraphAsset(reference);
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created shader graph: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create shader graph." : errorMessage);
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

        if (ImGui::BeginPopupModal("Create Animation Graph Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newAnimationGraphNameBuffer.data(), m_newAnimationGraphNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newAnimationGraphNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "AnimGraphs");
            if (!sanitizedName.empty())
            {
                const std::string fileName = sanitizedName + ".plutoanimgraph";
                ImGui::TextDisabled("Creates %s", DisplayCreatePath(*project, createDirectory, fileName).c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter an animation graph name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto graphPath = createDirectory / (sanitizedName + ".plutoanimgraph");
                const std::string reference = project->MakeAssetReference(graphPath);
                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveAnimationGraphAsset(reference, assets::CreateDefaultAnimationGraphAsset(), &errorMessage))
                {
                    EditorShell::GetInstance().RefreshProjectAssets();
                    m_assetCacheDirty = true;
                    editorShell.OpenAnimationGraphAsset(reference);
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created animation graph: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create animation graph." : errorMessage);
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

        const auto &assets = project->GetManifest().assetEntries;
        RefreshRegistryCache(*project);
        RebuildItems(*project);

        const bool showDetails = m_selectedAssetIndex >= 0 && m_selectedAssetIndex < static_cast<int>(assets.size());
        if (!ImGui::BeginTable("ContentBrowserLayout", 2,
                               ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                               ImGui::GetContentRegionAvail()))
        {
            return;
        }
        ImGui::TableSetupColumn("Explorer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed |
                                              (showDetails ? 0 : ImGuiTableColumnFlags_Disabled), 320.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("ContentBrowserBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        const float splitterWidth = 6.0f;
        m_folderTreeWidth = std::clamp(m_folderTreeWidth, 120.0f, std::max(120.0f, ImGui::GetContentRegionAvail().x * 0.5f));
        RenderFolderTree(*project);

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("FolderTreeSplitter", ImVec2(splitterWidth, std::max(1.0f, ImGui::GetContentRegionAvail().y)));
        if (ImGui::IsItemActive()) m_folderTreeWidth += ImGui::GetIO().MouseDelta.x;
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            const float splitterX = std::floor((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(splitterX, ImGui::GetItemRectMin().y), ImVec2(splitterX, ImGui::GetItemRectMax().y),
                                                ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered), 2.0f);
        }
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginChild("AssetColumn", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        const float statusHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        // Arrow keys move the item selection rather than ImGui's navigation cursor.
        ImGui::BeginChild("AssetPane", ImVec2(0.0f, -statusHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoNavInputs);
        if (!m_thumbnailCache) m_thumbnailCache = std::make_unique<AssetThumbnailCache>();
        m_thumbnailCache->BeginFrame();
        m_hoveredFolder.clear();
        m_hoveringFolderItem = false;
        if (!m_openModelReference.empty())
        {
            RenderModelObjectGrid(*project);
        }
        else if (m_viewMode == ViewMode::List)
        {
            RenderItemList(*project);
        }
        else
        {
            RenderItemGrid(*project);
            RenderBackgroundContextMenu();
        }
        ImGui::EndChild();
        RenderStatusBar();
        ImGui::EndChild();
        ImGui::EndChild();

        HandleShortcuts(*project);
        RunDeferredEdits();
        ProcessDroppedFiles(*project);
        RenderDeleteConfirmation(*project);

        if (showDetails && m_selectedAssetIndex >= 0 && m_selectedAssetIndex < static_cast<int>(assets.size()))
        {
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginChild("ContentBrowserDetails", ImVec2(0.0f, 0.0f), false);
            ImGui::SeparatorText("Details");
            // Actions below can replace the registry; retain the selected value.
            const auto asset = assets[static_cast<std::size_t>(m_selectedAssetIndex)];
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

            if (asset.type == assets::ProjectAssetType::Prefab)
            {
                if (ImGui::Button("Export as Static Mesh"))
                {
                    std::string outputReference, exportError;
                    if (ExportPrefabMesh(*project, asset, m_selectedFolder, &outputReference, &exportError))
                    {
                        EditorShell::GetInstance().RefreshProjectAssets();
                        m_assetCacheDirty = true;
                        editorShell.MarkProjectDirty();
                        editorShell.Log(EditorShell::ConsoleSeverity::Info, "Exported prefab mesh: " + outputReference);
                    }
                    else editorShell.Log(EditorShell::ConsoleSeverity::Error, exportError);
                }
                ImGui::TextDisabled("Bakes enabled static mesh components and child transforms into one mesh asset.");
            }
            else if (asset.type == assets::ProjectAssetType::ScriptableObject)
            {
                std::string className;
                std::unordered_map<std::string, scripting::ScriptFieldValue> values;
                auto &scriptEngine = editorShell.GetEngine().GetScriptEngine();
                if (!LoadScriptableObjectAsset(resolvedPath, className, values))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Invalid scriptable object asset.");
                }
                else if (const auto *definition = scriptEngine.FindClass(className))
                {
                    ImGui::SeparatorText(className.c_str());
                    bool changed = false;
                    int fieldIndex = 0;
                    for (const auto &field : definition->fields)
                    {
                        auto iterator = values.find(field.name);
                        if (iterator != values.end())
                        {
                            // The asset stores arrays as encoded text; restore their shape.
                            iterator->second = scripting::NormalizeFieldValue(field, std::move(iterator->second));
                        }
                        if (iterator == values.end() || !scripting::IsFieldValueCompatible(field, iterator->second))
                        {
                            iterator = values.insert_or_assign(field.name, scripting::ResolveFieldDefault(field)).first;
                        }
                        ImGui::PushID(fieldIndex++);
                        if (field.isArray)
                        {
                            auto &references = std::get<scripting::ScriptReferenceArray>(iterator->second);
                            changed |= RenderScriptAssetArrayField(field.name.c_str(), editorShell.GetProject(), field, references);
                            ImGui::PopID();
                            continue;
                        }
                        switch (field.type)
                        {
                        case scripting::ScriptFieldType::Boolean:
                        {
                            bool value = std::get<bool>(iterator->second);
                            if (ImGui::Checkbox(field.name.c_str(), &value)) { iterator->second = value; changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::Int32:
                        {
                            int value = std::get<int32_t>(iterator->second);
                            if (ImGui::DragInt(field.name.c_str(), &value)) { iterator->second = static_cast<int32_t>(value); changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::Float:
                        {
                            float value = std::get<float>(iterator->second);
                            if (ImGui::DragFloat(field.name.c_str(), &value, 0.01f)) { iterator->second = value; changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::Double:
                        {
                            double value = std::get<double>(iterator->second);
                            if (ImGui::InputDouble(field.name.c_str(), &value)) { iterator->second = value; changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::Vector2:
                        {
                            auto value = std::get<glm::vec2>(iterator->second);
                            if (ImGui::DragFloat2(field.name.c_str(), &value.x, 0.01f)) { iterator->second = value; changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::Vector3:
                        {
                            auto value = std::get<glm::vec3>(iterator->second);
                            if (ImGui::DragFloat3(field.name.c_str(), &value.x, 0.01f)) { iterator->second = value; changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::String:
                        {
                            auto &value = std::get<std::string>(iterator->second);
                            if (InputTextString(field.name.c_str(), value)) { changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::PrefabAsset:
                        case scripting::ScriptFieldType::ScriptableObjectAsset:
                        case scripting::ScriptFieldType::MaterialAsset:
                        case scripting::ScriptFieldType::InputMappingAsset:
                        case scripting::ScriptFieldType::Asset:
                        {
                            auto &value = std::get<std::string>(iterator->second);
                            if (RenderScriptAssetField(field.name.c_str(), editorShell.GetProject(), field, value)) { changed = true; }
                            break;
                        }
                        default:
                            ImGui::TextDisabled("%s (unsupported asset field)", field.name.c_str());
                            break;
                        }
                        ImGui::PopID();
                    }
                    if (changed)
                    {
                        if (SaveScriptableObjectAsset(resolvedPath, className, definition->fields, values))
                        {
                            editorShell.MarkProjectDirty();
                        }
                        else
                        {
                            editorShell.Log(EditorShell::ConsoleSeverity::Error, "Failed to save scriptable object: " + asset.reference);
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled("Type '%s' is not loaded. Build scripts to edit this asset.", className.c_str());
                }
            }
            else if (asset.type == assets::ProjectAssetType::SurfaceResponse)
            {
                auto &manager = core::Engine::GetInstance().GetAssetManager();
                if (m_surfaceEditorReference != asset.reference)
                {
                    m_surfaceEditorReference = asset.reference;
                    bool loaded = false;
                    m_surfaceDraft = manager.LoadSurfaceResponseAsset(asset.reference, &loaded);
                    m_surfaceError = loaded ? "" : "Could not load surface; saving will replace it with the values below.";
                }
                ImGui::TextUnformatted("Surface Response");
                ImGui::SliderFloat("Friction", &m_surfaceDraft.friction, 0.0f, 10.0f);
                const auto choose = [&](const char *label, std::string &value, assets::ProjectAssetType type)
                {
                    (void)RenderProjectAssetPicker(label, project, type, value, {.includeEngineAssets = true});
                };
                for (int event = 0; event < 2; ++event)
                {
                    ImGui::PushID(event);
                    ImGui::SeparatorText(event == 0 ? "Footstep" : "Impact");
                    auto &response = event == 0 ? m_surfaceDraft.footstep : m_surfaceDraft.impact;
                    choose("Sound", response.sound, assets::ProjectAssetType::Audio);
                    choose("Particles", response.particles, assets::ProjectAssetType::ParticleSystem);
                    choose("Decal material", response.decalMaterial, assets::ProjectAssetType::Material);
                    ImGui::PopID();
                }
                if (ImGui::Button("Save Surface"))
                {
                    if (manager.SaveSurfaceResponseAsset(asset.reference, m_surfaceDraft, &m_surfaceError)) editorShell.MarkProjectDirty();
                }
                ImGui::SameLine();
                if (ImGui::Button("Revert Unsaved Changes")) m_surfaceEditorReference.clear();
                if (!m_surfaceError.empty()) ImGui::TextWrapped("%s", m_surfaceError.c_str());
            }
            else if (asset.type == assets::ProjectAssetType::Material)
            {
                if (ImGui::Button("Open Material"))
                {
                    editorShell.OpenMaterialAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::Animation)
            {
                if (ImGui::Button("Add Clips From Model"))
                {
                    const std::string selectedPath = BrowseSourceModelPath();
                    if (!selectedPath.empty())
                    {
                        std::string errorMessage;
                        if (AddClipsFromSourceModelToAnimationAsset(*project, asset.reference, selectedPath, &errorMessage))
                        {
                            EditorShell::GetInstance().RefreshProjectAssets();
                            m_assetCacheDirty = true;
                            editorShell.MarkProjectDirty();
                            editorShell.Log(EditorShell::ConsoleSeverity::Info, "Added animation clips to: " + asset.reference);
                        }
                        else
                        {
                            editorShell.Log(EditorShell::ConsoleSeverity::Error,
                                            errorMessage.empty() ? "Failed to add animation clips." : errorMessage);
                        }
                    }
                }
            }
            else if (asset.type == assets::ProjectAssetType::Mesh)
            {
                if (ImGui::Button("Open Mesh"))
                {
                    editorShell.OpenMeshAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::Model)
            {
                if (m_modelExtractionReference != asset.reference)
                {
                    m_modelExtractionReference = asset.reference;
                    m_modelExtractionStatus.clear();
                }
                assets::ModelAsset model;
                std::string modelError;
                const bool imported = assets::LoadModelSourcePackage(*project, asset.reference, model, &modelError);
                if (imported)
                {
                    ImGui::SeparatorText("Imported Model");
                    ImGui::Text("Objects: %zu", model.objects.size());
                    ImGui::TextWrapped("Asset ID: %s", model.sourceAssetId.empty() ? "Unavailable" : model.sourceAssetId.c_str());
                    ImGui::Text("Source hash: %016llx", static_cast<unsigned long long>(model.sourceContentHash));
                    ImGui::Text("Importer version: %u", model.importerVersion);

                    ImGui::SeparatorText("Sub-assets");
                    ImGui::TextDisabled("Extract to: Assets%s%s",
                                        m_selectedFolder.empty() ? "" : "/",
                                        m_selectedFolder.c_str());
                    if (ImGui::Button("Extract All"))
                    {
                        std::size_t extractedCount = 0;
                        std::string firstError;
                        for (const auto &object : model.objects)
                        {
                            std::string extractedReference;
                            std::string extractionError;
                            if (ExtractModelSubAsset(*project, object, m_selectedFolder, &extractedReference, &extractionError))
                            {
                                ++extractedCount;
                            }
                            else if (firstError.empty())
                            {
                                firstError = std::move(extractionError);
                            }
                        }
                        EditorShell::GetInstance().RefreshProjectAssets();
                        m_assetCacheDirty = true;
                        if (extractedCount > 0) editorShell.MarkProjectDirty();
                        m_modelExtractionStatus = "Extracted " + std::to_string(extractedCount) + " of " +
                            std::to_string(model.objects.size()) + " objects to Assets/" + m_selectedFolder + ".";
                        if (!firstError.empty())
                        {
                            m_modelExtractionStatus += " " + firstError;
                            editorShell.Log(EditorShell::ConsoleSeverity::Error, firstError);
                        }
                        editorShell.Log(EditorShell::ConsoleSeverity::Info,
                                        "Extracted " + std::to_string(extractedCount) + " model sub-assets.");
                    }

                    if (!m_modelExtractionStatus.empty()) ImGui::TextWrapped("%s", m_modelExtractionStatus.c_str());
                    if (ImGui::BeginTable("ModelSubAssets", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Name");
                        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                        ImGui::TableHeadersRow();
                        for (std::size_t objectIndex = 0; objectIndex < model.objects.size(); ++objectIndex)
                        {
                            const auto &object = model.objects[objectIndex];
                            ImGui::PushID(static_cast<int>(objectIndex));
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(object.name.c_str());
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", object.reference.c_str());
                            ImGui::TableSetColumnIndex(1);
                            const std::string objectType(assets::Project::GetAssetTypeName(object.type));
                            ImGui::TextUnformatted(objectType.c_str());
                            ImGui::TableSetColumnIndex(2);
                            const char *extractLabel = object.type == assets::ProjectAssetType::Material ? "Extract & Use" : "Extract";
                            if (ImGui::SmallButton(extractLabel))
                            {
                                std::string extractedReference;
                                std::string extractionError;
                                if (ExtractModelSubAsset(*project, object, m_selectedFolder, &extractedReference, &extractionError))
                                {
                                    EditorShell::GetInstance().RefreshProjectAssets();
                                    m_assetCacheDirty = true;
                                    editorShell.MarkProjectDirty();
                                    editorShell.Log(EditorShell::ConsoleSeverity::Info,
                                                    "Extracted model sub-asset: " + extractedReference);
                                }
                                else
                                {
                                    editorShell.Log(EditorShell::ConsoleSeverity::Error, extractionError);
                                }
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                }
                else
                {
                    ImGui::TextDisabled("This model has not been imported yet.");
                }
                if (m_modelSettingsReference != asset.reference)
                {
                    m_modelSettingsAwaitingImport = false;
                    m_modelSettingsReference = asset.reference;
                    m_modelSettingsValid = assetimport::ModelImportService{}.ReadOptions(*project, asset.reference, m_modelSettingsCommitted, &m_modelSettingsError);
                    m_modelSettingsDraft = m_modelSettingsCommitted;
                }
                const bool importRunning = editorShell.IsModelImportRunning();
                if (imported)
                {
                    if (!m_importDependenciesInspector) m_importDependenciesInspector = std::make_unique<ModelImportDependenciesInspector>();
                    m_importDependenciesInspector->Render(*project, asset.reference, model.sourceAssetId, importRunning);
                    if (!m_modelHierarchyInspector) m_modelHierarchyInspector = std::make_unique<ModelHierarchyInspector>();
                    m_modelHierarchyInspector->Render(*project, asset.reference, model,
                        core::Engine::GetInstance().GetAssetManager().GetAssetCatalog(), importRunning || editorShell.GetEngine().IsRuntimeRunning(),
                        editorShell.GetScene(), [&](const auto &proposal, std::string *error) {
                            return editorShell.ApplyReviewedModelNodeRepair(proposal, error);
                        });
                    if (!m_staticHierarchyDialog) m_staticHierarchyDialog = std::make_unique<StaticModelHierarchyDialog>();
                    m_staticHierarchyDialog->Render(asset.reference);
                }
                if (m_modelSettingsAwaitingImport && !importRunning)
                {
                    m_modelSettingsAwaitingImport = false;
                    m_modelSettingsValid = assetimport::ModelImportService{}.ReadOptions(*project, asset.reference, m_modelSettingsCommitted, &m_modelSettingsError);
                    if (m_modelSettingsValid) m_modelSettingsError = editorShell.GetModelImportError(asset.reference);
                }
                if (importRunning)
                {
                    const auto progress = editorShell.GetModelImportProgress();
                    ImGui::TextWrapped("Import: %s", progress.c_str());
                    if (ImGui::Button("Cancel Import")) editorShell.CancelModelImport();
                }
                ImGui::SeparatorText("Import Settings");
                ImGui::BeginDisabled(!m_modelSettingsValid);
                ImGui::Checkbox("Generate LODs", &m_modelSettingsDraft.generateLods);
                ImGui::Checkbox("Optimize vertex cache", &m_modelSettingsDraft.optimizeVertexCache);
                ImGui::Checkbox("Optimize overdraw", &m_modelSettingsDraft.optimizeOverdraw);
                ImGui::EndDisabled();
                const bool settingsChanged = m_modelSettingsDraft.ToFlags() != m_modelSettingsCommitted.ToFlags();
                ImGui::BeginDisabled(importRunning || !m_modelSettingsValid || !settingsChanged);
                if (ImGui::Button("Apply Settings and Reimport"))
                {
                    if (editorShell.BeginModelImport(asset.reference, m_modelSettingsDraft, &m_modelSettingsError))
                        m_modelSettingsAwaitingImport = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(importRunning);
                if (ImGui::Button("Revert Settings"))
                {
                    m_modelSettingsValid = assetimport::ModelImportService{}.ReadOptions(*project, asset.reference, m_modelSettingsCommitted, &m_modelSettingsError);
                    m_modelSettingsDraft = m_modelSettingsCommitted;
                }
                ImGui::EndDisabled();
                if (!m_modelSettingsError.empty()) ImGui::TextWrapped("%s", m_modelSettingsError.c_str());
                ImGui::BeginDisabled(importRunning);
                if (ImGui::Button(imported ? "Reimport" : "Import"))
                {
                    if (editorShell.BeginModelImport(asset.reference, std::nullopt, &m_modelSettingsError))
                        m_modelSettingsAwaitingImport = true;
                }
                if (imported)
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Force Reimport"))
                        if (editorShell.BeginModelImport(asset.reference, std::nullopt, &m_modelSettingsError, true))
                            m_modelSettingsAwaitingImport = true;
                }
                ImGui::EndDisabled();
            }
            else if (asset.type == assets::ProjectAssetType::ShaderGraph)
            {
                if (ImGui::Button("Open Shader Graph"))
                {
                    editorShell.OpenShaderGraphAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::AnimationGraph)
            {
                if (ImGui::Button("Open Animation Graph"))
                {
                    editorShell.OpenAnimationGraphAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::AnimationClip)
            {
                if (ImGui::Button("Open Animation Clip"))
                    editorShell.OpenAnimationClipAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::ParticleSystem)
            {
                if (ImGui::Button("Open Particle System"))
                {
                    editorShell.OpenParticleSystemAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::LoadingScreen)
            {
                editorShell.OpenLoadingScreenAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::UITheme)
            {
                if (ImGui::Button("Open Theme Editor"))
                    editorShell.OpenUIThemeEditor(UIThemeTarget::ForAsset(asset.reference));
            }
            else if (asset.type == assets::ProjectAssetType::InputMapping)
            {
                if (ImGui::Button("Open Input Mapping"))
                    editorShell.OpenInputMappingAsset(asset.reference);
            }
            ImGui::EndChild();
        }
        ImGui::EndTable();
    }

    // =====================================================================
    // Explorer: items, folders, commands and file operations.

    namespace
    {
        constexpr std::string_view kFolderKeyPrefix = "folder://";
        constexpr const char *kInternalFolder = "__internal__";
        constexpr ImU32 kFolderColor = IM_COL32(216, 167, 64, 255);
        constexpr float kCutAlpha = 0.45f;

        // Layout shared by every card grid (items and model objects).
        struct CardMetrics
        {
            float scale = 1.0f;
            float gap = 0.0f;
            float inset = 0.0f;
            float labelGap = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float previewSize = 0.0f;
            float labelTop = 0.0f;
            float lineStep = 0.0f;
            float textWidth = 0.0f;
            int columns = 1;
        };

        // Pushes the scaled font and spacing used by card grids; pair with EndCardGrid.
        CardMetrics BeginCardGrid(float scale)
        {
            const auto &style = ImGui::GetStyle();
            ImGui::PushFont(nullptr, style.FontSizeBase * scale);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x * scale, style.ItemSpacing.y * scale));
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(3.0f * scale, 3.0f * scale));

            CardMetrics metrics;
            metrics.scale = scale;
            metrics.gap = 6.0f * scale;
            metrics.inset = 4.0f * scale;
            metrics.labelGap = 3.0f * scale;
            const float nominalWidth = 104.0f * scale;
            const float gridWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - 1.0f);
            metrics.columns = std::max(1, static_cast<int>((gridWidth + metrics.gap) / (nominalWidth + metrics.gap)));
            // Spread the leftover width across the columns so the grid fills the pane edge to edge.
            metrics.width = std::floor(std::min((gridWidth - metrics.gap * static_cast<float>(metrics.columns - 1)) /
                                                    static_cast<float>(metrics.columns),
                                                nominalWidth * 1.4f));
            metrics.previewSize = metrics.width - metrics.inset * 2.0f;
            metrics.labelTop = metrics.inset + metrics.previewSize + metrics.labelGap;
            metrics.lineStep = ImGui::GetTextLineHeight();
            metrics.height = metrics.labelTop + metrics.lineStep * 2.0f + metrics.inset;
            metrics.textWidth = metrics.width - metrics.inset * 2.0f;
            return metrics;
        }

        void EndCardGrid()
        {
            ImGui::PopStyleVar(2);
            ImGui::PopFont();
        }

        void DrawCardBackground(const CardMetrics &metrics, bool selected, bool hovered)
        {
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            const ImU32 fill = ImGui::GetColorU32(selected ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered : ImGuiCol_FrameBg);
            auto *drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(minimum, maximum, fill, 4.0f * metrics.scale);
            if (selected)
                drawList->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_NavCursor), 4.0f * metrics.scale, 0, 1.5f);
        }

        // Preview square with the thumbnail (or type icon) and its type accent.
        void DrawAssetPreview(const CardMetrics &metrics, std::uint64_t thumbnail, assets::ProjectAssetType type, float alpha)
        {
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 previewMin(minimum.x + metrics.inset, minimum.y + metrics.inset);
            const ImVec2 previewMax(previewMin.x + metrics.previewSize, previewMin.y + metrics.previewSize);
            auto *drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(previewMin, previewMax, IM_COL32(27, 30, 35, static_cast<int>(255 * alpha)), 3.0f * metrics.scale);
            if (thumbnail != 0)
                drawList->AddImage(static_cast<ImTextureID>(thumbnail), previewMin, previewMax, ImVec2(0, 1), ImVec2(1, 0),
                                   IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)));
            else
                DrawAssetTypeIcon(drawList, type, previewMin, previewMax);
            DrawAssetTypeAccent(drawList, type, previewMin, previewMax, 3.0f * metrics.scale);
        }

        void DrawCardLabels(const CardMetrics &metrics, const std::string &name, const std::string &detail, float alpha, bool drawName = true)
        {
            const ImVec2 minimum = ImGui::GetItemRectMin();
            auto *drawList = ImGui::GetWindowDrawList();
            if (drawName)
                DrawCenteredCardText(drawList, name, minimum.x + metrics.inset, metrics.textWidth, minimum.y + metrics.labelTop,
                                     ImGui::GetColorU32(ImGuiCol_Text, alpha));
            DrawCenteredCardText(drawList, detail, minimum.x + metrics.inset, metrics.textWidth,
                                 minimum.y + metrics.labelTop + metrics.lineStep, ImGui::GetColorU32(ImGuiCol_TextDisabled, alpha));
        }

    }

    // ---------------------------------------------------------------------
    // Keys

    std::string ContentBrowserPanel::FolderKey(const std::string &folder)
    {
        return std::string(kFolderKeyPrefix) + folder;
    }

    bool ContentBrowserPanel::IsFolderKey(const std::string &key)
    {
        return key.rfind(kFolderKeyPrefix, 0) == 0;
    }

    std::string ContentBrowserPanel::FolderFromKey(const std::string &key)
    {
        return IsFolderKey(key) ? key.substr(kFolderKeyPrefix.size()) : std::string{};
    }

    std::filesystem::path ContentBrowserPanel::PathForKey(const std::string &key) const
    {
        const auto *project = EditorShell::GetInstance().GetProject();
        if (!project)
        {
            return {};
        }
        if (IsFolderKey(key))
        {
            const auto folder = FolderFromKey(key);
            return folder.empty() ? project->GetAssetDirectoryPath()
                                  : project->GetAssetDirectoryPath() / PathFromUtf8(folder);
        }
        return project->ResolveAssetReference(key);
    }

    std::string ContentBrowserPanel::KeyForPath(const std::filesystem::path &path) const
    {
        const auto *project = EditorShell::GetInstance().GetProject();
        if (!project)
        {
            return {};
        }
        std::error_code error;
        if (std::filesystem::is_directory(path, error))
        {
            return FolderKey(NormalizeContentFolder(path.lexically_normal().lexically_relative(project->GetAssetDirectoryPath().lexically_normal())));
        }
        return project->MakeAssetReference(path);
    }

    std::vector<std::filesystem::path> ContentBrowserPanel::PathsForKeys(const std::vector<std::string> &keys) const
    {
        std::vector<std::filesystem::path> paths;
        paths.reserve(keys.size());
        for (const auto &key : keys)
        {
            if (IsKeyEditable(key))
            {
                paths.push_back(PathForKey(key));
            }
        }
        return paths;
    }

    bool ContentBrowserPanel::IsKeyEditable(const std::string &key) const
    {
        const auto *project = EditorShell::GetInstance().GetProject();
        if (!project || (!IsFolderKey(key) && !assets::Project::IsProjectAssetReference(key)))
        {
            return false;
        }
        if (IsFolderKey(key) && FolderFromKey(key).empty())
        {
            return false; // The asset root itself.
        }
        return ContentFileOperations(*project).IsEditable(PathForKey(key));
    }

    // ---------------------------------------------------------------------
    // Caches

    void ContentBrowserPanel::RefreshRegistryCache(const assets::Project &project)
    {
        const auto &assets = project.GetManifest().assetEntries;
        const auto catalog = core::Engine::GetInstance().GetAssetManager().GetAssetCatalog();
        const bool registryChanged = m_assetCacheDirty || m_cachedProject != &project || m_cachedCatalog != catalog ||
                                     m_cachedAssetReferences.size() != assets.size();
        if (!registryChanged)
        {
            return;
        }

        if (m_thumbnailCache) m_thumbnailCache->Clear();
        m_assetCacheDirty = false;
        m_itemsDirty = true;
        if (m_cachedProject != &project) m_surfaceEditorReference.clear();
        m_cachedProject = &project;
        m_cachedCatalog = catalog;
        m_cachedAssetReferences.clear();
        m_cachedAssetFolders.clear();
        m_cachedAssetFileNames.clear();
        m_cachedAssetRelativePaths.clear();
        m_cachedAssetReferences.reserve(assets.size());
        m_cachedAssetFolders.reserve(assets.size());
        m_cachedAssetFileNames.reserve(assets.size());
        m_cachedAssetRelativePaths.reserve(assets.size());

        std::set<std::string> importedNativeLocations;
        if (catalog)
            for (const auto &object : catalog->GetObjects())
                if (object.ownership == assets::AssetOwnership::Imported && object.type != assets::ProjectAssetType::Texture)
                {
                    importedNativeLocations.insert(object.location);
                    if (object.type == assets::ProjectAssetType::Mesh) importedNativeLocations.insert(object.location + ".materials");
                }
        // Texture descriptors currently also include source image inputs.
        // Keep those visible until the manifest records output provenance.
        std::set<std::string> folderSet{""};
        for (const auto &asset : assets)
        {
            m_cachedAssetReferences.push_back(asset.reference);
            const std::string relativePath = GetReferenceRelativePath(project, asset);
            const bool internalArtifact = importedNativeLocations.contains(asset.reference) ||
                                          relativePath == "Imported" || relativePath.rfind("Imported/", 0) == 0;
            const std::string folder = internalArtifact ? kInternalFolder : GetContentFolderParent(relativePath);
            const std::string fileName = GetContentFolderName(relativePath);
            m_cachedAssetRelativePaths.push_back(relativePath);
            m_cachedAssetFolders.push_back(folder);
            m_cachedAssetFileNames.push_back(fileName.empty() ? DisplayAssetReference(asset.reference) : fileName);
            for (std::string ancestor = internalArtifact ? std::string{} : folder; !ancestor.empty(); ancestor = GetContentFolderParent(ancestor))
            {
                folderSet.insert(ancestor);
            }
        }

        // Empty folders exist only on disk, so scan for them too.
        std::error_code directoryError;
        const auto assetDirectory = project.GetAssetDirectoryPath();
        if (std::filesystem::exists(assetDirectory, directoryError))
        {
            for (std::filesystem::recursive_directory_iterator iterator(assetDirectory, directoryError), end;
                 iterator != end && !directoryError; iterator.increment(directoryError))
            {
                if (!iterator->is_directory(directoryError) || directoryError)
                {
                    directoryError.clear();
                    continue;
                }
                const auto normalized = NormalizeContentFolder(iterator->path().lexically_relative(assetDirectory));
                if (normalized == "Imported" || normalized.rfind("Imported/", 0) == 0 ||
                    assets::IsAssetInfrastructurePath(project.GetRootDirectory(), iterator->path()))
                {
                    if (normalized == "Imported" || normalized.rfind("Imported/", 0) == 0) iterator.disable_recursion_pending();
                    continue;
                }
                folderSet.insert(normalized);
            }
        }

        m_cachedFolders.assign(folderSet.begin(), folderSet.end());
        m_cachedFolderLabels.assign(m_cachedFolders.size(), {});
        m_cachedFolderChildIndices.assign(m_cachedFolders.size(), {});
        m_cachedRootFolderIndices.clear();
        std::unordered_map<std::string, int> folderIndexByPath;
        folderIndexByPath.reserve(m_cachedFolders.size());
        for (std::size_t index = 0; index < m_cachedFolders.size(); ++index)
        {
            folderIndexByPath.emplace(m_cachedFolders[index], static_cast<int>(index));
            m_cachedFolderLabels[index] = GetContentFolderName(m_cachedFolders[index]);
        }
        for (std::size_t index = 0; index < m_cachedFolders.size(); ++index)
        {
            const auto &folder = m_cachedFolders[index];
            if (folder.empty())
            {
                continue;
            }
            const auto parent = GetContentFolderParent(folder);
            if (parent.empty())
            {
                m_cachedRootFolderIndices.push_back(static_cast<int>(index));
            }
            else if (const auto entry = folderIndexByPath.find(parent); entry != folderIndexByPath.end())
            {
                m_cachedFolderChildIndices[static_cast<std::size_t>(entry->second)].push_back(static_cast<int>(index));
            }
        }
        // Folders sort naturally in the tree ("Level 2" before "Level 10").
        const auto byLabel = [this](int a, int b)
        { return CompareNatural(m_cachedFolderLabels[static_cast<std::size_t>(a)], m_cachedFolderLabels[static_cast<std::size_t>(b)]) < 0; };
        std::sort(m_cachedRootFolderIndices.begin(), m_cachedRootFolderIndices.end(), byLabel);
        for (auto &children : m_cachedFolderChildIndices) std::sort(children.begin(), children.end(), byLabel);

        // The current folder may have been removed outside the editor.
        while (!m_selectedFolder.empty() && !folderIndexByPath.contains(m_selectedFolder))
        {
            m_selectedFolder = GetContentFolderParent(m_selectedFolder);
        }
        const std::unordered_set<std::string> references(m_cachedAssetReferences.begin(), m_cachedAssetReferences.end());
        const auto exists = [&](const std::string &key)
        { return IsFolderKey(key) ? folderIndexByPath.contains(FolderFromKey(key)) : references.contains(key); };
        m_selection.Prune(exists);
        std::erase_if(m_clipboardKeys, [&](const std::string &key) { return !exists(key); });
    }

    void ContentBrowserPanel::RebuildItems(const assets::Project &project)
    {
        const std::string filter(m_filterBuffer.data());
        if (!m_itemsDirty && filter == m_itemsFilter && m_itemsFolder == m_selectedFolder)
        {
            return;
        }
        m_itemsDirty = false;
        m_itemsFilter = filter;
        m_itemsFolder = m_selectedFolder;
        m_items.clear();

        const bool searching = !filter.empty();
        const auto &assets = project.GetManifest().assetEntries;
        const ContentFileOperations operations(project);
        const auto assetRoot = project.GetAssetDirectoryPath();
        const auto modifiedOf = [](const std::filesystem::path &path)
        {
            std::error_code error;
            const auto time = std::filesystem::last_write_time(path, error);
            return error ? std::int64_t{0} : ToUnixSeconds(time);
        };

        for (std::size_t index = 0; index < m_cachedFolders.size(); ++index)
        {
            const auto &folder = m_cachedFolders[index];
            if (folder.empty())
            {
                continue;
            }
            const bool inScope = searching
                                     ? folder != m_selectedFolder && IsContentFolderAncestorOrSelf(m_selectedFolder, folder) &&
                                           ContainsInsensitive(m_cachedFolderLabels[index], filter)
                                     : GetContentFolderParent(folder) == m_selectedFolder;
            if (!inScope)
            {
                continue;
            }
            const auto path = assetRoot / PathFromUtf8(folder);
            m_items.push_back({.folder = true,
                               .key = FolderKey(folder),
                               .folderPath = folder,
                               .name = m_cachedFolderLabels[index],
                               .typeName = "Folder",
                               .modified = modifiedOf(path),
                               .editable = operations.IsEditable(path)});
        }

        for (std::size_t index = 0; index < assets.size() && index < m_cachedAssetFolders.size(); ++index)
        {
            const auto &folder = m_cachedAssetFolders[index];
            if (folder == kInternalFolder)
            {
                continue;
            }
            const bool inScope = searching ? IsContentFolderAncestorOrSelf(m_selectedFolder, folder) : folder == m_selectedFolder;
            const auto &asset = assets[index];
            const std::string typeName(assets::Project::GetAssetTypeName(asset.type));
            const auto &name = m_cachedAssetFileNames[index];
            if (!inScope || (searching && !ContainsInsensitive(typeName + " " + m_cachedAssetRelativePaths[index], filter)))
            {
                continue;
            }
            const auto path = project.ResolveAssetReference(asset.reference);
            std::error_code error;
            const auto size = asset.size != 0 ? asset.size : std::filesystem::file_size(path, error);
            const bool projectAsset = assets::Project::IsProjectAssetReference(asset.reference);
            m_items.push_back({.folder = false,
                               .key = asset.reference,
                               .folderPath = folder,
                               .name = name,
                               .typeName = typeName,
                               .assetIndex = static_cast<int>(index),
                               .type = asset.type,
                               .size = error ? 0 : static_cast<std::uint64_t>(size),
                               .modified = projectAsset ? modifiedOf(path) : 0,
                               .editable = projectAsset && operations.IsEditable(path)});
        }

        const auto fields = [](const ContentItem &item)
        {
            return ContentSortFields{.folder = item.folder, .name = item.name, .typeName = item.typeName,
                                     .modified = item.modified, .size = item.size};
        };
        std::stable_sort(m_items.begin(), m_items.end(), [&](const ContentItem &a, const ContentItem &b)
                         { return ContentSortsBefore(fields(a), fields(b), m_sortOrder); });

        if (!m_renameWhenShown.empty() && FindItem(m_renameWhenShown))
        {
            BeginRename(m_renameWhenShown);
            m_renameWhenShown.clear();
        }
        SyncDetailsSelection();
    }

    const ContentBrowserPanel::ContentItem *ContentBrowserPanel::FindItem(const std::string &key) const
    {
        const auto it = std::find_if(m_items.begin(), m_items.end(), [&](const ContentItem &item) { return item.key == key; });
        return it == m_items.end() ? nullptr : &*it;
    }

    std::vector<std::string> ContentBrowserPanel::ShownKeys() const
    {
        std::vector<std::string> keys;
        keys.reserve(m_items.size());
        for (const auto &item : m_items) keys.push_back(item.key);
        return keys;
    }

    // ---------------------------------------------------------------------
    // Navigation and selection

    void ContentBrowserPanel::NavigateTo(const std::string &folder, bool recordHistory)
    {
        if (recordHistory) m_history.Visit(folder);
        if (!m_rename.key.empty()) m_rename = {};
        m_selectedFolder = folder;
        m_openModelReference.clear();
        m_openModelObjects.clear();
        m_selection.Clear();
        m_selectedAssetIndex = -1;
        m_revealFolderInTree = true;
        m_itemsDirty = true;
    }

    void ContentBrowserPanel::SelectKeys(const std::vector<std::string> &keys)
    {
        m_selection.Set(keys);
        if (!keys.empty()) m_scrollToKey = keys.back();
        SyncDetailsSelection();
    }

    void ContentBrowserPanel::SyncDetailsSelection()
    {
        const auto *primary = m_selection.Primary();
        m_selectedAssetIndex = -1;
        if (!primary || IsFolderKey(*primary))
        {
            return;
        }
        const auto it = std::find(m_cachedAssetReferences.begin(), m_cachedAssetReferences.end(), *primary);
        if (it != m_cachedAssetReferences.end())
        {
            m_selectedAssetIndex = static_cast<int>(it - m_cachedAssetReferences.begin());
        }
    }

    void ContentBrowserPanel::OpenModelPackage(assets::Project &project, const ContentItem &item)
    {
        assets::ModelAsset model;
        std::string modelError;
        if (assets::LoadModelSourcePackage(project, item.key, model, &modelError))
        {
            m_openModelReference = item.key;
            m_openModelName = std::filesystem::path(item.name).stem().string();
            m_openModelObjects = std::move(model.objects);
            m_selection.Clear();
            m_selectedAssetIndex = -1;
        }
        else
        {
            EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Error,
                                           modelError.empty() ? "Import the model before opening it." : modelError);
        }
    }

    // ---------------------------------------------------------------------
    // Commands

    ContentBrowserPanel::CommandScope ContentBrowserPanel::SelectionScope() const
    {
        return {.keys = m_selection.Keys(), .folder = m_selectedFolder};
    }

    bool ContentBrowserPanel::CanExecute(ContentCommand command, const CommandScope &scope) const
    {
        const auto anyEditable = [&]()
        { return std::any_of(scope.keys.begin(), scope.keys.end(), [&](const std::string &key) { return IsKeyEditable(key); }); };
        const auto target = scope.keys.size() == 1 ? PathForKey(scope.keys.front())
                                                   : scope.keys.empty() ? PathForKey(FolderKey(scope.folder)) : std::filesystem::path{};
        std::error_code error;
        const bool folderOnDisk = std::filesystem::is_directory(PathForKey(FolderKey(scope.folder)), error);
        switch (command)
        {
        case ContentCommand::Open: return !scope.keys.empty();
        case ContentCommand::NewFolder: return folderOnDisk;
        case ContentCommand::Rename: return scope.keys.size() == 1 && IsKeyEditable(scope.keys.front());
        case ContentCommand::Cut:
        case ContentCommand::Copy:
        case ContentCommand::Duplicate:
        case ContentCommand::Delete:
        case ContentCommand::DeletePermanently: return anyEditable();
        case ContentCommand::Paste: return !m_clipboardKeys.empty() && folderOnDisk;
        case ContentCommand::SelectAll: return !m_items.empty() && m_openModelReference.empty();
        case ContentCommand::RevealInFileManager:
        case ContentCommand::OpenWithSystem: return !target.empty() && std::filesystem::exists(target, error);
        case ContentCommand::CopyPath: return !scope.keys.empty() || folderOnDisk;
        case ContentCommand::CopyReference:
            return !scope.keys.empty() && std::none_of(scope.keys.begin(), scope.keys.end(), [](const std::string &key) { return IsFolderKey(key); });
        case ContentCommand::GoBack: return m_history.CanGoBack();
        case ContentCommand::GoForward: return m_history.CanGoForward();
        case ContentCommand::GoUp: return !m_selectedFolder.empty() || !m_openModelReference.empty();
        case ContentCommand::Refresh: return true;
        }
        return false;
    }

    void ContentBrowserPanel::Execute(ContentCommand command, const CommandScope &scope)
    {
        if (CanExecute(command, scope))
        {
            Defer([this, command, scope]() { ExecuteNow(command, scope); });
        }
    }

    bool ContentBrowserPanel::CommandMenuItem(ContentCommand command, const CommandScope &scope, const char *icon)
    {
        const auto &info = GetContentCommandInfo(command);
        const std::string label = icon ? std::string(icon) + "  " + info.label : std::string(info.label);
        if (ImGui::MenuItem(label.c_str(), info.shortcutText, false, CanExecute(command, scope)))
        {
            Execute(command, scope);
            return true;
        }
        return false;
    }

    void ContentBrowserPanel::ExecuteNow(ContentCommand command, const CommandScope &scope)
    {
        auto &shell = EditorShell::GetInstance();
        auto *project = shell.GetProject();
        if (!project || !CanExecute(command, scope))
        {
            return;
        }
        const ContentFileOperations operations(*project);
        const auto folderPath = PathForKey(FolderKey(scope.folder));
        const auto targetPath = scope.keys.size() == 1 ? PathForKey(scope.keys.front()) : folderPath;
        const auto report = [&](const ContentOperationReport &result, const std::string &summary) { ApplyReport(result, summary); };

        switch (command)
        {
        case ContentCommand::Open:
        {
            if (scope.keys.size() == 1 && IsFolderKey(scope.keys.front()))
            {
                NavigateTo(FolderFromKey(scope.keys.front()));
                break;
            }
            for (const auto &key : scope.keys)
            {
                const auto *item = FindItem(key);
                const auto &entries = project->GetManifest().assetEntries;
                if (!item || item->folder || item->assetIndex < 0 || static_cast<std::size_t>(item->assetIndex) >= entries.size() ||
                    entries[static_cast<std::size_t>(item->assetIndex)].reference != key)
                    continue;
                if (item->type == assets::ProjectAssetType::Model && scope.keys.size() == 1)
                    OpenModelPackage(*project, *item);
                else
                    OpenAsset(shell, *project, entries[static_cast<std::size_t>(item->assetIndex)]);
            }
            break;
        }
        case ContentCommand::NewFolder:
        {
            const auto result = operations.CreateFolder(folderPath);
            if (result.Succeeded() && !result.results.empty())
            {
                if (scope.folder != m_selectedFolder) NavigateTo(scope.folder);
                m_renameWhenShown = KeyForPath(result.results.front());
            }
            report(result, {});
            break;
        }
        case ContentCommand::Rename:
            BeginRename(scope.keys.front());
            break;
        case ContentCommand::Cut:
        case ContentCommand::Copy:
            m_clipboardKeys.clear();
            for (const auto &key : scope.keys)
                if (IsKeyEditable(key)) m_clipboardKeys.push_back(key);
            m_clipboardCut = command == ContentCommand::Cut;
            break;
        case ContentCommand::Paste:
        {
            const auto paths = PathsForKeys(m_clipboardKeys);
            if (m_clipboardCut)
            {
                const auto result = operations.Move(paths, folderPath);
                if (result.Succeeded())
                {
                    m_clipboardKeys.clear();
                    m_clipboardCut = false;
                }
                report(result, "Moved " + std::to_string(result.moved.size()) + " item(s).");
            }
            else
            {
                const auto result = operations.Copy(paths, folderPath);
                report(result, "Pasted " + std::to_string(result.results.size()) + " item(s).");
            }
            break;
        }
        case ContentCommand::Duplicate:
        {
            ContentOperationReport combined;
            for (const auto &path : ContentFileOperations::RemoveNested(PathsForKeys(scope.keys)))
            {
                auto result = operations.Copy(std::span(&path, 1), path.parent_path());
                combined.results.insert(combined.results.end(), result.results.begin(), result.results.end());
                combined.errors.insert(combined.errors.end(), result.errors.begin(), result.errors.end());
            }
            report(combined, "Duplicated " + std::to_string(combined.results.size()) + " item(s).");
            break;
        }
        case ContentCommand::Delete:
        case ContentCommand::DeletePermanently:
            m_pendingDeletePaths = ContentFileOperations::RemoveNested(PathsForKeys(scope.keys));
            m_pendingDeleteMode = command == ContentCommand::Delete ? ContentDeleteMode::Trash : ContentDeleteMode::Permanent;
            m_openDeletePopup = !m_pendingDeletePaths.empty();
            break;
        case ContentCommand::SelectAll:
            m_selection.Set(ShownKeys());
            SyncDetailsSelection();
            break;
        case ContentCommand::RevealInFileManager:
        case ContentCommand::OpenWithSystem:
        {
            std::string error;
            const bool ok = command == ContentCommand::RevealInFileManager ? RevealInFileManager(targetPath, &error)
                                                                          : OpenWithSystem(targetPath, &error);
            if (!ok) shell.Log(EditorShell::ConsoleSeverity::Error, error);
            break;
        }
        case ContentCommand::CopyPath:
        {
            std::string text;
            for (const auto &key : scope.keys.empty() ? std::vector<std::string>{FolderKey(scope.folder)} : scope.keys)
                text += (text.empty() ? "" : "\n") + PathToUtf8(PathForKey(key).lexically_normal());
            ImGui::SetClipboardText(text.c_str());
            break;
        }
        case ContentCommand::CopyReference:
        {
            std::string text;
            for (const auto &key : scope.keys) text += (text.empty() ? "" : "\n") + key;
            ImGui::SetClipboardText(text.c_str());
            break;
        }
        case ContentCommand::GoBack:
            NavigateTo(m_history.Back(), false);
            break;
        case ContentCommand::GoForward:
            NavigateTo(m_history.Forward(), false);
            break;
        case ContentCommand::GoUp:
            NavigateTo(m_openModelReference.empty() ? GetContentFolderParent(m_selectedFolder) : m_selectedFolder);
            break;
        case ContentCommand::Refresh:
            if (shell.RefreshProjectAssets())
            {
                m_assetCacheDirty = true;
                shell.Log(EditorShell::ConsoleSeverity::Info, "Refreshed project assets.");
            }
            break;
        }
    }

    void ContentBrowserPanel::RunDeferredEdits()
    {
        // Edits that queue further edits run on the next frame.
        auto edits = std::move(m_deferredEdits);
        m_deferredEdits.clear();
        for (auto &edit : edits) edit();
    }

    void ContentBrowserPanel::ApplyReport(const ContentOperationReport &report, const std::string &summary)
    {
        auto &shell = EditorShell::GetInstance();
        const auto *project = shell.GetProject();
        for (const auto &error : report.errors)
        {
            shell.Log(EditorShell::ConsoleSeverity::Error, error);
        }
        for (const auto &warning : report.warnings)
        {
            shell.Log(EditorShell::ConsoleSeverity::Warning, warning);
        }
        if (!project || !report.Changed())
        {
            return;
        }

        const auto assetRoot = project->GetAssetDirectoryPath().lexically_normal();
        const auto relativeOf = [&](const std::filesystem::path &path) { return NormalizeContentFolder(path.lexically_normal().lexically_relative(assetRoot)); };
        const auto projectScheme = std::string(assets::Project::kProjectAssetScheme);
        for (const auto &[source, destination] : report.moved)
        {
            const auto from = relativeOf(source);
            if (destination.empty())
            {
                // Deleted: forget everything inside it and step out of it.
                const auto inside = [&](const std::string &key)
                {
                    const auto rel = IsFolderKey(key) ? FolderFromKey(key)
                                     : key.rfind(projectScheme, 0) == 0 ? key.substr(projectScheme.size()) : std::string{};
                    return !rel.empty() && IsContentFolderAncestorOrSelf(from, rel);
                };
                m_selection.Prune([&](const std::string &key) { return !inside(key); });
                std::erase_if(m_clipboardKeys, inside);
                const auto stepOut = [&](const std::string &folder)
                { return IsContentFolderAncestorOrSelf(from, folder) ? GetContentFolderParent(from) : folder; };
                m_selectedFolder = stepOut(m_selectedFolder);
                m_history.Remap(stepOut);
                continue;
            }
            const auto to = relativeOf(destination);
            const auto remapKey = [&](const std::string &key)
            {
                if (IsFolderKey(key)) return FolderKey(RemapContentFolder(FolderFromKey(key), from, to));
                if (key.rfind(projectScheme, 0) == 0) return projectScheme + RemapContentFolder(key.substr(projectScheme.size()), from, to);
                return key;
            };
            m_selection.Remap(remapKey);
            for (auto &key : m_clipboardKeys) key = remapKey(key);
            m_selectedFolder = RemapContentFolder(m_selectedFolder, from, to);
            m_history.Remap([&](const std::string &folder) { return RemapContentFolder(folder, from, to); });
        }

        shell.RefreshProjectAssets();
        shell.MarkProjectDirty();
        m_assetCacheDirty = true;
        m_itemsDirty = true;
        // The manifest was replaced; the details pane re-syncs once items rebuild.
        m_selectedAssetIndex = -1;
        if (!report.results.empty())
        {
            std::vector<std::string> keys;
            for (const auto &path : report.results) keys.push_back(KeyForPath(path));
            // Not SelectKeys: the registry cache is stale until the next rebuild,
            // which re-syncs the details pane from this selection.
            m_selection.Set(keys);
            m_scrollToKey = keys.back();
        }
        if (report.Succeeded() && !summary.empty())
        {
            shell.Log(EditorShell::ConsoleSeverity::Info, summary);
        }
    }

    void ContentBrowserPanel::MoveOrCopyInto(const std::vector<std::string> &keys, const std::string &folder, bool copy)
    {
        Defer([this, keys, folder, copy]()
              {
                  const auto *project = EditorShell::GetInstance().GetProject();
                  if (!project) return;
                  const ContentFileOperations operations(*project);
                  const auto paths = PathsForKeys(keys);
                  if (paths.empty())
                  {
                      EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Warning, "Only project assets and folders can be moved.");
                      return;
                  }
                  const auto destination = PathForKey(FolderKey(folder));
                  const auto result = copy ? operations.Copy(paths, destination) : operations.Move(paths, destination);
                  const auto count = copy ? result.results.size() : result.moved.size();
                  ApplyReport(result, std::string(copy ? "Copied " : "Moved ") + std::to_string(count) + " item(s) to " +
                                          (folder.empty() ? std::string("Assets") : folder) + ".");
              });
    }

    // ---------------------------------------------------------------------
    // Rename

    void ContentBrowserPanel::BeginRename(const std::string &key)
    {
        if (!IsKeyEditable(key))
        {
            return;
        }
        m_rename = {};
        m_rename.key = key;
        m_rename.focus = true;
        const auto path = PathForKey(key);
        std::string name;
        if (IsFolderKey(key))
        {
            name = PathToUtf8(path.filename());
        }
        else
        {
            name = PathToUtf8(path.stem());
            m_rename.extension = PathToUtf8(path.extension());
        }
        std::memcpy(m_rename.buffer.data(), name.data(), std::min(name.size(), m_rename.buffer.size() - 1));
        m_scrollToKey = key;
    }

    bool ContentBrowserPanel::RenderInlineRename(float width)
    {
        ImGui::SetNextItemWidth(std::max(1.0f, width));
        if (m_rename.focus)
        {
            ImGui::SetKeyboardFocusHere();
            m_rename.focus = false;
        }
        const bool submitted = ImGui::InputText("##InlineRename", m_rename.buffer.data(), m_rename.buffer.size(),
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const auto problem = ValidateContentItemName(std::string(m_rename.buffer.data()) + m_rename.extension);
        if (!problem.empty())
        {
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                GetEditorColorU32(EditorColor::Error), ImGui::GetStyle().FrameRounding, 0, 1.5f);
            ImGui::SetItemTooltip("%s", problem.c_str());
        }
        else if (!m_rename.extension.empty())
        {
            ImGui::SetItemTooltip("Extension %s is kept", m_rename.extension.c_str());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_rename = {};
            return true;
        }
        if (submitted || ImGui::IsItemDeactivated())
        {
            Defer([this]() { CommitRename(); });
            return true;
        }
        return false;
    }

    void ContentBrowserPanel::CommitRename()
    {
        if (m_rename.key.empty())
        {
            return;
        }
        const auto key = m_rename.key;
        std::string name = m_rename.buffer.data();
        const auto extension = m_rename.extension;
        m_rename = {};
        // Typing the extension again must not double it.
        const auto endsWithExtension = name.size() >= extension.size() &&
            std::equal(extension.rbegin(), extension.rend(), name.rbegin(), [](char a, char b)
                       { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
        if (!extension.empty() && !endsWithExtension) name += extension;

        const auto *project = EditorShell::GetInstance().GetProject();
        const auto path = PathForKey(key);
        if (!project || name == PathToUtf8(path.filename()))
        {
            return;
        }
        ApplyReport(ContentFileOperations(*project).Rename(path, name), "Renamed to " + name + ".");
    }

    // ---------------------------------------------------------------------
    // Toolbar, tree and status

    void ContentBrowserPanel::RenderAddMenu()
    {
        auto &editorShell = EditorShell::GetInstance();
        CommandMenuItem(ContentCommand::NewFolder, SelectionScope(), icons::FolderPlus);
        ImGui::Separator();
        if (ImGui::BeginMenu("Rendering"))
        {
            if (ImGui::MenuItem("Material"))
            {
                m_newMaterialNameBuffer.fill('\0');
                m_pendingMenuAction = PendingMenuAction::CreateMaterial;
            }
            if (ImGui::MenuItem("Surface Response"))
            {
                m_newSurfaceNameBuffer.fill(0);
                m_pendingMenuAction = PendingMenuAction::CreateSurfaceResponse;
            }
            if (ImGui::MenuItem("Particle System"))
            {
                m_newParticleSystemNameBuffer.fill('\0');
                m_pendingMenuAction = PendingMenuAction::CreateParticleSystem;
            }
            if (ImGui::MenuItem("Post Process Preset"))
            {
                m_newPostProcessPresetNameBuffer.fill('\0');
                m_pendingMenuAction = PendingMenuAction::CreatePostProcessPreset;
            }
            if (ImGui::MenuItem("Render Texture"))
            {
                m_newRenderTextureNameBuffer.fill('\0');
                m_pendingMenuAction = PendingMenuAction::CreateRenderTexture;
            }
            if (ImGui::MenuItem("Shader Graph"))
            {
                m_newShaderGraphNameBuffer.fill('\0');
                m_pendingMenuAction = PendingMenuAction::CreateShaderGraph;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Animation"))
        {
            if (ImGui::MenuItem("Animation Graph"))
            {
                m_newAnimationGraphNameBuffer.fill('\0');
                m_pendingMenuAction = PendingMenuAction::CreateAnimationGraph;
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Scriptable Object"))
        {
            m_newScriptableObjectNameBuffer.fill('\0');
            m_newScriptableObjectClassIndex = 0;
            m_pendingMenuAction = PendingMenuAction::CreateScriptableObject;
        }
        if (ImGui::MenuItem("Loading Screen")) editorShell.OpenLoadingScreenAsset({});
        if (ImGui::MenuItem("RmlUi Document"))
        {
            m_newRmlDocumentNameBuffer.fill('\0');
            m_rmlDocumentCreateError.clear();
            m_pendingMenuAction = PendingMenuAction::CreateRmlDocument;
        }
        if (ImGui::MenuItem("Input Mapping"))
        {
            m_newInputMappingNameBuffer.fill('\0');
            m_pendingMenuAction = PendingMenuAction::CreateInputMapping;
        }
        if (ImGui::MenuItem("UI Theme"))
        {
            m_newUIThemeNameBuffer.fill('\0');
            m_newUIThemeStarter = true;
            m_pendingMenuAction = PendingMenuAction::CreateUITheme;
        }
        if (ImGui::BeginMenu("Import"))
        {
            if (ImGui::MenuItem("Files..."))
            {
                m_pendingMenuAction = PendingMenuAction::ImportFiles;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy files into the current folder; models bring the files they reference");
            if (ImGui::MenuItem("3D Model..."))
            {
                m_pendingMenuAction = PendingMenuAction::ImportModel;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy a model into SourceModels and import it");
            ImGui::TextDisabled("Or drop files onto the editor window.");
            ImGui::EndMenu();
        }
    }

    void ContentBrowserPanel::RenderToolbar(assets::Project &project)
    {
        const auto scope = SelectionScope();
        // Single toolbar row: navigation and actions, breadcrumb path, then search and view options aligned right.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, ImGui::GetStyle().ItemSpacing.y));
        const auto toolButton = [&](const char *id, const char *icon, ContentCommand command)
        {
            const auto &info = GetContentCommandInfo(command);
            ImGui::BeginDisabled(!CanExecute(command, scope));
            ImGui::PushID(id);
            if (ImGui::Button(icon)) Execute(command, scope);
            ImGui::PopID();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(info.shortcutText ? "%s (%s)" : "%s", info.label, info.shortcutText);
            ImGui::SameLine();
        };
        toolButton("Back", icons::ArrowLeft, ContentCommand::GoBack);
        toolButton("Forward", icons::ArrowRight, ContentCommand::GoForward);
        toolButton("Up", icons::ArrowUp, ContentCommand::GoUp);
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button((std::string(icons::Plus) + " Add").c_str()))
        {
            ImGui::OpenPopup("ContentBrowserAddMenu");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create a folder or asset, or import files");
        ImGui::SameLine();
        toolButton("NewFolder", icons::FolderPlus, ContentCommand::NewFolder);
        toolButton("Refresh", icons::Rotate, ContentCommand::Refresh);
        ImGui::SameLine(0.0f, 10.0f);

        const float toolbarSpacing = ImGui::GetStyle().ItemSpacing.x;
        const float scaleWidth = 64.0f;
        const float iconButtonWidth = ImGui::GetFrameHeight() + 6.0f;
        const float searchWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.3f, 120.0f, 280.0f);
        const float rightGroupX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - searchWidth - scaleWidth -
                                  iconButtonWidth * 2.0f - toolbarSpacing * 3.0f;
        {
            const ImVec2 breadcrumbMin = ImGui::GetCursorScreenPos();
            const ImVec2 breadcrumbMax(breadcrumbMin.x + std::max(0.0f, rightGroupX - ImGui::GetCursorPosX() - 10.0f),
                                       breadcrumbMin.y + ImGui::GetFrameHeight());
            ImGui::PushClipRect(breadcrumbMin, breadcrumbMax, true);
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
            const auto crumb = [&](const char *label, const std::string &folder, bool current)
            {
                if (current)
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(label);
                    return;
                }
                if (ImGui::Button(label)) Execute(ContentCommand::Open, {.keys = {FolderKey(folder)}, .folder = folder});
                AcceptDropsInto(folder);
            };
            const auto crumbSeparator = []()
            {
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("/");
                ImGui::SameLine(0.0f, 2.0f);
            };
            crumb("Assets", {}, m_selectedFolder.empty() && m_openModelReference.empty());
            std::string breadcrumb;
            std::size_t partStart = 0;
            while (partStart < m_selectedFolder.size())
            {
                const auto partEnd = m_selectedFolder.find('/', partStart);
                const std::string label = m_selectedFolder.substr(partStart, partEnd == std::string::npos ? std::string::npos : partEnd - partStart);
                breadcrumb = JoinContentFolder(breadcrumb, label);
                crumbSeparator();
                ImGui::PushID(breadcrumb.c_str());
                crumb(label.c_str(), breadcrumb, partEnd == std::string::npos && m_openModelReference.empty());
                ImGui::PopID();
                if (partEnd == std::string::npos) break;
                partStart = partEnd + 1;
            }
            if (!m_openModelReference.empty())
            {
                crumbSeparator();
                crumb(m_openModelName.c_str(), {}, true);
            }
            ImGui::PopStyleColor();
            ImGui::PopClipRect();
        }

        ImGui::SameLine(rightGroupX);
        ImGui::SetNextItemWidth(searchWidth);
        ImGui::InputTextWithHint("##ContentFilter", (std::string(icons::Search) + "  Search assets...").c_str(),
                                 m_filterBuffer.data(), m_filterBuffer.size());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Searches names and types in this folder and its subfolders.\nAssets: %zu",
                              project.GetManifest().assetEntries.size());

        ImGui::SameLine();
        if (ImGui::Button(m_sortOrder.ascending ? icons::SortAscending : icons::SortDescending, ImVec2(iconButtonWidth, 0.0f)))
            ImGui::OpenPopup("ContentBrowserSortMenu");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sort by %s (%s)", GetContentSortKeyName(m_sortOrder.key).data(),
                              m_sortOrder.ascending ? "ascending" : "descending");
        if (ImGui::BeginPopup("ContentBrowserSortMenu"))
        {
            for (const auto key : {ContentSortKey::Name, ContentSortKey::Type, ContentSortKey::Modified, ContentSortKey::Size})
            {
                if (ImGui::MenuItem(GetContentSortKeyName(key).data(), nullptr, m_sortOrder.key == key))
                {
                    m_sortOrder.key = key;
                    m_itemsDirty = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Ascending", nullptr, m_sortOrder.ascending)) { m_sortOrder.ascending = true; m_itemsDirty = true; }
            if (ImGui::MenuItem("Descending", nullptr, !m_sortOrder.ascending)) { m_sortOrder.ascending = false; m_itemsDirty = true; }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        const bool grid = m_viewMode == ViewMode::Grid;
        if (ImGui::Button(grid ? icons::ListView : icons::GridView, ImVec2(iconButtonWidth, 0.0f)))
            m_viewMode = grid ? ViewMode::List : ViewMode::Grid;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(grid ? "Switch to list view" : "Switch to grid view");

        ImGui::SameLine();
        ImGui::BeginDisabled(!grid);
        ImGui::SetNextItemWidth(scaleWidth);
        ImGui::SliderFloat("##AssetGridScale", &m_assetGridScale, 0.5f, 1.5f, "%.2fx");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Asset grid scale (thumbnails, labels, padding and spacing)");
        ImGui::PopStyleVar(2);
        if (ImGui::BeginPopup("ContentBrowserAddMenu"))
        {
            RenderAddMenu();
            ImGui::EndPopup();
        }
    }

    void ContentBrowserPanel::RenderFolderTree(const assets::Project &project)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_FrameBg, 0.45f));
        ImGui::BeginChild("FolderTree", ImVec2(m_folderTreeWidth, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoNavInputs);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        const ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow |
                                             ImGuiTreeNodeFlags_SpanAvailWidth |
                                             (m_selectedFolder.empty() ? ImGuiTreeNodeFlags_Selected : 0);
        const bool rootOpen = ImGui::TreeNodeEx("AssetsRoot", rootFlags, "%s  Assets", icons::FolderOpen);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) NavigateTo({});
        AcceptDropsInto({});
        if (ImGui::BeginPopupContextItem("FolderTreeRootContext"))
        {
            RenderFolderContextMenu({});
            ImGui::EndPopup();
        }
        if (rootOpen)
        {
            for (const int index : m_cachedRootFolderIndices)
            {
                if (index >= 0 && static_cast<std::size_t>(index) < m_cachedFolders.size())
                    RenderFolderTreeNode(project, static_cast<std::size_t>(index));
            }
            ImGui::TreePop();
        }
        m_revealFolderInTree = false;
        ImGui::EndChild();
    }

    void ContentBrowserPanel::RenderFolderTreeNode(const assets::Project &project, std::size_t folderIndex)
    {
        const auto &folder = m_cachedFolders[folderIndex];
        const auto &children = m_cachedFolderChildIndices[folderIndex];
        const auto key = FolderKey(folder);
        const bool selected = folder == m_selectedFolder;
        const bool renaming = m_rename.key == key;
        // Opening the path to the current folder keeps it visible after navigation.
        if (m_revealFolderInTree && folder != m_selectedFolder && IsContentFolderAncestorOrSelf(folder, m_selectedFolder))
        {
            ImGui::SetNextItemOpen(true);
        }
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                         (renaming ? 0 : ImGuiTreeNodeFlags_SpanAvailWidth) |
                                         (selected ? ImGuiTreeNodeFlags_Selected : 0) |
                                         (children.empty() ? ImGuiTreeNodeFlags_Leaf : 0);
        const bool cut = m_clipboardCut && std::find(m_clipboardKeys.begin(), m_clipboardKeys.end(), key) != m_clipboardKeys.end();
        ImGui::PushID(folder.c_str());
        if (cut) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * kCutAlpha);
        bool open = false;
        if (renaming)
        {
            open = ImGui::TreeNodeEx("##Folder", flags, "%s", selected ? icons::FolderOpen : icons::Folder);
            ImGui::SameLine();
            RenderInlineRename(ImGui::GetContentRegionAvail().x);
        }
        else
        {
            open = ImGui::TreeNodeEx("##Folder", flags, "%s  %s", selected ? icons::FolderOpen : icons::Folder,
                                     m_cachedFolderLabels[folderIndex].c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) NavigateTo(folder);
            const ContentItem item{.folder = true, .key = key, .folderPath = folder, .name = m_cachedFolderLabels[folderIndex],
                                   .typeName = "Folder", .editable = IsKeyEditable(key)};
            HandleItemDragSource(item);
            AcceptDropsInto(folder);
            if (ImGui::BeginPopupContextItem("FolderTreeContext"))
            {
                RenderFolderContextMenu(folder);
                ImGui::EndPopup();
            }
        }
        if (cut) ImGui::PopStyleVar();
        if (open)
        {
            for (const int child : children)
            {
                if (child >= 0 && static_cast<std::size_t>(child) < m_cachedFolders.size())
                    RenderFolderTreeNode(project, static_cast<std::size_t>(child));
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void ContentBrowserPanel::RenderStatusBar()
    {
        ImGui::Separator();
        std::string status;
        if (!m_openModelReference.empty())
        {
            status = std::to_string(m_openModelObjects.size()) + " objects in " + m_openModelName;
        }
        else
        {
            status = std::to_string(m_items.size()) + (m_items.size() == 1 ? " item" : " items");
            if (!m_selection.Empty()) status += "   " + std::to_string(m_selection.Size()) + " selected";
            if (!m_itemsFilter.empty()) status += "   matching \"" + m_itemsFilter + "\"";
        }
        if (!m_clipboardKeys.empty())
            status += "   " + std::to_string(m_clipboardKeys.size()) + (m_clipboardCut ? " cut" : " copied");
        ImGui::TextDisabled("%s", status.c_str());
    }

    // ---------------------------------------------------------------------
    // Item views

    void ContentBrowserPanel::HandleItemInteraction(assets::Project &project, const ContentItem &item)
    {
        (void)project;
        const ImGuiIO &io = ImGui::GetIO();
        const bool selected = m_selection.Contains(item.key);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            if (io.KeyShift || io.KeyCtrl)
                m_selection.Click(item.key, io.KeyCtrl, io.KeyShift, ShownKeys());
            else if (selected && m_selection.Size() > 1)
                m_releaseSelectKey = item.key;
            else
                m_selection.Select(item.key);
            SyncDetailsSelection();
        }
        if (m_releaseSelectKey == item.key && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (hovered)
            {
                m_selection.Select(item.key);
                SyncDetailsSelection();
            }
            m_releaseSelectKey.clear();
        }
        // Context actions apply to the item under the cursor.
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected)
        {
            m_selection.Select(item.key);
            SyncDetailsSelection();
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            Execute(ContentCommand::Open, {.keys = {item.key}, .folder = m_selectedFolder});
        }
        HandleItemDragSource(item);
        if (item.folder)
        {
            AcceptDropsInto(item.folderPath);
            if (hovered)
            {
                m_hoveredFolder = item.folderPath;
                m_hoveringFolderItem = true;
            }
        }
        if (ImGui::BeginPopupContextItem("ItemContext"))
        {
            RenderItemContextMenu(project, item);
            ImGui::EndPopup();
        }
        if (hovered && !ImGui::GetDragDropPayload())
        {
            if (item.folder)
                ImGui::SetTooltip("%s\nFolder: %s", item.name.c_str(), item.folderPath.c_str());
            else
                ImGui::SetTooltip("%s\n%s\n%s   %s", item.name.c_str(), item.key.c_str(), FormatContentSize(item.size).c_str(),
                                  FormatContentTimestamp(item.modified).c_str());
        }
    }

    void ContentBrowserPanel::HandleItemDragSource(const ContentItem &item)
    {
        if (!ImGui::BeginDragDropSource())
        {
            return;
        }
        m_releaseSelectKey.clear();
        if (item.folder)
            ImGui::SetDragDropPayload(kContentBrowserFolderDragDropPayload, item.folderPath.c_str(), item.folderPath.size() + 1);
        else
            ImGui::SetDragDropPayload(kContentBrowserAssetDragDropPayload, item.key.c_str(), item.key.size() + 1);
        const std::size_t count = m_selection.Contains(item.key) ? m_selection.Size() : 1;
        ImGui::Text("%s  %s", item.folder ? icons::Folder : GetAssetTypeIcon(item.type).glyph, item.name.c_str());
        if (count > 1) ImGui::TextDisabled("+%zu more", count - 1);
        ImGui::TextDisabled(ImGui::GetIO().KeyCtrl ? "Drop on a folder to copy" : "Drop on a folder to move (Ctrl to copy)");
        ImGui::EndDragDropSource();
    }

    void ContentBrowserPanel::AcceptDropsInto(const std::string &folder)
    {
        if (!ImGui::BeginDragDropTarget())
        {
            return;
        }
        std::string draggedKey;
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
            draggedKey.assign(static_cast<const char *>(payload->Data), payload->DataSize > 0 ? payload->DataSize - 1 : 0);
        else if (const ImGuiPayload *folderPayload = ImGui::AcceptDragDropPayload(kContentBrowserFolderDragDropPayload))
            draggedKey = FolderKey(std::string(static_cast<const char *>(folderPayload->Data),
                                               folderPayload->DataSize > 0 ? folderPayload->DataSize - 1 : 0));
        if (!draggedKey.empty() && draggedKey != FolderKey(folder))
        {
            // Dragging part of the selection moves the whole selection.
            const auto keys = m_selection.Contains(draggedKey) ? m_selection.Keys() : std::vector<std::string>{draggedKey};
            MoveOrCopyInto(keys, folder, ImGui::GetIO().KeyCtrl);
        }
        ImGui::EndDragDropTarget();
    }

    void ContentBrowserPanel::RenderItemContextMenu(assets::Project &project, const ContentItem &item)
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto scope = SelectionScope();
        const bool single = scope.keys.size() <= 1;
        CommandMenuItem(ContentCommand::Open, scope);
        if (!item.folder && single)
        {
            const auto &asset = project.GetManifest().assetEntries[static_cast<std::size_t>(item.assetIndex)];
            if ((asset.type == assets::ProjectAssetType::Model || asset.type == assets::ProjectAssetType::Mesh) &&
                ImGui::MenuItem("Add to Scene", nullptr, false, editorShell.GetScene() && !editorShell.GetEngine().IsRuntimeRunning()))
                AddAssetInFrontOfCamera(asset.reference);
            if (project.ResolveAssetReference(asset.reference).extension() == ".rml" && ImGui::MenuItem("Open in UI Editor"))
                editorShell.OpenRmlDocument(asset.reference);
            if (ImGui::MenuItem("Find References..."))
            {
                if (!m_referenceSearch) m_referenceSearch = std::make_unique<AssetReferenceSearchPanel>();
                m_referenceSearch->Open(project.GetAssetDirectoryPath(), asset.reference);
            }
            if (asset.type == assets::ProjectAssetType::Prefab && ImGui::MenuItem("Export as Static Mesh"))
            {
                const auto prefab = asset;
                Defer([this, prefab]()
                      {
                          auto &shell = EditorShell::GetInstance();
                          auto *currentProject = shell.GetProject();
                          std::string outputReference, exportError;
                          if (currentProject && ExportPrefabMesh(*currentProject, prefab, m_selectedFolder, &outputReference, &exportError))
                          {
                              shell.RefreshProjectAssets();
                              m_assetCacheDirty = true;
                              shell.MarkProjectDirty();
                              shell.Log(EditorShell::ConsoleSeverity::Info, "Exported prefab mesh: " + outputReference);
                          }
                          else if (!exportError.empty()) shell.Log(EditorShell::ConsoleSeverity::Error, exportError);
                      });
            }
        }
        ImGui::Separator();
        CommandMenuItem(ContentCommand::Cut, scope, icons::Cut);
        CommandMenuItem(ContentCommand::Copy, scope, icons::Copy);
        CommandMenuItem(ContentCommand::Paste, {.keys = {}, .folder = item.folder ? item.folderPath : m_selectedFolder}, icons::Paste);
        CommandMenuItem(ContentCommand::Duplicate, scope);
        CommandMenuItem(ContentCommand::Rename, scope, icons::Rename);
        CommandMenuItem(ContentCommand::Delete, scope, icons::Trash);
        CommandMenuItem(ContentCommand::DeletePermanently, scope);
        ImGui::Separator();
        CommandMenuItem(ContentCommand::RevealInFileManager, scope, icons::FolderOpen);
        if (!item.folder) CommandMenuItem(ContentCommand::OpenWithSystem, scope, icons::OpenExternal);
        CommandMenuItem(ContentCommand::CopyPath, scope, icons::Link);
        if (!item.folder) CommandMenuItem(ContentCommand::CopyReference, scope);
        if (!item.editable && !item.folder)
        {
            ImGui::Separator();
            ImGui::TextDisabled(assets::Project::IsEngineAssetReference(item.key) ? "Engine assets are read-only."
                                                                                   : "Generated assets are managed by their importer.");
        }
    }

    void ContentBrowserPanel::RenderFolderContextMenu(const std::string &folder)
    {
        const CommandScope scope{.keys = folder.empty() ? std::vector<std::string>{} : std::vector<std::string>{FolderKey(folder)},
                                 .folder = folder};
        const CommandScope inside{.keys = {}, .folder = folder};
        if (!folder.empty()) CommandMenuItem(ContentCommand::Open, scope);
        CommandMenuItem(ContentCommand::NewFolder, inside, icons::FolderPlus);
        CommandMenuItem(ContentCommand::Paste, inside, icons::Paste);
        if (!folder.empty())
        {
            ImGui::Separator();
            CommandMenuItem(ContentCommand::Cut, scope, icons::Cut);
            CommandMenuItem(ContentCommand::Copy, scope, icons::Copy);
            CommandMenuItem(ContentCommand::Duplicate, scope);
            CommandMenuItem(ContentCommand::Rename, scope, icons::Rename);
            CommandMenuItem(ContentCommand::Delete, scope, icons::Trash);
            CommandMenuItem(ContentCommand::DeletePermanently, scope);
        }
        ImGui::Separator();
        CommandMenuItem(ContentCommand::RevealInFileManager, scope, icons::FolderOpen);
        CommandMenuItem(ContentCommand::CopyPath, scope, icons::Link);
    }

    void ContentBrowserPanel::RenderBackgroundContextMenu()
    {
        if (!ImGui::BeginPopupContextWindow("ContentBrowserContextMenu", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
        {
            return;
        }
        const CommandScope here{.keys = {}, .folder = m_selectedFolder};
        CommandMenuItem(ContentCommand::Paste, here, icons::Paste);
        CommandMenuItem(ContentCommand::SelectAll, here);
        ImGui::Separator();
        RenderAddMenu();
        ImGui::Separator();
        CommandMenuItem(ContentCommand::RevealInFileManager, here, icons::FolderOpen);
        CommandMenuItem(ContentCommand::CopyPath, here, icons::Link);
        ImGui::EndPopup();
    }

    void ContentBrowserPanel::RenderItemGrid(assets::Project &project)
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto metrics = BeginCardGrid(m_assetGridScale);
        m_gridColumns = metrics.columns;
        if (!m_itemsFilter.empty())
        {
            ImGui::TextDisabled("Search results in %s", m_selectedFolder.empty() ? "Assets" : m_selectedFolder.c_str());
        }
        if (m_items.empty())
        {
            ImGui::TextDisabled(m_itemsFilter.empty() ? "This folder is empty. Right-click to create something, or drop files here."
                                                      : "No matching assets.");
        }

        const auto &assets = project.GetManifest().assetEntries;
        const int rowCount = static_cast<int>((m_items.size() + static_cast<std::size_t>(metrics.columns) - 1) /
                                              static_cast<std::size_t>(metrics.columns));
        int scrollRow = -1;
        if (!m_scrollToKey.empty())
        {
            const auto it = std::find_if(m_items.begin(), m_items.end(), [&](const ContentItem &item) { return item.key == m_scrollToKey; });
            if (it != m_items.end()) scrollRow = static_cast<int>(it - m_items.begin()) / metrics.columns;
        }

        if (ImGui::BeginTable("AssetIconGrid", metrics.columns, ImGuiTableFlags_SizingFixedFit))
        {
            for (int column = 0; column < metrics.columns; ++column)
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, metrics.width);
            ImGuiListClipper clipper;
            clipper.Begin(rowCount);
            if (scrollRow >= 0) clipper.IncludeItemByIndex(scrollRow);
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, metrics.height);
                    for (int column = 0; column < metrics.columns; ++column)
                    {
                        const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(metrics.columns) +
                                                  static_cast<std::size_t>(column);
                        if (index >= m_items.size()) break;
                        ImGui::TableSetColumnIndex(column);
                        const ContentItem &item = m_items[index];
                        const bool selected = m_selection.Contains(item.key);
                        const bool cut = m_clipboardCut &&
                                         std::find(m_clipboardKeys.begin(), m_clipboardKeys.end(), item.key) != m_clipboardKeys.end();
                        const float alpha = cut ? kCutAlpha : 1.0f;
                        const bool renaming = m_rename.key == item.key;
                        ImGui::PushID(item.key.c_str());
                        // The rename field sits on top of the card and must receive its clicks.
                        if (renaming) ImGui::SetNextItemAllowOverlap();
                        ImGui::InvisibleButton("Card", ImVec2(metrics.width, metrics.height));
                        const bool hovered = ImGui::IsItemHovered();
                        DrawCardBackground(metrics, selected, hovered);
                        const ImVec2 minimum = ImGui::GetItemRectMin();
                        if (item.folder)
                        {
                            // Folders have no preview frame, so the icon and name are centred together as one group.
                            const FittedIcon folderIcon = FitIcon(icons::Folder, ImVec2(metrics.previewSize * 0.86f, metrics.previewSize * 0.86f));
                            const float groupHeight = folderIcon.size.y + metrics.labelGap + ImGui::GetTextLineHeight();
                            const float iconTop = minimum.y + std::floor((metrics.height - groupHeight) * 0.5f);
                            ImU32 folderColor = kFolderColor;
                            if (cut) folderColor = (folderColor & ~IM_COL32_A_MASK) | (static_cast<ImU32>(255 * kCutAlpha) << IM_COL32_A_SHIFT);
                            DrawFittedIcon(ImGui::GetWindowDrawList(), icons::Folder, folderIcon,
                                           ImVec2(minimum.x + (metrics.width - folderIcon.size.x) * 0.5f, iconTop), folderColor);
                            if (!renaming)
                                DrawCenteredCardText(ImGui::GetWindowDrawList(), item.name, minimum.x + metrics.inset, metrics.textWidth,
                                                     iconTop + folderIcon.size.y + metrics.labelGap, ImGui::GetColorU32(ImGuiCol_Text, alpha));
                        }
                        else
                        {
                            const auto &asset = assets[static_cast<std::size_t>(item.assetIndex)];
                            DrawAssetPreview(metrics, m_thumbnailCache->Get(project, editorShell.GetEngine(), asset), item.type, alpha);
                            DrawCardLabels(metrics, item.name, item.typeName, alpha, !renaming);
                        }
                        if (item.key == m_scrollToKey)
                        {
                            if (!ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
                            m_scrollToKey.clear();
                        }
                        HandleItemInteraction(project, item);
                        if (renaming)
                        {
                            ImGui::SetCursorScreenPos(ImVec2(minimum.x + metrics.inset / 2.0f, minimum.y + metrics.labelTop - 2.0f));
                            RenderInlineRename(metrics.width - metrics.inset);
                        }
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndTable();
        }
        EndCardGrid();
        HandleArrowNavigation();
    }

    void ContentBrowserPanel::RenderItemList(assets::Project &project)
    {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
        m_gridColumns = 1;
        if (!ImGui::BeginTable("ContentList", 4, flags, ImGui::GetContentRegionAvail()))
        {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        const auto sortFlags = [this](ContentSortKey key)
        { return m_sortOrder.key == key ? ImGuiTableColumnFlags_DefaultSort : ImGuiTableColumnFlags_None; };
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | sortFlags(ContentSortKey::Name), 3.0f, static_cast<ImGuiID>(ContentSortKey::Name));
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch | sortFlags(ContentSortKey::Type), 1.2f, static_cast<ImGuiID>(ContentSortKey::Type));
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch | sortFlags(ContentSortKey::Size), 0.8f, static_cast<ImGuiID>(ContentSortKey::Size));
        ImGui::TableSetupColumn("Date Modified", ImGuiTableColumnFlags_WidthStretch | sortFlags(ContentSortKey::Modified), 1.4f, static_cast<ImGuiID>(ContentSortKey::Modified));
        ImGui::TableHeadersRow();

        // Headers and the toolbar's sort menu share one sort order.
        if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs())
        {
            if (specs->SpecsDirty && specs->SpecsCount > 0)
            {
                m_sortOrder = {static_cast<ContentSortKey>(specs->Specs[0].ColumnUserID),
                               specs->Specs[0].SortDirection != ImGuiSortDirection_Descending};
                m_itemsDirty = true;
                specs->SpecsDirty = false;
            }
            else if (specs->SpecsCount > 0 &&
                     (static_cast<ContentSortKey>(specs->Specs[0].ColumnUserID) != m_sortOrder.key ||
                      (specs->Specs[0].SortDirection != ImGuiSortDirection_Descending) != m_sortOrder.ascending))
            {
                // Columns are declared Name, Type, Size, Date Modified.
                const int column = m_sortOrder.key == ContentSortKey::Name   ? 0
                                   : m_sortOrder.key == ContentSortKey::Type ? 1
                                   : m_sortOrder.key == ContentSortKey::Size ? 2
                                                                             : 3;
                ImGui::TableSetColumnSortDirection(column, m_sortOrder.ascending ? ImGuiSortDirection_Ascending : ImGuiSortDirection_Descending, false);
            }
        }

        const float rowHeight = ImGui::GetFrameHeight();
        int scrollRow = -1;
        if (!m_scrollToKey.empty())
        {
            const auto it = std::find_if(m_items.begin(), m_items.end(), [&](const ContentItem &item) { return item.key == m_scrollToKey; });
            if (it != m_items.end()) scrollRow = static_cast<int>(it - m_items.begin());
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_items.size()), rowHeight);
        if (scrollRow >= 0) clipper.IncludeItemByIndex(scrollRow);
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const ContentItem &item = m_items[static_cast<std::size_t>(row)];
                const bool selected = m_selection.Contains(item.key);
                const bool cut = m_clipboardCut && std::find(m_clipboardKeys.begin(), m_clipboardKeys.end(), item.key) != m_clipboardKeys.end();
                const bool renaming = m_rename.key == item.key;
                ImGui::PushID(item.key.c_str());
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
                ImGui::TableSetColumnIndex(0);
                if (cut) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * kCutAlpha);
                const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                ImGui::Selectable("##Row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0.0f, rowHeight));
                if (item.key == m_scrollToKey)
                {
                    if (!ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
                    m_scrollToKey.clear();
                }
                HandleItemInteraction(project, item);

                const auto icon = item.folder ? AssetTypeIcon{icons::Folder, kFolderColor} : GetAssetTypeIcon(item.type);
                auto *drawList = ImGui::GetWindowDrawList();
                const float textY = rowStart.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
                drawList->AddText(ImVec2(rowStart.x + 2.0f, textY), icon.color, icon.glyph);
                const float nameX = rowStart.x + ImGui::GetFontSize() * 1.6f;
                if (renaming)
                {
                    ImGui::SetCursorScreenPos(ImVec2(nameX, rowStart.y));
                    RenderInlineRename(ImGui::GetContentRegionAvail().x);
                }
                else
                {
                    drawList->AddText(ImVec2(nameX, textY), ImGui::GetColorU32(ImGuiCol_Text), item.name.c_str());
                }
                const auto cell = [&](int column, const std::string &text)
                {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", text.c_str());
                };
                cell(1, item.typeName);
                cell(2, item.folder ? std::string{} : FormatContentSize(item.size));
                cell(3, FormatContentTimestamp(item.modified));
                if (cut) ImGui::PopStyleVar();
                ImGui::PopID();
            }
        }
        // The list scrolls inside the table, so its empty space hosts the background menu.
        RenderBackgroundContextMenu();
        ImGui::EndTable();
        HandleArrowNavigation();
    }

    void ContentBrowserPanel::RenderModelObjectGrid(assets::Project &project)
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto metrics = BeginCardGrid(m_assetGridScale);
        const int rowCount = static_cast<int>((m_openModelObjects.size() + static_cast<std::size_t>(metrics.columns) - 1) /
                                              static_cast<std::size_t>(metrics.columns));
        if (ImGui::BeginTable("ModelObjectGrid", metrics.columns, ImGuiTableFlags_SizingFixedFit))
        {
            for (int column = 0; column < metrics.columns; ++column)
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, metrics.width);
            ImGuiListClipper clipper;
            clipper.Begin(rowCount);
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, metrics.height);
                    for (int column = 0; column < metrics.columns; ++column)
                    {
                        const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(metrics.columns) +
                                                  static_cast<std::size_t>(column);
                        if (index >= m_openModelObjects.size()) break;
                        ImGui::TableSetColumnIndex(column);
                        const auto &object = m_openModelObjects[index];
                        const assets::ProjectAssetEntry asset{.reference = object.reference, .type = object.type};
                        ImGui::PushID(static_cast<int>(index));
                        ImGui::InvisibleButton("ModelObjectCard", ImVec2(metrics.width, metrics.height));
                        const bool hovered = ImGui::IsItemHovered();
                        DrawCardBackground(metrics, false, hovered);
                        DrawAssetPreview(metrics, m_thumbnailCache->Get(project, editorShell.GetEngine(), asset), object.type, 1.0f);
                        DrawCardLabels(metrics, object.name, std::string(assets::Project::GetAssetTypeName(object.type)), 1.0f);
                        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) OpenAsset(editorShell, project, asset);
                        if (ImGui::BeginDragDropSource())
                        {
                            ImGui::SetDragDropPayload(kContentBrowserAssetDragDropPayload, object.reference.c_str(), object.reference.size() + 1);
                            ImGui::TextUnformatted(object.name.c_str());
                            ImGui::EndDragDropSource();
                        }
                        if (ImGui::BeginPopupContextItem("ModelObjectContext"))
                        {
                            if (object.type == assets::ProjectAssetType::Mesh &&
                                ImGui::MenuItem("Add to Scene", nullptr, false, editorShell.GetScene() && !editorShell.GetEngine().IsRuntimeRunning()))
                                AddAssetInFrontOfCamera(object.reference);
                            const char *extractLabel = object.type == assets::ProjectAssetType::Material ? "Extract and Use" : "Extract";
                            if (ImGui::MenuItem(extractLabel))
                            {
                                const auto extracted = object;
                                Defer([this, extracted]()
                                      {
                                          auto &shell = EditorShell::GetInstance();
                                          auto *currentProject = shell.GetProject();
                                          std::string extractedReference, extractionError;
                                          if (currentProject && ExtractModelSubAsset(*currentProject, extracted, m_selectedFolder, &extractedReference, &extractionError))
                                          {
                                              shell.RefreshProjectAssets();
                                              m_assetCacheDirty = true;
                                              shell.MarkProjectDirty();
                                              shell.Log(EditorShell::ConsoleSeverity::Info, "Extracted model sub-asset: " + extractedReference);
                                          }
                                          else if (!extractionError.empty()) shell.Log(EditorShell::ConsoleSeverity::Error, extractionError);
                                      });
                            }
                            ImGui::EndPopup();
                        }
                        if (hovered) ImGui::SetTooltip("%s\n%s\nRight-click to extract", object.name.c_str(), object.reference.c_str());
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndTable();
        }
        EndCardGrid();
    }

    // ---------------------------------------------------------------------
    // Keyboard, OS drops and confirmation

    void ContentBrowserPanel::HandleShortcuts(assets::Project &project)
    {
        (void)project;
        const ImGuiIO &io = ImGui::GetIO();
        if (!HasKeyboardFocus() || io.WantTextInput || !m_rename.key.empty() ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        {
            return;
        }
        const auto scope = SelectionScope();
        for (const auto &info : GetContentCommands())
        {
            if (info.chord != ImGuiKey_None && ImGui::IsKeyChordPressed(info.chord))
            {
                Execute(info.command, scope);
                return;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !io.KeyCtrl && !io.KeyAlt)
            Execute(ContentCommand::GoUp, scope);
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            m_selection.Clear();
            SyncDetailsSelection();
        }
    }

    void ContentBrowserPanel::HandleArrowNavigation()
    {
        const ImGuiIO &io = ImGui::GetIO();
        if (!HasKeyboardFocus() || io.WantTextInput || !m_rename.key.empty() || io.KeyAlt || io.KeyCtrl || m_items.empty())
        {
            return;
        }
        int step = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) step = m_gridColumns > 1 ? 1 : 0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) step = m_gridColumns > 1 ? -1 : 0;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) step = m_gridColumns;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) step = -m_gridColumns;
        const bool home = ImGui::IsKeyPressed(ImGuiKey_Home, false);
        const bool end = ImGui::IsKeyPressed(ImGuiKey_End, false);
        if (step == 0 && !home && !end)
        {
            return;
        }
        const int count = static_cast<int>(m_items.size());
        int current = -1;
        if (const auto *primary = m_selection.Primary())
        {
            const auto it = std::find_if(m_items.begin(), m_items.end(), [&](const ContentItem &item) { return item.key == *primary; });
            if (it != m_items.end()) current = static_cast<int>(it - m_items.begin());
        }
        int next = home ? 0 : end ? count - 1 : current < 0 ? 0 : std::clamp(current + step, 0, count - 1);
        const auto &key = m_items[static_cast<std::size_t>(next)].key;
        if (io.KeyShift)
            m_selection.Click(key, false, true, ShownKeys());
        else
            m_selection.Select(key);
        m_scrollToKey = key;
        SyncDetailsSelection();
    }

    void ContentBrowserPanel::ProcessDroppedFiles(assets::Project &project)
    {
        auto &window = EditorShell::GetInstance().GetEngine().GetWindow();
        if (window.GetDroppedPaths().empty() || !IsOpen() || !m_openModelReference.empty())
        {
            return;
        }
        // The content browser is the editor's only OS drop target: files land
        // in the folder under the cursor, or in the current folder.
        const auto paths = window.GetDroppedPaths();
        window.ClearDroppedPaths();
        ImportExternalItems(project, paths, m_hoveringFolderItem ? m_hoveredFolder : m_selectedFolder);
    }

    void ContentBrowserPanel::ImportExternalItems(assets::Project &project, const std::vector<std::filesystem::path> &paths,
                                                  const std::string &folder)
    {
        const auto report = ContentFileOperations(project).Import(paths, PathForKey(FolderKey(folder)));
        ApplyReport(report, "Imported " + std::to_string(report.results.size()) + " item(s) into " +
                                (folder.empty() ? std::string("Assets") : folder) + ".");

        // Source models also need their import run before they can be used.
        auto &shell = EditorShell::GetInstance();
        if (shell.GetEngine().IsRuntimeRunning() || shell.IsModelImportRunning())
        {
            return;
        }
        for (const auto &imported : report.results)
        {
            std::vector<std::filesystem::path> models;
            std::error_code error;
            if (std::filesystem::is_directory(imported, error))
            {
                for (std::filesystem::recursive_directory_iterator it(imported, error), end; it != end && !error; it.increment(error))
                    if (it->is_regular_file(error) && assetimport::IsSupportedModelSourceFile(it->path())) models.push_back(it->path());
            }
            else if (assetimport::IsSupportedModelSourceFile(imported))
            {
                models.push_back(imported);
            }
            for (const auto &model : models)
            {
                const assets::ProjectAssetEntry entry{.reference = project.MakeAssetReference(model), .type = assets::ProjectAssetType::Model};
                std::string importError;
                if (ImportSourceModelAsset(project, entry, &importError))
                    shell.Log(EditorShell::ConsoleSeverity::Info, "Imported model: " + entry.reference);
                else if (!importError.empty())
                    shell.Log(EditorShell::ConsoleSeverity::Error, importError);
            }
        }
    }

    void ContentBrowserPanel::RenderDeleteConfirmation(assets::Project &project)
    {
        if (m_openDeletePopup)
        {
            ImGui::OpenPopup("Delete Content");
            m_openDeletePopup = false;
        }
        if (!ImGui::BeginPopupModal("Delete Content", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        const auto count = m_pendingDeletePaths.size();
        const bool permanent = m_pendingDeleteMode == ContentDeleteMode::Permanent;
        const bool anyFolder = std::any_of(m_pendingDeletePaths.begin(), m_pendingDeletePaths.end(), [](const std::filesystem::path &path)
                                           { std::error_code error; return std::filesystem::is_directory(path, error); });
        const char *verb = permanent ? "Permanently delete" : "Move to the trash:";
        if (count == 1)
            ImGui::TextWrapped(permanent ? "%s '%s'?" : "%s '%s'", verb, PathToUtf8(m_pendingDeletePaths.front().filename()).c_str());
        else
            ImGui::Text(permanent ? "%s %zu items?" : "%s %zu items", verb, count);
        constexpr std::size_t kListed = 8;
        if (count > 1)
        {
            for (std::size_t index = 0; index < std::min(count, kListed); ++index)
                ImGui::BulletText("%s", PathToUtf8(m_pendingDeletePaths[index].filename()).c_str());
            if (count > kListed) ImGui::TextDisabled("...and %zu more", count - kListed);
        }
        if (anyFolder) ImGui::TextDisabled("Folders are removed with all of their contents.");
        ImGui::TextDisabled(permanent ? "This cannot be undone. Scenes that use these assets will lose them."
                                      : "Restore items from the system trash to bring them back.");
        ImGui::Spacing();
        const auto remove = [&](ContentDeleteMode mode)
        {
            const auto report = ContentFileOperations(project).Delete(m_pendingDeletePaths, mode);
            const auto removed = std::to_string(report.moved.size()) + " item(s)";
            ApplyReport(report, mode == ContentDeleteMode::Trash ? "Moved " + removed + " to the trash." : "Deleted " + removed + ".");
            if (mode == ContentDeleteMode::Trash && !report.errors.empty())
                EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Warning,
                                               "Items that could not be trashed were kept; use Delete Permanently (Shift+Del) to remove them.");
            m_pendingDeletePaths.clear();
            ImGui::CloseCurrentPopup();
        };
        ImGui::PushStyleColor(ImGuiCol_Button, GetEditorColorU32(permanent ? EditorColor::Error : EditorColor::Accent, 0.75f));
        const bool confirmed = ImGui::Button(permanent ? "Delete Permanently" : "Move to Trash") || ImGui::IsKeyPressed(ImGuiKey_Enter, false);
        ImGui::PopStyleColor();
        if (confirmed)
        {
            remove(m_pendingDeleteMode);
        }
        else if (!permanent)
        {
            ImGui::SameLine();
            if (ImGui::Button("Delete Permanently")) remove(ContentDeleteMode::Permanent);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            m_pendingDeletePaths.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
