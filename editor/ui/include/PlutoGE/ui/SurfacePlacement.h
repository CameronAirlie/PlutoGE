#pragma once
#include "PlutoGE/ui/GroundPlacement.h"
#include "PlutoGE/ui/ViewportPicking.h"
#include "PlutoGE/render/RenderCommand.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::assets { class AssetManager; class Project; }
namespace PlutoGE::ui
{
    struct PlacementSurfaceHit
    {
        glm::vec3 point{0};
        glm::vec3 normal{0, 1, 0};
        scene::EntityID entityId = 0;
    };

    // Visible rigid mesh/terrain queries do not require gameplay colliders.
    // Collidable geometry also participates; the nearest exact surface wins.
    std::optional<PlacementSurfaceHit> RaycastPlacementSurface(
        scene::Scene &scene, const ViewportPickRay &ray, float maxDistance = 10000);

    // Editor-owned prototype and transient render commands. No active scene
    // objects are allocated until Stamp, which the caller wraps in one edit.
    class SurfacePlacementSession
    {
    public:
        SurfacePlacementSession();
        ~SurfacePlacementSession();
        SurfacePlacementSession(const SurfacePlacementSession &) = delete;
        SurfacePlacementSession &operator=(const SurfacePlacementSession &) = delete;
        bool Begin(std::string reference, assets::AssetManager &assets, std::string &error, const assets::Project *project = nullptr);
        void Cancel();
        bool IsActive() const;
        const std::string &GetReference() const;
        const scene::Entity *GetPrototype() const;
        glm::vec3 GetPreviewSize() const;
        bool Update(const PlacementSurfaceHit &hit, const scene::Entity *parent,
                    const SurfacePlacementOptions &options, std::string &error);
        void InvalidatePose();
        const std::optional<scene::Transform> &GetPose() const;
        const std::vector<render::RenderCommand> &GetRenderCommands() const;
        scene::Entity *Stamp(scene::Scene &destination, scene::Entity *parent, std::string &error);

    private:
        struct State;
        std::unique_ptr<State> m_state;
    };
}
