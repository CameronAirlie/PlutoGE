#include "PlutoGE/editor_native.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/PostProcessPresetAsset.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ClothComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/NavAgentComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/SoundListenerComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <limits>
#include <map>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint32_t kApiVersion = 6;
    constexpr PlutoEditorHandle kEngineHandle = 0x504c55544f454e47ull;
    thread_local std::string g_lastError;

    void SetError(std::string message)
    {
        g_lastError = std::move(message);
    }

    struct ViewportState
    {
        PlutoEditorHandle handle = 0;
        std::unique_ptr<PlutoGE::render::RenderTarget> renderTarget;
        ImGuiContext *imguiContext = nullptr;
        uint32_t selectedEntityId = 0;
        ImGuizmo::OPERATION gizmoOperation = ImGuizmo::TRANSLATE;
        uint64_t frameCount = 0;
        double accumulatedFrameMs = 0.0;
        double maximumFrameMs = 0.0;
        double lastResizeMs = 0.0;
        int width = 0;
        int height = 0;
        float targetRefreshHz = 0.0f;
        bool previousMouseButtons[3]{};
        bool previousFocus = false;
        bool gizmoActive = false;
        PlutoGE::render::CameraData lastCameraData{};
        bool hasCameraData = false;
    };

    struct PendingComponentEdit
    {
        uint32_t entityId = 0;
        uint32_t componentIndex = 0;
        uint32_t propertyIndex = 0;
        std::string value;
    };

    struct EngineState
    {
        PlutoGE::core::Engine *engine = nullptr;
        std::unique_ptr<PlutoGE::assets::Project> project;
        std::unique_ptr<PlutoGE::scene::Scene> scene;
        std::vector<std::unique_ptr<PlutoGE::render::IPostProcessEffect>> editorCameraPostProcessEffects;
        std::vector<std::unique_ptr<PlutoGE::render::IPostProcessEffect>> retiredPostProcessEffects;
        std::unordered_map<PlutoEditorHandle, std::unique_ptr<ViewportState>> viewports;
        std::vector<PendingComponentEdit> pendingComponentEdits;
        PlutoEditorHandle nextViewportHandle = 1;
        std::mutex mutex;
    };

    std::unique_ptr<EngineState> g_state;
    std::mutex g_stateMutex;

    EngineState *ResolveEngine(PlutoEditorHandle handle)
    {
        return handle == kEngineHandle ? g_state.get() : nullptr;
    }

    ViewportState *ResolveViewport(EngineState &state, PlutoEditorHandle handle)
    {
        const auto iterator = state.viewports.find(handle);
        return iterator == state.viewports.end() ? nullptr : iterator->second.get();
    }

    void CollectEntities(PlutoGE::scene::Entity *entity, std::vector<PlutoGE::scene::Entity *> &entities)
    {
        if (!entity)
        {
            return;
        }
        entities.push_back(entity);
        for (auto *child : entity->GetChildren())
        {
            CollectEntities(child, entities);
        }
    }

    std::vector<PlutoGE::scene::Entity *> FlattenScene(const PlutoGE::scene::Scene &scene)
    {
        std::vector<PlutoGE::scene::Entity *> entities;
        for (auto *root : scene.GetRootEntities())
        {
            CollectEntities(root, entities);
        }
        return entities;
    }

    void PopulateInitialScene(PlutoGE::core::Engine &engine, PlutoGE::scene::Scene &scene)
    {
        auto world = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "World"});
        auto *worldEntity = scene.AddEntity(std::move(world));

        auto previewCube = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Preview Cube"});
        previewCube->SetPosition(glm::vec3(0.0f, 0.5f, 0.0f));
        auto *previewCubeEntity = scene.AddEntity(std::move(previewCube), worldEntity);
        if (previewCubeEntity)
        {
            auto *mesh = previewCubeEntity->CreateComponent<PlutoGE::scene::MeshComponent>(PlutoGE::scene::MeshComponentConfig{
                .mesh = engine.GetAssetManager().LoadMeshAsset(std::string(PlutoGE::assets::Project::kBuiltinCubeMeshReference)),
                .material = engine.GetAssetManager().LoadMaterialAsset(std::string(PlutoGE::assets::Project::kBuiltinDefaultShadedMaterialReference)),
            });
            if (mesh)
            {
                mesh->SetSourceMeshPath(std::string(PlutoGE::assets::Project::kBuiltinCubeMeshReference));
                mesh->SetMaterialAssetForMaterialSlot(0, std::string(PlutoGE::assets::Project::kBuiltinDefaultShadedMaterialReference));
            }
        }

        auto directionalLight = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Directional Light"});
        auto *directionalLightEntity = scene.AddEntity(std::move(directionalLight), worldEntity);
        if (directionalLightEntity)
        {
            auto *light = directionalLightEntity->CreateComponent<PlutoGE::scene::LightComponent>();
            if (light)
            {
                light->SetLightType(PlutoGE::scene::LightType::Directional);
                light->SetIntensity(4.0f);
                light->SetDirection(glm::normalize(glm::vec3(-0.45f, -0.85f, -0.25f)));
            }
        }
        scene.AddEntity(std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Camera"}));
    }

    bool InitializeViewport(ViewportState &viewport, int width, int height)
    {
        viewport.imguiContext = ImGui::CreateContext();
        if (!viewport.imguiContext)
        {
            SetError("Failed to create the viewport ImGui context.");
            return false;
        }

        ImGui::SetCurrentContext(viewport.imguiContext);
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        if (!ImGui_ImplOpenGL3_Init("#version 430 core"))
        {
            SetError("Failed to initialize the ImGui OpenGL backend.");
            ImGui::DestroyContext(viewport.imguiContext);
            viewport.imguiContext = nullptr;
            return false;
        }

        PlutoGE::render::RenderTargetConfig targetConfig;
        targetConfig.width = std::max(width, 1);
        targetConfig.height = std::max(height, 1);
        targetConfig.clearColor = glm::vec4(0.055f, 0.065f, 0.08f, 1.0f);
        viewport.renderTarget = std::make_unique<PlutoGE::render::RenderTarget>(targetConfig);
        if (!viewport.renderTarget->IsInitialized())
        {
            SetError("Failed to create the PlutoGE viewport render target.");
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext(viewport.imguiContext);
            viewport.imguiContext = nullptr;
            viewport.renderTarget.reset();
            return false;
        }

        viewport.width = targetConfig.width;
        viewport.height = targetConfig.height;
        return true;
    }

    void DestroyViewport(EngineState &state, ViewportState &viewport)
    {
        if (viewport.imguiContext)
        {
            ImGui::SetCurrentContext(viewport.imguiContext);
            ImGui_ImplOpenGL3_Shutdown();
        }
        if (viewport.renderTarget)
        {
            state.engine->GetRenderer().ReleaseRenderTarget(viewport.renderTarget.get());
            viewport.renderTarget.reset();
        }
        if (viewport.imguiContext)
        {
            ImGui::DestroyContext(viewport.imguiContext);
            viewport.imguiContext = nullptr;
        }
    }

    PlutoGE::render::CameraData BuildCameraData(const PlutoEditorViewportFrame &frame)
    {
        const glm::vec3 position(frame.camera_x, frame.camera_y, frame.camera_z);
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = glm::rotate(transform, glm::radians(frame.camera_yaw_degrees), glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::rotate(transform, glm::radians(frame.camera_pitch_degrees), glm::vec3(1.0f, 0.0f, 0.0f));

        PlutoGE::render::Camera camera(PlutoGE::render::CameraConfig{
            .fovY = std::clamp(frame.camera_fov_degrees, 10.0f, 150.0f),
            .nearPlane = std::clamp(frame.camera_near_plane, 0.001f, 1000.0f),
            .farPlane = std::max(frame.camera_far_plane, frame.camera_near_plane + 0.001f),
        });
        return camera.GetCameraDataForTransform(transform, std::max(frame.width, 1), std::max(frame.height, 1));
    }

    bool BuildPickRay(const ViewportState &viewport,
                      float mouseX,
                      float mouseY,
                      glm::vec3 &origin,
                      glm::vec3 &direction)
    {
        if (!viewport.hasCameraData || viewport.width <= 0 || viewport.height <= 0 ||
            mouseX < 0.0f || mouseY < 0.0f ||
            mouseX > static_cast<float>(viewport.width) || mouseY > static_cast<float>(viewport.height))
        {
            return false;
        }

        const float clipX = mouseX / static_cast<float>(viewport.width) * 2.0f - 1.0f;
        const float clipY = 1.0f - mouseY / static_cast<float>(viewport.height) * 2.0f;
        glm::vec4 eyeDirection = glm::inverse(viewport.lastCameraData.projection) * glm::vec4(clipX, clipY, -1.0f, 1.0f);
        eyeDirection = glm::vec4(eyeDirection.x, eyeDirection.y, -1.0f, 0.0f);

        const glm::mat4 inverseView = glm::inverse(viewport.lastCameraData.view);
        glm::vec4 worldOrigin = inverseView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        if (std::abs(worldOrigin.w) <= 0.000001f)
        {
            return false;
        }

        origin = glm::vec3(worldOrigin / worldOrigin.w);
        direction = glm::normalize(glm::vec3(inverseView * eyeDirection));
        return std::isfinite(direction.x) && std::isfinite(direction.y) && std::isfinite(direction.z);
    }

    bool IntersectSphere(const glm::vec3 &origin,
                         const glm::vec3 &direction,
                         const glm::vec3 &center,
                         float radius,
                         float &distance)
    {
        const glm::vec3 offset = origin - center;
        const float b = glm::dot(offset, direction);
        const float c = glm::dot(offset, offset) - radius * radius;
        if (c <= 0.0f)
        {
            distance = 0.0f;
            return true;
        }
        if (b > 0.0f)
        {
            return false;
        }

        const float discriminant = b * b - c;
        if (discriminant < 0.0f)
        {
            return false;
        }

        distance = std::max(0.0f, -b - std::sqrt(discriminant));
        return true;
    }

    uint32_t PickEntity(EngineState &state, ViewportState &viewport, float mouseX, float mouseY)
    {
        glm::vec3 rayOrigin{0.0f};
        glm::vec3 rayDirection{0.0f};
        if (!BuildPickRay(viewport, mouseX, mouseY, rayOrigin, rayDirection))
        {
            return 0;
        }

        PlutoGE::scene::Entity *pickedEntity = nullptr;
        float closestDistance = std::numeric_limits<float>::max();
        for (auto *entity : FlattenScene(*state.scene))
        {
            if (!entity || !entity->IsActive())
            {
                continue;
            }

            if (auto *terrain = entity->GetComponent<PlutoGE::scene::TerrainComponent>();
                terrain && terrain->IsEnabled())
            {
                glm::vec3 hitPoint{0.0f};
                if (terrain->Raycast(rayOrigin, rayDirection, hitPoint))
                {
                    const float distance = glm::length(hitPoint - rayOrigin);
                    if (distance < closestDistance)
                    {
                        closestDistance = distance;
                        pickedEntity = entity;
                    }
                }
            }

            auto *meshComponent = entity->GetComponent<PlutoGE::scene::MeshComponent>();
            if (!meshComponent || !meshComponent->IsEnabled() || !meshComponent->IsVisible() || !meshComponent->GetMesh())
            {
                continue;
            }

            const auto &bounds = meshComponent->GetMesh()->GetBounds();
            const glm::mat4 worldTransform = entity->GetWorldTransform() * meshComponent->GetMeshOffsetTransform();
            const glm::vec3 center = glm::vec3(worldTransform * glm::vec4(bounds.center, 1.0f));
            const float maximumScale = std::max({
                glm::length(glm::vec3(worldTransform[0])),
                glm::length(glm::vec3(worldTransform[1])),
                glm::length(glm::vec3(worldTransform[2]))});
            float distance = 0.0f;
            if (IntersectSphere(rayOrigin, rayDirection, center,
                                std::max(bounds.radius * maximumScale, 0.001f), distance) &&
                distance < closestDistance)
            {
                closestDistance = distance;
                pickedEntity = entity;
            }
        }

        return pickedEntity ? pickedEntity->GetID() : 0;
    }

    void ApplyGizmo(EngineState &state,
                    ViewportState &viewport,
                    const PlutoEditorViewportFrame &frame,
                    const PlutoGE::render::CameraData &cameraData)
    {
        viewport.gizmoActive = false;
        auto *entity = state.scene->FindEntityByID(viewport.selectedEntityId);
        if (!entity)
        {
            return;
        }

        glm::mat4 worldTransform = entity->GetWorldTransform();
        glm::mat4 gizmoProjection = glm::perspective(glm::radians(std::clamp(frame.camera_fov_degrees, 10.0f, 150.0f)),
                                                     static_cast<float>(std::max(frame.width, 1)) / static_cast<float>(std::max(frame.height, 1)),
                                                     0.1f,
                                                     1000.0f);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::Enable(true);
        ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
        ImGuizmo::SetRect(0.0f, 0.0f, static_cast<float>(frame.width), static_cast<float>(frame.height));
        ImGuizmo::Manipulate(glm::value_ptr(cameraData.view),
                             glm::value_ptr(gizmoProjection),
                             viewport.gizmoOperation,
                             ImGuizmo::LOCAL,
                             glm::value_ptr(worldTransform));

        if (!ImGuizmo::IsUsing())
        {
            return;
        }

        viewport.gizmoActive = true;

        glm::mat4 localTransform = worldTransform;
        if (auto *parent = entity->GetParent())
        {
            localTransform = glm::inverse(parent->GetWorldTransform()) * worldTransform;
        }

        float translation[3]{};
        float rotation[3]{};
        float scale[3]{};
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localTransform), translation, rotation, scale);
        entity->SetPosition(glm::make_vec3(translation));
        entity->SetRotation(glm::make_vec3(rotation));
        entity->SetScale(glm::make_vec3(scale));
    }

    void DrawOverlay(EngineState &state,
                     ViewportState &viewport,
                     const PlutoEditorViewportFrame &frame,
                     const PlutoGE::render::CameraData &cameraData)
    {
        ImGui::SetCurrentContext(viewport.imguiContext);
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(frame.width), static_cast<float>(frame.height));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = std::clamp(frame.delta_seconds, 1.0f / 1000.0f, 0.25f);
        const bool focused = frame.focused != 0;
        if (focused != viewport.previousFocus)
        {
            io.AddFocusEvent(focused);
            viewport.previousFocus = focused;
        }
        io.AddMousePosEvent(frame.mouse_x, frame.mouse_y);
        io.AddMouseWheelEvent(0.0f, frame.mouse_wheel);

        const bool buttons[3]{frame.mouse_left != 0, frame.mouse_right != 0, frame.mouse_middle != 0};
        for (int index = 0; index < 3; ++index)
        {
            if (buttons[index] != viewport.previousMouseButtons[index])
            {
                io.AddMouseButtonEvent(index, buttons[index]);
                viewport.previousMouseButtons[index] = buttons[index];
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.82f);
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                           ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoFocusOnAppearing |
                                           ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("Viewport overlay", nullptr, flags))
        {
            if (ImGui::Button("Move")) viewport.gizmoOperation = ImGuizmo::TRANSLATE;
            ImGui::SameLine();
            if (ImGui::Button("Rotate")) viewport.gizmoOperation = ImGuizmo::ROTATE;
            ImGui::SameLine();
            if (ImGui::Button("Scale")) viewport.gizmoOperation = ImGuizmo::SCALE;
            ImGui::SameLine();
            const double average = viewport.frameCount > 0 ? viewport.accumulatedFrameMs / static_cast<double>(viewport.frameCount) : 0.0;
            ImGui::Text("%.1f Hz  %.2f ms", frame.target_refresh_hz, average);
        }
        ImGui::End();

        ApplyGizmo(state, viewport, frame, cameraData);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    int32_t ValidateFrame(const PlutoEditorViewportFrame *frame)
    {
        if (!frame || frame->struct_size < sizeof(PlutoEditorViewportFrame) || frame->width <= 0 || frame->height <= 0 || !frame->get_proc_address)
        {
            SetError("Invalid viewport frame data.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        return PLUTO_EDITOR_OK;
    }

    template <std::size_t Size>
    void CopyString(char (&destination)[Size], std::string_view source)
    {
        std::memset(destination, 0, Size);
        std::memcpy(destination, source.data(), std::min(source.size(), Size - 1));
    }

    void CopyString(char *destination, uint32_t destinationSize, std::string_view source)
    {
        if (!destination || destinationSize == 0)
            return;
        std::memset(destination, 0, destinationSize);
        std::memcpy(destination, source.data(), std::min(source.size(), static_cast<size_t>(destinationSize - 1)));
    }

    std::string JoinOptions(const std::vector<std::string> &options)
    {
        std::string joined;
        for (const auto &option : options)
        {
            if (!joined.empty()) joined.push_back('\n');
            joined += option;
        }
        return joined;
    }

    bool IsEditableProperty(const PlutoGE::scene::Property &property)
    {
        if (property.name == "PostProcessEffectCount") return false;
        return property.name.size() < 5 || property.name.substr(property.name.size() - 5) != ".Type";
    }

    std::vector<PlutoGE::scene::Component *> FlattenComponents(PlutoGE::scene::Entity &entity);

    bool ApplyPendingComponentEdits(EngineState &state)
    {
        if (state.pendingComponentEdits.empty()) return true;

        std::map<std::pair<uint32_t, uint32_t>, std::vector<PendingComponentEdit>> groupedEdits;
        for (auto &edit : state.pendingComponentEdits)
            groupedEdits[{edit.entityId, edit.componentIndex}].push_back(std::move(edit));
        state.pendingComponentEdits.clear();

        for (const auto &[componentKey, edits] : groupedEdits)
        {
            auto *entity = state.scene->FindEntityByID(componentKey.first);
            if (!entity)
            {
                SetError("The component's entity no longer exists.");
                return false;
            }
            const auto components = FlattenComponents(*entity);
            if (componentKey.second >= components.size())
            {
                SetError("The component no longer exists.");
                return false;
            }

            auto properties = components[componentKey.second]->Serialize();
            for (const auto &edit : edits)
            {
                if (edit.propertyIndex >= properties.size() || !IsEditableProperty(properties[edit.propertyIndex]))
                {
                    SetError("The component property is no longer available.");
                    return false;
                }
                properties[edit.propertyIndex].value = edit.value;
            }

            try
            {
                components[componentKey.second]->Deserialize(properties);
            }
            catch (const std::exception &exception)
            {
                SetError(exception.what());
                return false;
            }
        }
        return true;
    }

    std::vector<PlutoGE::scene::Component *> FlattenComponents(PlutoGE::scene::Entity &entity)
    {
        std::vector<PlutoGE::scene::Component *> components;
        for (const auto &bucket : entity.GetComponentBuckets())
            components.insert(components.end(), bucket.begin(), bucket.end());
        return components;
    }

    const char *GetComponentDisplayName(const PlutoGE::scene::Component &component)
    {
        using namespace PlutoGE::scene;
        if (dynamic_cast<const MeshComponent *>(&component)) return "Mesh Component";
        if (dynamic_cast<const TerrainComponent *>(&component)) return "Terrain Component";
        if (dynamic_cast<const FoliageComponent *>(&component)) return "Foliage Component";
        if (dynamic_cast<const ClothComponent *>(&component)) return "Cloth Component";
        if (dynamic_cast<const ParticleSystemComponent *>(&component)) return "Particle System Component";
        if (dynamic_cast<const SplineComponent *>(&component)) return "Spline Track Component";
        if (dynamic_cast<const OceanComponent *>(&component)) return "Ocean Component";
        if (dynamic_cast<const AnimationComponent *>(&component)) return "Animation Component";
        if (dynamic_cast<const SkeletonAttachmentComponent *>(&component)) return "Skeleton Attachment Component";
        if (dynamic_cast<const CameraComponent *>(&component)) return "Camera Component";
        if (dynamic_cast<const LightComponent *>(&component)) return "Light Component";
        if (dynamic_cast<const RigidbodyComponent *>(&component)) return "Rigidbody Component";
        if (dynamic_cast<const NavAgentComponent *>(&component)) return "Navigation Agent Component";
        if (dynamic_cast<const NavigationMeshComponent *>(&component)) return "Navigation Mesh Component";
        if (dynamic_cast<const ColliderComponent *>(&component)) return "Collider Component";
        if (dynamic_cast<const IblCaptureComponent *>(&component)) return "IBL Capture Component";
        if (dynamic_cast<const VolumetricCloudComponent *>(&component)) return "Volumetric Cloud Component";
        if (dynamic_cast<const PhysicalSkyComponent *>(&component)) return "Physical Sky Component";
        if (dynamic_cast<const ScriptComponent *>(&component)) return "Script Component";
        if (dynamic_cast<const SoundEmitterComponent *>(&component)) return "Sound Emitter Component";
        if (dynamic_cast<const SoundListenerComponent *>(&component)) return "Sound Listener Component";
        if (dynamic_cast<const CanvasComponent *>(&component)) return "Canvas Component";
        if (dynamic_cast<const RectTransformComponent *>(&component)) return "Rect Transform Component";
        if (dynamic_cast<const UIImageComponent *>(&component)) return "UI Image Component";
        if (dynamic_cast<const UITextComponent *>(&component)) return "UI Text Component";
        if (dynamic_cast<const UIButtonComponent *>(&component)) return "UI Button Component";
        return "Component";
    }

    enum class AddableComponentType
    {
        Mesh, Terrain, Spline, Ocean, Foliage, Cloth, ParticleSystem, Camera, Light,
        Animation, Rigidbody, Collider, NavAgent, NavigationMesh, IblCapture,
        PhysicalSky, VolumetricCloud, Script, SoundEmitter, SoundListener,
        Canvas, RectTransform, UIImage, UIText, UIButton,
    };

    struct AddableComponentDefinition
    {
        const char *typeName;
        const char *displayName;
        const char *category;
        AddableComponentType type;
    };

    constexpr AddableComponentDefinition kAddableComponentDefinitions[] = {
        {"MeshComponent", "Mesh", "Rendering", AddableComponentType::Mesh},
        {"TerrainComponent", "Terrain", "Rendering", AddableComponentType::Terrain},
        {"SplineComponent", "Spline Track", "Rendering", AddableComponentType::Spline},
        {"OceanComponent", "Ocean", "Rendering", AddableComponentType::Ocean},
        {"FoliageComponent", "Foliage", "Rendering", AddableComponentType::Foliage},
        {"ClothComponent", "Cloth", "Rendering", AddableComponentType::Cloth},
        {"ParticleSystemComponent", "Particle System", "Rendering", AddableComponentType::ParticleSystem},
        {"CameraComponent", "Camera", "Rendering", AddableComponentType::Camera},
        {"LightComponent", "Light", "Rendering", AddableComponentType::Light},
        {"AnimationComponent", "Animation", "Animation", AddableComponentType::Animation},
        {"RigidbodyComponent", "Rigidbody", "Physics", AddableComponentType::Rigidbody},
        {"ColliderComponent", "Collider", "Physics", AddableComponentType::Collider},
        {"NavAgentComponent", "Navigation Agent", "AI", AddableComponentType::NavAgent},
        {"NavigationMeshComponent", "Navigation Mesh", "AI", AddableComponentType::NavigationMesh},
        {"IblCaptureComponent", "IBL Capture", "Environment", AddableComponentType::IblCapture},
        {"PhysicalSkyComponent", "Physical Sky", "Environment", AddableComponentType::PhysicalSky},
        {"VolumetricCloudComponent", "Volumetric Cloud", "Environment", AddableComponentType::VolumetricCloud},
        {"ScriptComponent", "Script", "Scripting", AddableComponentType::Script},
        {"SoundEmitterComponent", "Sound Emitter", "Audio", AddableComponentType::SoundEmitter},
        {"SoundListenerComponent", "Sound Listener", "Audio", AddableComponentType::SoundListener},
        {"CanvasComponent", "Canvas", "UI", AddableComponentType::Canvas},
        {"RectTransformComponent", "Rect Transform", "UI", AddableComponentType::RectTransform},
        {"UIImageComponent", "Image", "UI", AddableComponentType::UIImage},
        {"UITextComponent", "Text", "UI", AddableComponentType::UIText},
        {"UIButtonComponent", "Button", "UI", AddableComponentType::UIButton},
    };

    bool CanAddComponent(const PlutoGE::scene::Entity &entity, AddableComponentType type)
    {
        using namespace PlutoGE::scene;
        switch (type)
        {
        case AddableComponentType::Mesh: return !entity.HasComponent<MeshComponent>();
        case AddableComponentType::Terrain: return !entity.HasComponent<TerrainComponent>();
        case AddableComponentType::Spline: return !entity.HasComponent<SplineComponent>();
        case AddableComponentType::Ocean: return !entity.HasComponent<OceanComponent>();
        case AddableComponentType::Foliage: return !entity.HasComponent<FoliageComponent>();
        case AddableComponentType::Cloth: return !entity.HasComponent<ClothComponent>();
        case AddableComponentType::ParticleSystem: return !entity.HasComponent<ParticleSystemComponent>();
        case AddableComponentType::Camera: return !entity.HasComponent<CameraComponent>();
        case AddableComponentType::Light: return !entity.HasComponent<LightComponent>();
        case AddableComponentType::Animation: return !entity.HasComponent<AnimationComponent>();
        case AddableComponentType::Rigidbody: return !entity.HasComponent<RigidbodyComponent>();
        case AddableComponentType::Collider: return !entity.HasComponent<ColliderComponent>();
        case AddableComponentType::NavAgent: return !entity.HasComponent<NavAgentComponent>();
        case AddableComponentType::NavigationMesh: return !entity.HasComponent<NavigationMeshComponent>();
        case AddableComponentType::IblCapture: return !entity.HasComponent<IblCaptureComponent>();
        case AddableComponentType::PhysicalSky: return !entity.HasComponent<PhysicalSkyComponent>();
        case AddableComponentType::VolumetricCloud: return !entity.HasComponent<VolumetricCloudComponent>();
        case AddableComponentType::Script: return !entity.HasComponent<ScriptComponent>();
        case AddableComponentType::SoundEmitter: return !entity.HasComponent<SoundEmitterComponent>();
        case AddableComponentType::SoundListener: return !entity.HasComponent<SoundListenerComponent>();
        case AddableComponentType::Canvas: return !entity.HasComponent<CanvasComponent>();
        case AddableComponentType::RectTransform: return !entity.HasComponent<RectTransformComponent>();
        case AddableComponentType::UIImage: return !entity.HasComponent<UIImageComponent>();
        case AddableComponentType::UIText: return !entity.HasComponent<UITextComponent>();
        case AddableComponentType::UIButton: return !entity.HasComponent<UIButtonComponent>();
        }
        return false;
    }

    bool AddComponent(EngineState &state, PlutoGE::scene::Entity &entity, AddableComponentType type)
    {
        using namespace PlutoGE;
        if (!CanAddComponent(entity, type)) return false;
        auto &assetManager = state.engine->GetAssetManager();
        switch (type)
        {
        case AddableComponentType::Mesh:
        {
            auto *component = entity.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{
                .mesh = nullptr,
                .material = assetManager.LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
            });
            if (component) component->SetMaterialAssetForMaterialSlot(0, std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
            return component != nullptr;
        }
        case AddableComponentType::Terrain:
        {
            auto *component = entity.CreateComponent<scene::TerrainComponent>(scene::TerrainComponentConfig{
                .material = assetManager.LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
            });
            if (component) component->SetMaterialAssetReference(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
            return component != nullptr;
        }
        case AddableComponentType::Spline:
        {
            auto *component = entity.CreateComponent<scene::SplineComponent>(scene::SplineComponentConfig{
                .material = assetManager.LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
                .materialAssetReference = std::string(assets::Project::kBuiltinDefaultShadedMaterialReference),
            });
            if (component) component->Rebuild();
            return component != nullptr;
        }
        case AddableComponentType::Ocean: return entity.CreateComponent<scene::OceanComponent>() != nullptr;
        case AddableComponentType::Foliage:
        {
            auto *component = entity.CreateComponent<scene::FoliageComponent>();
            if (component) component->SetMaterialAssetReference(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
            return component != nullptr;
        }
        case AddableComponentType::Cloth: return entity.CreateComponent<scene::ClothComponent>() != nullptr;
        case AddableComponentType::ParticleSystem: return entity.CreateComponent<scene::ParticleSystemComponent>() != nullptr;
        case AddableComponentType::Camera:
        {
            bool hasCamera = false;
            if (entity.GetScene())
                for (auto *candidate : FlattenScene(*entity.GetScene()))
                    hasCamera |= candidate && candidate->HasComponent<scene::CameraComponent>();
            auto *component = entity.CreateComponent<scene::CameraComponent>(new render::Camera(render::CameraConfig{
                .fovY = 60.0f, .nearPlane = 0.1f, .farPlane = 100.0f,
            }));
            if (component) component->SetMainCamera(!hasCamera);
            return component != nullptr;
        }
        case AddableComponentType::Light: return entity.CreateComponent<scene::LightComponent>() != nullptr;
        case AddableComponentType::Animation: return entity.CreateComponent<scene::AnimationComponent>() != nullptr;
        case AddableComponentType::Rigidbody: return entity.CreateComponent<scene::RigidbodyComponent>() != nullptr;
        case AddableComponentType::Collider:
            return entity.CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{
                .shape = entity.HasComponent<scene::TerrainComponent>() ? scene::ColliderShape::Terrain
                       : entity.HasComponent<scene::SplineComponent>() ? scene::ColliderShape::Mesh
                                                                      : scene::ColliderShape::Box,
            }) != nullptr;
        case AddableComponentType::NavAgent: return entity.CreateComponent<scene::NavAgentComponent>() != nullptr;
        case AddableComponentType::NavigationMesh: return entity.CreateComponent<scene::NavigationMeshComponent>() != nullptr;
        case AddableComponentType::IblCapture: return entity.CreateComponent<scene::IblCaptureComponent>() != nullptr;
        case AddableComponentType::PhysicalSky: return entity.CreateComponent<scene::PhysicalSkyComponent>() != nullptr;
        case AddableComponentType::VolumetricCloud: return entity.CreateComponent<scene::VolumetricCloudComponent>() != nullptr;
        case AddableComponentType::Script: return entity.CreateComponent<scene::ScriptComponent>(scene::ScriptComponentConfig{}) != nullptr;
        case AddableComponentType::SoundEmitter: return entity.CreateComponent<scene::SoundEmitterComponent>() != nullptr;
        case AddableComponentType::SoundListener: return entity.CreateComponent<scene::SoundListenerComponent>() != nullptr;
        case AddableComponentType::Canvas: return entity.CreateComponent<scene::CanvasComponent>() != nullptr;
        case AddableComponentType::RectTransform: return entity.CreateComponent<scene::RectTransformComponent>() != nullptr;
        case AddableComponentType::UIImage: return entity.CreateComponent<scene::UIImageComponent>() != nullptr;
        case AddableComponentType::UIText: return entity.CreateComponent<scene::UITextComponent>() != nullptr;
        case AddableComponentType::UIButton: return entity.CreateComponent<scene::UIButtonComponent>() != nullptr;
        }
        return false;
    }

    void ReplaceScene(EngineState &state, std::unique_ptr<PlutoGE::scene::Scene> scene)
    {
        state.engine->SetScene(nullptr);
        state.pendingComponentEdits.clear();
        state.scene = std::move(scene);
        state.engine->SetScene(state.scene.get());
        for (auto &[handle, viewport] : state.viewports)
            viewport->selectedEntityId = 0;
    }

    void ApplyProjectEditorPostProcess(EngineState &state, const PlutoGE::assets::ProjectManifest &manifest)
    {
        std::vector<std::unique_ptr<PlutoGE::render::IPostProcessEffect>> effects;
        bool projectStackApplied = false;

        if (!manifest.editorCameraPostProcessPreset.empty())
        {
            bool loaded = false;
            const auto preset = state.engine->GetAssetManager().LoadPostProcessPresetAsset(
                manifest.editorCameraPostProcessPreset, &loaded);
            if (loaded)
            {
                effects = PlutoGE::assets::InstantiatePostProcessPreset(preset);
                projectStackApplied = true;
            }
        }

        if (!projectStackApplied && !manifest.editorCameraPostProcessEffects.empty())
        {
            projectStackApplied = true;
            effects.reserve(manifest.editorCameraPostProcessEffects.size());
            for (const auto &serializedEffect : manifest.editorCameraPostProcessEffects)
            {
                auto effect = PlutoGE::render::CreatePostProcessEffect(serializedEffect.typeName);
                if (!effect)
                    continue;

                effect->SetEnabled(serializedEffect.enabled);
                auto parameters = effect->GetParameters();
                for (const auto &serializedParameter : serializedEffect.parameters)
                {
                    const auto parameter = std::find_if(parameters.begin(), parameters.end(),
                                                        [&serializedParameter](const PlutoGE::render::PostProcessParameter &candidate)
                                                        {
                                                            return candidate.name == serializedParameter.name;
                                                        });
                    if (parameter == parameters.end())
                        continue;
                    parameter->type = static_cast<PlutoGE::render::PostProcessParameterType>(serializedParameter.type);
                    parameter->value = serializedParameter.value;
                }
                effect->SetParameters(parameters);
                effects.push_back(std::move(effect));
            }
        }

        if (!projectStackApplied)
            effects = PlutoGE::assets::InstantiatePostProcessPreset(PlutoGE::assets::CreateDefaultPostProcessPresetAsset());

        for (auto &effect : state.editorCameraPostProcessEffects)
            state.retiredPostProcessEffects.push_back(std::move(effect));
        state.editorCameraPostProcessEffects = std::move(effects);
    }
}

extern "C"
{
    uint32_t pluto_editor_api_version(void)
    {
        return kApiVersion;
    }

    int32_t pluto_editor_engine_create(const PlutoEditorEngineConfig *config, PlutoEditorHandle *engineHandle)
    {
        if (!config || !engineHandle || config->struct_size < sizeof(PlutoEditorEngineConfig) || config->api_version != kApiVersion || !config->get_proc_address)
        {
            SetError("Invalid engine configuration or API version.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }

        std::scoped_lock stateLock(g_stateMutex);
        if (g_state)
        {
            SetError("Only one PlutoGE engine instance may be hosted in a process.");
            return PLUTO_EDITOR_ALREADY_INITIALIZED;
        }

        auto state = std::make_unique<EngineState>();
        state->engine = &PlutoGE::core::Engine::GetInstance();
        PlutoGE::core::EngineConfig engineConfig;
        engineConfig.windowConfig.title = "PlutoGE Avalonia Host";
        engineConfig.windowConfig.width = std::max(config->initial_width, 1);
        engineConfig.windowConfig.height = std::max(config->initial_height, 1);
        engineConfig.windowConfig.visible = false;
        engineConfig.windowConfig.externalOpenGLProcAddress = config->get_proc_address;
        engineConfig.windowConfig.externalOpenGLUserData = config->user_data;
        if (!state->engine->Initialize(engineConfig))
        {
            SetError("PlutoGE requires a current desktop OpenGL 4.3 context.");
            return PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }

        state->editorCameraPostProcessEffects = PlutoGE::assets::InstantiatePostProcessPreset(
            PlutoGE::assets::CreateDefaultPostProcessPresetAsset());
        state->scene = std::make_unique<PlutoGE::scene::Scene>();
        PopulateInitialScene(*state->engine, *state->scene);
        state->engine->SetScene(state->scene.get());
        g_state = std::move(state);
        *engineHandle = kEngineHandle;
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_engine_destroy(PlutoEditorHandle engineHandle)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            SetError("Invalid engine handle.");
            return PLUTO_EDITOR_INVALID_HANDLE;
        }

        {
            std::scoped_lock engineLock(state->mutex);
            for (auto &[handle, viewport] : state->viewports)
            {
                DestroyViewport(*state, *viewport);
            }
            state->viewports.clear();
            state->engine->SetScene(nullptr);
            state->scene.reset();
            state->retiredPostProcessEffects.clear();
            state->editorCameraPostProcessEffects.clear();
            state->engine->Shutdown();
        }
        g_state.reset();
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_create(PlutoEditorHandle engineHandle, PlutoEditorHandle *viewportHandle)
    {
        if (!viewportHandle)
        {
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        std::scoped_lock engineLock(state->mutex);
        auto viewport = std::make_unique<ViewportState>();
        viewport->handle = ++state->nextViewportHandle;
        *viewportHandle = viewport->handle;
        state->viewports.emplace(viewport->handle, std::move(viewport));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_destroy(PlutoEditorHandle engineHandle, PlutoEditorHandle viewportHandle)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        std::scoped_lock engineLock(state->mutex);
        const auto iterator = state->viewports.find(viewportHandle);
        if (iterator == state->viewports.end())
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        DestroyViewport(*state, *iterator->second);
        state->viewports.erase(iterator);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_render(PlutoEditorHandle engineHandle, PlutoEditorHandle viewportHandle, const PlutoEditorViewportFrame *frame)
    {
        const int32_t frameValidation = ValidateFrame(frame);
        if (frameValidation != PLUTO_EDITOR_OK)
        {
            return frameValidation;
        }

        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        std::scoped_lock engineLock(state->mutex);
        auto *viewport = ResolveViewport(*state, viewportHandle);
        if (!viewport)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }

        auto &window = state->engine->GetWindow();
        window.SetExternalOpenGLContext(frame->get_proc_address, frame->user_data);
        window.SetExternalExtents(frame->width, frame->height);
        if (!window.EnsureOpenGLContextCurrent(viewport->renderTarget == nullptr))
        {
            SetError("The current viewport context is not desktop OpenGL 4.3 or is not shared with the engine context.");
            return viewport->renderTarget ? PLUTO_EDITOR_CONTEXT_NOT_SHARED : PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }

        if (!viewport->renderTarget && !InitializeViewport(*viewport, frame->width, frame->height))
        {
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }

        if (glIsTexture(viewport->renderTarget->GetColorTextureID()) == GL_FALSE)
        {
            SetError("Avalonia created a viewport context that does not share PlutoGE OpenGL objects.");
            return PLUTO_EDITOR_CONTEXT_NOT_SHARED;
        }

        // Effect destructors can release GPU resources, so defer removals until
        // Avalonia has made the viewport's shared OpenGL context current.
        state->retiredPostProcessEffects.clear();

        if (!ApplyPendingComponentEdits(*state))
            return PLUTO_EDITOR_INVALID_ARGUMENT;

        if (viewport->width != frame->width || viewport->height != frame->height)
        {
            const auto resizeStart = std::chrono::steady_clock::now();
            if (!viewport->renderTarget->Resize(frame->width, frame->height))
            {
                SetError("Failed to resize the viewport render target.");
                return PLUTO_EDITOR_INTERNAL_ERROR;
            }
            viewport->lastResizeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - resizeStart).count();
            viewport->width = frame->width;
            viewport->height = frame->height;
        }

        const auto cameraData = BuildCameraData(*frame);
        viewport->lastCameraData = cameraData;
        viewport->hasCameraData = true;

        auto &renderer = state->engine->GetRenderer();
        renderer.BeginProfilingFrame();
        renderer.ClearRenderCommands();
        renderer.SetSubmissionCullingCameras({cameraData});
        state->engine->UpdateAsyncMeshImports();
        state->scene->Update(std::clamp(frame->delta_seconds, 0.0f, 0.25f));
        renderer.BeginFrame(viewport->renderTarget.get());
        std::vector<PlutoGE::render::IPostProcessEffect *> postProcessEffects;
        postProcessEffects.reserve(state->editorCameraPostProcessEffects.size());
        for (const auto &effect : state->editorCameraPostProcessEffects)
            postProcessEffects.push_back(effect.get());
        renderer.RenderFrame(cameraData,
                             viewport->renderTarget.get(),
                             state->scene->GetLights(),
                             &postProcessEffects,
                             state->scene.get(),
                             true,
                             true);
        renderer.EndFrame(viewport->renderTarget.get());

        glBindFramebuffer(GL_READ_FRAMEBUFFER, viewport->renderTarget->GetFramebufferID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(frame->framebuffer));
        glBlitFramebuffer(0, 0, frame->width, frame->height,
                          0, 0, frame->width, frame->height,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(frame->framebuffer));
        glViewport(0, 0, frame->width, frame->height);

        DrawOverlay(*state, *viewport, *frame, cameraData);

        const double frameMs = std::max(0.0, static_cast<double>(frame->delta_seconds) * 1000.0);
        ++viewport->frameCount;
        viewport->accumulatedFrameMs += frameMs;
        viewport->maximumFrameMs = std::max(viewport->maximumFrameMs, frameMs);
        viewport->targetRefreshHz = frame->target_refresh_hz;
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_set_selected_entity(PlutoEditorHandle engineHandle, PlutoEditorHandle viewportHandle, uint32_t entityId)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        std::scoped_lock engineLock(state->mutex);
        auto *viewport = ResolveViewport(*state, viewportHandle);
        if (!viewport)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        viewport->selectedEntityId = entityId;
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_pick_entity(PlutoEditorHandle engineHandle,
                                              PlutoEditorHandle viewportHandle,
                                              float mouseX,
                                              float mouseY,
                                              uint32_t *entityId)
    {
        if (!entityId || !std::isfinite(mouseX) || !std::isfinite(mouseY))
        {
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }

        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        std::scoped_lock engineLock(state->mutex);
        auto *viewport = ResolveViewport(*state, viewportHandle);
        if (!viewport)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }

        ImGui::SetCurrentContext(viewport->imguiContext);
        if (viewport->imguiContext && (ImGui::GetIO().WantCaptureMouse || ImGuizmo::IsOver() || ImGuizmo::IsUsing()))
        {
            *entityId = viewport->selectedEntityId;
            return PLUTO_EDITOR_OK;
        }

        viewport->selectedEntityId = PickEntity(*state, *viewport, mouseX, mouseY);
        *entityId = viewport->selectedEntityId;
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_set_gizmo_operation(PlutoEditorHandle engineHandle, PlutoEditorHandle viewportHandle, int32_t operation)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        std::scoped_lock engineLock(state->mutex);
        auto *viewport = ResolveViewport(*state, viewportHandle);
        if (!viewport)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        switch (operation)
        {
        case PLUTO_EDITOR_GIZMO_TRANSLATE: viewport->gizmoOperation = ImGuizmo::TRANSLATE; break;
        case PLUTO_EDITOR_GIZMO_ROTATE: viewport->gizmoOperation = ImGuizmo::ROTATE; break;
        case PLUTO_EDITOR_GIZMO_SCALE: viewport->gizmoOperation = ImGuizmo::SCALE; break;
        default: return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_get_gizmo_active(PlutoEditorHandle engineHandle, PlutoEditorHandle viewportHandle, uint8_t *active)
    {
        if (!active) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *viewport = ResolveViewport(*state, viewportHandle);
        if (!viewport) return PLUTO_EDITOR_INVALID_HANDLE;
        *active = viewport->gizmoActive ? 1 : 0;
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_get_stats(PlutoEditorHandle engineHandle, PlutoEditorHandle viewportHandle, PlutoEditorFrameStats *stats)
    {
        if (!stats) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *viewport = ResolveViewport(*state, viewportHandle);
        if (!viewport) return PLUTO_EDITOR_INVALID_HANDLE;
        stats->frame_count = viewport->frameCount;
        stats->average_frame_ms = viewport->frameCount ? viewport->accumulatedFrameMs / static_cast<double>(viewport->frameCount) : 0.0;
        stats->maximum_frame_ms = viewport->maximumFrameMs;
        stats->resize_ms = viewport->lastResizeMs;
        stats->width = viewport->width;
        stats->height = viewport->height;
        stats->target_refresh_hz = viewport->targetRefreshHz;
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_scene_get_entity_count(PlutoEditorHandle engineHandle, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        *count = static_cast<uint32_t>(FlattenScene(*state->scene).size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_scene_get_entity(PlutoEditorHandle engineHandle, uint32_t index, PlutoEditorEntityInfo *entityInfo)
    {
        if (!entityInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        const auto entities = FlattenScene(*state->scene);
        if (index >= entities.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto *entity = entities[index];
        entityInfo->id = entity->GetID();
        entityInfo->parent_id = entity->GetParent() ? entity->GetParent()->GetID() : 0;
        entityInfo->active = entity->IsSelfActive() ? 1 : 0;
        std::memset(entityInfo->name, 0, sizeof(entityInfo->name));
        const std::string name = entity->GetName();
        std::memcpy(entityInfo->name, name.data(), std::min(name.size(), sizeof(entityInfo->name) - 1));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_get_transform(PlutoEditorHandle engineHandle, uint32_t entityId, PlutoEditorTransform *transform)
    {
        if (!transform) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        const auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const glm::vec3 position = entity->GetPosition();
        const glm::vec3 rotation = entity->GetRotation();
        const glm::vec3 scale = entity->GetScale();
        std::memcpy(transform->position, glm::value_ptr(position), sizeof(transform->position));
        std::memcpy(transform->rotation, glm::value_ptr(rotation), sizeof(transform->rotation));
        std::memcpy(transform->scale, glm::value_ptr(scale), sizeof(transform->scale));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_set_transform(PlutoEditorHandle engineHandle, uint32_t entityId, const PlutoEditorTransform *transform)
    {
        if (!transform) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        entity->SetPosition(glm::make_vec3(transform->position));
        entity->SetRotation(glm::make_vec3(transform->rotation));
        entity->SetScale(glm::make_vec3(transform->scale));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_project_load(PlutoEditorHandle engineHandle, const char *manifestPath)
    {
        if (!manifestPath || manifestPath[0] == '\0') return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);

        std::string errorMessage;
        auto project = PlutoGE::assets::Project::Load(manifestPath, &errorMessage);
        if (!project)
        {
            SetError(errorMessage.empty() ? "Failed to load project." : errorMessage);
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }

        project->RefreshAssetRegistry();
        state->engine->GetAssetManager().SetProjectContext(
            project->GetRootDirectory().string(), project->GetManifest().assetDirectory);
        if (!state->engine->GetWindow().EnsureOpenGLContextCurrent(true))
        {
            SetError("Failed to make the PlutoGE resource context current while loading the project.");
            return PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }
        ApplyProjectEditorPostProcess(*state, project->GetManifest());

        std::unique_ptr<PlutoGE::scene::Scene> scene;
        const auto &startupScene = project->GetManifest().startupScene;
        if (!startupScene.empty())
        {
            const auto scenePath = project->ResolveAssetReference(startupScene);
            if (!scenePath.empty())
                scene = PlutoGE::scene::SceneSerializer::Load(scenePath.string(), &errorMessage);
        }
        if (!scene)
            scene = std::make_unique<PlutoGE::scene::Scene>();

        state->project = std::move(project);
        ReplaceScene(*state, std::move(scene));
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_project_get_info(PlutoEditorHandle engineHandle, PlutoEditorProjectInfo *projectInfo)
    {
        if (!projectInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->project)
        {
            SetError("No project is loaded.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        const auto &manifest = state->project->GetManifest();
        CopyString(projectInfo->name, manifest.name);
        CopyString(projectInfo->manifest_path, state->project->GetManifestPath().string());
        CopyString(projectInfo->asset_directory, state->project->GetAssetDirectoryPath().string());
        CopyString(projectInfo->startup_scene, manifest.startupScene);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_project_get_asset_count(PlutoEditorHandle engineHandle, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->project) return PLUTO_EDITOR_INVALID_ARGUMENT;
        *count = static_cast<uint32_t>(state->project->GetManifest().assetEntries.size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_project_get_asset(PlutoEditorHandle engineHandle, uint32_t index, PlutoEditorAssetInfo *assetInfo)
    {
        if (!assetInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->project) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &assets = state->project->GetManifest().assetEntries;
        if (index >= assets.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &asset = assets[index];
        assetInfo->type = static_cast<int32_t>(asset.type);
        assetInfo->size = static_cast<uint64_t>(asset.size);
        CopyString(assetInfo->reference, asset.reference);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_scene_load(PlutoEditorHandle engineHandle, const char *pathOrReference)
    {
        if (!pathOrReference || pathOrReference[0] == '\0') return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);

        std::filesystem::path scenePath(pathOrReference);
        if (state->project && PlutoGE::assets::Project::IsProjectAssetReference(pathOrReference))
            scenePath = state->project->ResolveAssetReference(pathOrReference);

        if (!state->engine->GetWindow().EnsureOpenGLContextCurrent(true))
        {
            SetError("Failed to make the PlutoGE resource context current while loading the scene.");
            return PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }

        std::string errorMessage;
        auto scene = PlutoGE::scene::SceneSerializer::Load(scenePath.string(), &errorMessage);
        if (!scene)
        {
            SetError(errorMessage.empty() ? "Failed to load scene." : errorMessage);
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }
        ReplaceScene(*state, std::move(scene));
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_scene_get_info(PlutoEditorHandle engineHandle, PlutoEditorSceneInfo *sceneInfo)
    {
        if (!sceneInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        CopyString(sceneInfo->path, state->scene ? state->scene->GetFilePath() : std::string{});
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_get_component_count(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        *count = static_cast<uint32_t>(FlattenComponents(*entity).size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_get_component(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t componentIndex, PlutoEditorComponentInfo *componentInfo)
    {
        if (!componentInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto components = FlattenComponents(*entity);
        if (componentIndex >= components.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto *component = components[componentIndex];
        componentInfo->index = componentIndex;
        componentInfo->enabled = component->IsEnabled() ? 1 : 0;
        CopyString(componentInfo->name, GetComponentDisplayName(*component));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_component_set_enabled(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t componentIndex, uint8_t enabled)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto components = FlattenComponents(*entity);
        if (componentIndex >= components.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        components[componentIndex]->SetEnabled(enabled != 0);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_component_get_property_count(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t componentIndex, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto components = FlattenComponents(*entity);
        if (componentIndex >= components.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        *count = static_cast<uint32_t>(components[componentIndex]->Serialize().size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_component_get_property(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t componentIndex, uint32_t propertyIndex, PlutoEditorComponentProperty *propertyInfo)
    {
        if (!propertyInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto components = FlattenComponents(*entity);
        if (componentIndex >= components.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto properties = components[componentIndex]->Serialize();
        if (propertyIndex >= properties.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &property = properties[propertyIndex];
        propertyInfo->type = static_cast<int32_t>(property.type);
        propertyInfo->editable = IsEditableProperty(property) ? 1 : 0;
        CopyString(propertyInfo->name, property.name);
        CopyString(propertyInfo->value, property.value);
        CopyString(propertyInfo->enum_options, JoinOptions(property.enumOptions));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_component_set_property(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t componentIndex, uint32_t propertyIndex, const char *value)
    {
        if (!value) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto components = FlattenComponents(*entity);
        if (componentIndex >= components.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        auto properties = components[componentIndex]->Serialize();
        if (propertyIndex >= properties.size() || !IsEditableProperty(properties[propertyIndex]))
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        auto existing = std::find_if(state->pendingComponentEdits.begin(), state->pendingComponentEdits.end(),
                                     [entityId, componentIndex, propertyIndex](const PendingComponentEdit &edit)
                                     {
                                         return edit.entityId == entityId && edit.componentIndex == componentIndex &&
                                                edit.propertyIndex == propertyIndex;
                                     });
        if (existing != state->pendingComponentEdits.end())
            existing->value = value;
        else
            state->pendingComponentEdits.push_back(PendingComponentEdit{entityId, componentIndex, propertyIndex, value});
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_get_addable_component_type_count(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->scene || !state->scene->FindEntityByID(entityId)) return PLUTO_EDITOR_INVALID_ARGUMENT;
        *count = static_cast<uint32_t>(std::size(kAddableComponentDefinitions));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_get_addable_component_type(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t typeIndex, PlutoEditorAddableComponentType *componentType)
    {
        if (!componentType) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene ? state->scene->FindEntityByID(entityId) : nullptr;
        if (!entity || typeIndex >= std::size(kAddableComponentDefinitions)) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &definition = kAddableComponentDefinitions[typeIndex];
        componentType->can_add = CanAddComponent(*entity, definition.type) ? 1 : 0;
        CopyString(componentType->type_name, definition.typeName);
        CopyString(componentType->display_name, definition.displayName);
        CopyString(componentType->category, definition.category);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_add_component(PlutoEditorHandle engineHandle, uint32_t entityId, const char *typeName)
    {
        if (!typeName) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene ? state->scene->FindEntityByID(entityId) : nullptr;
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        if (!ApplyPendingComponentEdits(*state)) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto definition = std::find_if(std::begin(kAddableComponentDefinitions), std::end(kAddableComponentDefinitions),
                                             [typeName](const AddableComponentDefinition &candidate)
                                             {
                                                 return std::string_view(candidate.typeName) == typeName;
                                             });
        if (definition == std::end(kAddableComponentDefinitions) || !AddComponent(*state, *entity, definition->type))
        {
            SetError("The selected component cannot be added to this GameObject.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_remove_component(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t componentIndex)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene ? state->scene->FindEntityByID(entityId) : nullptr;
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        if (!ApplyPendingComponentEdits(*state)) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto components = FlattenComponents(*entity);
        if (componentIndex >= components.size() || !entity->RemoveComponent(components[componentIndex]))
        {
            SetError("The selected component could not be removed.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_post_process_get_registered_type_count(PlutoEditorHandle engineHandle, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        if (!ResolveEngine(engineHandle)) return PLUTO_EDITOR_INVALID_HANDLE;
        *count = static_cast<uint32_t>(PlutoGE::render::GetRegisteredPostProcessEffectTypes().size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_post_process_get_registered_type(PlutoEditorHandle engineHandle, uint32_t typeIndex, char *typeName, uint32_t typeNameSize)
    {
        if (!typeName || typeNameSize == 0) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        if (!ResolveEngine(engineHandle)) return PLUTO_EDITOR_INVALID_HANDLE;
        const auto &types = PlutoGE::render::GetRegisteredPostProcessEffectTypes();
        if (typeIndex >= types.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        CopyString(typeName, typeNameSize, types[typeIndex]);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_get_post_process_effect_count(PlutoEditorHandle engineHandle, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        *count = static_cast<uint32_t>(state->editorCameraPostProcessEffects.size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_get_post_process_effect(PlutoEditorHandle engineHandle, uint32_t effectIndex, PlutoEditorPostProcessEffectInfo *effectInfo)
    {
        if (!effectInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (effectIndex >= state->editorCameraPostProcessEffects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &effect = state->editorCameraPostProcessEffects[effectIndex];
        effectInfo->index = effectIndex;
        effectInfo->enabled = effect->IsEnabled() ? 1 : 0;
        CopyString(effectInfo->type_name, effect->GetTypeName());
        CopyString(effectInfo->display_name, effect->GetDisplayName());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_add_post_process_effect(PlutoEditorHandle engineHandle, const char *typeName)
    {
        if (!typeName) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto effect = PlutoGE::render::CreatePostProcessEffect(typeName);
        if (!effect)
        {
            SetError("Unknown post-process effect type.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        state->editorCameraPostProcessEffects.push_back(std::move(effect));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_remove_post_process_effect(PlutoEditorHandle engineHandle, uint32_t effectIndex)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (effectIndex >= state->editorCameraPostProcessEffects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        state->retiredPostProcessEffects.push_back(std::move(state->editorCameraPostProcessEffects[effectIndex]));
        state->editorCameraPostProcessEffects.erase(state->editorCameraPostProcessEffects.begin() + effectIndex);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_move_post_process_effect(PlutoEditorHandle engineHandle, uint32_t fromIndex, uint32_t toIndex)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto &effects = state->editorCameraPostProcessEffects;
        if (fromIndex >= effects.size() || toIndex >= effects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        if (fromIndex == toIndex) return PLUTO_EDITOR_OK;
        auto effect = std::move(effects[fromIndex]);
        effects.erase(effects.begin() + fromIndex);
        effects.insert(effects.begin() + toIndex, std::move(effect));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_set_post_process_effect_enabled(PlutoEditorHandle engineHandle, uint32_t effectIndex, uint8_t enabled)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (effectIndex >= state->editorCameraPostProcessEffects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        state->editorCameraPostProcessEffects[effectIndex]->SetEnabled(enabled != 0);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_get_post_process_parameter_count(PlutoEditorHandle engineHandle, uint32_t effectIndex, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (effectIndex >= state->editorCameraPostProcessEffects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        *count = static_cast<uint32_t>(state->editorCameraPostProcessEffects[effectIndex]->GetParameters().size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_get_post_process_parameter(PlutoEditorHandle engineHandle, uint32_t effectIndex, uint32_t parameterIndex, PlutoEditorPostProcessParameter *parameterInfo)
    {
        if (!parameterInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (effectIndex >= state->editorCameraPostProcessEffects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto parameters = state->editorCameraPostProcessEffects[effectIndex]->GetParameters();
        if (parameterIndex >= parameters.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &parameter = parameters[parameterIndex];
        parameterInfo->type = static_cast<int32_t>(parameter.type);
        CopyString(parameterInfo->name, parameter.name);
        CopyString(parameterInfo->value, parameter.value);
        CopyString(parameterInfo->enum_options, JoinOptions(parameter.enumOptions));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_set_post_process_parameter(PlutoEditorHandle engineHandle, uint32_t effectIndex, uint32_t parameterIndex, const char *value)
    {
        if (!value) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (effectIndex >= state->editorCameraPostProcessEffects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        auto parameters = state->editorCameraPostProcessEffects[effectIndex]->GetParameters();
        if (parameterIndex >= parameters.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        parameters[parameterIndex].value = value;
        state->editorCameraPostProcessEffects[effectIndex]->SetParameters(parameters);
        return PLUTO_EDITOR_OK;
    }

    const char *pluto_editor_get_last_error(void)
    {
        return g_lastError.c_str();
    }
}
