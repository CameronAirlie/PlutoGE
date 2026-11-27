#include "PlutoGE/scene/components/BakedLightingComponent.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/FastTextNumbers.h"
#include "PlutoGE/scene/LightmapFile.h"
#include "PlutoGE/scene/BakedProbeVolume.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace PlutoGE::scene
{
    namespace
    {
        // Revisions are global so a component replaced by another never repeats the previous one's revision.
        std::atomic<std::uint64_t> nextRevision{1};

        // "Lightmap Rects": "<version> <hasMask> <maskDir xyz> <count> {entity submesh scale.xy offset.xy}..."
        // The version also names the lightmap UV layouts the rects were baked against: bakes from another version
        // load as no bake (realtime lighting until rebaked) rather than sampling the wrong texels.
        // 2: stretched built-in shapes use layouts fitted to their scale (MeshComponent::GetLightmapMesh).
        constexpr int kRectTextVersion = 2;
        constexpr std::size_t kMaxRects = 1u << 20;

        std::string EncodeRects(const LightmapBakeOutput &bake)
        {
            // Sorted, so saves are stable across runs (unordered_map order is not).
            std::vector<std::pair<LightmapKey, render::LightmapRect>> rects(bake.rects.begin(), bake.rects.end());
            std::sort(rects.begin(), rects.end(), [](const auto &a, const auto &b) {
                return a.first.entity != b.first.entity ? a.first.entity < b.first.entity : a.first.submesh < b.first.submesh;
            });
            std::string text;
            text.reserve(64 + rects.size() * 48);
            const auto number = [&text](auto value) {
                if (!text.empty()) text.push_back(' ');
                if constexpr (std::is_floating_point_v<decltype(value)>) AppendNumber(text, static_cast<float>(value));
                else AppendNumber(text, static_cast<std::uint64_t>(value));
            };
            number(kRectTextVersion);
            number(bake.hasShadowMask ? 1u : 0u);
            number(bake.shadowMaskDirection.x); number(bake.shadowMaskDirection.y); number(bake.shadowMaskDirection.z);
            number(rects.size());
            for (const auto &[key, rect] : rects)
            {
                number(key.entity); number(key.submesh);
                number(rect.scale.x); number(rect.scale.y); number(rect.offset.x); number(rect.offset.y);
            }
            return text;
        }

        class RectReader
        {
        public:
            explicit RectReader(std::string_view text) : m_text(text) {}
            explicit operator bool() const { return m_good; }
            template <typename T>
            RectReader &operator>>(T &value)
            {
                std::string_view token;
                while (m_good)
                {
                    if (!NextToken(m_text, ' ', token)) { m_good = false; break; }
                    if (token.empty()) continue;
                    if (const auto parsed = ParseNumberToken<T>(token)) value = *parsed;
                    else m_good = false;
                    break;
                }
                return *this;
            }
        private:
            std::string_view m_text;
            bool m_good = true;
        };

        bool DecodeRects(std::string_view text, LightmapBakeOutput &bake)
        {
            RectReader in(text);
            int version = 0;
            int hasMask = 0;
            std::size_t count = 0;
            in >> version >> hasMask >> bake.shadowMaskDirection.x >> bake.shadowMaskDirection.y >> bake.shadowMaskDirection.z >> count;
            if (!in || version != kRectTextVersion || count > kMaxRects)
                return false;
            bake.hasShadowMask = hasMask == 1;
            bake.rects.clear();
            bake.rects.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                LightmapKey key;
                render::LightmapRect rect;
                in >> key.entity >> key.submesh >> rect.scale.x >> rect.scale.y >> rect.offset.x >> rect.offset.y;
                if (!in)
                    return false;
                if (rect.IsValid())
                    bake.rects[key] = rect;
            }
            return true;
        }
    }

    void SanitizeLightmapBakeSettings(LightmapBakeSettings &settings)
    {
        const auto finite = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
        settings.texelsPerUnit = std::clamp(finite(settings.texelsPerUnit, 4.0f), 0.05f, 256.0f);
        settings.maxAtlasSize = std::clamp(settings.maxAtlasSize, 64, 8192);
        settings.padding = std::clamp(settings.padding, 1, 16);
        settings.directShadowSamples = std::clamp(settings.directShadowSamples, 1, 256);
        settings.indirectSamples = std::clamp(settings.indirectSamples, 0, 4096);
        settings.bounces = std::clamp(settings.bounces, 0, 8);
        settings.bounceStrength = std::clamp(finite(settings.bounceStrength, 1.0f), 0.0f, 4.0f);
    }

    void BakedLightingComponent::SetLightingMode(SceneLightingMode mode)
    {
        if (mode == m_mode)
            return;
        m_mode = mode;
        Touch(false);
    }

    void BakedLightingComponent::SetMixedShadows(MixedShadowMode mode)
    {
        if (mode == m_mixedShadows)
            return;
        m_mixedShadows = mode;
        Touch(false);
    }

    void BakedLightingComponent::SetBakeSettings(LightmapBakeSettings settings)
    {
        SanitizeLightmapBakeSettings(settings);
        m_settings = settings;
    }

    bool BakedLightingComponent::HasBake() const
    {
        return m_bake.atlas && m_bake.atlas->IsValid() && !m_bake.rects.empty();
    }

    bool BakedLightingComponent::IsActive() const
    {
        return m_mode == SceneLightingMode::Baked && HasBake() && IsEnabled() && (!GetOwner() || GetOwner()->IsActive());
    }

    render::LightmapRect BakedLightingComponent::FindRect(EntityID entity, std::uint32_t submesh) const
    {
        const auto found = m_bake.rects.find({entity, submesh});
        return found != m_bake.rects.end() ? found->second : render::LightmapRect{};
    }

    void BakedLightingComponent::SetBake(LightmapBakeOutput bake)
    {
        m_bake = std::move(bake);
        Touch(true);
    }

    void BakedLightingComponent::ClearBake()
    {
        SetBake({});
    }

    void BakedLightingComponent::SetAtlasReference(std::string reference)
    {
        if (reference == m_bake.atlasReference)
            return;
        m_bake.atlasReference = std::move(reference);
        // Saved data changes; the render state does not.
        m_bakeRevision = nextRevision.fetch_add(1);
    }

    bool BakedLightingComponent::LoadAtlas(std::string *error)
    {
        if (m_bake.atlas || m_bake.atlasReference.empty())
            return m_bake.atlas != nullptr;
        const auto path = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(m_bake.atlasReference);
        if (path.empty())
        {
            if (error) *error = "Cannot resolve " + m_bake.atlasReference;
            return false;
        }
        auto atlas = ReadLightmapFile(path, error);
        if (!atlas)
            return false;
        m_bake.atlas = std::make_shared<const render::LightmapAtlas>(std::move(*atlas));
        Touch(false);
        return true;
    }

    void BakedLightingComponent::Touch(bool bakeChanged)
    {
        m_revision = nextRevision.fetch_add(1);
        if (bakeChanged)
        {
            m_bakeRevision = m_revision;
            MarkMeshesDirty();
        }
    }

    void BakedLightingComponent::MarkMeshesDirty() const
    {
        const auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene)
            return; // Nothing has built render commands yet.
        const auto visit = [](auto &&self, Entity *entity) -> void {
            if (!entity) return;
            for (auto *mesh : entity->GetComponents<MeshComponent>())
                if (mesh->IsStatic())
                    mesh->NotifyBakedLightingChanged();
            for (auto *child : entity->GetChildren())
                self(self, child);
        };
        for (auto *root : scene->GetRootEntities())
            visit(visit, root);
    }

    std::vector<Property> BakedLightingComponent::SerializeEditableProperties() const
    {
        return {
            {"Lighting Mode", PropertyType::Enum, std::to_string(static_cast<int>(m_mode)), {"Realtime", "Baked"}},
            {"Mixed Shadows", PropertyType::Enum, std::to_string(static_cast<int>(m_mixedShadows)),
             {"All Realtime", "Moving Objects Only (Shadowmask)"}},
            {"Texels Per Unit", PropertyType::Float, std::to_string(m_settings.texelsPerUnit)},
            {"Max Atlas Size", PropertyType::Int, std::to_string(m_settings.maxAtlasSize)},
            {"Chart Padding", PropertyType::Int, std::to_string(m_settings.padding)},
            {"Direct Shadow Samples", PropertyType::Int, std::to_string(m_settings.directShadowSamples)},
            {"Indirect Samples", PropertyType::Int, std::to_string(m_settings.indirectSamples)},
            {"Bounces", PropertyType::Int, std::to_string(m_settings.bounces)},
            {"Bounce Strength", PropertyType::Float, std::to_string(m_settings.bounceStrength)},
            {"Denoise", PropertyType::Bool, m_settings.denoise ? "true" : "false"},
        };
    }

    std::vector<Property> BakedLightingComponent::Serialize() const
    {
        return MaterializeSharedProperties(SerializeShared());
    }

    std::vector<Property> BakedLightingComponent::SerializeShared() const
    {
        auto properties = SerializeEditableProperties();
        if (m_bake.atlasReference.empty() || m_bake.rects.empty())
            return properties;
        properties.push_back({"Lightmap Atlas", PropertyType::String, m_bake.atlasReference});
        Property rects{"Lightmap Rects", PropertyType::String};
        rects.sharedValue = m_rectText.Get(m_bakeRevision, [this] { return EncodeRects(m_bake); });
        properties.push_back(std::move(rects));
        return properties;
    }

    void BakedLightingComponent::Deserialize(const std::vector<Property> &properties)
    {
        auto settings = m_settings;
        std::string atlasReference;
        std::optional<LightmapBakeOutput> bake;
        bool sawLightingMode = false, sawMixedShadows = false;
        for (const auto &property : properties)
        {
            try
            {
                if (property.name == "Lighting Mode")
                {
                    sawLightingMode = true;
                    SetLightingMode(std::stoi(property.value) == 0 ? SceneLightingMode::Realtime : SceneLightingMode::Baked);
                }
                else if (property.name == "Mixed Shadows")
                {
                    sawMixedShadows = true;
                    SetMixedShadows(std::stoi(property.value) == 0 ? MixedShadowMode::Realtime : MixedShadowMode::Shadowmask);
                }
                else if (property.name == "Texels Per Unit") settings.texelsPerUnit = std::stof(property.value);
                else if (property.name == "Max Atlas Size") settings.maxAtlasSize = std::stoi(property.value);
                else if (property.name == "Chart Padding") settings.padding = std::stoi(property.value);
                else if (property.name == "Direct Shadow Samples") settings.directShadowSamples = std::stoi(property.value);
                else if (property.name == "Indirect Samples") settings.indirectSamples = std::stoi(property.value);
                else if (property.name == "Bounces") settings.bounces = std::stoi(property.value);
                else if (property.name == "Bounce Strength") settings.bounceStrength = std::stof(property.value);
                else if (property.name == "Denoise") settings.denoise = property.value == "true" || property.value == "1";
                else if (property.name == "Lightmap Atlas") atlasReference = property.value;
                else if (property.name == "Lightmap Rects")
                {
                    LightmapBakeOutput decoded;
                    if (DecodeRects(property.Text(), decoded))
                        bake = std::move(decoded);
                }
            }
            catch (const std::exception &)
            {
                // Keep the previous value on malformed text.
            }
        }
        // A complete property set without the setting was saved before it existed, when every caster was realtime.
        if (sawLightingMode && !sawMixedShadows)
            SetMixedShadows(MixedShadowMode::Realtime);
        SetBakeSettings(settings);
        if (bake && !atlasReference.empty())
        {
            bake->atlasReference = atlasReference;
            // The same atlas file keeps the pixels already in memory (inspector edits re-deserialize).
            if (atlasReference == m_bake.atlasReference)
                bake->atlas = m_bake.atlas;
            SetBake(std::move(*bake));
            std::string ignored;
            LoadAtlas(&ignored);
        }
    }

    MixedShadowsStatus DescribeMixedShadows(const Scene &scene)
    {
        const auto *baked = scene.GetBakedLighting();
        if (!baked)
            return {false, false, "Mixed lights' static shadows are realtime until lighting is baked."};
        if (baked->GetMixedShadows() == MixedShadowMode::Realtime)
            return {false, false, "Every object casts realtime shadows from Mixed lights. Moving Objects Only is faster: "
                                  "static shadows come from the bake."};
        if (baked->GetLightingMode() != SceneLightingMode::Baked)
            return {false, false, "No effect while Lighting Mode is Realtime."};
        if (!baked->HasBake())
            return {false, true, "Bake lighting (Scene > Lighting > Bake Lighting) to bake static shadows."};

        // The renderer's sun: the first directional light.
        const Light *sun = nullptr;
        for (const auto *light : scene.GetLights())
            if (light && light->type == LightType::Directional)
            {
                sun = light;
                break;
            }
        if (!sun)
            return {false, false, "No directional light: only a Mixed sun's static shadows are baked."};
        if (sun->bakeMode == render::LightBakeMode::Baked)
            return {false, false, "The sun is Baked: all of its light and shadows come from the bake already."};
        if (sun->bakeMode != render::LightBakeMode::Mixed || !sun->castsShadows)
            return {false, true, "Set the sun's Mode to Mixed, with Casts Shadows on, and bake lighting to bake its "
                                 "static shadows."};
        const auto &bake = baked->GetBake();
        if (!bake.hasShadowMask)
            return {false, true, "The bake has no sun shadows (the sun was not Mixed when baked): bake lighting again."};
        if (!render::SameBakedSunDirection(sun->direction, bake.shadowMaskDirection))
            return {false, true, "The sun has turned since the bake, so every object casts realtime shadows until you "
                                 "bake lighting again."};

        bool probesHaveSun = false;
        for (const auto *volume : scene.GetBakedProbeVolumes())
            probesHaveSun |= volume && volume->HasSunVisibility() &&
                             render::SameBakedSunDirection(sun->direction, volume->sunVisibilityDirection);
        if (!probesHaveSun)
            return {true, true, "Static shadows are baked; only moving objects cast realtime shadows. Moving objects and "
                                "surfaces without lightmaps are not shadowed by static geometry yet: bake lighting again "
                                "so the ambient volume records the sun."};
        return {true, false, "Static shadows are baked; only moving objects cast realtime shadows. Moving objects take "
                             "static shadows from the ambient volume."};
    }
}
