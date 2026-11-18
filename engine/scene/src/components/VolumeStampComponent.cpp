#include "PlutoGE/scene/components/VolumeStampComponent.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    namespace
    {
        // Every stamp alive, so volumes find a scene's stamps without walking it each frame (main thread only).
        std::vector<VolumeStampComponent *> &Stamps()
        {
            static std::vector<VolumeStampComponent *> stamps;
            return stamps;
        }

        float ParseFloat(const std::string &text, float fallback)
        {
            try { return std::stof(text); } catch (...) { return fallback; }
        }

        int ParseInt(const std::string &text, int fallback)
        {
            try { return std::stoi(text); } catch (...) { return fallback; }
        }

        glm::vec3 ParseVector(const std::string &text, glm::vec3 fallback)
        {
            glm::vec3 value;
            std::size_t at = 0;
            for (int axis = 0; axis < 3; ++axis)
            {
                try
                {
                    std::size_t used = 0;
                    value[axis] = std::stof(text.substr(at), &used);
                    at += used;
                }
                catch (...)
                {
                    return fallback;
                }
                while (at < text.size() && (text[at] == ',' || text[at] == ' '))
                    ++at;
            }
            return value;
        }

        std::string VectorText(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }
    }

    VolumeStampComponent::VolumeStampComponent()
    {
        Stamps().push_back(this);
    }

    VolumeStampComponent::~VolumeStampComponent()
    {
        auto &stamps = Stamps();
        stamps.erase(std::remove(stamps.begin(), stamps.end(), this), stamps.end());
    }

    void VolumeStampComponent::SetLayer(int layer) { m_layer = std::clamp(layer, 0, 255); }
    void VolumeStampComponent::SetRadius(float radius) { m_radius = std::max(radius, 0.01f); }
    void VolumeStampComponent::SetHalfExtents(const glm::vec3 &halfExtents) { m_halfExtents = glm::max(halfExtents, glm::vec3(0.01f)); }
    void VolumeStampComponent::SetLength(float length) { m_length = std::max(length, 0.0f); }

    std::vector<Property> VolumeStampComponent::Serialize() const
    {
        return {
            {"Shape", PropertyType::Enum, std::to_string(static_cast<int>(m_shape)), {"Sphere", "Box", "Capsule"}},
            {"Mode", PropertyType::Enum, std::to_string(static_cast<int>(m_mode)), {"Carve", "Fill", "Paint"}},
            {"Layer", PropertyType::Int, std::to_string(m_layer)},
            {"Radius", PropertyType::Float, std::to_string(m_radius)},
            {"HalfExtents", PropertyType::Vec3, VectorText(m_halfExtents)},
            {"Length", PropertyType::Float, std::to_string(m_length)},
            {"Order", PropertyType::Int, std::to_string(m_order)},
        };
    }

    void VolumeStampComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            const auto &text = property.Text();
            if (property.name == "Shape")
                m_shape = static_cast<Shape>(std::clamp(ParseInt(text, static_cast<int>(m_shape)), 0, 2));
            else if (property.name == "Mode")
                m_mode = static_cast<volume::VolumeEditMode>(std::clamp(ParseInt(text, static_cast<int>(m_mode)), 0, 2));
            else if (property.name == "Layer")
                SetLayer(ParseInt(text, m_layer));
            else if (property.name == "Radius")
                SetRadius(ParseFloat(text, m_radius));
            else if (property.name == "HalfExtents")
                SetHalfExtents(ParseVector(text, m_halfExtents));
            else if (property.name == "Length")
                SetLength(ParseFloat(text, m_length));
            else if (property.name == "Order")
                m_order = ParseInt(text, m_order);
        }
    }

    std::shared_ptr<const volume::VolumeShape> VolumeStampComponent::MakeShape(Shape shape, float radius, const glm::vec3 &halfExtents,
                                                                               float length)
    {
        switch (shape)
        {
        case Shape::Sphere:
            return std::make_shared<volume::SphereShape>(glm::vec3(0.0f), radius);
        case Shape::Box:
            return std::make_shared<volume::BoxShape>(glm::vec3(0.0f), halfExtents);
        case Shape::Capsule:
            return std::make_shared<volume::CapsuleShape>(glm::vec3(0.0f, -length * 0.5f, 0.0f), glm::vec3(0.0f, length * 0.5f, 0.0f), radius);
        }
        return nullptr;
    }

    std::shared_ptr<const volume::VolumeShape> VolumeStampComponent::MakeLocalShape() const
    {
        return MakeShape(m_shape, m_radius, m_halfExtents, m_length);
    }

    bool VolumeStampComponent::IsLive() const
    {
        const auto *owner = GetOwner();
        return IsEnabled() && owner && owner->IsActiveInHierarchy();
    }

    void VolumeStampComponent::ForEach(const Scene &scene, const std::function<void(const VolumeStampComponent &)> &visit)
    {
        std::vector<const VolumeStampComponent *> live;
        for (const auto *stamp : Stamps())
            if (stamp->GetOwner() && stamp->GetOwner()->GetScene() == &scene && stamp->IsLive())
                live.push_back(stamp);
        std::sort(live.begin(), live.end(), [](const VolumeStampComponent *a, const VolumeStampComponent *b) {
            return a->m_order != b->m_order ? a->m_order < b->m_order : a->GetOwner()->GetID() < b->GetOwner()->GetID();
        });
        for (const auto *stamp : live)
            visit(*stamp);
    }
}
