#include "PlutoGE/scene/TimelineBindings.h"
#include "PlutoGE/scene/ComponentRegistry.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/render/Camera.h"
#include <charconv>
#include <cmath>
#include <string_view>

namespace PlutoGE::scene
{
    namespace
    {
        void RefreshPresentation(const std::vector<Entity *> &roots)
        {
            for (auto *entity : roots)
            {
                if (auto *light = entity->GetComponent<LightComponent>()) light->Update(0);
                if (auto *camera = entity->GetComponent<CameraComponent>()) camera->Update(0);
                RefreshPresentation(entity->GetChildren());
            }
        }

        // A property track's component and serialized property, by "<Type>.<Property>".
        struct PropertyBinding
        {
            Component *component = nullptr;
            Property property;
        };

        bool ResolveProperty(Scene &scene, const TimelineTrack &track, PropertyBinding &binding)
        {
            auto *entity = scene.FindEntityByID(track.entity);
            const auto dot = track.property.find('.');
            if (!entity || dot == std::string::npos)
                return false;
            const auto *info = ComponentRegistry::Get().Find(std::string_view(track.property).substr(0, dot));
            const auto &buckets = entity->GetComponentBuckets();
            if (!info || info->typeID >= buckets.size() || buckets[info->typeID].empty())
                return false;
            binding.component = buckets[info->typeID].front();
            const std::string name = track.property.substr(dot + 1);
            for (auto &property : binding.component->Serialize())
                if (property.name == name)
                {
                    binding.property = std::move(property);
                    return true;
                }
            return false;
        }

        int PropertyWidth(PropertyType type)
        {
            switch (type)
            {
            case PropertyType::Vec2: return 2;
            case PropertyType::Vec3: return 3;
            case PropertyType::Color:
            case PropertyType::Vec4: return 4;
            case PropertyType::Float:
            case PropertyType::Double:
            case PropertyType::Int:
            case PropertyType::Enum:
            case PropertyType::Bool: return 1;
            default: return 0; // strings and entity references are not animatable
            }
        }

        std::array<float, 4> ParseComponents(const Property &property)
        {
            std::array<float, 4> value{};
            if (property.type == PropertyType::Bool)
            {
                value[0] = property.value == "true" || property.value == "1" ? 1.0f : 0.0f;
                return value;
            }
            std::string_view text = property.value;
            for (int index = 0; index < 4 && !text.empty(); ++index)
            {
                const auto comma = text.find(',');
                const auto token = text.substr(0, comma);
                std::from_chars(token.data(), token.data() + token.size(), value[index]);
                text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
            }
            return value;
        }

        std::string FormatComponents(const Property &property, const std::array<float, 4> &value)
        {
            switch (property.type)
            {
            case PropertyType::Bool: return value[0] > 0.5f ? "true" : "false";
            case PropertyType::Int:
            case PropertyType::Enum: return std::to_string(static_cast<int>(std::lround(value[0])));
            default: break;
            }
            std::string text;
            for (int index = 0; index < PropertyWidth(property.type); ++index)
            {
                char buffer[32];
                const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value[index]);
                if (index) text += ',';
                text.append(buffer, result.ptr);
            }
            return text;
        }
    }

    bool GetTimelinePropertyType(Scene &scene, const TimelineTrack &track, PropertyType &type)
    {
        PropertyBinding binding;
        if (track.channel != TimelineChannel::Property || !ResolveProperty(scene, track, binding))
            return false;
        type = binding.property.type;
        return true;
    }

    int GetTimelinePropertyWidth(Scene &scene, const TimelineTrack &track)
    {
        PropertyBinding binding;
        return track.channel == TimelineChannel::Property && ResolveProperty(scene, track, binding)
                   ? PropertyWidth(binding.property.type)
                   : 0;
    }

    bool ReadTimelineValue(Scene &scene, const TimelineTrack &track, std::array<float, 4> &value)
    {
        if (track.channel == TimelineChannel::Property)
        {
            PropertyBinding binding;
            if (!ResolveProperty(scene, track, binding) || PropertyWidth(binding.property.type) == 0)
                return false;
            value = ParseComponents(binding.property);
            return true;
        }
        auto *entity = scene.FindEntityByID(track.entity);
        if (!entity) return false;
        glm::vec3 v(0);
        switch (track.channel)
        {
        case TimelineChannel::Position: v = entity->GetPosition(); break;
        case TimelineChannel::Rotation: v = entity->GetRotation(); break;
        case TimelineChannel::Scale: v = entity->GetScale(); break;
        case TimelineChannel::CameraFov:
            if (auto *c = entity->GetComponent<CameraComponent>(); c && c->GetCamera()) v.x = c->GetCamera()->GetFOV(); else return false;
            break;
        case TimelineChannel::LightColor:
            if (auto *c = entity->GetComponent<LightComponent>()) v = c->GetLight().color; else return false;
            break;
        case TimelineChannel::LightIntensity:
            if (auto *c = entity->GetComponent<LightComponent>()) v.x = c->GetLight().intensity; else return false;
            break;
        case TimelineChannel::AudioVolume:
            if (auto *c = entity->GetComponent<SoundEmitterComponent>()) v.x = c->GetVolume(); else return false;
            break;
        default: return false;
        }
        value = {v.x, v.y, v.z};
        return true;
    }
    static bool WriteValue(Scene &scene, const TimelineTrack &track, const std::array<float, 4> &value, bool clamp)
    {
        for (auto v : value) if (!std::isfinite(v)) return false;
        if (track.channel == TimelineChannel::Property)
        {
            PropertyBinding binding;
            if (!ResolveProperty(scene, track, binding) || PropertyWidth(binding.property.type) == 0)
                return false;
            binding.property.value = FormatComponents(binding.property, value);
            binding.component->Deserialize({binding.property});
            return true;
        }
        std::array<float, 4> current;
        if (!ReadTimelineValue(scene, track, current)) return false;
        auto *entity = scene.FindEntityByID(track.entity);
        const glm::vec3 v(value[0], value[1], value[2]);
        switch (track.channel)
        {
        case TimelineChannel::Position: entity->SetPosition(v); break;
        case TimelineChannel::Rotation: entity->SetRotation(v); break;
        case TimelineChannel::Scale: entity->SetScale(v); break;
        case TimelineChannel::CameraFov: entity->GetComponent<CameraComponent>()->GetCamera()->SetFOV(clamp ? glm::clamp(v.x, 1.0f, 179.0f) : v.x); break;
        case TimelineChannel::LightColor: entity->GetComponent<LightComponent>()->SetColor(glm::max(v, glm::vec3(0))); break;
        case TimelineChannel::LightIntensity: entity->GetComponent<LightComponent>()->SetIntensity(std::max(0.0f, v.x)); break;
        case TimelineChannel::AudioVolume: entity->GetComponent<SoundEmitterComponent>()->SetVolume(v.x); break;
        default: return false;
        }
        return true;
    }
    bool WriteTimelineValue(Scene &scene, const TimelineTrack &track, const std::array<float, 4> &value)
    {
        return WriteValue(scene, track, value, true);
    }
    ScopedTimelinePose::ScopedTimelinePose(Scene &scene, const TimelinePlayer &player) : m_scene(scene)
    {
        const auto &tracks = player.Data().tracks;
        const auto captureLights = [&](const std::vector<Entity *> &roots, const auto &self) -> void
        {
            for (auto *entity : roots)
            {
                for (auto *component : entity->GetComponents<LightComponent>())
                {
                    const auto &light = component->GetLight();
                    m_lights.push_back({component, {light.position.x, light.position.y, light.position.z}, {light.direction.x, light.direction.y, light.direction.z}});
                }
                self(entity->GetChildren(), self);
            }
        };
        captureLights(scene.GetRootEntities(), captureLights);
        m_saved.reserve(tracks.size());
        // Finish all potentially allocating work before mutating scene values.
        for (const auto &track : tracks)
        {
            std::array<float, 4> value;
            if (!track.keys.empty() && ReadTimelineValue(scene, track, value))
                m_saved.push_back({{track.entity, track.channel, track.interpolation, {}, track.property}, value});
        }
        for (std::size_t i = 0; i < tracks.size(); ++i)
            if (!tracks[i].keys.empty()) WriteTimelineValue(scene, tracks[i], player.Evaluate(i));
        RefreshPresentation(scene.GetRootEntities());
    }
    ScopedTimelinePose::~ScopedTimelinePose()
    {
        for (auto it = m_saved.rbegin(); it != m_saved.rend(); ++it) WriteValue(m_scene, it->binding, it->value, false);
        RefreshPresentation(m_scene.GetRootEntities());
        for (const auto &saved : m_lights)
        {
            auto &light = saved.component->GetLight();
            light.position = {saved.position[0], saved.position[1], saved.position[2]};
            light.direction = {saved.direction[0], saved.direction[1], saved.direction[2]};
            saved.component->MarkDirty(); // Preview shadow caches cannot be reused for the authoring pose.
        }
    }
}
