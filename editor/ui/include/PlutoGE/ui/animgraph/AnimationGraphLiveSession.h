#pragma once

#include "PlutoGE/assets/AnimationGraph.h"
#include "PlutoGE/ui/animgraph/StateMachineCanvas.h"

#include <optional>
#include <string>

namespace PlutoGE::scene
{
    class AnimationComponent;
    class Entity;
}

namespace PlutoGE::ui::animgraph
{
    // Connects the editor to an entity playing the graph so the canvas can
    // show the active state and parameters can be tweaked while it runs.
    // The entity is the selected one or its first descendant using the graph.
    class AnimationGraphLiveSession
    {
    public:
        void Refresh(const std::string &graphReference, const assets::AnimationGraphAsset &graph);

        bool IsAttached() const { return m_component != nullptr; }
        scene::AnimationComponent *Component() const { return m_component; }
        const std::string &EntityName() const { return m_entityName; }
        const std::optional<LivePlaybackOverlay> &Overlay() const { return m_overlay; }

    private:
        scene::AnimationComponent *m_component = nullptr;
        std::string m_entityName;
        std::optional<LivePlaybackOverlay> m_overlay;
    };
}
