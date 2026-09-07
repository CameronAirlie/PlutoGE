#include "PlutoGE/scene/components/CameraRigComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <limits>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace PlutoGE::scene
{
    namespace
    {
        bool Finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
        std::string Number(float value)
        {
            std::ostringstream text;
            text.imbue(std::locale::classic());
            text << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
            return text.str();
        }
        std::string Vector(glm::vec3 v) { return Number(v.x) + "," + Number(v.y) + "," + Number(v.z); }
        glm::vec3 ParseVector(std::string text)
        {
            std::replace(text.begin(), text.end(), ',', ' ');
            std::istringstream input(text);
            input.imbue(std::locale::classic());
            glm::vec3 value;
            if (!(input >> value.x >> value.y >> value.z)) throw std::invalid_argument("Invalid camera rig vector");
            return value;
        }
    }

    bool CameraRigComponent::SetSettings(CameraRigSettings settings)
    {
        if (!Finite(settings.offset) || !Finite(settings.focusOffset) || !std::isfinite(settings.yaw) ||
            !std::isfinite(settings.pitch) || !std::isfinite(settings.distance) || !std::isfinite(settings.smoothing) ||
            !std::isfinite(settings.collisionRadius) || !std::isfinite(settings.collisionPadding) ||
            (settings.mode != CameraRigMode::Follow && settings.mode != CameraRigMode::Orbit)) return false;
        settings.offset = glm::clamp(settings.offset, glm::vec3(-100000), glm::vec3(100000));
        settings.focusOffset = glm::clamp(settings.focusOffset, glm::vec3(-100000), glm::vec3(100000));
        settings.yaw = std::remainder(settings.yaw, 360.0f);
        settings.pitch = std::clamp(settings.pitch, -89.0f, 89.0f);
        settings.distance = std::clamp(settings.distance, 0.01f, 100000.0f);
        settings.smoothing = std::clamp(settings.smoothing, 0.0f, 60.0f);
        settings.collisionRadius = std::clamp(settings.collisionRadius, 0.0f, 100.0f);
        settings.collisionPadding = std::clamp(settings.collisionPadding, 0.0f, 10.0f);
        if (settings.target != m_settings.target) ResetRuntime();
        m_settings = settings;
        return true;
    }

    void CameraRigComponent::ResetRuntime()
    {
        m_initialized = false;
        m_blendDuration = m_shakeDuration = 0;
        m_blendTime = m_shakeTime = 0;
    }

    bool CameraRigComponent::BlendTo(std::uint32_t target, float seconds)
    {
        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        auto *entity = scene ? scene->FindEntityByID(target) : nullptr;
        if (!entity || entity == owner || !std::isfinite(seconds) || seconds < 0) return false;
        for (auto *parent = entity->GetParent(); parent; parent = parent->GetParent())
            if (parent == owner) return false;
        const auto blendPosition = owner->GetWorldPosition();
        const auto forward = glm::vec3(owner->GetWorldTransform()[2]);
        if (!Finite(blendPosition) || !Finite(forward) || glm::length(forward) < 0.00001f) return false;
        m_blendPosition = blendPosition;
        m_blendFocus = m_initialized ? m_focus : m_blendPosition -
            glm::normalize(forward) * m_settings.distance;
        m_settings.target = target;
        m_blendDuration = std::min(seconds, 60.0f);
        m_blendTime = 0;
        m_initialized = false;
        return true;
    }

    bool CameraRigComponent::Shake(float amplitude, float seconds, float frequency)
    {
        if (!std::isfinite(amplitude) || !std::isfinite(seconds) || !std::isfinite(frequency) ||
            amplitude < 0 || seconds < 0 || frequency <= 0) return false;
        m_shakeAmplitude = std::min(amplitude, 100.0f);
        m_shakeDuration = std::min(seconds, 60.0f);
        m_shakeFrequency = std::min(frequency, 100.0f);
        m_shakeTime = 0;
        return true;
    }

    void CameraRigComponent::UpdateRig(float deltaTime)
    {
        if (!std::isfinite(deltaTime) || deltaTime <= 0) return;
        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        auto *camera = owner ? owner->GetComponent<CameraComponent>() : nullptr;
        if (!scene || !IsEnabled() || !owner->IsActive() || !camera || !camera->IsEnabled()) { ResetRuntime(); return; }
        auto *target = scene->FindEntityByID(m_settings.target);
        if (!target || !target->IsActive() || target == owner) { ResetRuntime(); return; }
        for (auto *parent = target->GetParent(); parent; parent = parent->GetParent())
            if (parent == owner) { ResetRuntime(); return; } // A camera cannot follow its own descendants.
        if (auto *parent = owner->GetParent())
        {
            const float determinant = glm::determinant(parent->GetWorldTransform());
            if (!std::isfinite(determinant) || std::abs(determinant) < 0.000001f) return;
        }

        glm::vec3 focus = target->GetWorldPosition() + m_settings.focusOffset;
        glm::vec3 offset = m_settings.offset;
        if (m_settings.mode == CameraRigMode::Orbit)
        {
            const float yaw = glm::radians(m_settings.yaw), pitch = glm::radians(m_settings.pitch);
            offset = glm::vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)) * m_settings.distance;
        }
        else if (m_settings.followHeading)
        {
            const auto angles = glm::radians(target->GetWorldRotation());
            offset = glm::mat3(glm::eulerAngleXYZ(angles.x, angles.y, angles.z)) * offset;
        }
        const glm::vec3 desired = focus + offset;
        if (!Finite(focus) || !Finite(desired)) return;
        if (m_blendDuration > 0)
        {
            m_blendTime += deltaTime;
            const float t = std::clamp(static_cast<float>(m_blendTime / m_blendDuration), 0.0f, 1.0f);
            const float weight = t * t * (3 - 2 * t);
            m_position = glm::mix(m_blendPosition, desired, weight);
            m_focus = glm::mix(m_blendFocus, focus, weight);
            if (t >= 1) m_blendDuration = 0;
        }
        else
        {
            const float alpha = !m_initialized || m_settings.smoothing == 0 ? 1.0f : -std::expm1(-deltaTime / m_settings.smoothing);
            m_position = glm::mix(m_position, desired, alpha);
            m_focus = glm::mix(m_focus, focus, alpha);
        }
        m_initialized = true;
        glm::vec3 output = m_position;
        m_shakeTime += deltaTime;
        if (m_shakeTime < m_shakeDuration)
        {
            const double phase = m_shakeTime * m_shakeFrequency * 6.283185307179586;
            const float envelope = m_shakeAmplitude * (1 - static_cast<float>(m_shakeTime / m_shakeDuration));
            output += envelope * glm::vec3(std::sin(phase), std::sin(phase * 1.37), std::sin(phase * 1.79));
        }
        if (m_settings.collisionRadius > 0)
        {
            scene->SynchronizePhysicsQueries();
            PhysicsRaycastHit hit;
            // Sweep the final shaken/blended boom; never smooth back through a wall.
            if (scene->SweepSphere(m_focus, output, m_settings.collisionRadius, hit, target->GetID(), owner->GetID()))
            {
                const auto boom = output - m_focus;
                const float length = glm::length(boom);
                if (length > 0.00001f) output = m_focus + boom / length * std::max(0.0f, hit.distance - m_settings.collisionPadding);
            }
        }
        owner->SetWorldPosition(output);
        if (glm::length(m_focus - output) > 0.0001f)
        {
            const auto direction = glm::normalize(m_focus - output);
            const auto up = std::abs(direction.y) > 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            const auto rotation = glm::inverse(glm::lookAt(output, m_focus, up));
            glm::vec3 angles;
            glm::extractEulerAngleXYZ(rotation, angles.x, angles.y, angles.z);
            owner->SetWorldRotation(glm::degrees(angles));
        }
    }

    std::vector<Property> CameraRigComponent::Serialize() const
    {
        return {{"Target Entity", PropertyType::Entity, std::to_string(m_settings.target)},
                {"Mode", PropertyType::Enum, std::to_string(static_cast<int>(m_settings.mode)), {"Follow", "Orbit"}},
                {"Offset", PropertyType::Vec3, Vector(m_settings.offset)},
                {"Focus Offset", PropertyType::Vec3, Vector(m_settings.focusOffset)},
                {"Follow Heading", PropertyType::Bool, m_settings.followHeading ? "true" : "false"},
                {"Yaw", PropertyType::Float, Number(m_settings.yaw)}, {"Pitch", PropertyType::Float, Number(m_settings.pitch)},
                {"Distance", PropertyType::Float, Number(m_settings.distance)},
                {"Smoothing Seconds", PropertyType::Float, Number(m_settings.smoothing)},
                {"Collision Radius", PropertyType::Float, Number(m_settings.collisionRadius)},
                {"Collision Padding", PropertyType::Float, Number(m_settings.collisionPadding)}};
    }

    void CameraRigComponent::Deserialize(const std::vector<Property> &properties)
    {
        auto settings = m_settings;
        try
        {
            for (const auto &p : properties)
            {
                if (p.name == "Target Entity")
                {
                    const auto id = std::stoull(p.value);
                    if (id > std::numeric_limits<std::uint32_t>::max()) return;
                    settings.target = static_cast<std::uint32_t>(id);
                }
                else if (p.name == "Mode") settings.mode = static_cast<CameraRigMode>(std::stoi(p.value));
                else if (p.name == "Offset") settings.offset = ParseVector(p.value);
                else if (p.name == "Focus Offset") settings.focusOffset = ParseVector(p.value);
                else if (p.name == "Follow Heading") settings.followHeading = p.value == "true" || p.value == "1";
                else if (p.name == "Yaw") settings.yaw = std::stof(p.value);
                else if (p.name == "Pitch") settings.pitch = std::stof(p.value);
                else if (p.name == "Distance") settings.distance = std::stof(p.value);
                else if (p.name == "Smoothing Seconds") settings.smoothing = std::stof(p.value);
                else if (p.name == "Collision Radius") settings.collisionRadius = std::stof(p.value);
                else if (p.name == "Collision Padding") settings.collisionPadding = std::stof(p.value);
            }
            SetSettings(settings);
        }
        catch (const std::exception &) { /* Invalid data must not poison camera transforms. */ }
    }
}
