#include "PlutoGE/scene/CameraStack.h"

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/scene/CameraTagFilter.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/CameraComponent.h"

#include <algorithm>

namespace PlutoGE::scene
{
    namespace
    {
        void CollectCameras(Entity *entity, std::vector<CameraComponent *> &cameras)
        {
            if (!entity)
                return;
            if (auto *camera = entity->GetComponent<CameraComponent>();
                camera && entity->IsActive() && camera->IsEnabled() && camera->GetCamera())
                cameras.push_back(camera);
            for (auto *child : entity->GetChildren())
                CollectCameras(child, cameras);
        }

        // Overlays have no background, so effects that meter or reproject the
        // whole frame would respond to empty pixels rather than the scene.
        bool SupportsOverlay(const render::IPostProcessEffect &effect)
        {
            const auto type = effect.GetTypeName();
            return type != "AutoExposure" && type != "TAA" && type != "MotionBlur";
        }
    }

    CameraStack ResolveCameraStack(const Scene &scene)
    {
        std::vector<CameraComponent *> cameras;
        for (auto *root : scene.GetRootEntities())
            CollectCameras(root, cameras);

        CameraStack stack;
        for (auto *camera : cameras)
        {
            if (camera->IsOverlay())
                stack.overlays.push_back(camera);
            else if (!stack.base || (camera->IsMainCamera() && !stack.base->IsMainCamera()))
                stack.base = camera;
        }
        if (!stack.base)
        {
            stack.overlays.clear();
            return stack;
        }
        std::ranges::stable_sort(stack.overlays, {}, &CameraComponent::GetOverlayOrder);
        return stack;
    }

    render::RenderCommandView CameraCommandFilter::Apply(const Scene &scene, const CameraTagFilter &filter,
                                                         render::RenderCommandView commands)
    {
        if (filter.AcceptsEverything())
            return commands;
        m_accepted.clear();
        m_ownerAcceptance.clear();
        for (const auto &command : commands)
        {
            // Many commands share an owner; resolve each entity's tags once.
            auto [acceptance, inserted] = m_ownerAcceptance.try_emplace(command.ownerEntity, false);
            if (inserted)
                acceptance->second = filter.Accepts(command.ownerEntity ? scene.FindEntityByID(command.ownerEntity) : nullptr);
            if (acceptance->second)
                m_accepted.push_back(&command);
        }
        return render::RenderCommandView(m_accepted);
    }

    std::span<const render::CameraOverlayLayer> CameraOverlayLayerBuilder::Build(
        const Scene &scene, std::span<CameraComponent *const> overlays,
        render::RenderCommandView commands, int width, int height)
    {
        // Size storage before taking views into it.
        m_storage.resize(overlays.size());
        m_layers.clear();
        m_layers.reserve(overlays.size());
        for (std::size_t index = 0; index < overlays.size(); ++index)
        {
            const auto &camera = *overlays[index];
            auto &storage = m_storage[index];
            storage.postProcessEffects.clear();
            for (const auto &effect : camera.GetPostProcessEffects())
                if (effect && SupportsOverlay(*effect))
                    storage.postProcessEffects.push_back(effect.get());
            m_layers.push_back({
                .cameraData = camera.GetCameraData(width, height),
                .commands = storage.filter.Apply(scene, camera.GetTagFilter(), commands),
                .postProcessEffects = storage.postProcessEffects,
            });
        }
        return m_layers;
    }
}
