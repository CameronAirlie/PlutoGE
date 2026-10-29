#include "PlutoGE/assets/PostProcessStack.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/PostProcessPresetAsset.h"

namespace PlutoGE::assets
{
    bool PostProcessStack::SetPreset(AssetManager &assets, std::string reference)
    {
        if (reference.empty())
        {
            m_presetReference.clear();
            m_appliedRevision = 0;
            m_effects.clear();
            return true;
        }
        bool loaded = false;
        const auto preset = assets.LoadPostProcessPresetAsset(reference, &loaded);
        if (!loaded)
            return false;
        // A different preset shares nothing with the current effects worth keeping.
        if (reference != m_presetReference)
            m_effects.clear();
        SyncPostProcessEffects(preset, m_effects);
        m_appliedRevision = assets.GetPostProcessPresetRevision(reference);
        m_presetReference = std::move(reference);
        return true;
    }

    bool PostProcessStack::IsOutOfDate(const AssetManager &assets) const
    {
        return HasPreset() && assets.GetPostProcessPresetRevision(m_presetReference) != m_appliedRevision;
    }

    bool PostProcessStack::Refresh(AssetManager &assets)
    {
        if (!IsOutOfDate(assets))
            return false;
        m_appliedRevision = assets.GetPostProcessPresetRevision(m_presetReference);
        bool loaded = false;
        const auto preset = assets.LoadPostProcessPresetAsset(m_presetReference, &loaded);
        // A preset that became unreadable keeps the last good effects rather than rendering without any.
        return loaded && SyncPostProcessEffects(preset, m_effects);
    }

    void PostProcessStack::SetInlineEffects(EffectList effects)
    {
        m_presetReference.clear();
        m_appliedRevision = 0;
        m_effects = std::move(effects);
    }
}
