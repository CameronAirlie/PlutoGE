#include "EditorSession.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/PostProcessPresetAsset.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/ComponentFactory.h"
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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <typeinfo>

namespace
{
    using namespace PlutoGE;

    std::string JsonEscape(std::string_view value)
    {
        std::ostringstream output;
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20)
                {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(character) << std::dec;
                }
                else
                {
                    output << static_cast<char>(character);
                }
            }
        }
        return output.str();
    }

    int HexDigit(char character)
    {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    }

    std::string Decode(std::string_view value)
    {
        if (!value.empty() && value.front() == '~') value.remove_prefix(1);
        std::string decoded;
        decoded.reserve(value.size());
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            if (value[index] == '%' && index + 2 < value.size())
            {
                const int high = HexDigit(value[index + 1]);
                const int low = HexDigit(value[index + 2]);
                if (high >= 0 && low >= 0)
                {
                    decoded.push_back(static_cast<char>((high << 4) | low));
                    index += 2;
                    continue;
                }
            }
            decoded.push_back(value[index] == '+' ? ' ' : value[index]);
        }
        return decoded;
    }

    const char *ComponentTypeName(const scene::Component &component)
    {
#define PLUTO_COMPONENT_NAME(Type) if (dynamic_cast<const scene::Type *>(&component)) return #Type
        PLUTO_COMPONENT_NAME(MeshComponent);
        PLUTO_COMPONENT_NAME(TerrainComponent);
        PLUTO_COMPONENT_NAME(FoliageComponent);
        PLUTO_COMPONENT_NAME(ClothComponent);
        PLUTO_COMPONENT_NAME(ParticleSystemComponent);
        PLUTO_COMPONENT_NAME(SplineComponent);
        PLUTO_COMPONENT_NAME(OceanComponent);
        PLUTO_COMPONENT_NAME(AnimationComponent);
        PLUTO_COMPONENT_NAME(SkeletonAttachmentComponent);
        PLUTO_COMPONENT_NAME(CameraComponent);
        PLUTO_COMPONENT_NAME(LightComponent);
        PLUTO_COMPONENT_NAME(RigidbodyComponent);
        PLUTO_COMPONENT_NAME(NavAgentComponent);
        PLUTO_COMPONENT_NAME(NavigationMeshComponent);
        PLUTO_COMPONENT_NAME(ColliderComponent);
        PLUTO_COMPONENT_NAME(IblCaptureComponent);
        PLUTO_COMPONENT_NAME(VolumetricCloudComponent);
        PLUTO_COMPONENT_NAME(PhysicalSkyComponent);
        PLUTO_COMPONENT_NAME(ScriptComponent);
        PLUTO_COMPONENT_NAME(SoundEmitterComponent);
        PLUTO_COMPONENT_NAME(SoundListenerComponent);
        PLUTO_COMPONENT_NAME(CanvasComponent);
        PLUTO_COMPONENT_NAME(RectTransformComponent);
        PLUTO_COMPONENT_NAME(UIImageComponent);
        PLUTO_COMPONENT_NAME(UITextComponent);
        PLUTO_COMPONENT_NAME(UIButtonComponent);
#undef PLUTO_COMPONENT_NAME
        return typeid(component).name();
    }

    scene::Component *ComponentAt(scene::Entity &entity, std::size_t requestedIndex)
    {
        std::size_t index = 0;
        for (const auto &bucket : entity.GetComponentBuckets())
        {
            for (auto *component : bucket)
            {
                if (index++ == requestedIndex) return component;
            }
        }
        return nullptr;
    }

    int PostProcessPropertyType(render::PostProcessParameterType type)
    {
        switch (type)
        {
        case render::PostProcessParameterType::Float: return static_cast<int>(scene::PropertyType::Float);
        case render::PostProcessParameterType::Int: return static_cast<int>(scene::PropertyType::Int);
        case render::PostProcessParameterType::Bool: return static_cast<int>(scene::PropertyType::Bool);
        case render::PostProcessParameterType::Enum: return static_cast<int>(scene::PropertyType::Enum);
        case render::PostProcessParameterType::String:
        default: return static_cast<int>(scene::PropertyType::String);
        }
    }

    void WritePostProcessEffectsJson(
        std::ostream &output,
        const std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects)
    {
        output << '[';
        bool firstEffect = true;
        for (const auto &effect : effects)
        {
            if (!effect) continue;
            if (!firstEffect) output << ',';
            firstEffect = false;
            output << "{\"typeName\":\"" << JsonEscape(effect->GetTypeName())
                   << "\",\"displayName\":\"" << JsonEscape(effect->GetDisplayName())
                   << "\",\"enabled\":" << (effect->IsEnabled() ? "true" : "false")
                   << ",\"parameters\":[";
            const auto parameters = effect->GetParameters();
            for (std::size_t parameterIndex = 0; parameterIndex < parameters.size(); ++parameterIndex)
            {
                const auto &parameter = parameters[parameterIndex];
                if (parameterIndex > 0) output << ',';
                output << "{\"name\":\"" << JsonEscape(parameter.name)
                       << "\",\"type\":" << PostProcessPropertyType(parameter.type)
                       << ",\"value\":\"" << JsonEscape(parameter.value) << "\",\"enumOptions\":[";
                for (std::size_t optionIndex = 0; optionIndex < parameter.enumOptions.size(); ++optionIndex)
                {
                    if (optionIndex > 0) output << ',';
                    output << '"' << JsonEscape(parameter.enumOptions[optionIndex]) << '"';
                }
                output << "]}";
            }
            output << "]}";
        }
        output << ']';
    }

    void WriteEntityJson(std::ostream &output, const scene::Entity &entity)
    {
        const auto position = entity.GetPosition();
        const auto rotation = entity.GetRotation();
        const auto scale = entity.GetScale();
        output << "{\"id\":" << entity.GetID()
               << ",\"parentId\":" << (entity.GetParent() ? entity.GetParent()->GetID() : 0)
               << ",\"name\":\"" << JsonEscape(entity.GetName()) << "\""
               << ",\"active\":" << (entity.IsSelfActive() ? "true" : "false")
               << ",\"position\":[" << position.x << ',' << position.y << ',' << position.z << ']'
               << ",\"rotation\":[" << rotation.x << ',' << rotation.y << ',' << rotation.z << ']'
               << ",\"scale\":[" << scale.x << ',' << scale.y << ',' << scale.z << ']'
               << ",\"components\":[";

        bool firstComponent = true;
        for (const auto &bucket : entity.GetComponentBuckets())
        {
            for (const auto *component : bucket)
            {
                if (!component) continue;
                if (!firstComponent) output << ',';
                firstComponent = false;
                output << "{\"type\":\"" << ComponentTypeName(*component) << "\",\"enabled\":"
                       << (component->IsEnabled() ? "true" : "false") << ",\"properties\":[";
                const auto properties = component->Serialize();
                for (std::size_t propertyIndex = 0; propertyIndex < properties.size(); ++propertyIndex)
                {
                    const auto &property = properties[propertyIndex];
                    if (propertyIndex > 0) output << ',';
                    output << "{\"name\":\"" << JsonEscape(property.name) << "\",\"type\":" << static_cast<int>(property.type)
                           << ",\"value\":\"" << JsonEscape(property.value) << "\",\"enumOptions\":[";
                    for (std::size_t optionIndex = 0; optionIndex < property.enumOptions.size(); ++optionIndex)
                    {
                        if (optionIndex > 0) output << ',';
                        output << '\"' << JsonEscape(property.enumOptions[optionIndex]) << '\"';
                    }
                    output << "]}";
                }
                output << ']';
                if (const auto *camera = dynamic_cast<const scene::CameraComponent *>(component))
                {
                    output << ",\"postProcessPresetReference\":\""
                           << JsonEscape(camera->GetPostProcessPresetAssetReference())
                           << "\",\"postProcessEffects\":";
                    WritePostProcessEffectsJson(output, camera->GetPostProcessEffects());
                }
                output << '}';
            }
        }
        output << "]}";
    }

    void CollectEntities(const scene::Entity &entity, std::vector<const scene::Entity *> &entities)
    {
        entities.push_back(&entity);
        for (const auto *child : entity.GetChildren())
        {
            if (child) CollectEntities(*child, entities);
        }
    }

    std::vector<std::unique_ptr<render::IPostProcessEffect>> CreateDefaultEditorPostProcessEffects()
    {
        return assets::InstantiatePostProcessPreset(assets::CreateDefaultPostProcessPresetAsset());
    }

    std::vector<std::unique_ptr<render::IPostProcessEffect>> CreateEditorPostProcessEffects(
        const std::vector<assets::ProjectPostProcessEffect> &serializedEffects)
    {
        std::vector<std::unique_ptr<render::IPostProcessEffect>> effects;
        effects.reserve(serializedEffects.size());
        for (const auto &serializedEffect : serializedEffects)
        {
            auto effect = render::CreatePostProcessEffect(serializedEffect.typeName);
            if (!effect) continue;

            effect->SetEnabled(serializedEffect.enabled);
            auto parameters = effect->GetParameters();
            for (const auto &serializedParameter : serializedEffect.parameters)
            {
                const auto parameter = std::find_if(parameters.begin(), parameters.end(),
                                                    [&serializedParameter](const render::PostProcessParameter &candidate)
                                                    {
                                                        return candidate.name == serializedParameter.name;
                                                    });
                if (parameter == parameters.end()) continue;
                parameter->type = static_cast<render::PostProcessParameterType>(serializedParameter.type);
                parameter->value = serializedParameter.value;
            }
            effect->SetParameters(parameters);
            effects.push_back(std::move(effect));
        }
        return effects;
    }

    std::vector<assets::ProjectPostProcessEffect> CaptureEditorPostProcessEffects(
        const std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects)
    {
        std::vector<assets::ProjectPostProcessEffect> serializedEffects;
        serializedEffects.reserve(effects.size());
        for (const auto &effect : effects)
        {
            if (!effect) continue;
            assets::ProjectPostProcessEffect serializedEffect;
            serializedEffect.typeName = effect->GetTypeName();
            serializedEffect.enabled = effect->IsEnabled();
            for (const auto &parameter : effect->GetParameters())
            {
                serializedEffect.parameters.push_back(assets::ProjectPostProcessParameter{
                    .name = parameter.name,
                    .type = static_cast<int>(parameter.type),
                    .value = parameter.value,
                });
            }
            serializedEffects.push_back(std::move(serializedEffect));
        }
        return serializedEffects;
    }
}

EditorSession::EditorSession(PlutoGE::core::Engine &engine) : m_engine(engine) {}
EditorSession::~EditorSession()
{
    Shutdown();
}

void EditorSession::Shutdown()
{
    m_engine.StopRuntime();
    m_engine.SetScene(nullptr);
    m_scene.reset();
    m_editorPostProcessEffects.clear();
    m_editorPostProcessPresetReference.clear();
    m_engine.GetAssetManager().ClearProjectContext();
    m_project.reset();
    m_projectPath.clear();
}

bool EditorSession::Initialize()
{
    m_editorPostProcessEffects = CreateDefaultEditorPostProcessEffects();
    m_editorPostProcessPresetReference.clear();
    auto scene = std::make_unique<PlutoGE::scene::Scene>();
    auto camera = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Main Camera"});
    auto *cameraEntity = scene->AddEntity(std::move(camera));
    cameraEntity->SetPosition({0.0f, 2.0f, 5.0f});
    auto *cameraComponent = static_cast<PlutoGE::scene::CameraComponent *>(PlutoGE::scene::AddComponentByTypeName(*cameraEntity, "CameraComponent"));
    cameraComponent->SetMainCamera(true);
    auto light = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Directional Light"});
    auto *lightEntity = scene->AddEntity(std::move(light));
    auto *lightComponent = static_cast<PlutoGE::scene::LightComponent *>(PlutoGE::scene::AddComponentByTypeName(*lightEntity, "LightComponent"));
    lightComponent->SetLightType(PlutoGE::scene::LightType::Directional);
    lightComponent->SetIntensity(4.0f);
    scene->AddEntity(std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Environment"}));
    return SetScene(std::move(scene), false);
}

PlutoGE::scene::Scene *EditorSession::GetScene() const { return m_scene.get(); }

void EditorSession::Update(float deltaTime)
{
    m_engine.UpdateAsyncMeshImports();
    if (m_scene) m_scene->Update(deltaTime);
}

bool EditorSession::SetViewportStats(int submittedRenderCommands, int visibleRenderCommands)
{
    if (m_submittedRenderCommands == submittedRenderCommands && m_visibleRenderCommands == visibleRenderCommands)
    {
        return false;
    }
    m_submittedRenderCommands = submittedRenderCommands;
    m_visibleRenderCommands = visibleRenderCommands;
    return true;
}

bool EditorSession::SetScene(std::unique_ptr<PlutoGE::scene::Scene> scene, bool dirty)
{
    if (!scene) return false;
    m_engine.StopRuntime();
    m_engine.SetScene(nullptr);
    m_scene = std::move(scene);
    m_engine.SetScene(m_scene.get());
    m_selectedEntityId = 0;
    m_dirty = dirty;
    return true;
}

bool EditorSession::CaptureScene(std::string &state) const
{
    std::string error;
    return m_scene && PlutoGE::scene::SceneSerializer::SaveToString(*m_scene, state, &error);
}

bool EditorSession::RestoreScene(const std::string &state, bool dirty)
{
    std::string error;
    auto restored = PlutoGE::scene::SceneSerializer::LoadFromString(state, &error);
    const auto selected = m_selectedEntityId;
    if (!SetScene(std::move(restored), dirty)) return false;
    if (m_scene->FindEntityByID(selected)) m_selectedEntityId = selected;
    return true;
}

bool EditorSession::CommitEdit(const std::string &before)
{
    std::string after;
    if (!CaptureScene(after) || before == after) return false;
    m_undo.push_back({before, std::move(after)});
    if (m_undo.size() > 64) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_dirty = true;
    return true;
}

PlutoGE::scene::Entity *EditorSession::FindEntity(std::uint32_t id) const
{
    return m_scene ? m_scene->FindEntityByID(id) : nullptr;
}

bool EditorSession::HandleCommand(const std::string &commandLine, std::string &errorMessage)
{
    std::istringstream input(commandLine);
    std::string command;
    input >> command;
    if (command == "editor_snapshot") return true;

    if (command == "select")
    {
        input >> m_selectedEntityId;
        if (m_selectedEntityId != 0 && !FindEntity(m_selectedEntityId)) m_selectedEntityId = 0;
        return true;
    }

    if (command == "set_editor_camera")
    {
        int gridVisible = m_editorCamera.gridVisible ? 1 : 0;
        input >> m_editorCamera.position.x >> m_editorCamera.position.y >> m_editorCamera.position.z
              >> m_editorCamera.yawDegrees >> m_editorCamera.pitchDegrees
              >> m_editorCamera.fovY >> m_editorCamera.nearPlane >> m_editorCamera.farPlane
              >> m_editorCamera.moveSpeed >> m_editorCamera.speedAdjustment >> gridVisible;
        m_editorCamera.pitchDegrees = std::clamp(m_editorCamera.pitchDegrees, -89.0f, 89.0f);
        m_editorCamera.fovY = std::clamp(m_editorCamera.fovY, 1.0f, 179.0f);
        m_editorCamera.nearPlane = std::max(m_editorCamera.nearPlane, 0.001f);
        m_editorCamera.farPlane = std::max(m_editorCamera.farPlane, m_editorCamera.nearPlane + 0.001f);
        m_editorCamera.moveSpeed = std::clamp(m_editorCamera.moveSpeed, 0.1f, 1000.0f);
        m_editorCamera.speedAdjustment = std::clamp(m_editorCamera.speedAdjustment, 0.1f, 10.0f);
        m_editorCamera.gridVisible = gridVisible != 0;
        return true;
    }

    if (command.rfind("editor_effect_", 0) == 0)
    {
        if (m_engine.IsRuntimeRunning())
        {
            errorMessage = "Stop play mode before editing the editor camera post-process stack.";
            return false;
        }

        if (command == "editor_effect_add")
        {
            std::string type;
            input >> type;
            if (auto effect = PlutoGE::render::CreatePostProcessEffect(Decode(type)))
            {
                m_editorPostProcessEffects.push_back(std::move(effect));
            }
        }
        else if (command == "editor_effect_remove")
        {
            std::size_t index = 0;
            input >> index;
            if (index < m_editorPostProcessEffects.size())
            {
                m_engine.GetWindow().EnsureOpenGLContextCurrent(true);
                m_editorPostProcessEffects.erase(m_editorPostProcessEffects.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }
        else if (command == "editor_effect_move")
        {
            std::size_t from = 0, to = 0;
            input >> from >> to;
            if (from < m_editorPostProcessEffects.size() && to < m_editorPostProcessEffects.size() && from != to)
            {
                auto effect = std::move(m_editorPostProcessEffects[from]);
                m_editorPostProcessEffects.erase(m_editorPostProcessEffects.begin() + static_cast<std::ptrdiff_t>(from));
                m_editorPostProcessEffects.insert(m_editorPostProcessEffects.begin() + static_cast<std::ptrdiff_t>(to), std::move(effect));
            }
        }
        else if (command == "editor_effect_enabled")
        {
            std::size_t index = 0;
            int enabled = 1;
            input >> index >> enabled;
            if (index < m_editorPostProcessEffects.size() && m_editorPostProcessEffects[index])
            {
                m_editorPostProcessEffects[index]->SetEnabled(enabled != 0);
            }
        }
        else if (command == "editor_effect_parameter")
        {
            std::size_t effectIndex = 0, parameterIndex = 0;
            std::string value;
            input >> effectIndex >> parameterIndex >> value;
            if (effectIndex < m_editorPostProcessEffects.size() && m_editorPostProcessEffects[effectIndex])
            {
                auto parameters = m_editorPostProcessEffects[effectIndex]->GetParameters();
                if (parameterIndex < parameters.size())
                {
                    parameters[parameterIndex].value = Decode(value);
                    m_editorPostProcessEffects[effectIndex]->SetParameters(parameters);
                }
            }
        }
        else if (command == "editor_effect_preset")
        {
            std::string reference;
            input >> reference;
            const auto decodedReference = Decode(reference);
            if (decodedReference.empty())
            {
                m_editorPostProcessEffects.clear();
                m_editorPostProcessPresetReference.clear();
            }
            else
            {
                bool loaded = false;
                const auto preset = m_engine.GetAssetManager().LoadPostProcessPresetAsset(decodedReference, &loaded);
                if (!loaded)
                {
                    errorMessage = "Could not load editor camera post-process preset: " + decodedReference;
                    return false;
                }
                m_editorPostProcessEffects = PlutoGE::assets::InstantiatePostProcessPreset(preset);
                m_editorPostProcessPresetReference = decodedReference;
            }
        }
        else if (command == "editor_effect_save_preset")
        {
            if (m_editorPostProcessPresetReference.empty())
            {
                errorMessage = "Set an editor camera post-process preset reference before saving.";
                return false;
            }
            const auto preset = PlutoGE::assets::CapturePostProcessPreset(m_editorPostProcessEffects);
            if (!m_engine.GetAssetManager().SavePostProcessPresetAsset(m_editorPostProcessPresetReference, preset, &errorMessage)) return false;
        }
        else
        {
            errorMessage = "Unknown editor camera post-process command.";
            return false;
        }
        return true;
    }

    if (command == "reset_editor_camera")
    {
        m_editorCamera = EditorCameraState{};
        return true;
    }

    if (command == "frame_selected")
    {
        if (auto *entity = FindEntity(m_selectedEntityId))
        {
            const glm::vec3 target = entity->GetWorldPosition();
            glm::vec3 direction = target - m_editorCamera.position;
            if (glm::dot(direction, direction) < 0.0001f)
            {
                m_editorCamera.position = target + glm::vec3(0.0f, 2.0f, 6.0f);
                direction = target - m_editorCamera.position;
            }
            direction = glm::normalize(direction);
            m_editorCamera.yawDegrees = glm::degrees(std::atan2(-direction.x, -direction.z));
            m_editorCamera.pitchDegrees = glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));
        }
        return true;
    }

    if (command == "undo" || command == "redo")
    {
        auto &source = command == "undo" ? m_undo : m_redo;
        auto &destination = command == "undo" ? m_redo : m_undo;
        if (source.empty()) return true;
        auto entry = std::move(source.back());
        source.pop_back();
        const bool restored = RestoreScene(command == "undo" ? entry.before : entry.after, true);
        if (restored) destination.push_back(std::move(entry));
        return true;
    }

    if (command == "new_scene")
    {
        m_scenePath.clear();
        m_undo.clear();
        m_redo.clear();
        return SetScene(std::make_unique<PlutoGE::scene::Scene>(), false);
    }

    if (command == "load_project" || command == "create_project")
    {
        std::string encodedPath;
        input >> encodedPath;
        const auto path = Decode(encodedPath);
        std::unique_ptr<PlutoGE::assets::Project> project;
        if (command == "create_project")
        {
            std::string encodedName;
            input >> encodedName;
            project = PlutoGE::assets::Project::Create(path, Decode(encodedName), &errorMessage);
        }
        else
        {
            project = PlutoGE::assets::Project::Load(path, &errorMessage);
        }
        if (!project) return false;

        m_engine.GetAssetManager().SetProjectContext(project->GetRootDirectory().string(), project->GetManifest().assetDirectory);
        project->RefreshAssetRegistry();
        const auto &manifest = project->GetManifest();
        m_engine.GetRenderer().SetVSyncEnabled(manifest.vSyncEnabled);
        m_editorCamera.position = {manifest.editorCamera.positionX, manifest.editorCamera.positionY, manifest.editorCamera.positionZ};
        m_editorCamera.moveSpeed = std::clamp(manifest.editorCamera.moveSpeed, 0.1f, 1000.0f);
        m_editorCamera.speedAdjustment = 1.0f;
        m_editorCamera.yawDegrees = manifest.editorCamera.yawDegrees;
        m_editorCamera.pitchDegrees = std::clamp(manifest.editorCamera.pitchDegrees, -89.0f, 89.0f);
        m_editorCamera.fovY = std::clamp(manifest.editorCamera.fovY, 1.0f, 179.0f);
        m_editorCamera.nearPlane = std::max(manifest.editorCamera.nearPlane, 0.001f);
        m_editorCamera.farPlane = std::max(manifest.editorCamera.farPlane, m_editorCamera.nearPlane + 0.001f);
        m_editorPostProcessEffects = CreateDefaultEditorPostProcessEffects();
        m_editorPostProcessPresetReference.clear();
        bool loadedEditorPostProcessPreset = false;
        if (!manifest.editorCameraPostProcessPreset.empty())
        {
            bool loaded = false;
            const auto preset = m_engine.GetAssetManager().LoadPostProcessPresetAsset(manifest.editorCameraPostProcessPreset, &loaded);
            if (loaded)
            {
                m_editorPostProcessEffects = PlutoGE::assets::InstantiatePostProcessPreset(preset);
                m_editorPostProcessPresetReference = manifest.editorCameraPostProcessPreset;
                loadedEditorPostProcessPreset = true;
            }
        }
        if (!loadedEditorPostProcessPreset && !manifest.editorCameraPostProcessEffects.empty())
        {
            m_editorPostProcessEffects = CreateEditorPostProcessEffects(manifest.editorCameraPostProcessEffects);
        }

        std::unique_ptr<PlutoGE::scene::Scene> loadedScene;
        std::string startupScenePath;
        bool loadedStartupScene = false;
        if (!manifest.startupScene.empty())
        {
            startupScenePath = m_engine.GetAssetManager().ResolveAssetPath(manifest.startupScene);
            if (!startupScenePath.empty())
            {
                loadedScene = PlutoGE::scene::SceneSerializer::Load(startupScenePath, &errorMessage);
                loadedStartupScene = loadedScene != nullptr;
            }
        }
        if (!loadedScene) loadedScene = std::make_unique<PlutoGE::scene::Scene>();

        m_project = std::move(project);
        m_projectPath = path;
        m_scenePath = loadedStartupScene ? startupScenePath : std::string{};
        m_undo.clear();
        m_redo.clear();
        return SetScene(std::move(loadedScene), false);
    }

    if (command == "load_scene")
    {
        std::string encodedPath;
        input >> encodedPath;
        const auto path = Decode(encodedPath);
        auto loaded = PlutoGE::scene::SceneSerializer::Load(path, &errorMessage);
        if (!loaded) return false;
        m_scenePath = path;
        m_undo.clear();
        m_redo.clear();
        return SetScene(std::move(loaded), false);
    }

    if (command == "save_project")
    {
        if (!m_project)
        {
            errorMessage = "No project is open.";
            return false;
        }
        auto &manifest = m_project->GetManifest();
        manifest.editorCamera = PlutoGE::assets::ProjectEditorCameraSettings{
            .positionX = m_editorCamera.position.x,
            .positionY = m_editorCamera.position.y,
            .positionZ = m_editorCamera.position.z,
            .moveSpeed = m_editorCamera.moveSpeed,
            .yawDegrees = m_editorCamera.yawDegrees,
            .pitchDegrees = m_editorCamera.pitchDegrees,
            .fovY = m_editorCamera.fovY,
            .nearPlane = m_editorCamera.nearPlane,
            .farPlane = m_editorCamera.farPlane,
        };
        manifest.editorCameraPostProcessPreset = m_editorPostProcessPresetReference;
        manifest.editorCameraPostProcessEffects = m_editorPostProcessPresetReference.empty()
                                                    ? CaptureEditorPostProcessEffects(m_editorPostProcessEffects)
                                                    : std::vector<PlutoGE::assets::ProjectPostProcessEffect>{};
        return m_project->Save(&errorMessage);
    }

    if (command == "save_scene")
    {
        std::string encodedPath;
        input >> encodedPath;
        const auto path = Decode(encodedPath);
        if (!path.empty()) m_scenePath = path;
        if (m_scenePath.empty()) { errorMessage = "No scene path was provided."; return false; }
        if (!PlutoGE::scene::SceneSerializer::Save(*m_scene, m_scenePath, &errorMessage)) return false;
        m_dirty = false;
        return true;
    }

    if (command == "runtime")
    {
        int start = 0;
        input >> start;
        if (start && !m_engine.IsRuntimeRunning())
        {
            CaptureScene(m_runtimeSnapshot);
            m_engine.StartRuntime();
        }
        else if (!start && m_engine.IsRuntimeRunning())
        {
            m_engine.StopRuntime();
            if (!m_runtimeSnapshot.empty()) RestoreScene(m_runtimeSnapshot, m_dirty);
            m_runtimeSnapshot.clear();
        }
        return true;
    }

    if (m_engine.IsRuntimeRunning())
    {
        errorMessage = "Stop play mode before editing the scene.";
        return false;
    }

    std::string before;
    CaptureScene(before);

    if (command == "refresh_assets")
    {
        if (!m_project)
        {
            errorMessage = "Open a project before refreshing assets.";
            return false;
        }
        m_project->RefreshAssetRegistry();
        if (!m_project->Save(&errorMessage)) return false;
        return true;
    }

    if (command == "create")
    {
        std::string preset;
        std::uint32_t parentId = 0;
        input >> preset >> parentId;
        const std::string presetName = Decode(preset);
        auto entity = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = presetName == "Empty Entity" ? "New Entity" : presetName});
        auto *created = m_scene->AddEntity(std::move(entity), FindEntity(parentId));
        if (!created)
        {
            errorMessage = "Could not create the preset entity.";
            return false;
        }
        if (presetName == "Cube")
        {
            auto *mesh = PlutoGE::scene::AddMeshComponent(
                *created,
                m_engine.GetAssetManager().LoadMeshAsset(std::string(PlutoGE::assets::Project::kBuiltinCubeMeshReference)),
                m_engine.GetAssetManager().LoadMaterialAsset(std::string(PlutoGE::assets::Project::kBuiltinDefaultShadedMaterialReference)));
            if (mesh)
            {
                mesh->SetSourceMeshPath(std::string(PlutoGE::assets::Project::kBuiltinCubeMeshReference));
                mesh->SetMaterialAssetForMaterialSlot(0, std::string(PlutoGE::assets::Project::kBuiltinDefaultShadedMaterialReference));
            }
        }
        else if (presetName == "Camera")
        {
            created->SetPosition({0.0f, 2.0f, 5.0f});
            PlutoGE::scene::AddComponentByTypeName(*created, "CameraComponent");
        }
        else if (presetName == "Directional Light" || presetName == "Point Light")
        {
            auto *light = static_cast<PlutoGE::scene::LightComponent *>(PlutoGE::scene::AddComponentByTypeName(*created, "LightComponent"));
            light->SetLightType(presetName == "Directional Light" ? PlutoGE::scene::LightType::Directional : PlutoGE::scene::LightType::Point);
            light->SetIntensity(presetName == "Directional Light" ? 4.0f : 8.0f);
            if (presetName == "Point Light") created->SetPosition({0.0f, 2.0f, 0.0f});
        }
        else if (presetName == "Sky")
        {
            PlutoGE::scene::AddComponentByTypeName(*created, "PhysicalSkyComponent");
            PlutoGE::scene::AddComponentByTypeName(*created, "VolumetricCloudComponent");
        }
        else if (presetName == "Ocean") PlutoGE::scene::AddComponentByTypeName(*created, "OceanComponent");
        else if (presetName == "Terrain") PlutoGE::scene::AddComponentByTypeName(*created, "TerrainComponent");
        else if (presetName == "Particle System") PlutoGE::scene::AddComponentByTypeName(*created, "ParticleSystemComponent");
        m_selectedEntityId = created->GetID();
    }
    else if (command == "instantiate_asset")
    {
        std::string encodedReference;
        input >> encodedReference;
        const std::string reference = Decode(encodedReference);
        if (!m_project || PlutoGE::assets::Project::GetAssetTypeForReference(reference) != PlutoGE::assets::ProjectAssetType::Model)
        {
            errorMessage = "Only imported project model assets can be placed in the scene.";
            return false;
        }
        const auto sourcePath = m_project->ResolveAssetReference(reference);
        PlutoGE::core::ImportedRenderMeshAsset imported;
        try
        {
            imported = m_engine.ImportMeshAsset(sourcePath.string());
        }
        catch (const std::exception &exception)
        {
            errorMessage = std::string("Failed to import model: ") + exception.what();
            return false;
        }
        if (!imported.mesh)
        {
            errorMessage = "The selected model did not contain a renderable mesh.";
            return false;
        }
        auto entity = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = sourcePath.stem().string()});
        auto *created = m_scene->AddEntity(std::move(entity));
        auto *mesh = PlutoGE::scene::AddMeshComponent(*created, imported.mesh, nullptr);
        mesh->SetMaterials(imported.materials);
        mesh->SetSourceMeshPath(reference);
        if (imported.animations && !imported.animations->empty())
        {
            auto *animation = created->CreateComponent<PlutoGE::scene::AnimationComponent>();
            animation->SetClipsFromImportedAnimations(*imported.animations);
            animation->SetSourceAnimationPath(reference);
        }
        m_selectedEntityId = created->GetID();
    }
    else if (command == "delete")
    {
        std::uint32_t id = 0;
        input >> id;
        if (m_scene->DestroyEntity(id) && m_selectedEntityId == id) m_selectedEntityId = 0;
    }
    else if (command == "set_name")
    {
        std::uint32_t id = 0;
        std::string value;
        input >> id >> value;
        if (auto *entity = FindEntity(id)) entity->SetName(Decode(value));
    }
    else if (command == "set_active")
    {
        std::uint32_t id = 0;
        int active = 1;
        input >> id >> active;
        if (auto *entity = FindEntity(id)) entity->SetActive(active != 0);
    }
    else if (command == "set_transform")
    {
        std::uint32_t id = 0;
        glm::vec3 position{}, rotation{}, scale{1.0f};
        input >> id >> position.x >> position.y >> position.z >> rotation.x >> rotation.y >> rotation.z >> scale.x >> scale.y >> scale.z;
        if (auto *entity = FindEntity(id)) { entity->SetPosition(position); entity->SetRotation(rotation); entity->SetScale(scale); }
    }
    else if (command == "reparent")
    {
        std::uint32_t id = 0, parentId = 0;
        input >> id >> parentId;
        if (auto *entity = FindEntity(id)) entity->SetParent(FindEntity(parentId));
    }
    else if (command == "component_enabled")
    {
        std::uint32_t id = 0;
        std::size_t componentIndex = 0;
        int enabled = 1;
        input >> id >> componentIndex >> enabled;
        if (auto *entity = FindEntity(id)) if (auto *component = ComponentAt(*entity, componentIndex)) component->SetEnabled(enabled != 0);
    }
    else if (command == "set_property")
    {
        std::uint32_t id = 0;
        std::size_t componentIndex = 0, propertyIndex = 0;
        std::string value;
        input >> id >> componentIndex >> propertyIndex >> value;
        if (auto *entity = FindEntity(id))
        {
            if (auto *component = ComponentAt(*entity, componentIndex))
            {
                auto properties = component->Serialize();
                if (propertyIndex < properties.size()) { properties[propertyIndex].value = Decode(value); component->Deserialize(properties); }
            }
        }
    }
    else if (command == "add_component")
    {
        std::uint32_t id = 0;
        std::string type;
        input >> id >> type;
        if (auto *entity = FindEntity(id)) PlutoGE::scene::AddComponentByTypeName(*entity, Decode(type));
    }
    else if (command == "remove_component")
    {
        std::uint32_t id = 0;
        std::size_t componentIndex = 0;
        input >> id >> componentIndex;
        if (auto *entity = FindEntity(id)) if (auto *component = ComponentAt(*entity, componentIndex)) entity->RemoveComponent(component);
    }
    else if (command.rfind("camera_effect_", 0) == 0)
    {
        std::uint32_t id = 0;
        std::size_t componentIndex = 0;
        input >> id >> componentIndex;
        auto *entity = FindEntity(id);
        auto *camera = entity ? dynamic_cast<PlutoGE::scene::CameraComponent *>(ComponentAt(*entity, componentIndex)) : nullptr;
        if (!camera)
        {
            errorMessage = "The selected component is not a camera.";
            return false;
        }

        if (command == "camera_effect_add")
        {
            std::string type;
            input >> type;
            camera->AddPostProcessEffectByType(Decode(type));
        }
        else if (command == "camera_effect_remove")
        {
            std::size_t index = 0;
            input >> index;
            camera->RemovePostProcessEffect(index);
        }
        else if (command == "camera_effect_move")
        {
            std::size_t from = 0, to = 0;
            input >> from >> to;
            camera->MovePostProcessEffect(from, to);
        }
        else if (command == "camera_effect_enabled")
        {
            std::size_t index = 0;
            int enabled = 1;
            input >> index >> enabled;
            if (auto *effect = camera->GetPostProcessEffect(index)) effect->SetEnabled(enabled != 0);
        }
        else if (command == "camera_effect_parameter")
        {
            std::size_t effectIndex = 0, parameterIndex = 0;
            std::string value;
            input >> effectIndex >> parameterIndex >> value;
            if (auto *effect = camera->GetPostProcessEffect(effectIndex))
            {
                auto parameters = effect->GetParameters();
                if (parameterIndex < parameters.size())
                {
                    parameters[parameterIndex].value = Decode(value);
                    effect->SetParameters(parameters);
                }
            }
        }
        else if (command == "camera_effect_preset")
        {
            std::string reference;
            input >> reference;
            if (!camera->SetPostProcessPresetAssetReference(Decode(reference)))
            {
                errorMessage = "Could not load camera post-process preset.";
                return false;
            }
        }
        else if (command == "camera_effect_save_preset")
        {
            const auto &reference = camera->GetPostProcessPresetAssetReference();
            if (reference.empty())
            {
                errorMessage = "Set a camera post-process preset reference before saving.";
                return false;
            }
            const auto preset = PlutoGE::assets::CapturePostProcessPreset(camera->GetPostProcessEffects());
            if (!m_engine.GetAssetManager().SavePostProcessPresetAsset(reference, preset, &errorMessage)) return false;
        }
        else
        {
            errorMessage = "Unknown camera post-process command.";
            return false;
        }
    }
    else
    {
        errorMessage = "Unknown editor command: " + command;
        return false;
    }

    CommitEdit(before);
    return true;
}

std::string EditorSession::BuildSnapshotEvent() const
{
    int renderableMeshComponents = 0;
    const int registeredMeshComponents = m_scene ? static_cast<int>(m_scene->GetMeshComponents().size()) : 0;
    if (m_scene)
    {
        for (const auto *mesh : m_scene->GetMeshComponents())
        {
            if (mesh && mesh->IsEnabled() && mesh->IsVisible() && mesh->GetMesh() && mesh->GetMaterial()) ++renderableMeshComponents;
        }
    }
    std::ostringstream output;
    output << "{\"type\":\"editor-state\",\"projectPath\":\"" << JsonEscape(m_projectPath)
           << "\",\"projectName\":\"" << JsonEscape(m_project ? m_project->GetManifest().name : std::string{})
           << "\",\"assetDirectoryPath\":\"" << JsonEscape(m_project ? m_project->GetAssetDirectoryPath().string() : std::string{})
           << "\",\"assets\":[";
    if (m_project)
    {
        const auto &assets = m_project->GetManifest().assetEntries;
        for (std::size_t assetIndex = 0; assetIndex < assets.size(); ++assetIndex)
        {
            if (assetIndex > 0) output << ',';
            output << "{\"reference\":\"" << JsonEscape(assets[assetIndex].reference)
                   << "\",\"size\":" << assets[assetIndex].size
                   << ",\"type\":\"" << PlutoGE::assets::Project::GetAssetTypeName(assets[assetIndex].type) << "\"}";
        }
    }
    output << ']'
           << ",\"scenePath\":\"" << JsonEscape(m_scenePath)
           << "\",\"dirty\":" << (m_dirty ? "true" : "false")
           << ",\"postProcessEffectTypes\":[";
    const auto &registeredEffectTypes = PlutoGE::render::GetRegisteredPostProcessEffectTypes();
    for (std::size_t typeIndex = 0; typeIndex < registeredEffectTypes.size(); ++typeIndex)
    {
        if (typeIndex > 0) output << ',';
        output << '"' << JsonEscape(registeredEffectTypes[typeIndex]) << '"';
    }
    output << ']'
           << ",\"running\":" << (m_engine.IsRuntimeRunning() ? "true" : "false")
           << ",\"selectedEntityId\":" << m_selectedEntityId
           << ",\"canUndo\":" << (!m_undo.empty() ? "true" : "false")
           << ",\"canRedo\":" << (!m_redo.empty() ? "true" : "false")
           << ",\"editorCamera\":{\"position\":[" << m_editorCamera.position.x << ',' << m_editorCamera.position.y << ',' << m_editorCamera.position.z << ']'
           << ",\"yawDegrees\":" << m_editorCamera.yawDegrees
           << ",\"pitchDegrees\":" << m_editorCamera.pitchDegrees
           << ",\"fovY\":" << m_editorCamera.fovY
           << ",\"nearPlane\":" << m_editorCamera.nearPlane
           << ",\"farPlane\":" << m_editorCamera.farPlane
           << ",\"moveSpeed\":" << m_editorCamera.moveSpeed
           << ",\"speedAdjustment\":" << m_editorCamera.speedAdjustment
           << ",\"gridVisible\":" << (m_editorCamera.gridVisible ? "true" : "false")
           << ",\"postProcessEffectCount\":" << m_editorPostProcessEffects.size()
           << ",\"postProcessPresetReference\":\"" << JsonEscape(m_editorPostProcessPresetReference)
           << "\",\"postProcessEffects\":";
    WritePostProcessEffectsJson(output, m_editorPostProcessEffects);
    output << '}'
           << ",\"viewportStats\":{\"submittedRenderCommands\":" << m_submittedRenderCommands
           << ",\"visibleRenderCommands\":" << m_visibleRenderCommands
           << ",\"registeredMeshComponents\":" << registeredMeshComponents
           << ",\"renderableMeshComponents\":" << renderableMeshComponents << '}'
           << ",\"entities\":[";
    std::vector<const PlutoGE::scene::Entity *> entities;
    if (m_scene)
    {
        for (const auto *root : m_scene->GetRootEntities()) if (root) CollectEntities(*root, entities);
    }
    for (std::size_t index = 0; index < entities.size(); ++index)
    {
        if (index > 0) output << ',';
        WriteEntityJson(output, *entities[index]);
    }
    output << "]}";
    return output.str();
}
