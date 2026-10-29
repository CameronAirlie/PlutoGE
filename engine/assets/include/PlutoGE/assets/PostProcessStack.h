#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::assets
{
    class AssetManager;

    // A camera's post-process effects, normally taken from a preset asset. While a preset is assigned, Refresh keeps
    // the effects in step with it, so saving (or previewing) the preset updates every camera that uses it. Without
    // one, the effects are an inline stack the owner manages directly (code-built cameras, older scenes).
    //
    // Destroying effects can release OpenGL objects; owners make the context current before calls that can.
    class PostProcessStack
    {
    public:
        using EffectList = std::vector<std::unique_ptr<render::IPostProcessEffect>>;

        // Assigns a preset and applies it. An empty reference clears the preset and the effects. Returns false,
        // changing nothing, when the preset cannot be loaded.
        bool SetPreset(AssetManager &assets, std::string reference);
        [[nodiscard]] const std::string &GetPresetReference() const { return m_presetReference; }
        [[nodiscard]] bool HasPreset() const { return !m_presetReference.empty(); }

        // True when the assigned preset changed since it was last applied. A map lookup; safe every frame.
        [[nodiscard]] bool IsOutOfDate(const AssetManager &assets) const;
        // Re-applies the assigned preset if it changed. Returns true when the effects changed.
        bool Refresh(AssetManager &assets);

        // The inline effects. Editing them while a preset is assigned lasts until the preset next changes.
        [[nodiscard]] EffectList &GetEffects() { return m_effects; }
        [[nodiscard]] const EffectList &GetEffects() const { return m_effects; }
        // Replaces the effects with an inline stack and forgets the preset.
        void SetInlineEffects(EffectList effects);

    private:
        std::string m_presetReference;
        std::uint64_t m_appliedRevision = 0;
        EffectList m_effects;
    };
}
