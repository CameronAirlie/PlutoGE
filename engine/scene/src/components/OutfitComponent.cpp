#include "PlutoGE/scene/components/OutfitComponent.h"

#include "PlutoGE/scene/AffineTransform.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/WearableComponent.h"
#include "PlutoGE/scripting/ScriptLogging.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr std::string_view kCountProperty = "Wearable Count";
        constexpr std::string_view kItemPrefix = "Wearables.";

        // The character's own skinned mesh: on `entity` or below it, skipping generated content, meshes that follow
        // another skeleton, and characters of their own (an entity below with its own outfit).
        MeshComponent *FindBodyIn(Entity *entity, bool isOutfitOwner)
        {
            if (!entity || entity->IsTransient() || (!isOutfitOwner && entity->GetComponent<OutfitComponent>()))
                return nullptr;
            if (auto *mesh = entity->GetComponent<MeshComponent>();
                mesh && mesh->GetMesh() && mesh->GetMesh()->HasSkeleton() && !mesh->GetFollowParentSkeleton())
                return mesh;
            for (auto *child : entity->GetChildren())
                if (auto *mesh = FindBodyIn(child, false))
                    return mesh;
            return nullptr;
        }

        template <typename Visit>
        void ForEachInTree(Entity &root, const Visit &visit)
        {
            visit(root);
            for (auto *child : root.GetChildren())
                if (child)
                    ForEachInTree(*child, visit);
        }

        // Ordered from best to worst fit.
        bool FitsWorse(const SkinFitReport &candidate, const SkinFitReport &current)
        {
            if (candidate.quality != current.quality)
                return candidate.quality > current.quality;
            return candidate.largestProportionError > current.largestProportionError;
        }

        // A freshly spawned wearable becomes generated content and sits at the body's origin.
        void PrepareSpawned(Entity &root)
        {
            root.SetTransient(true);
            root.AddTag(std::string(OutfitComponent::WornTag));
            root.SetLocalTransform(Transform{});
        }

        // Every mesh of a worn item follows the body's skeleton, whatever its prefab says. Applied on every refresh, so
        // items taken over after an undo, or rebuilt when their prefab changed, follow too.
        void FollowBody(Entity &root)
        {
            ForEachInTree(root, [](Entity &entity)
            {
                if (auto *mesh = entity.GetComponent<MeshComponent>())
                    mesh->SetFollowParentSkeleton(true);
            });
        }

        void Describe(Entity &root, const MeshComponent &body, OutfitComponent::WornItem &item)
        {
            if (const auto *wearable = root.GetComponent<WearableComponent>())
            {
                item.hasWearableComponent = true;
                item.slot = wearable->GetSlot();
                if (wearable->GetHidesCoveredSkin())
                    item.coveredJoints = wearable->GetCoveredJoints();
            }
            const auto &bodySkeleton = body.GetMesh()->GetSkeleton();
            bool first = true;
            ForEachInTree(root, [&](Entity &entity)
            {
                const auto *mesh = entity.GetComponent<MeshComponent>();
                if (!mesh || !mesh->GetMesh() || !mesh->GetMesh()->HasSkeleton())
                    return;
                const auto &skeleton = mesh->GetMesh()->GetSkeleton();
                const auto report = AssessSkinFit(bodySkeleton, skeleton, BuildSkinPoseFollowMap(bodySkeleton, skeleton));
                if (first || FitsWorse(report, item.fit))
                    item.fit = report;
                first = false;
                item.hasSkinnedMesh = true;
            });
        }

        void ReportPoorFit(const Entity &owner, const OutfitComponent::WornItem &item)
        {
            if (!item.hasSkinnedMesh || item.fit.quality == SkinFitQuality::Fits || item.fit.quality == SkinFitQuality::Loose)
                return;
            std::string message = "Outfit on '" + owner.GetName() + "': '" + item.prefab + "' ";
            if (item.fit.quality == SkinFitQuality::Unmatched)
                message += "shares no joints with the body's skeleton, so it cannot follow it.";
            else
                message += "was made for a body of other proportions (bone '" + item.fit.worstJoint + "' differs by " +
                           std::to_string(static_cast<int>(std::lround(item.fit.largestProportionError * 100.0f))) +
                           " cm), so it will not sit on this body.";
            scripting::DispatchScriptLog(scripting::ScriptLogSeverity::Warning, message);
        }
    }

    // ---- the list -----------------------------------------------------------------------------------------------

    void OutfitComponent::SetWearables(std::vector<std::string> prefabs)
    {
        std::erase_if(prefabs, [](const std::string &prefab) { return prefab.empty(); });
        if (prefabs == m_wearables)
            return;
        m_wearables = std::move(prefabs);
        m_dirty = true;
    }

    bool OutfitComponent::Wear(std::string prefab)
    {
        if (prefab.empty() || std::ranges::find(m_wearables, prefab) != m_wearables.end())
            return false;
        m_wearables.push_back(std::move(prefab));
        m_dirty = true;
        return true;
    }

    bool OutfitComponent::TakeOff(std::string_view prefab)
    {
        const auto found = std::ranges::find(m_wearables, prefab);
        if (found == m_wearables.end())
            return false;
        m_wearables.erase(found);
        m_dirty = true;
        return true;
    }

    void OutfitComponent::TakeOffAll()
    {
        if (m_wearables.empty())
            return;
        m_wearables.clear();
        m_dirty = true;
    }

    MeshComponent *OutfitComponent::FindBody() const { return FindBodyIn(GetOwner(), true); }

    // ---- keeping the body dressed ---------------------------------------------------------------------------------

    void OutfitComponent::Update(float)
    {
        if (NeedsRefresh())
            Refresh();
    }

    bool OutfitComponent::NeedsRefresh() const
    {
        if (m_dirty)
            return true;
        const auto *owner = GetOwner();
        const auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene)
            return false;
        const auto *body = FindBody();
        if ((body ? body->GetOwner()->GetID() : 0) != m_bodyEntity || (body ? body->GetMesh() : nullptr) != m_bodyMesh)
            return true;
        // Something removed a worn item (an undo restoring the scene, a script destroying it).
        return std::ranges::any_of(m_items, [&](const WornItem &item)
                                   { return item.instance != 0 && !scene->IsEntityAlive(item.instance); });
    }

    void OutfitComponent::Refresh()
    {
        m_dirty = false;
        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene)
            return;
        auto *body = IsEnabled() ? FindBody() : nullptr;
        auto *bodyEntity = body ? body->GetOwner() : nullptr;
        m_bodyEntity = bodyEntity ? bodyEntity->GetID() : 0;
        m_bodyMesh = body ? body->GetMesh() : nullptr;

        // Items already on this body, by prefab: this outfit's last items, and tagged items it lost track of (its
        // component was rebuilt by an undo). Anything elsewhere is taken off.
        std::unordered_multimap<std::string, EntityID> spawned;
        std::set<EntityID> known;
        const auto keep = [&](const std::string &prefab, EntityID id)
        {
            if (!known.insert(id).second)
                return;
            const auto *entity = scene->IsEntityAlive(id) ? scene->FindEntityByID(id) : nullptr;
            if (entity && bodyEntity && entity->GetParent() == bodyEntity)
                spawned.emplace(prefab, id);
            else if (entity)
                scene->DestroyEntity(id);
        };
        for (const auto &item : m_items)
            if (item.instance)
                keep(item.prefab, item.instance);
        if (bodyEntity)
            for (auto *child : std::vector<Entity *>(bodyEntity->GetChildren()))
                if (child && child->IsTransient() && child->HasTag(std::string(WornTag)))
                    keep(child->GetPrefabSource(), child->GetID());

        std::vector<WornItem> items;
        items.reserve(m_wearables.size());
        for (const auto &prefab : m_wearables)
        {
            WornItem item{.prefab = prefab};
            if (prefab.empty())
            {
                item.state = ItemState::Failed;
                item.error = "No wearable chosen.";
                items.push_back(std::move(item));
                continue;
            }
            if (!body)
            {
                item.state = ItemState::NoBody;
                items.push_back(std::move(item));
                continue;
            }
            Entity *root = nullptr;
            if (const auto found = spawned.find(prefab); found != spawned.end())
            {
                root = scene->FindEntityByID(found->second);
                spawned.erase(found);
            }
            const bool fresh = !root;
            if (fresh)
            {
                std::string error;
                root = Prefab::Instantiate(*scene, prefab, bodyEntity, &error);
                if (!root)
                {
                    item.state = ItemState::Failed;
                    item.error = error.empty() ? "The prefab could not be spawned." : error;
                    items.push_back(std::move(item));
                    continue;
                }
                PrepareSpawned(*root);
            }
            FollowBody(*root);
            item.instance = root->GetID();
            item.state = ItemState::Worn;
            Describe(*root, *body, item);
            if (fresh)
                ReportPoorFit(*owner, item);
            items.push_back(std::move(item));
        }
        for (const auto &[prefab, id] : spawned)
            scene->DestroyEntity(id);
        m_items = std::move(items);

        // One item shows per slot, the last listed; earlier ones are kept, hidden, for when it comes off.
        std::vector<std::string> covered;
        for (std::size_t index = 0; index < m_items.size(); ++index)
        {
            auto &item = m_items[index];
            if (item.state != ItemState::Worn && item.state != ItemState::Covered)
                continue;
            const auto later = item.slot.empty() ? m_items.end()
                : std::find_if(m_items.begin() + static_cast<std::ptrdiff_t>(index) + 1, m_items.end(),
                               [&](const WornItem &other) { return other.instance && other.slot == item.slot; });
            item.state = later == m_items.end() ? ItemState::Worn : ItemState::Covered;
            item.error = later == m_items.end() ? std::string{} : "Covered by '" + later->prefab + "' in slot " + item.slot + ".";
            if (auto *entity = scene->FindEntityByID(item.instance))
                entity->SetActive(item.state == ItemState::Worn);
            if (item.state == ItemState::Worn)
                covered.insert(covered.end(), item.coveredJoints.begin(), item.coveredJoints.end());
        }

        // Skin under worn items: cleared on a body this outfit no longer dresses.
        if (m_coveredBodyEntity != m_bodyEntity)
            UncoverBody(m_coveredBodyEntity);
        m_coveredBodyEntity = m_bodyEntity;
        if (body)
            body->SetCoveredJoints(std::move(covered));
    }

    void OutfitComponent::UncoverBody(EntityID bodyEntity) const
    {
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        auto *entity = scene && bodyEntity ? scene->FindEntityByID(bodyEntity) : nullptr;
        if (auto *mesh = entity ? entity->GetComponent<MeshComponent>() : nullptr)
            mesh->SetCoveredJoints({});
    }

    void OutfitComponent::Despawn()
    {
        if (auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr)
            for (auto &item : m_items)
                if (item.instance && scene->IsEntityAlive(item.instance))
                    scene->DestroyEntity(item.instance);
        for (auto &item : m_items)
            item.instance = 0;
        UncoverBody(m_coveredBodyEntity);
        m_coveredBodyEntity = 0;
        m_dirty = true;
    }

    void OutfitComponent::OnRemoved() { Despawn(); }

    void OutfitComponent::OnEnabledChanged()
    {
        if (!IsEnabled())
            Despawn();
        m_dirty = true;
    }

    // ---- saving ---------------------------------------------------------------------------------------------------

    std::vector<Property> OutfitComponent::Serialize() const
    {
        std::vector<Property> properties{{std::string(kCountProperty), PropertyType::Int, std::to_string(m_wearables.size())}};
        for (std::size_t index = 0; index < m_wearables.size(); ++index)
            properties.push_back({std::string(kItemPrefix) + std::to_string(index), PropertyType::String, m_wearables[index]});
        return properties;
    }

    void OutfitComponent::Deserialize(const std::vector<Property> &properties)
    {
        auto wearables = m_wearables;
        for (const auto &property : properties)
        {
            if (property.name == kCountProperty)
            {
                try { wearables.resize(static_cast<std::size_t>(std::clamp(std::stoi(property.value), 0, 256))); }
                catch (...) {}
            }
            else if (property.name.starts_with(kItemPrefix))
            {
                try
                {
                    const auto index = std::stoul(property.name.substr(kItemPrefix.size()));
                    if (index < 256)
                    {
                        if (index >= wearables.size())
                            wearables.resize(index + 1);
                        wearables[index] = property.value;
                    }
                }
                catch (...) {}
            }
        }
        // Empty entries are kept while a list is edited entry by entry; they are skipped when worn.
        if (wearables != m_wearables)
        {
            m_wearables = std::move(wearables);
                m_dirty = true;
        }
    }
}
