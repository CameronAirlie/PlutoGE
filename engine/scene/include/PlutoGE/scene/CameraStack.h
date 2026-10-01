#pragma once

#include "PlutoGE/render/RenderCommandView.h"
#include "PlutoGE/render/RhiCameraStack.h"
#include "PlutoGE/scene/Entity.h"

#include <span>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    class IPostProcessEffect;
}

namespace PlutoGE::scene
{
    class CameraComponent;
    class CameraTagFilter;
    class Scene;

    // The cameras that produce one frame: a base camera plus the overlay
    // cameras composited over it.
    struct CameraStack
    {
        CameraComponent *base = nullptr;
        // Ascending overlay order; ties keep scene hierarchy order.
        std::vector<CameraComponent *> overlays;

        explicit operator bool() const noexcept { return base != nullptr; }
    };

    // Considers enabled cameras on active entities. The base camera is the main
    // base camera, falling back to the first base camera in hierarchy order.
    // Overlays are ignored when the scene has no base camera.
    [[nodiscard]] CameraStack ResolveCameraStack(const Scene &scene);

    // Narrows a frame's render commands to those a camera's tag filter accepts.
    // Keep one instance per filtered list and reuse it to avoid reallocation.
    class CameraCommandFilter
    {
    public:
        // Returns `commands` itself when the filter accepts everything. Otherwise
        // the view borrows `commands` and this object until the next Apply.
        [[nodiscard]] render::RenderCommandView Apply(const Scene &scene, const CameraTagFilter &filter,
                                                      render::RenderCommandView commands);

    private:
        std::vector<const render::RenderCommand *> m_accepted;
        std::unordered_map<EntityID, bool> m_ownerAcceptance;
    };

    // Translates a stack's overlay cameras into render layers for one frame.
    class CameraOverlayLayerBuilder
    {
    public:
        // The returned layers borrow `commands` and this object until the next Build.
        [[nodiscard]] std::span<const render::CameraOverlayLayer> Build(
            const Scene &scene, std::span<CameraComponent *const> overlays,
            render::RenderCommandView commands, int width, int height);

    private:
        struct LayerStorage
        {
            CameraCommandFilter filter;
            std::vector<render::IPostProcessEffect *> postProcessEffects;
        };
        std::vector<LayerStorage> m_storage;
        std::vector<render::CameraOverlayLayer> m_layers;
    };
}
