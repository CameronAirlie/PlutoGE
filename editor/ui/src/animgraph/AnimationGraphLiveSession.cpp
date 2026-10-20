#include "PlutoGE/ui/animgraph/AnimationGraphLiveSession.h"

#include "PlutoGE/assets/AnimationGraphUtils.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/ui/EditorShell.h"

namespace PlutoGE::ui::animgraph
{
    namespace
    {
        scene::AnimationComponent *FindUsingGraph(scene::Entity &entity, const std::string &resolvedGraphPath)
        {
            auto &assetManager = core::Engine::GetInstance().GetAssetManager();
            if (auto *animation = entity.GetComponent<scene::AnimationComponent>();
                animation && !animation->GetAnimationGraphAssetReference().empty() &&
                assetManager.ResolveAssetPath(animation->GetAnimationGraphAssetReference()) == resolvedGraphPath)
                return animation;
            for (auto *child : entity.GetChildren())
                if (child)
                    if (auto *found = FindUsingGraph(*child, resolvedGraphPath))
                        return found;
            return nullptr;
        }

        // Runtime states mirror the saved asset; names survive unsaved edits
        // to the document, indices do not.
        int StateIdForRuntimeIndex(const scene::AnimationComponent &animation, const assets::AnimationGraphAsset &graph, int index)
        {
            const auto &states = animation.GetGraphStates();
            if (index < 0 || index >= static_cast<int>(states.size()))
                return 0;
            const auto *state = assets::animgraph::FindStateByName(graph, states[static_cast<std::size_t>(index)].name);
            return state ? state->id : 0;
        }
    }

    void AnimationGraphLiveSession::Refresh(const std::string &graphReference, const assets::AnimationGraphAsset &graph)
    {
        m_component = nullptr;
        m_entityName.clear();
        m_overlay.reset();

        auto *selected = EditorShell::GetInstance().GetSelectedEntity();
        if (!selected || graphReference.empty())
            return;
        const std::string resolvedGraphPath = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(graphReference);
        if (resolvedGraphPath.empty())
            return;
        m_component = FindUsingGraph(*selected, resolvedGraphPath);
        if (!m_component)
            return;

        m_entityName = m_component->GetOwner() ? m_component->GetOwner()->GetName() : std::string("Entity");
        const auto info = m_component->GetGraphPlaybackInfo();
        if (info.currentStateIndex < 0)
            return;
        LivePlaybackOverlay overlay;
        overlay.activeStateId = StateIdForRuntimeIndex(*m_component, graph, info.currentStateIndex);
        overlay.activeStateProgress = info.currentStateNormalizedTime;
        if (info.transitionDestinationStateIndex >= 0)
        {
            overlay.transitionFromStateId = StateIdForRuntimeIndex(*m_component, graph, info.transitionSourceStateIndex);
            overlay.transitionToStateId = StateIdForRuntimeIndex(*m_component, graph, info.transitionDestinationStateIndex);
            overlay.transitionProgress = info.transitionProgress;
        }
        m_overlay = overlay;
    }
}
