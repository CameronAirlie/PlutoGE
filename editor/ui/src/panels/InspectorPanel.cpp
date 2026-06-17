
#include "PlutoGE/ui/panels/InspectorPanel.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <imgui.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace PlutoGE::ui
{
    namespace
    {
        constexpr std::size_t kInspectorPathBufferSize = 512;
        constexpr std::size_t kNewScriptNameBufferSize = 128;
        constexpr const char *kAddableComponentLabels[] = {
            "Mesh Component",
            "Animation Component",
            "Camera Component",
            "Light Component",
            "Rigidbody Component",
            "Collider Component",
            "IBL Capture Component",
            "Script Component",
            "Canvas Component",
            "Rect Transform Component",
            "UI Image Component",
            "UI Text Component",
            "UI Button Component",
        };

        enum class AddableComponentType
        {
            Mesh = 0,
            Animation = 1,
            Camera = 2,
            Light = 3,
            Rigidbody = 4,
            Collider = 5,
            IblCapture = 6,
            Script = 7,
            Canvas = 8,
            RectTransform = 9,
            UIImage = 10,
            UIText = 11,
            UIButton = 12,
        };

        struct ScriptAssetOption
        {
            std::string reference;
            std::string displayName;
            std::string className;
            bool classLoaded = false;
        };

        struct AssetReferenceOption
        {
            std::string reference;
            std::string displayName;
        };

        std::string_view TrimWhitespace(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
            {
                text.remove_prefix(1);
            }

            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
            {
                text.remove_suffix(1);
            }

            return text;
        }

        std::string JoinTags(const std::vector<std::string> &tags)
        {
            std::string joined;
            for (const auto &tag : tags)
            {
                if (tag.empty())
                {
                    continue;
                }

                if (!joined.empty())
                {
                    joined += ", ";
                }
                joined += tag;
            }
            return joined;
        }

        std::vector<std::string> ParseTags(std::string_view text)
        {
            std::vector<std::string> tags;
            while (!text.empty())
            {
                const auto comma = text.find(',');
                auto token = TrimWhitespace(text.substr(0, comma));
                if (!token.empty())
                {
                    const std::string tag(token);
                    if (std::find(tags.begin(), tags.end(), tag) == tags.end())
                    {
                        tags.push_back(tag);
                    }
                }

                if (comma == std::string_view::npos)
                {
                    break;
                }
                text.remove_prefix(comma + 1);
            }
            return tags;
        }

        bool StartsWith(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        bool EndsWithInsensitive(std::string_view text, std::string_view suffix)
        {
            if (text.size() < suffix.size())
            {
                return false;
            }

            const auto offset = text.size() - suffix.size();
            for (std::size_t index = 0; index < suffix.size(); ++index)
            {
                const auto left = static_cast<unsigned char>(text[offset + index]);
                const auto right = static_cast<unsigned char>(suffix[index]);
                if (std::tolower(left) != std::tolower(right))
                {
                    return false;
                }
            }

            return true;
        }

        std::string ExtractClassName(std::string_view line)
        {
            const std::size_t classPos = line.find("class ");
            if (classPos == std::string_view::npos)
            {
                return {};
            }

            std::string_view remainder = line.substr(classPos + 6);
            remainder = TrimWhitespace(remainder);

            std::size_t length = 0;
            while (length < remainder.size())
            {
                const unsigned char character = static_cast<unsigned char>(remainder[length]);
                if (!std::isalnum(character) && character != '_')
                {
                    break;
                }

                ++length;
            }

            return std::string(remainder.substr(0, length));
        }

        std::optional<std::string> ParseScriptClassNameFromFile(const std::filesystem::path &filePath)
        {
            std::ifstream input(filePath);
            if (!input.is_open())
            {
                return std::nullopt;
            }

            std::string namespaceName;
            std::string firstClassName;
            std::string scriptClassName;
            std::string line;
            while (std::getline(input, line))
            {
                std::string_view trimmed = TrimWhitespace(line);
                if (trimmed.empty() || StartsWith(trimmed, "//"))
                {
                    continue;
                }

                if (namespaceName.empty() && StartsWith(trimmed, "namespace "))
                {
                    trimmed.remove_prefix(std::string_view("namespace ").size());
                    trimmed = TrimWhitespace(trimmed);
                    const std::size_t delimiterPos = trimmed.find_first_of("{;");
                    namespaceName = std::string(TrimWhitespace(trimmed.substr(0, delimiterPos)));
                }

                if (trimmed.find("class ") == std::string_view::npos)
                {
                    continue;
                }

                const std::string className = ExtractClassName(trimmed);
                if (className.empty())
                {
                    continue;
                }

                if (firstClassName.empty())
                {
                    firstClassName = className;
                }

                if (trimmed.find("ScriptBehaviour") != std::string_view::npos)
                {
                    scriptClassName = className;
                    break;
                }
            }

            const std::string &resolvedClassName = scriptClassName.empty() ? firstClassName : scriptClassName;
            if (resolvedClassName.empty())
            {
                return std::nullopt;
            }

            if (namespaceName.empty())
            {
                return resolvedClassName;
            }

            return namespaceName + "." + resolvedClassName;
        }

        std::string FindLoadedClassForShortName(const std::vector<std::string> &loadedClassNames, std::string_view shortName)
        {
            if (shortName.empty())
            {
                return {};
            }

            std::vector<std::string> suffixMatches;
            suffixMatches.reserve(loadedClassNames.size());
            for (const auto &className : loadedClassNames)
            {
                if (className == shortName)
                {
                    return className;
                }

                if (className.size() > shortName.size() &&
                    className.compare(className.size() - shortName.size(), shortName.size(), shortName) == 0 &&
                    className[className.size() - shortName.size() - 1] == '.')
                {
                    suffixMatches.push_back(className);
                }
            }

            return suffixMatches.size() == 1 ? suffixMatches.front() : std::string{};
        }

        std::string ResolveScriptClassName(const std::filesystem::path &filePath, const std::vector<std::string> &loadedClassNames)
        {
            const auto parsedClassName = ParseScriptClassNameFromFile(filePath);
            if (parsedClassName.has_value())
            {
                if (std::find(loadedClassNames.begin(), loadedClassNames.end(), *parsedClassName) != loadedClassNames.end())
                {
                    return *parsedClassName;
                }

                const auto shortName = filePath.stem().string();
                const auto loadedClassName = FindLoadedClassForShortName(loadedClassNames, shortName);
                if (!loadedClassName.empty())
                {
                    return loadedClassName;
                }

                return *parsedClassName;
            }

            const auto shortName = filePath.stem().string();
            const auto loadedClassName = FindLoadedClassForShortName(loadedClassNames, shortName);
            if (!loadedClassName.empty())
            {
                return loadedClassName;
            }

            return shortName;
        }

        std::vector<ScriptAssetOption> CollectProjectScriptAssetOptions(const assets::Project &project,
                                                                        const std::vector<std::string> &loadedClassNames)
        {
            std::vector<ScriptAssetOption> options;
            for (const auto &assetEntry : project.GetManifest().assetEntries)
            {
                if (!EndsWithInsensitive(assetEntry.reference, ".cs"))
                {
                    continue;
                }

                ScriptAssetOption option;
                option.reference = assetEntry.reference;
                option.displayName = assetEntry.reference;
                if (StartsWith(option.displayName, assets::Project::kProjectAssetScheme))
                {
                    option.displayName.erase(0, assets::Project::kProjectAssetScheme.size());
                }

                const auto assetPath = project.ResolveAssetReference(assetEntry.reference);
                option.className = ResolveScriptClassName(assetPath, loadedClassNames);
                option.classLoaded = !option.className.empty() &&
                                     std::find(loadedClassNames.begin(), loadedClassNames.end(), option.className) != loadedClassNames.end();
                options.push_back(std::move(option));
            }

            std::sort(options.begin(), options.end(),
                      [](const ScriptAssetOption &left, const ScriptAssetOption &right)
                      {
                          return left.displayName < right.displayName;
                      });
            return options;
        }

        std::vector<AssetReferenceOption> CollectAssetReferenceOptions(const assets::Project *project, assets::ProjectAssetType type)
        {
            std::vector<AssetReferenceOption> options;
            if (!project)
            {
                for (const auto &reference : assets::Project::GetBuiltinAssetReferences())
                {
                    if (assets::Project::GetAssetTypeForReference(reference) != type)
                    {
                        continue;
                    }

                    options.push_back(AssetReferenceOption{.reference = reference, .displayName = reference});
                }
                return options;
            }

            for (const auto &assetEntry : project->GetManifest().assetEntries)
            {
                if (assetEntry.type != type)
                {
                    continue;
                }

                std::string displayName = assetEntry.reference;
                if (StartsWith(displayName, assets::Project::kProjectAssetScheme))
                {
                    displayName.erase(0, assets::Project::kProjectAssetScheme.size());
                }
                else if (StartsWith(displayName, assets::Project::kEngineAssetScheme))
                {
                    displayName.erase(0, assets::Project::kEngineAssetScheme.size());
                }

                options.push_back(AssetReferenceOption{.reference = assetEntry.reference, .displayName = std::move(displayName)});
            }

            std::sort(options.begin(), options.end(),
                      [](const AssetReferenceOption &left, const AssetReferenceOption &right)
                      {
                          return left.displayName < right.displayName;
                      });
            return options;
        }

        const ScriptAssetOption *FindScriptAssetOptionForClassName(const std::vector<ScriptAssetOption> &options,
                                                                   std::string_view className)
        {
            for (const auto &option : options)
            {
                if (option.className == className)
                {
                    return &option;
                }
            }

            return nullptr;
        }

        std::string SanitizeScriptIdentifier(std::string_view text)
        {
            std::string identifier;
            identifier.reserve(text.size());

            for (const char rawCharacter : text)
            {
                const unsigned char character = static_cast<unsigned char>(rawCharacter);
                if (std::isalnum(character) != 0 || rawCharacter == '_')
                {
                    if (identifier.empty() && std::isdigit(character) != 0)
                    {
                        identifier.push_back('_');
                    }

                    identifier.push_back(rawCharacter);
                    continue;
                }

                if (!identifier.empty() && identifier.back() != '_')
                {
                    identifier.push_back('_');
                }
            }

            while (!identifier.empty() && identifier.back() == '_')
            {
                identifier.pop_back();
            }

            return identifier;
        }

        glm::vec3 ParseVec3Property(const std::string &value)
        {
            glm::vec3 parsedValue{0.0f};
            sscanf_s(value.c_str(), "%f,%f,%f", &parsedValue.x, &parsedValue.y, &parsedValue.z);
            return parsedValue;
        }

        std::array<char, kInspectorPathBufferSize> &GetLightmapPathBuffer(const scene::Entity &entity, uint32_t materialSlot)
        {
            static std::unordered_map<std::string, std::array<char, kInspectorPathBufferSize>> lightmapPathBuffers;
            const std::string key = std::to_string(entity.GetID()) + ":" + std::to_string(materialSlot);
            return lightmapPathBuffers[key];
        }

        const char *GetComponentDisplayName(const scene::Component &component)
        {
            if (dynamic_cast<const scene::MeshComponent *>(&component))
            {
                return "Mesh Component";
            }
            if (dynamic_cast<const scene::AnimationComponent *>(&component))
            {
                return "Animation Component";
            }
            if (dynamic_cast<const scene::CameraComponent *>(&component))
            {
                return "Camera Component";
            }
            if (dynamic_cast<const scene::LightComponent *>(&component))
            {
                return "Light Component";
            }
            if (dynamic_cast<const scene::RigidbodyComponent *>(&component))
            {
                return "Rigidbody Component";
            }
            if (dynamic_cast<const scene::ColliderComponent *>(&component))
            {
                return "Collider Component";
            }
            if (dynamic_cast<const scene::IblCaptureComponent *>(&component))
            {
                return "IBL Capture Component";
            }
            if (dynamic_cast<const scene::ScriptComponent *>(&component))
            {
                return "Script Component";
            }
            if (dynamic_cast<const scene::CanvasComponent *>(&component))
            {
                return "Canvas Component";
            }
            if (dynamic_cast<const scene::RectTransformComponent *>(&component))
            {
                return "Rect Transform Component";
            }
            if (dynamic_cast<const scene::UIImageComponent *>(&component))
            {
                return "UI Image Component";
            }
            if (dynamic_cast<const scene::UITextComponent *>(&component))
            {
                return "UI Text Component";
            }
            if (dynamic_cast<const scene::UIButtonComponent *>(&component))
            {
                return "UI Button Component";
            }

            return typeid(component).name();
        }

        std::string GetComponentPrefabTypeName(const scene::Component &component)
        {
            if (dynamic_cast<const scene::MeshComponent *>(&component))
                return "MeshComponent";
            if (dynamic_cast<const scene::AnimationComponent *>(&component))
                return "AnimationComponent";
            if (dynamic_cast<const scene::CameraComponent *>(&component))
                return "CameraComponent";
            if (dynamic_cast<const scene::LightComponent *>(&component))
                return "LightComponent";
            if (dynamic_cast<const scene::RigidbodyComponent *>(&component))
                return "RigidbodyComponent";
            if (dynamic_cast<const scene::ColliderComponent *>(&component))
                return "ColliderComponent";
            if (dynamic_cast<const scene::IblCaptureComponent *>(&component))
                return "IblCaptureComponent";
            if (dynamic_cast<const scene::ScriptComponent *>(&component))
                return "ScriptComponent";
            if (dynamic_cast<const scene::CanvasComponent *>(&component))
                return "CanvasComponent";
            if (dynamic_cast<const scene::RectTransformComponent *>(&component))
                return "RectTransformComponent";
            if (dynamic_cast<const scene::UIImageComponent *>(&component))
                return "UIImageComponent";
            if (dynamic_cast<const scene::UITextComponent *>(&component))
                return "UITextComponent";
            if (dynamic_cast<const scene::UIButtonComponent *>(&component))
                return "UIButtonComponent";
            return {};
        }

        void CollectEntitiesRecursive(scene::Entity *entity, std::vector<scene::Entity *> &entities)
        {
            if (!entity)
            {
                return;
            }

            entities.push_back(entity);
            for (auto *child : entity->GetChildren())
            {
                CollectEntitiesRecursive(child, entities);
            }
        }

        bool SceneHasAnyCamera(const scene::Scene *scene)
        {
            if (!scene)
            {
                return false;
            }

            std::vector<scene::Entity *> entities;
            for (auto *rootEntity : scene->GetRootEntities())
            {
                CollectEntitiesRecursive(rootEntity, entities);
            }

            for (auto *candidate : entities)
            {
                if (candidate && candidate->GetComponent<scene::CameraComponent>())
                {
                    return true;
                }
            }

            return false;
        }

        void SetSceneMainCamera(scene::Scene *scene, scene::CameraComponent *selectedCamera, bool isMainCamera)
        {
            if (!selectedCamera)
            {
                return;
            }

            if (!scene || !isMainCamera)
            {
                selectedCamera->SetMainCamera(isMainCamera);
                return;
            }

            std::vector<scene::Entity *> entities;
            for (auto *rootEntity : scene->GetRootEntities())
            {
                CollectEntitiesRecursive(rootEntity, entities);
            }

            for (auto *entity : entities)
            {
                if (!entity)
                {
                    continue;
                }

                if (auto *cameraComponent = entity->GetComponent<scene::CameraComponent>())
                {
                    cameraComponent->SetMainCamera(cameraComponent == selectedCamera);
                }
            }
        }

        bool CanAddComponentType(const scene::Entity &entity, AddableComponentType componentType)
        {
            switch (componentType)
            {
            case AddableComponentType::Mesh:
                return !entity.HasComponent<scene::MeshComponent>();
            case AddableComponentType::Animation:
                return !entity.HasComponent<scene::AnimationComponent>();
            case AddableComponentType::Camera:
                return !entity.HasComponent<scene::CameraComponent>();
            case AddableComponentType::Light:
                return !entity.HasComponent<scene::LightComponent>();
            case AddableComponentType::Rigidbody:
                return !entity.HasComponent<scene::RigidbodyComponent>();
            case AddableComponentType::Collider:
                return !entity.HasComponent<scene::ColliderComponent>();
            case AddableComponentType::IblCapture:
                return !entity.HasComponent<scene::IblCaptureComponent>();
            case AddableComponentType::Script:
                return !entity.HasComponent<scene::ScriptComponent>();
            case AddableComponentType::Canvas:
                return !entity.HasComponent<scene::CanvasComponent>();
            case AddableComponentType::RectTransform:
                return !entity.HasComponent<scene::RectTransformComponent>();
            case AddableComponentType::UIImage:
                return !entity.HasComponent<scene::UIImageComponent>();
            case AddableComponentType::UIText:
                return !entity.HasComponent<scene::UITextComponent>();
            case AddableComponentType::UIButton:
                return !entity.HasComponent<scene::UIButtonComponent>();
            default:
                return false;
            }
        }

        void AddComponentToEntity(scene::Entity &entity, AddableComponentType componentType)
        {
            auto &engine = core::Engine::GetInstance();
            switch (componentType)
            {
            case AddableComponentType::Mesh:
            {
                auto *meshComponent = entity.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{
                    .mesh = nullptr,
                    .material = engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
                });
                if (meshComponent)
                {
                    meshComponent->SetMaterialAssetForMaterialSlot(0, std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                }
                break;
            }
            case AddableComponentType::Camera:
            {
                const bool sceneAlreadyHasCamera = SceneHasAnyCamera(entity.GetScene());
                auto *cameraComponent = entity.CreateComponent<scene::CameraComponent>(new render::Camera(render::CameraConfig{
                    .fovY = 60.0f,
                    .nearPlane = 0.1f,
                    .farPlane = 100.0f,
                }));
                if (cameraComponent)
                {
                    cameraComponent->SetMainCamera(!sceneAlreadyHasCamera);
                }
                break;
            }
            case AddableComponentType::Animation:
                entity.CreateComponent<scene::AnimationComponent>();
                break;
            case AddableComponentType::Light:
                entity.CreateComponent<scene::LightComponent>();
                break;
            case AddableComponentType::Rigidbody:
                entity.CreateComponent<scene::RigidbodyComponent>();
                break;
            case AddableComponentType::Collider:
                entity.CreateComponent<scene::ColliderComponent>();
                break;
            case AddableComponentType::IblCapture:
                entity.CreateComponent<scene::IblCaptureComponent>();
                break;
            case AddableComponentType::Script:
                entity.CreateComponent<scene::ScriptComponent>(scene::ScriptComponentConfig{});
                break;
            case AddableComponentType::Canvas:
                entity.CreateComponent<scene::CanvasComponent>();
                break;
            case AddableComponentType::RectTransform:
                entity.CreateComponent<scene::RectTransformComponent>();
                break;
            case AddableComponentType::UIImage:
                entity.CreateComponent<scene::UIImageComponent>();
                break;
            case AddableComponentType::UIText:
                entity.CreateComponent<scene::UITextComponent>();
                break;
            case AddableComponentType::UIButton:
                entity.CreateComponent<scene::UIButtonComponent>();
                break;
            default:
                break;
            }
        }
    }

    void InspectorPanel::Initialize()
    {
        // Initialization code for the InspectorPanel
    }

    void InspectorPanel::RenderSceneEnvironmentInspector(scene::Scene &scene) const
    {
        auto &engine = core::Engine::GetInstance();
        auto &editorShell = EditorShell::GetInstance();
        static std::array<char, kInspectorPathBufferSize> environmentPathBuffer{};
        static std::string cachedEnvironmentPath;

        if (cachedEnvironmentPath != scene.GetEnvironmentMapPath())
        {
            cachedEnvironmentPath = scene.GetEnvironmentMapPath();
            std::fill(environmentPathBuffer.begin(), environmentPathBuffer.end(), '\0');
            strncpy_s(environmentPathBuffer.data(), environmentPathBuffer.size(), cachedEnvironmentPath.c_str(), _TRUNCATE);
        }

        ImGui::TextUnformatted("Scene Settings");
        if (!ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::InputText("HDRI Path", environmentPathBuffer.data(), environmentPathBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("...##Environment"))
        {
#ifdef _WIN32
            OPENFILENAMEA ofn = {};
            char fileName[MAX_PATH] = "";
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = nullptr;
            ofn.lpstrFilter = "Environment Maps\0*.hdr;*.pfm;*.png;*.jpg;*.jpeg;*.tga;*.bmp\0All Files\0*.*\0";
            ofn.lpstrFile = fileName;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn))
            {
                strncpy_s(environmentPathBuffer.data(), environmentPathBuffer.size(), fileName, _TRUNCATE);
            }
#endif
        }

        ImGui::BeginDisabled(std::strlen(environmentPathBuffer.data()) == 0);
        if (ImGui::Button("Load HDRI"))
        {
            const std::string selectedPath(environmentPathBuffer.data());
            auto *environmentTexture = engine.GetTextureManager().LoadEnvironmentTextureFromFile(selectedPath.c_str());
            scene.SetEnvironmentMap(environmentTexture, selectedPath);
            cachedEnvironmentPath = scene.GetEnvironmentMapPath();
            editorShell.MarkSceneDirty();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Clear HDRI"))
        {
            scene.ClearEnvironmentMap();
            cachedEnvironmentPath.clear();
            std::fill(environmentPathBuffer.begin(), environmentPathBuffer.end(), '\0');
            editorShell.MarkSceneDirty();
        }

        float environmentIntensity = scene.GetEnvironmentIntensity();
        if (ImGui::DragFloat("Environment Intensity", &environmentIntensity, 0.01f, 0.0f, 32.0f))
        {
            scene.SetEnvironmentIntensity(environmentIntensity);
            editorShell.MarkSceneDirty();
        }

        ImGui::Text("Sky / IBL: %s", scene.HasEnvironmentMap() ? "loaded" : (scene.GetEnvironmentMapPath().empty() ? "not set" : "failed to load"));
    }

    bool InspectorPanel::RenderPropertyEditor(scene::Property &property) const
    {
        switch (property.type)
        {
        case scene::PropertyType::Float:
        {
            float value = std::stof(property.value);
            if (ImGui::DragFloat(property.name.c_str(), &value))
            {
                property.value = std::to_string(value);
                return true;
            }
            break;
        }
        case scene::PropertyType::Int:
        {
            int value = std::stoi(property.value);
            if (ImGui::DragInt(property.name.c_str(), &value))
            {
                property.value = std::to_string(value);
                return true;
            }
            break;
        }
        case scene::PropertyType::String:
        {
            char buffer[256];
            strncpy_s(buffer, sizeof(buffer), property.value.c_str(), _TRUNCATE);
            if (ImGui::InputText(property.name.c_str(), buffer, sizeof(buffer)))
            {
                property.value = std::string(buffer);
                return true;
            }
            break;
        }
        case scene::PropertyType::Vec3:
        {
            glm::vec3 value = ParseVec3Property(property.value);
            if (ImGui::DragFloat3(property.name.c_str(), &value.x))
            {
                property.value = std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
                return true;
            }
            break;
        }
        case scene::PropertyType::Vec2:
        {
            glm::vec2 value{0.0f, 0.0f};
            sscanf_s(property.value.c_str(), "%f,%f", &value.x, &value.y);
            if (ImGui::DragFloat2(property.name.c_str(), &value.x))
            {
                property.value = std::to_string(value.x) + "," + std::to_string(value.y);
                return true;
            }
            break;
        }
        case scene::PropertyType::Double:
        {
            double value = std::stod(property.value);
            if (ImGui::InputDouble(property.name.c_str(), &value))
            {
                property.value = std::to_string(value);
                return true;
            }
            break;
        }
        case scene::PropertyType::Bool:
        {
            bool value = (property.value == "true" || property.value == "True" || property.value == "1");
            if (ImGui::Checkbox(property.name.c_str(), &value))
            {
                property.value = value ? "true" : "false";
                return true;
            }
            break;
        }
        case scene::PropertyType::Enum:
        {
            int currentIndex = std::stoi(property.value);
            if (ImGui::BeginCombo(property.name.c_str(), property.enumOptions[currentIndex].c_str()))
            {
                for (size_t i = 0; i < property.enumOptions.size(); ++i)
                {
                    bool isSelected = (currentIndex == static_cast<int>(i));
                    if (ImGui::Selectable(property.enumOptions[i].c_str(), isSelected))
                    {
                        currentIndex = static_cast<int>(i);
                        property.value = std::to_string(currentIndex);
                        ImGui::EndCombo();
                        return true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        default:
            break;
        }

        return false;
    }

    bool InspectorPanel::RenderScriptComponentEditor(scene::ScriptComponent &scriptComponent, scene::Entity &entity) const
    {
        (void)entity;

        auto &editorShell = EditorShell::GetInstance();
        auto *project = editorShell.GetProject();
        auto &scriptEngine = core::Engine::GetInstance().GetScriptEngine();
        const auto classNames = scriptEngine.GetClassNames();
        const auto scriptAssetOptions = project != nullptr
                                            ? CollectProjectScriptAssetOptions(*project, classNames)
                                            : std::vector<ScriptAssetOption>{};
        const std::string currentSource = scriptComponent.GetSource();
        std::string previewValue = currentSource.empty() ? "<None>" : currentSource;
        if (const auto *selectedAsset = FindScriptAssetOptionForClassName(scriptAssetOptions, currentSource))
        {
            previewValue = selectedAsset->displayName;
        }

        bool changed = false;
        if (ImGui::BeginCombo("Source", previewValue.c_str()))
        {
            const bool isNoneSelected = currentSource.empty();
            if (ImGui::Selectable("<None>", isNoneSelected))
            {
                scriptComponent.SetSource({});
                changed = true;
            }
            if (isNoneSelected)
            {
                ImGui::SetItemDefaultFocus();
            }

            if (!scriptAssetOptions.empty())
            {
                ImGui::Separator();
                ImGui::TextDisabled("Scripts in Assets");
                for (const auto &option : scriptAssetOptions)
                {
                    const bool isSelected = option.className == currentSource;
                    std::string label = option.displayName;
                    if (!option.classLoaded)
                    {
                        label += " (not loaded)";
                    }
                    label += "##" + option.reference;

                    if (ImGui::Selectable(label.c_str(), isSelected))
                    {
                        scriptComponent.SetSource(option.className);
                        changed = true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }

            std::unordered_set<std::string> assetClassNames;
            assetClassNames.reserve(scriptAssetOptions.size());
            for (const auto &option : scriptAssetOptions)
            {
                assetClassNames.insert(option.className);
            }

            bool hasLooseLoadedClasses = false;
            for (const auto &className : classNames)
            {
                if (!assetClassNames.contains(className))
                {
                    hasLooseLoadedClasses = true;
                    break;
                }
            }

            if (hasLooseLoadedClasses)
            {
                ImGui::Separator();
                ImGui::TextDisabled("Other Loaded Classes");
                for (const auto &className : classNames)
                {
                    if (assetClassNames.contains(className))
                    {
                        continue;
                    }

                    const bool isSelected = className == currentSource;
                    if (ImGui::Selectable(className.c_str(), isSelected))
                    {
                        scriptComponent.SetSource(className);
                        changed = true;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }

            ImGui::EndCombo();
        }

        if (project)
        {
            const bool scriptAuthoringDisabled = editorShell.IsRuntimeExportProject();
            ImGui::SameLine();
            if (ImGui::Button("Refresh##ScriptSources"))
            {
                project->RefreshAssetRegistry();
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(scriptAuthoringDisabled);
            if (ImGui::Button("New##ScriptSource"))
            {
                ImGui::OpenPopup("Create Script");
            }
            ImGui::EndDisabled();

            static std::array<char, kNewScriptNameBufferSize> newScriptNameBuffer{};
            static std::string createScriptErrorMessage;
            if (ImGui::BeginPopupModal("Create Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::InputText("Name", newScriptNameBuffer.data(), newScriptNameBuffer.size());
                ImGui::TextDisabled("Creates Assets/Scripts/<Name>.cs");
                if (!createScriptErrorMessage.empty())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", createScriptErrorMessage.c_str());
                }

                if (ImGui::Button("Create"))
                {
                    std::string createdClassName;
                    if (editorShell.CreateScriptAsset(newScriptNameBuffer.data(), &createdClassName, &createScriptErrorMessage))
                    {
                        scriptComponent.SetSource(createdClassName);
                        changed = true;
                        editorShell.BuildProjectScripts();
                        std::fill(newScriptNameBuffer.begin(), newScriptNameBuffer.end(), '\0');
                        createScriptErrorMessage.clear();
                        ImGui::CloseCurrentPopup();
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    std::fill(newScriptNameBuffer.begin(), newScriptNameBuffer.end(), '\0');
                    createScriptErrorMessage.clear();
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }

            if (scriptAuthoringDisabled)
            {
                ImGui::TextDisabled("Script authoring is disabled for exported runtime bundles.");
            }
        }

        if (currentSource.empty() && scriptAssetOptions.empty() && classNames.empty())
        {
            ImGui::TextDisabled(project ? "No scripts were found in the asset directory or loaded assembly."
                                        : "Open a project to browse script assets.");
        }
        else if (!currentSource.empty() && !scriptEngine.HasClass(currentSource))
        {
            ImGui::TextDisabled("Selected script is not present in the loaded assembly yet. Build scripts to compile new or changed sources.");
        }
        else if (classNames.empty())
        {
            ImGui::TextDisabled("No script classes are loaded.");
        }

        int fieldIndex = 0;
        for (const auto &field : scriptComponent.GetSerializedFields())
        {
            auto fieldValue = scriptComponent.GetFieldValue(field.name);
            if (!fieldValue.has_value())
            {
                fieldValue = scripting::IsFieldValueCompatible(field.type, field.defaultValue)
                                 ? field.defaultValue
                                 : scripting::MakeDefaultFieldValue(field.type);
            }

            ImGui::PushID(fieldIndex++);
            switch (field.type)
            {
            case scripting::ScriptFieldType::Boolean:
            {
                bool value = std::get<bool>(*fieldValue);
                if (ImGui::Checkbox(field.name.c_str(), &value))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, value);
                }
                break;
            }
            case scripting::ScriptFieldType::Int32:
            {
                int value = std::get<int32_t>(*fieldValue);
                if (ImGui::DragInt(field.name.c_str(), &value))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, static_cast<int32_t>(value));
                }
                break;
            }
            case scripting::ScriptFieldType::Float:
            {
                float value = std::get<float>(*fieldValue);
                if (ImGui::DragFloat(field.name.c_str(), &value, 0.01f))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, value);
                }
                break;
            }
            case scripting::ScriptFieldType::Double:
            {
                double value = std::get<double>(*fieldValue);
                if (ImGui::InputDouble(field.name.c_str(), &value))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, value);
                }
                break;
            }
            case scripting::ScriptFieldType::String:
            {
                char buffer[256];
                const auto &value = std::get<std::string>(*fieldValue);
                strncpy_s(buffer, sizeof(buffer), value.c_str(), _TRUNCATE);
                if (ImGui::InputText(field.name.c_str(), buffer, sizeof(buffer)))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, std::string(buffer));
                }
                break;
            }
            case scripting::ScriptFieldType::Vector2:
            {
                auto value = std::get<glm::vec2>(*fieldValue);
                if (ImGui::DragFloat2(field.name.c_str(), &value.x, 0.01f))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, value);
                }
                break;
            }
            case scripting::ScriptFieldType::Vector3:
            {
                auto value = std::get<glm::vec3>(*fieldValue);
                if (ImGui::DragFloat3(field.name.c_str(), &value.x, 0.01f))
                {
                    changed |= scriptComponent.SetFieldValue(field.name, value);
                }
                break;
            }
            case scripting::ScriptFieldType::EntityId:
            case scripting::ScriptFieldType::GameObject:
            case scripting::ScriptFieldType::MeshComponent:
            case scripting::ScriptFieldType::CameraComponent:
            case scripting::ScriptFieldType::LightComponent:
            case scripting::ScriptFieldType::RigidbodyComponent:
            case scripting::ScriptFieldType::ColliderComponent:
            case scripting::ScriptFieldType::AnimationComponent:
            case scripting::ScriptFieldType::CanvasComponent:
            case scripting::ScriptFieldType::RectTransformComponent:
            case scripting::ScriptFieldType::UIImageComponent:
            case scripting::ScriptFieldType::UITextComponent:
            case scripting::ScriptFieldType::UIButtonComponent:
            {
                uint32_t selectedEntityId = std::get<uint32_t>(*fieldValue);
                scene::Scene *scene = entity.GetScene();
                std::vector<scene::Entity *> entities;
                if (scene)
                {
                    for (auto *rootEntity : scene->GetRootEntities())
                    {
                        CollectEntitiesRecursive(rootEntity, entities);
                    }
                }

                auto isCompatibleEntity = [field](const scene::Entity &candidate) -> bool
                {
                    switch (field.type)
                    {
                    case scripting::ScriptFieldType::MeshComponent:
                        return candidate.HasComponent<scene::MeshComponent>();
                    case scripting::ScriptFieldType::CameraComponent:
                        return candidate.HasComponent<scene::CameraComponent>();
                    case scripting::ScriptFieldType::LightComponent:
                        return candidate.HasComponent<scene::LightComponent>();
                    case scripting::ScriptFieldType::RigidbodyComponent:
                        return candidate.HasComponent<scene::RigidbodyComponent>();
                    case scripting::ScriptFieldType::ColliderComponent:
                        return candidate.HasComponent<scene::ColliderComponent>();
                    case scripting::ScriptFieldType::AnimationComponent:
                        return candidate.HasComponent<scene::AnimationComponent>();
                    case scripting::ScriptFieldType::CanvasComponent:
                        return candidate.HasComponent<scene::CanvasComponent>();
                    case scripting::ScriptFieldType::RectTransformComponent:
                        return candidate.HasComponent<scene::RectTransformComponent>();
                    case scripting::ScriptFieldType::UIImageComponent:
                        return candidate.HasComponent<scene::UIImageComponent>();
                    case scripting::ScriptFieldType::UITextComponent:
                        return candidate.HasComponent<scene::UITextComponent>();
                    case scripting::ScriptFieldType::UIButtonComponent:
                        return candidate.HasComponent<scene::UIButtonComponent>();
                    case scripting::ScriptFieldType::EntityId:
                    case scripting::ScriptFieldType::GameObject:
                    default:
                        return true;
                    }
                };

                std::string previewLabel = "<None>";
                if (scene)
                {
                    if (auto *selectedEntity = scene->FindEntityByID(selectedEntityId))
                    {
                        previewLabel = selectedEntity->GetName().empty()
                                           ? ("Entity " + std::to_string(selectedEntityId))
                                           : selectedEntity->GetName();
                    }
                }

                if (ImGui::BeginCombo(field.name.c_str(), previewLabel.c_str()))
                {
                    const bool isNoneSelected = selectedEntityId == 0;
                    if (ImGui::Selectable("<None>", isNoneSelected))
                    {
                        changed |= scriptComponent.SetFieldValue(field.name, uint32_t{0});
                    }
                    if (isNoneSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }

                    for (auto *candidate : entities)
                    {
                        if (!candidate || !isCompatibleEntity(*candidate))
                        {
                            continue;
                        }

                        const bool isSelected = candidate->GetID() == selectedEntityId;
                        std::string label = candidate->GetName().empty()
                                                ? ("Entity " + std::to_string(candidate->GetID()))
                                                : (candidate->GetName() + "##" + std::to_string(candidate->GetID()));
                        if (ImGui::Selectable(label.c_str(), isSelected))
                        {
                            changed |= scriptComponent.SetFieldValue(field.name, candidate->GetID());
                        }
                        if (isSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }

                    ImGui::EndCombo();
                }
                break;
            }
            case scripting::ScriptFieldType::None:
            default:
                ImGui::TextDisabled("Unsupported field: %s", field.name.c_str());
                break;
            }
            ImGui::PopID();
        }

        if (!currentSource.empty() && scriptComponent.GetSerializedFields().empty())
        {
            ImGui::TextDisabled("Selected script exposes no serialized fields.");
        }

        return changed;
    }

    void InspectorPanel::RenderCameraPostProcessEditor(scene::CameraComponent &cameraComponent) const
    {
        if (!ImGui::TreeNode("Post Processing"))
        {
            return;
        }

        const auto &registeredTypes = render::GetRegisteredPostProcessEffectTypes();
        static int selectedEffectTypeIndex = 0;
        if (!registeredTypes.empty())
        {
            selectedEffectTypeIndex = std::clamp(selectedEffectTypeIndex, 0, static_cast<int>(registeredTypes.size()) - 1);
            if (ImGui::BeginCombo("Add Effect", registeredTypes[selectedEffectTypeIndex].c_str()))
            {
                for (int index = 0; index < static_cast<int>(registeredTypes.size()); ++index)
                {
                    const bool isSelected = (selectedEffectTypeIndex == index);
                    if (ImGui::Selectable(registeredTypes[index].c_str(), isSelected))
                    {
                        selectedEffectTypeIndex = index;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (ImGui::Button("Add") && selectedEffectTypeIndex >= 0 && selectedEffectTypeIndex < static_cast<int>(registeredTypes.size()))
            {
                cameraComponent.AddPostProcessEffectByType(registeredTypes[selectedEffectTypeIndex]);
            }
        }

        for (size_t effectIndex = 0; effectIndex < cameraComponent.GetPostProcessEffects().size(); ++effectIndex)
        {
            auto *effect = cameraComponent.GetPostProcessEffect(effectIndex);
            if (!effect)
            {
                continue;
            }

            ImGui::PushID(static_cast<int>(effectIndex));
            if (ImGui::TreeNode(effect->GetDisplayName().c_str()))
            {
                bool isEnabled = effect->IsEnabled();
                if (ImGui::Checkbox("Enabled", &isEnabled))
                {
                    effect->SetEnabled(isEnabled);
                }

                if (ImGui::Button("Up") && effectIndex > 0)
                {
                    cameraComponent.MovePostProcessEffect(effectIndex, effectIndex - 1);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::Button("Down") && effectIndex + 1 < cameraComponent.GetPostProcessEffects().size())
                {
                    cameraComponent.MovePostProcessEffect(effectIndex, effectIndex + 1);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove"))
                {
                    cameraComponent.RemovePostProcessEffect(effectIndex);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }

                auto parameters = effect->GetParameters();
                bool parametersChanged = false;
                for (auto &parameter : parameters)
                {
                    scene::Property property{
                        .name = parameter.name,
                        .type = scene::PropertyType::String,
                        .value = parameter.value,
                        .enumOptions = parameter.enumOptions,
                    };

                    switch (parameter.type)
                    {
                    case render::PostProcessParameterType::Float:
                        property.type = scene::PropertyType::Float;
                        break;
                    case render::PostProcessParameterType::Int:
                        property.type = scene::PropertyType::Int;
                        break;
                    case render::PostProcessParameterType::Bool:
                        property.type = scene::PropertyType::Bool;
                        break;
                    case render::PostProcessParameterType::Enum:
                        property.type = scene::PropertyType::Enum;
                        break;
                    case render::PostProcessParameterType::String:
                    default:
                        property.type = scene::PropertyType::String;
                        break;
                    }

                    parametersChanged |= RenderPropertyEditor(property);
                    parameter.value = property.value;
                    parameter.enumOptions = property.enumOptions;
                }

                if (parametersChanged)
                {
                    effect->SetParameters(parameters);
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        ImGui::TreePop();
    }

    void InspectorPanel::RenderEditorCameraInspector(EditorShell::EditorViewportCamera &camera) const
    {
        ImGui::TextUnformatted("Editor Camera");

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat3("Position", &camera.position.x, 0.05f);
            ImGui::DragFloat("Yaw", &camera.yawDegrees, 0.1f);
            ImGui::DragFloat("Pitch", &camera.pitchDegrees, 0.1f, -89.0f, 89.0f);
        }

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float fov = camera.camera.GetFOV();
            if (ImGui::DragFloat("FOV", &fov, 0.1f, 1.0f, 179.0f))
            {
                camera.camera.SetFOV(fov);
            }

            float nearPlane = camera.camera.GetNearPlane();
            if (ImGui::DragFloat("Near Plane", &nearPlane, 0.01f, 0.001f, camera.camera.GetFarPlane() - 0.001f))
            {
                camera.camera.SetNearPlane(nearPlane);
            }

            float farPlane = camera.camera.GetFarPlane();
            const float minFarPlane = camera.camera.GetNearPlane() + 0.001f;
            if (ImGui::DragFloat("Far Plane", &farPlane, 0.1f, minFarPlane, 10000.0f))
            {
                camera.camera.SetFarPlane(farPlane);
            }
        }

        RenderEditorCameraPostProcessEditor(camera);
    }

    void InspectorPanel::RenderEditorCameraPostProcessEditor(EditorShell::EditorViewportCamera &camera) const
    {
        if (!ImGui::TreeNode("Post Processing"))
        {
            return;
        }

        const auto &registeredTypes = render::GetRegisteredPostProcessEffectTypes();
        static int selectedEffectTypeIndex = 0;
        if (!registeredTypes.empty())
        {
            selectedEffectTypeIndex = std::clamp(selectedEffectTypeIndex, 0, static_cast<int>(registeredTypes.size()) - 1);
            if (ImGui::BeginCombo("Add Effect", registeredTypes[selectedEffectTypeIndex].c_str()))
            {
                for (int index = 0; index < static_cast<int>(registeredTypes.size()); ++index)
                {
                    const bool isSelected = (selectedEffectTypeIndex == index);
                    if (ImGui::Selectable(registeredTypes[index].c_str(), isSelected))
                    {
                        selectedEffectTypeIndex = index;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (ImGui::Button("Add") && selectedEffectTypeIndex >= 0 && selectedEffectTypeIndex < static_cast<int>(registeredTypes.size()))
            {
                camera.AddPostProcessEffectByType(registeredTypes[selectedEffectTypeIndex]);
            }
        }

        for (size_t effectIndex = 0; effectIndex < camera.GetPostProcessEffects().size(); ++effectIndex)
        {
            auto *effect = camera.GetPostProcessEffect(effectIndex);
            if (!effect)
            {
                continue;
            }

            ImGui::PushID(static_cast<int>(effectIndex));
            if (ImGui::TreeNode(effect->GetDisplayName().c_str()))
            {
                bool isEnabled = effect->IsEnabled();
                if (ImGui::Checkbox("Enabled", &isEnabled))
                {
                    effect->SetEnabled(isEnabled);
                }

                if (ImGui::Button("Up") && effectIndex > 0)
                {
                    camera.MovePostProcessEffect(effectIndex, effectIndex - 1);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::Button("Down") && effectIndex + 1 < camera.GetPostProcessEffects().size())
                {
                    camera.MovePostProcessEffect(effectIndex, effectIndex + 1);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove"))
                {
                    camera.RemovePostProcessEffect(effectIndex);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }

                auto parameters = effect->GetParameters();
                bool parametersChanged = false;
                for (auto &parameter : parameters)
                {
                    scene::Property property{
                        .name = parameter.name,
                        .type = scene::PropertyType::String,
                        .value = parameter.value,
                        .enumOptions = parameter.enumOptions,
                    };

                    switch (parameter.type)
                    {
                    case render::PostProcessParameterType::Float:
                        property.type = scene::PropertyType::Float;
                        break;
                    case render::PostProcessParameterType::Int:
                        property.type = scene::PropertyType::Int;
                        break;
                    case render::PostProcessParameterType::Bool:
                        property.type = scene::PropertyType::Bool;
                        break;
                    case render::PostProcessParameterType::Enum:
                        property.type = scene::PropertyType::Enum;
                        break;
                    case render::PostProcessParameterType::String:
                    default:
                        property.type = scene::PropertyType::String;
                        break;
                    }

                    parametersChanged |= RenderPropertyEditor(property);
                    parameter.value = property.value;
                    parameter.enumOptions = property.enumOptions;
                }

                if (parametersChanged)
                {
                    effect->SetParameters(parameters);
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        ImGui::TreePop();
    }

    void InspectorPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        if (editorShell.IsEditorCameraSelected())
        {
            RenderEditorCameraInspector(editorShell.GetEditorCamera());
            return;
        }

        auto entity = editorShell.GetSelectedEntity();
        if (!entity)
        {
            auto *scene = core::Engine::GetInstance().GetScene();
            if (!scene)
            {
                ImGui::Text("No entity selected.");
                return;
            }

            RenderSceneEnvironmentInspector(*scene);
            return;
        }

        ImGui::Text("Entity Name: %s", entity->GetName().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("ID: %u", entity->GetID());
        auto isActive = entity->IsSelfActive();
        if (ImGui::Checkbox("Active", &isActive))
        {
            entity->SetActive(isActive);
            entity->AddPrefabOverride("Active");
            editorShell.MarkSceneDirty();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Hierarchy: %s", entity->IsActive() ? "Active" : "Inactive");

        {
            static std::unordered_map<scene::EntityID, std::array<char, 256>> tagBuffers;
            static std::unordered_map<scene::EntityID, std::string> cachedTagText;

            const auto entityId = entity->GetID();
            const std::string currentTagText = JoinTags(entity->GetTags());
            auto &cachedText = cachedTagText[entityId];
            auto &tagBuffer = tagBuffers[entityId];
            if (cachedText != currentTagText)
            {
                cachedText = currentTagText;
                std::fill(tagBuffer.begin(), tagBuffer.end(), '\0');
                strncpy_s(tagBuffer.data(), tagBuffer.size(), cachedText.c_str(), _TRUNCATE);
            }

            if (ImGui::InputText("Tags", tagBuffer.data(), tagBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue) ||
                ImGui::IsItemDeactivatedAfterEdit())
            {
                const std::string editedText(tagBuffer.data());
                if (editedText != currentTagText)
                {
                    entity->SetTags(ParseTags(editedText));
                    cachedText = JoinTags(entity->GetTags());
                    std::fill(tagBuffer.begin(), tagBuffer.end(), '\0');
                    strncpy_s(tagBuffer.data(), tagBuffer.size(), cachedText.c_str(), _TRUNCATE);
                    entity->AddPrefabOverride("Tags");
                    editorShell.MarkSceneDirty();
                }
            }
        }

        if (!entity->GetPrefabSource().empty())
        {
            ImGui::Separator();
            ImGui::Text("Prefab: %s", entity->GetPrefabSource().c_str());
            ImGui::TextDisabled(entity->IsPrefabInstanceRoot() ? "Instance Root" : "Nested Prefab Entity");
            ImGui::TextDisabled("Overrides: %zu", entity->GetPrefabOverrides().size());
            ImGui::BeginDisabled(!entity->IsPrefabInstanceRoot());
            if (ImGui::Button("Update From Prefab"))
            {
                std::string errorMessage;
                if (scene::Prefab::UpdateInstance(*entity, &errorMessage))
                {
                    editorShell.MarkSceneDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Updated prefab instance.");
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error,
                                    errorMessage.empty() ? "Failed to update prefab instance." : errorMessage);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply To Prefab"))
            {
                std::string errorMessage;
                if (scene::Prefab::ApplyInstanceToPrefab(*entity, &errorMessage))
                {
                    if (auto *project = editorShell.GetProject())
                    {
                        project->RefreshAssetRegistry();
                    }
                    if (auto *currentScene = core::Engine::GetInstance().GetScene())
                    {
                        scene::Prefab::UpdateInstances(*currentScene, entity->GetPrefabSource());
                    }
                    entity->ClearPrefabOverridesRecursive();
                    editorShell.MarkSceneDirty();
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Applied instance overrides to prefab.");
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error,
                                    errorMessage.empty() ? "Failed to apply prefab." : errorMessage);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Unpack"))
            {
                entity->ClearPrefabLinkRecursive();
                editorShell.MarkSceneDirty();
            }
            ImGui::EndDisabled();
        }

        {
            // Transform
            auto position = entity->GetPosition();
            auto rotation = entity->GetRotation();
            auto scale = entity->GetScale();
            const auto originalPosition = position;
            const auto originalRotation = rotation;
            const auto originalScale = scale;

            if (ImGui::CollapsingHeader("Transform"))
            {
                bool transformChanged = false;
                transformChanged |= ImGui::DragFloat3("Position", &position.x, 0.01f);
                transformChanged |= ImGui::DragFloat3("Rotation", &rotation.x, 0.1f);
                transformChanged |= ImGui::DragFloat3("Scale", &scale.x, 0.01f);

                entity->SetPosition(position);
                entity->SetRotation(rotation);
                entity->SetScale(scale);
                if (transformChanged)
                {
                    if (position != originalPosition)
                    {
                        entity->AddPrefabOverride("Transform.Position");
                    }
                    if (rotation != originalRotation)
                    {
                        entity->AddPrefabOverride("Transform.Rotation");
                    }
                    if (scale != originalScale)
                    {
                        entity->AddPrefabOverride("Transform.Scale");
                    }
                    editorShell.MarkSceneDirty();
                }
            }

            // Mesh Import UI (if entity has MeshComponent)
            if (auto *meshComponent = entity->GetComponent<PlutoGE::scene::MeshComponent>())
            {
                auto &engine = PlutoGE::core::Engine::GetInstance();
                const auto meshImportStatus = engine.GetMeshImportStatus(entity->GetID());

                ImGui::Separator();
                ImGui::Text("Mesh Asset");
                const auto meshAssetOptions = CollectAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::Mesh);
                std::string meshPreview = meshComponent->GetSourceMeshPath().empty() ? "None" : meshComponent->GetSourceMeshPath();
                for (const auto &option : meshAssetOptions)
                {
                    if (option.reference == meshComponent->GetSourceMeshPath())
                    {
                        meshPreview = option.displayName;
                        break;
                    }
                }

                if (ImGui::BeginCombo("Source Mesh", meshPreview.c_str()))
                {
                    for (const auto &option : meshAssetOptions)
                    {
                        const bool selected = option.reference == meshComponent->GetSourceMeshPath();
                        if (ImGui::Selectable(option.displayName.c_str(), selected))
                        {
                            if (assets::Project::IsEngineAssetReference(option.reference))
                            {
                                if (auto *mesh = engine.GetAssetManager().LoadMeshAsset(option.reference))
                                {
                                    meshComponent->SetMesh(mesh);
                                    meshComponent->SetSourceMeshPath(option.reference);
                                    if (!meshComponent->GetMaterialForMaterialSlot(0))
                                    {
                                        meshComponent->SetMaterialForMaterialSlot(
                                            0,
                                            engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)));
                                        meshComponent->SetMaterialAssetForMaterialSlot(0, std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                                    }
                                    meshComponent->CreateSubmeshChildEntities();
                                    editorShell.MarkSceneDirty();
                                }
                            }
                            else
                            {
                                const std::string resolvedPath = engine.GetAssetManager().ResolveMeshAssetSourcePath(option.reference);
                                auto importedMeshAsset = engine.ImportMeshAsset(resolvedPath);
                                if (importedMeshAsset.mesh)
                                {
                                    meshComponent->SetMesh(importedMeshAsset.mesh);
                                    meshComponent->SetMaterials(importedMeshAsset.materials);
                                    meshComponent->SetSourceMeshPath(option.reference);
                                    meshComponent->CreateSubmeshChildEntities();
                                    if (importedMeshAsset.animations && !importedMeshAsset.animations->empty())
                                    {
                                        auto *animationComponent = entity->GetComponent<scene::AnimationComponent>();
                                        if (!animationComponent)
                                        {
                                            animationComponent = entity->CreateComponent<scene::AnimationComponent>();
                                        }

                                        animationComponent->SetClipsFromImportedAnimations(*importedMeshAsset.animations);
                                        animationComponent->SetSourceAnimationPath(option.reference);
                                    }
                                    editorShell.MarkSceneDirty();
                                }
                            }
                        }

                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::Text("Mesh Import");
                static char meshPath[512] = "";
                ImGui::InputText("Mesh Path", meshPath, sizeof(meshPath));
                ImGui::SameLine();
                if (ImGui::Button("..."))
                {
#ifdef _WIN32
                    OPENFILENAMEA ofn = {};
                    char fileName[MAX_PATH] = "";
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = nullptr;
                    ofn.lpstrFilter = "glTF Files\0*.glb;*.gltf\0All Files\0*.*\0";
                    ofn.lpstrFile = fileName;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameA(&ofn))
                    {
                        strncpy_s(meshPath, sizeof(meshPath), fileName, _TRUNCATE);
                    }
#endif
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(meshImportStatus.pending || strlen(meshPath) == 0);
                if (ImGui::Button("Import Mesh"))
                {
                    engine.QueueMeshImport(entity->GetID(), meshPath);
                    editorShell.MarkSceneDirty();
                }
                ImGui::EndDisabled();

                if (meshImportStatus.pending)
                {
                    ImGui::TextUnformatted("Importing mesh on background thread...");
                }
                else if (!meshImportStatus.errorMessage.empty())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", meshImportStatus.errorMessage.c_str());
                }

                if (meshComponent->GetMesh() && meshComponent->GetMesh()->GetSubmeshCount() > 1)
                {
                    ImGui::Separator();
                    if (ImGui::CollapsingHeader("Submesh Materials"))
                    {
                        for (size_t submeshIndex = 0; submeshIndex < meshComponent->GetMesh()->GetSubmeshCount(); ++submeshIndex)
                        {
                            const auto &submesh = meshComponent->GetMesh()->GetSubmesh(submeshIndex);
                            auto *material = meshComponent->GetMaterialForSubmesh(submeshIndex);

                            ImGui::PushID(static_cast<int>(submeshIndex));
                            if (ImGui::TreeNode((std::string("Submesh ") + std::to_string(submeshIndex)).c_str()))
                            {
                                ImGui::Text("Material Slot: %u", submesh.materialIndex);
                                ImGui::Text("Indices: %u", submesh.indexCount);

                                if (material)
                                {
                                    const auto materialAssetOptions = CollectAssetReferenceOptions(editorShell.GetProject(), assets::ProjectAssetType::Material);
                                    std::string materialPreview = meshComponent->GetMaterialAssetForSubmesh(submeshIndex);
                                    if (materialPreview.empty())
                                    {
                                        materialPreview = meshComponent->GetMaterialAssetForMaterialSlot(submesh.materialIndex);
                                    }
                                    if (materialPreview.empty())
                                    {
                                        materialPreview = "Inline Override";
                                    }
                                    for (const auto &option : materialAssetOptions)
                                    {
                                        if (option.reference == materialPreview)
                                        {
                                            materialPreview = option.displayName;
                                            break;
                                        }
                                    }

                                    if (ImGui::BeginCombo("Material Asset", materialPreview.c_str()))
                                    {
                                        for (const auto &option : materialAssetOptions)
                                        {
                                            const bool selected = option.reference == meshComponent->GetMaterialAssetForSubmesh(submeshIndex) ||
                                                                  (meshComponent->GetMaterialAssetForSubmesh(submeshIndex).empty() &&
                                                                   option.reference == meshComponent->GetMaterialAssetForMaterialSlot(submesh.materialIndex));
                                            if (ImGui::Selectable(option.displayName.c_str(), selected))
                                            {
                                                if (auto *materialAsset = engine.GetAssetManager().LoadMaterialAsset(option.reference))
                                                {
                                                    meshComponent->SetMaterialForSubmesh(submeshIndex, materialAsset);
                                                    meshComponent->SetMaterialAssetForSubmesh(submeshIndex, option.reference);
                                                    editorShell.MarkSceneDirty();
                                                    material = materialAsset;
                                                }
                                            }

                                            if (selected)
                                            {
                                                ImGui::SetItemDefaultFocus();
                                            }
                                        }
                                        ImGui::EndCombo();
                                    }

                                    const bool materialUsesAssetReference =
                                        !meshComponent->GetMaterialAssetForSubmesh(submeshIndex).empty() ||
                                        !meshComponent->GetMaterialAssetForMaterialSlot(submesh.materialIndex).empty();
                                    if (materialUsesAssetReference)
                                    {
                                        ImGui::TextDisabled("Using shared material asset. Make it unique to edit inline values.");
                                        ImGui::BeginDisabled();
                                    }

                                    const auto &materialConfig = material->GetConfig();
                                    float color[4] = {
                                        materialConfig.color.r,
                                        materialConfig.color.g,
                                        materialConfig.color.b,
                                        materialConfig.color.a,
                                    };
                                    if (ImGui::ColorEdit4("Color", color))
                                    {
                                        material->SetColor(glm::vec4(color[0], color[1], color[2], color[3]));
                                        editorShell.MarkSceneDirty();
                                    }

                                    float metallic = materialConfig.metallic;
                                    if (ImGui::DragFloat("Metallic", &metallic, 0.01f, 0.0f, 1.0f))
                                    {
                                        material->SetMetallic(metallic);
                                        editorShell.MarkSceneDirty();
                                    }

                                    float roughness = materialConfig.roughness;
                                    if (ImGui::DragFloat("Roughness", &roughness, 0.01f, 0.04f, 1.0f))
                                    {
                                        material->SetRoughness(roughness);
                                        editorShell.MarkSceneDirty();
                                    }

                                    bool flipNormalY = materialConfig.flipNormalY;
                                    if (ImGui::Checkbox("Flip Normal Y", &flipNormalY))
                                    {
                                        material->SetFlipNormalY(flipNormalY);
                                        editorShell.MarkSceneDirty();
                                    }

                                    ImGui::Text("Textures: Albedo %s | Normal %s | Metallic/Roughness %s",
                                                materialConfig.albedoTexture ? "yes" : "no",
                                                materialConfig.normalTexture ? "yes" : "no",
                                                materialConfig.metallicTexture || materialConfig.roughnessTexture ? "yes" : "no");

                                    auto &lightmapPathBuffer = GetLightmapPathBuffer(*entity, static_cast<uint32_t>(submeshIndex));
                                    ImGui::InputText("Lightmap Path", lightmapPathBuffer.data(), lightmapPathBuffer.size());
                                    ImGui::SameLine();
                                    if (ImGui::Button("...##Lightmap"))
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
                                            strncpy_s(lightmapPathBuffer.data(), lightmapPathBuffer.size(), fileName, _TRUNCATE);
                                        }
#endif
                                    }

                                    ImGui::BeginDisabled(std::strlen(lightmapPathBuffer.data()) == 0);
                                    if (ImGui::Button("Load Lightmap"))
                                    {
                                        auto *lightmapTexture = engine.GetTextureManager().LoadLightmapFromFile(lightmapPathBuffer.data());
                                        material->SetLightmapTexture(lightmapTexture);
                                    }
                                    ImGui::EndDisabled();

                                    ImGui::SameLine();
                                    if (ImGui::Button("Clear Lightmap"))
                                    {
                                        material->SetLightmapTexture(nullptr);
                                    }

                                    ImGui::Text("Baked Lightmap: %s", materialConfig.lightmapTexture ? "yes" : "no");

                                    if (meshComponent->IsStatic() && materialConfig.lightmapTexture && meshComponent->GetMesh() && !meshComponent->GetMesh()->HasUsableLightmapUvsForSubmesh(submeshIndex))
                                    {
                                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Baking is using UV0 because TEXCOORD_1 / UV2 is missing. UV2 is still preferred to avoid overlap artifacts.");
                                    }

                                    if (materialUsesAssetReference)
                                    {
                                        ImGui::EndDisabled();
                                    }

                                    if (ImGui::Button("Make Unique Override"))
                                    {
                                        auto *overrideMaterial = new render::Material(material->GetConfig());
                                        meshComponent->SetMaterialForSubmesh(submeshIndex, overrideMaterial);
                                        meshComponent->SetMaterialAssetForSubmesh(submeshIndex, {});
                                        editorShell.MarkSceneDirty();
                                    }
                                }
                                else
                                {
                                    ImGui::Text("No material assigned.");
                                }

                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                    }
                }
            }

            // Components
            if (ImGui::CollapsingHeader("Components"))
            {
                static int selectedComponentTypeIndex = 0;
                selectedComponentTypeIndex = std::clamp(selectedComponentTypeIndex, 0, static_cast<int>(IM_ARRAYSIZE(kAddableComponentLabels)) - 1);

                ImGui::SetNextItemWidth(180.0f);
                ImGui::Combo("Add Component", &selectedComponentTypeIndex, kAddableComponentLabels, IM_ARRAYSIZE(kAddableComponentLabels));
                ImGui::SameLine();

                const auto selectedComponentType = static_cast<AddableComponentType>(selectedComponentTypeIndex);
                const bool canAddSelectedComponent = CanAddComponentType(*entity, selectedComponentType);
                ImGui::BeginDisabled(!canAddSelectedComponent);
                if (ImGui::Button("Add##Component"))
                {
                    const scene::EntityID entityId = entity->GetID();
                    editorShell.ExecuteSceneEdit("Add Component",
                                                 [entityId, selectedComponentType]()
                                                 {
                                                     auto *currentScene = core::Engine::GetInstance().GetScene();
                                                     if (auto *target = currentScene ? currentScene->FindEntityByID(entityId) : nullptr)
                                                     {
                                                         AddComponentToEntity(*target, selectedComponentType);
                                                     }
                                                 });
                }
                ImGui::EndDisabled();

                if (!canAddSelectedComponent)
                {
                    ImGui::TextDisabled("Selected component already exists on this entity.");
                }

                int componentIndex = 0;
                scene::Component *componentToRemove = nullptr;
                for (const auto &component : entity->GetComponentBuckets())
                {
                    if (!component.empty())
                    {
                        auto *componentPtr = component.front();
                        ImGui::PushID(componentIndex++);
                        const bool isComponentOpen = ImGui::TreeNodeEx(GetComponentDisplayName(*componentPtr), ImGuiTreeNodeFlags_DefaultOpen);
                        ImGui::SameLine();
                        if (ImGui::Button("Remove"))
                        {
                            componentToRemove = componentPtr;
                        }

                        if (!isComponentOpen)
                        {
                            ImGui::PopID();
                            if (componentToRemove)
                            {
                                break;
                            }
                            continue;
                        }

                        bool isEnabled = componentPtr->IsEnabled();
                        if (ImGui::Checkbox("Enabled", &isEnabled))
                        {
                            componentPtr->SetEnabled(isEnabled);
                            const auto componentTypeName = GetComponentPrefabTypeName(*componentPtr);
                            if (!componentTypeName.empty())
                            {
                                entity->AddPrefabOverride("Component:" + componentTypeName + ":Enabled");
                            }
                            editorShell.MarkSceneDirty();
                        }

                        std::vector<scene::Property> properties;
                        bool propertiesProvided = false;
                        bool propertiesChanged = false;

                        if (auto *cameraComponent = dynamic_cast<scene::CameraComponent *>(componentPtr))
                        {
                            bool isMainCamera = cameraComponent->IsMainCamera();
                            if (ImGui::Checkbox("Main Camera", &isMainCamera))
                            {
                                SetSceneMainCamera(entity->GetScene(), cameraComponent, isMainCamera);
                            }

                            RenderCameraPostProcessEditor(*cameraComponent);
                        }
                        else if (auto *scriptComponent = dynamic_cast<scene::ScriptComponent *>(componentPtr))
                        {
                            if (RenderScriptComponentEditor(*scriptComponent, *entity))
                            {
                                entity->AddPrefabOverride("Component:ScriptComponent:Source");
                                editorShell.MarkSceneDirty();
                            }
                        }
                        else if (dynamic_cast<scene::LightComponent *>(componentPtr))
                        {
                            ImGui::TextDisabled("Only lights marked Static contribute to Bake Scene.");
                        }
                        else if (auto *iblCaptureComponent = dynamic_cast<scene::IblCaptureComponent *>(componentPtr))
                        {
                            properties = iblCaptureComponent->SerializeEditableProperties();
                            propertiesProvided = true;
                            const bool hasCapture = iblCaptureComponent->GetCaptureTexture() != nullptr;
                            ImGui::Text("Captured HDRI: %s", hasCapture ? "ready" : "not captured");
                            if (ImGui::Button("Capture Scene"))
                            {
                                EditorShell::GetInstance().RequestIblCapture(iblCaptureComponent);
                            }

                            ImGui::SameLine();
                            if (ImGui::Button("Mark Dirty"))
                            {
                                iblCaptureComponent->MarkDirty();
                            }
                        }

                        if (!propertiesProvided)
                        {
                            properties = componentPtr->Serialize();
                        }

                        int propertyIndex = 0;
                        for (auto &property : properties)
                        {
                            if (property.name == "PostProcessEffectCount" || property.name == "MainCamera" || property.name == "Primary" || property.name.rfind("PostProcessEffects.", 0) == 0)
                            {
                                continue;
                            }

                            ImGui::PushID(propertyIndex++);
                            propertiesChanged |= RenderPropertyEditor(property);
                            ImGui::PopID();
                        }

                        if (propertiesChanged)
                        {
                            componentPtr->Deserialize(properties);
                            const auto componentTypeName = GetComponentPrefabTypeName(*componentPtr);
                            if (!componentTypeName.empty())
                            {
                                for (const auto &property : properties)
                                {
                                    entity->AddPrefabOverride("Component:" + componentTypeName + ":" + property.name);
                                }
                            }
                            editorShell.MarkSceneDirty();
                        }

                        ImGui::TreePop();
                        ImGui::PopID();

                        if (componentToRemove)
                        {
                            break;
                        }
                    }
                }

                if (componentToRemove)
                {
                    const scene::EntityID entityId = entity->GetID();
                    editorShell.ExecuteSceneEdit("Remove Component",
                                                 [entityId, componentToRemove]()
                                                 {
                                                     auto *currentScene = core::Engine::GetInstance().GetScene();
                                                     if (auto *target = currentScene ? currentScene->FindEntityByID(entityId) : nullptr)
                                                     {
                                                         target->RemoveComponent(componentToRemove);
                                                     }
                                                 });
                }
            }
        }
    }

    void InspectorPanel::Shutdown()
    {
        // Cleanup code for the InspectorPanel
    }
}
