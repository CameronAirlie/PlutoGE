
#include "PlutoGE/ui/panels/InspectorPanel.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
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
        constexpr std::size_t kInspectorPathBufferSize = 512;
        constexpr const char *kAddableComponentLabels[] = {
            "Mesh Component",
            "Camera Component",
            "Light Component",
        };

        enum class AddableComponentType
        {
            Mesh = 0,
            Camera = 1,
            Light = 2,
        };

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
            if (dynamic_cast<const scene::CameraComponent *>(&component))
            {
                return "Camera Component";
            }
            if (dynamic_cast<const scene::LightComponent *>(&component))
            {
                return "Light Component";
            }

            return typeid(component).name();
        }

        bool CanAddComponentType(const scene::Entity &entity, AddableComponentType componentType)
        {
            switch (componentType)
            {
            case AddableComponentType::Mesh:
                return !entity.HasComponent<scene::MeshComponent>();
            case AddableComponentType::Camera:
                return !entity.HasComponent<scene::CameraComponent>();
            case AddableComponentType::Light:
                return !entity.HasComponent<scene::LightComponent>();
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
                entity.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{
                    .mesh = nullptr,
                    .material = engine.GetAssetManager().CreateDefaultMaterial(),
                });
                break;
            }
            case AddableComponentType::Camera:
            {
                entity.CreateComponent<scene::CameraComponent>(new render::Camera(render::CameraConfig{
                    .fovY = 60.0f,
                    .nearPlane = 0.1f,
                    .farPlane = 100.0f,
                }));
                break;
            }
            case AddableComponentType::Light:
                entity.CreateComponent<scene::LightComponent>();
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
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Clear HDRI"))
        {
            scene.ClearEnvironmentMap();
            cachedEnvironmentPath.clear();
            std::fill(environmentPathBuffer.begin(), environmentPathBuffer.end(), '\0');
        }

        float environmentIntensity = scene.GetEnvironmentIntensity();
        if (ImGui::DragFloat("Environment Intensity", &environmentIntensity, 0.01f, 0.0f, 32.0f))
        {
            scene.SetEnvironmentIntensity(environmentIntensity);
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
        case scene::PropertyType::Bool:
        {
            bool value = (property.value == "true");
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
        auto isActive = entity->IsActive();
        if (ImGui::Checkbox("Active", &isActive))
        {
            entity->SetActive(isActive);
        }

        {
            // Transform
            auto position = entity->GetPosition();
            auto rotation = entity->GetRotation();
            auto scale = entity->GetScale();

            if (ImGui::CollapsingHeader("Transform"))
            {
                ImGui::DragFloat3("Position", &position.x, 0.01f);
                ImGui::DragFloat3("Rotation", &rotation.x, 0.1f);
                ImGui::DragFloat3("Scale", &scale.x, 0.01f);

                entity->SetPosition(position);
                entity->SetRotation(rotation);
                entity->SetScale(scale);
            }

            // Mesh Import UI (if entity has MeshComponent)
            if (auto *meshComponent = entity->GetComponent<PlutoGE::scene::MeshComponent>())
            {
                auto &engine = PlutoGE::core::Engine::GetInstance();
                const auto meshImportStatus = engine.GetMeshImportStatus(entity->GetID());

                ImGui::Separator();
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

                if (meshComponent->GetMesh() && meshComponent->GetMesh()->GetSubmeshCount() > 0)
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
                                    }

                                    float metallic = materialConfig.metallic;
                                    if (ImGui::DragFloat("Metallic", &metallic, 0.01f, 0.0f, 1.0f))
                                    {
                                        material->SetMetallic(metallic);
                                    }

                                    float roughness = materialConfig.roughness;
                                    if (ImGui::DragFloat("Roughness", &roughness, 0.01f, 0.04f, 1.0f))
                                    {
                                        material->SetRoughness(roughness);
                                    }

                                    bool flipNormalY = materialConfig.flipNormalY;
                                    if (ImGui::Checkbox("Flip Normal Y", &flipNormalY))
                                    {
                                        material->SetFlipNormalY(flipNormalY);
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

                                    if (ImGui::Button("Make Unique Override"))
                                    {
                                        auto *overrideMaterial = new render::Material(material->GetConfig());
                                        meshComponent->SetMaterialForSubmesh(submeshIndex, overrideMaterial);
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
                    AddComponentToEntity(*entity, selectedComponentType);
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
                        }

                        auto properties = componentPtr->Serialize();
                        bool propertiesChanged = false;

                        if (auto *cameraComponent = dynamic_cast<scene::CameraComponent *>(componentPtr))
                        {
                            RenderCameraPostProcessEditor(*cameraComponent);
                        }
                        else if (dynamic_cast<scene::LightComponent *>(componentPtr))
                        {
                            ImGui::TextDisabled("Only lights marked Static contribute to Bake Scene.");
                        }

                        int propertyIndex = 0;
                        for (auto &property : properties)
                        {
                            if (property.name == "PostProcessEffectCount" || property.name.rfind("PostProcessEffects.", 0) == 0)
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
                    entity->RemoveComponent(componentToRemove);
                }
            }
        }
    }

    void InspectorPanel::Shutdown()
    {
        // Cleanup code for the InspectorPanel
    }
}