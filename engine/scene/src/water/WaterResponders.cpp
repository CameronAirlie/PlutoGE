#include "PlutoGE/scene/water/WaterResponders.h"

#include "PlutoGE/core/Engine.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/WaterEffectsComponent.h"
#include "PlutoGE/scene/water/WaterEffectsProfile.h"

#include <memory>

namespace PlutoGE::scene::water
{
    WaterEffectsComponent *FindWaterEffects(Scene &scene, std::uint32_t waterEntity)
    {
        auto *entity = waterEntity ? scene.FindEntityByID(waterEntity) : nullptr;
        auto *effects = entity ? entity->GetComponent<WaterEffectsComponent>() : nullptr;
        return effects && effects->IsEnabled() ? effects : nullptr;
    }

    // ---- ripples ---------------------------------------------------------------------------------------------------

    void WaterRippleResponder::OnWaterInteraction(Scene &scene, const WaterInteractionEvent &event)
    {
        if (!event.channels.ripples)
            return;
        auto *effects = FindWaterEffects(scene, event.waterEntity);
        if (!effects || !effects->GetProfile().ripples.enabled)
            return;
        const glm::mat4 toLocal = glm::inverse(effects->GetOwner()->GetWorldTransform());
        const glm::vec3 local = glm::vec3(toLocal * glm::vec4(event.position, 1.0f));
        effects->GetRipples().Emit({local.x, local.z}, event.intensity);
    }

    // ---- splashes --------------------------------------------------------------------------------------------------

    Entity *WaterSplashResponder::Emitter(Scene &scene, const std::string &particles)
    {
        if (const auto found = m_emitters.find(particles); found != m_emitters.end())
        {
            if (scene.IsEntityAlive(found->second))
                return scene.FindEntityByID(found->second);
            m_emitters.erase(found);
        }
        if (m_failed.contains(particles))
            return nullptr;

        auto component = CreateParticleSystemComponent();
        auto *system = static_cast<ParticleSystemComponent *>(component.get());
        if (!system->SetParticleSystemAssetReference(particles))
        {
            m_failed.insert(particles);
            return nullptr;
        }
        // A silent world-space emitter: it only emits when a splash asks it to, and its particles stay where they
        // were emitted whatever the emitter does.
        system->SetPlayOnAwake(false);
        system->SetLooping(true);
        system->SetEmissionRateOverTime(0.0f);
        system->SetBurstCount(0);
        system->SetSimulationSpace(ParticleSimulationSpace::World);

        auto entity = std::make_unique<Entity>(EntityConfig{.name = "Water Splash Emitter", .tags = {"Runtime"}});
        entity->AddComponent(component.release());
        auto *added = scene.AddEntity(std::move(entity));
        m_emitters[particles] = added->GetID();
        return added;
    }

    void WaterSplashResponder::OnWaterInteraction(Scene &scene, const WaterInteractionEvent &event)
    {
        // Nobody sees particles on a headless server.
        if (!event.channels.splashes || core::Engine::GetInstance().GetConfig().headless)
            return;
        const auto *effects = FindWaterEffects(scene, event.waterEntity);
        if (!effects)
            return;
        const auto choice = ChooseSplash(effects->GetProfile(), event.type, event.intensity);
        if (!choice.particles || choice.count <= 0)
            return;
        if (auto *emitter = Emitter(scene, *choice.particles))
            if (auto *system = emitter->GetComponent<ParticleSystemComponent>())
                system->EmitAt(event.position, choice.count);
    }

    void WaterSplashResponder::OnWaterReset(Scene &scene)
    {
        for (const auto &[particles, entity] : m_emitters)
            scene.DestroyEntity(entity);
        m_emitters.clear();
        m_failed.clear();
    }

    // ---- sounds ----------------------------------------------------------------------------------------------------

    float WaterSoundResponder::NextRandom()
    {
        // xorshift32: cheap, and deterministic for tests.
        m_random ^= m_random << 13;
        m_random ^= m_random >> 17;
        m_random ^= m_random << 5;
        return static_cast<float>(m_random >> 8) * (1.0f / 16777216.0f);
    }

    void WaterSoundResponder::OnWaterInteraction(Scene &scene, const WaterInteractionEvent &event)
    {
        if (!event.channels.sounds)
            return;
        const auto *effects = FindWaterEffects(scene, event.waterEntity);
        if (!effects)
            return;
        const auto &profile = effects->GetProfile();
        if (event.type == WaterInteractionType::Wake)
        {
            const auto last = m_lastWakeSound.find(event.entity);
            if (last != m_lastWakeSound.end() && m_time - last->second < profile.wakeSoundInterval)
                return;
        }
        const auto choice = ChooseSound(profile, event.type, event.intensity, NextRandom());
        if (!choice.clip)
            return;
        const auto &sound = profile.Sound(event.type);
        const auto handle = scene.PlayOneShotSound({
            .clipReference = *choice.clip,
            .position = event.position,
            .volume = choice.volume,
            .pitch = choice.pitch,
            .minDistance = sound.minDistance,
            .maxDistance = sound.maxDistance,
        });
        if (handle && event.type == WaterInteractionType::Wake)
            m_lastWakeSound[event.entity] = m_time;
    }

    void WaterSoundResponder::OnWaterFrame(Scene &, float deltaTime)
    {
        m_time += std::max(deltaTime, 0.0f);
        // Forget bodies that have not made a wake sound for a while.
        std::erase_if(m_lastWakeSound, [&](const auto &entry) { return m_time - entry.second > 30.0; });
    }

    void WaterSoundResponder::OnWaterReset(Scene &)
    {
        m_lastWakeSound.clear();
        m_time = 0.0;
    }
}
