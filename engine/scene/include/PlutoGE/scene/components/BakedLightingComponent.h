#pragma once

#include "PlutoGE/render/BakedLighting.h"
#include "PlutoGE/scene/HeavyPropertyCache.h"
#include "PlutoGE/scene/components/Component.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace PlutoGE::scene
{
    using EntityID = std::uint32_t;

    // Whether static geometry uses baked lighting at runtime. Serialized by value: append new modes only.
    enum class SceneLightingMode : std::uint8_t
    {
        // Every light is realtime; lightmaps are ignored (dynamic GI such as DDGI or SSGI supplies bounce light).
        Realtime,
        // Static geometry samples its lightmap; lights follow their Mode (Realtime, Mixed or Baked).
        Baked,
    };

    // How the lightmap bake samples. Saved with the scene, so every bake of it is reproducible.
    struct LightmapBakeSettings
    {
        float texelsPerUnit = 4.0f;  // lightmap texels per metre before the atlas budget scales them down
        int maxAtlasSize = 2048;     // the atlas' width and height limit, in texels
        int padding = 2;             // empty texels around every chart, so bilinear reads never bleed
        int directShadowSamples = 16; // shadow rays per texel for soft (sun-softness) shadows
        int indirectSamples = 128;   // gather rays per texel
        int bounces = 2;             // diffuse bounces (1: light reflected once off a surface)
        float bounceStrength = 1.0f; // scales every bounce; 1 is physically based
        bool denoise = true;         // edge-aware filtering of the gathered light

        bool operator==(const LightmapBakeSettings &) const = default;
    };
    void SanitizeLightmapBakeSettings(LightmapBakeSettings &settings);

    // A draw's identity in a bake: its entity and submesh.
    struct LightmapKey
    {
        EntityID entity = 0;
        std::uint32_t submesh = 0;
        bool operator==(const LightmapKey &) const = default;
    };
    struct LightmapKeyHash
    {
        std::size_t operator()(const LightmapKey &key) const noexcept
        {
            return std::hash<std::uint64_t>{}((std::uint64_t(key.entity) << 32) ^ key.submesh);
        }
    };

    // Everything one lightmap bake produced.
    struct LightmapBakeOutput
    {
        std::shared_ptr<const render::LightmapAtlas> atlas;
        // The saved atlas (a project:// reference); empty until the editor or a tool writes it.
        std::string atlasReference;
        std::unordered_map<LightmapKey, render::LightmapRect, LightmapKeyHash> rects;
        // The directional light whose static shadows the atlas' alpha holds, and its direction (towards the scene)
        // at bake time; zero when no Mixed directional light took part.
        glm::vec3 shadowMaskDirection{0.0f};
        bool hasShadowMask = false;
    };

    // The scene's baked lighting: the runtime lighting mode, the lightmap bake settings and the last bake's output
    // (the atlas, saved beside the scene's assets, and where each static draw's texels are). One per scene; the
    // first enabled, active one counts. Meshes look their rect up when they rebuild render commands, and a new bake
    // marks them for rebuilding. See docs/BAKED_LIGHTING.md.
    class BakedLightingComponent : public TypedComponent<BakedLightingComponent>
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }

        std::vector<Property> Serialize() const override;
        std::vector<Property> SerializeEditableProperties() const;
        std::vector<Property> SerializeForInspector() const override { return SerializeEditableProperties(); }
        std::vector<Property> SerializeShared() const override;
        void Deserialize(const std::vector<Property> &properties) override;
        // A missing "Lightmap Rects" keeps the current bake.
        void DeserializeKeepingHeavyData(const std::vector<Property> &properties) override { Deserialize(properties); }
        std::uint64_t GetHeavyDataRevision() const override { return m_bakeRevision; }

        SceneLightingMode GetLightingMode() const { return m_mode; }
        void SetLightingMode(SceneLightingMode mode);
        const LightmapBakeSettings &GetBakeSettings() const { return m_settings; }
        void SetBakeSettings(LightmapBakeSettings settings);

        // A bake is loaded: an atlas and at least one rect.
        bool HasBake() const;
        // Baked lighting applies: the mode is Baked, a bake is loaded and the component is enabled and active.
        bool IsActive() const;
        const LightmapBakeOutput &GetBake() const { return m_bake; }
        // The draw's rect, or an invalid one when it was not baked.
        render::LightmapRect FindRect(EntityID entity, std::uint32_t submesh) const;
        // Replaces the bake and marks every mesh in the scene to rebuild its render commands.
        void SetBake(LightmapBakeOutput bake);
        void ClearBake();
        // Names the file the bake's atlas was saved to (LightmapFile.h), so the scene saves the bake and loads it again.
        // The atlas and rects in memory are unchanged.
        void SetAtlasReference(std::string reference);
        // Loads the atlas named by the bake's reference when it is not in memory (scene load). Returns false and
        // leaves the bake without an atlas when the file is missing or unreadable.
        bool LoadAtlas(std::string *error = nullptr);

        // Changes with the mode, the bake and the atlas; the renderer re-uploads the atlas when it changes.
        std::uint64_t GetRevision() const { return m_revision; }

    private:
        void Touch(bool bakeChanged);
        void MarkMeshesDirty() const;

        SceneLightingMode m_mode = SceneLightingMode::Baked;
        LightmapBakeSettings m_settings;
        LightmapBakeOutput m_bake;
        std::uint64_t m_revision = 0;
        std::uint64_t m_bakeRevision = 0;
        HeavyPropertyCache m_rectText;
    };
}
