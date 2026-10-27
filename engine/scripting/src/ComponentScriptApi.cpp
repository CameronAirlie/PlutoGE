#include "ComponentScriptApi.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/ComponentRegistry.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/UISystem.h"

#include <algorithm>
#include <memory>
#include <string>

namespace PlutoGE::scripting
{
    namespace
    {
        scene::Scene *ActiveScene() { return core::Engine::GetInstance().GetScene(); }

        scene::Entity *FindEntity(std::uint32_t entity)
        {
            auto *scene = ActiveScene();
            return scene && entity != 0 ? scene->FindEntityByID(entity) : nullptr;
        }

        scene::Component *FindComponent(scene::Entity &entity, const scene::ComponentTypeInfo &info)
        {
            const auto &buckets = entity.GetComponentBuckets();
            if (info.typeID >= buckets.size() || buckets[info.typeID].empty())
                return nullptr;
            return buckets[info.typeID].front();
        }

        bool GetProperty(const scene::Component &component, std::string_view name, std::string &value)
        {
            if (const auto *runtime = dynamic_cast<const scene::IUIRuntimeProperties *>(&component);
                runtime && runtime->GetRuntimeProperty(name, value))
                return true;
            for (const auto &property : component.Serialize())
                if (property.name == name)
                {
                    value = property.value;
                    return true;
                }
            return false;
        }

        bool SetProperty(scene::Component &component, std::string_view name, std::string_view value)
        {
            if (auto *runtime = dynamic_cast<scene::IUIRuntimeProperties *>(&component);
                runtime && runtime->SetRuntimeProperty(name, value))
                return true;
            // Only names the component serializes; others would be ignored silently.
            const auto properties = component.Serialize();
            const auto found = std::find_if(properties.begin(), properties.end(),
                                            [&](const scene::Property &property) { return property.name == name; });
            if (found == properties.end())
                return false;
            scene::Property update = *found;
            update.value = std::string(value);
            component.Deserialize({update});
            return true;
        }

        void Handle(NativeComponentRequest &request)
        {
            thread_local std::string text;
            request.result = 0;
            request.resultEntity = 0;
            request.resultText = nullptr;
            const auto operation = static_cast<ComponentScriptOperation>(request.operation);

            auto *scene = ActiveScene();
            if (!scene)
                return;
            auto &ui = scene->GetUISystem();
            switch (operation)
            {
            case ComponentScriptOperation::GetUIFocus:
                request.resultEntity = ui.GetFocusedEntity();
                return;
            case ComponentScriptOperation::SetUIFocus:
                ui.SetSelected(request.entity);
                request.result = 1;
                return;
            case ComponentScriptOperation::IsPointerOverUI:
                request.result = ui.IsPointerOverUI() ? 1 : 0;
                return;
            case ComponentScriptOperation::IsKeyboardCaptured:
                request.result = ui.IsKeyboardCaptured() ? 1 : 0;
                return;
            case ComponentScriptOperation::PollUIEvents:
            {
                const auto &events = ui.GetFrameEvents();
                const auto count = std::min<std::size_t>(events.size(), static_cast<std::size_t>(std::max(request.eventCapacity, 0)));
                for (std::size_t index = 0; index < count && request.events; ++index)
                    request.events[index] = {static_cast<std::int32_t>(events[index].type), events[index].entity};
                request.result = static_cast<std::int32_t>(count);
                request.sequence = ui.GetUpdateSequence();
                return;
            }
            case ComponentScriptOperation::CreateEntity:
            {
                // A missing parent fails rather than creating a stray root.
                auto *parent = request.entity != 0 ? FindEntity(request.entity) : nullptr;
                if (request.entity != 0 && !parent)
                    return;
                auto *created = scene->AddEntity(
                    std::make_unique<scene::Entity>(scene::EntityConfig{.name = request.value ? request.value : ""}), parent);
                request.resultEntity = created ? created->GetID() : 0;
                request.result = created ? 1 : 0;
                return;
            }
            case ComponentScriptOperation::GetParent:
                if (auto *child = FindEntity(request.entity); child && child->GetParent())
                {
                    request.resultEntity = child->GetParent()->GetID();
                    request.result = 1;
                }
                return;
            case ComponentScriptOperation::SetParent:
            {
                auto *child = FindEntity(request.entity);
                auto *parent = request.argumentEntity != 0 ? FindEntity(request.argumentEntity) : nullptr;
                if (!child || (request.argumentEntity != 0 && !parent) || (parent && parent->IsDescendantOf(child)) ||
                    parent == child)
                    return;
                if (child->GetParent() != parent)
                    child->SetParent(parent);
                if (request.argument >= 0)
                    child->SetSiblingIndex(static_cast<std::size_t>(request.argument));
                request.result = 1;
                return;
            }
            case ComponentScriptOperation::GetChildren:
            {
                const auto *parent = FindEntity(request.entity);
                if (!parent)
                    return;
                const auto &children = parent->GetChildren();
                const auto count = std::min<std::size_t>(children.size(), static_cast<std::size_t>(std::max(request.entityCapacity, 0)));
                for (std::size_t index = 0; index < count && request.entities; ++index)
                    request.entities[index] = children[index]->GetID();
                request.result = static_cast<std::int32_t>(children.size());
                return;
            }
            default:
                break;
            }

            auto *entity = FindEntity(request.entity);
            const auto &registry = scene::ComponentRegistry::Get();
            const auto *info = request.typeName ? registry.Find(std::string_view(request.typeName)) : nullptr;
            if (!entity || !info)
                return;
            auto *component = FindComponent(*entity, *info);
            switch (operation)
            {
            case ComponentScriptOperation::HasComponent:
                request.result = component ? 1 : 0;
                break;
            case ComponentScriptOperation::AddComponent:
                request.result = component || registry.AddToEntity(*entity, *info) ? 1 : 0;
                break;
            case ComponentScriptOperation::RemoveComponent:
                request.result = component && entity->RemoveComponent(component) ? 1 : 0;
                break;
            case ComponentScriptOperation::GetProperty:
                if (component && request.property && GetProperty(*component, request.property, text))
                {
                    request.resultText = text.c_str();
                    request.result = 1;
                }
                break;
            case ComponentScriptOperation::SetProperty:
                request.result = component && request.property &&
                                 SetProperty(*component, request.property, request.value ? request.value : "") ? 1 : 0;
                break;
            default:
                break;
            }
        }
    }

    void PLUTO_COMPONENT_API_CALL ControlComponents(NativeComponentRequest *request)
    {
        if (request)
            Handle(*request);
    }
}
