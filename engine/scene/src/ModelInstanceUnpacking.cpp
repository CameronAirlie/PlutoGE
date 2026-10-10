#include "PlutoGE/scene/ModelInstanceUnpacking.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include <stdexcept>
#include <charconv>
#include <set>

namespace PlutoGE::scene
{
    bool PrepareStaticModelInstanceUnpacking(const Scene &source, std::uint32_t rootEntityId,
        const std::unordered_map<std::string, std::string> &authoredReferences,
        assets::AssetManager &sharedAssets, std::unique_ptr<Scene> &output, std::string *errorMessage)
    {
        const auto fail = [&](const std::string &message) { if (errorMessage) *errorMessage = message; return false; };
        try
        {
            if (&sharedAssets != &core::Engine::GetInstance().GetAssetManager())
                return fail("Unpacking requires the active project reader and a scene without streamed sections.");
            std::vector<const Entity *> pending(source.GetRootEntities().begin(), source.GetRootEntities().end());
            while (!pending.empty())
            {
                const auto *entity = pending.back(); pending.pop_back();
                if (source.GetSectionOwner(entity->GetID())) return fail("Streamed scenes require section-scoped unpacking.");
                for (const auto *child : entity->GetChildren()) pending.push_back(child);
            }
            const auto found = source.GetStaticModelInstances().find(rootEntityId);
            if (found == source.GetStaticModelInstances().end() || !found->second.resources)
                return fail("Selected entity is not a complete linked model instance.");
            const auto &original = found->second;
            const auto owner = original.state.accepted.layout.sourceAssetId;
            const auto catalog = sharedAssets.GetAssetCatalog();
            if (!catalog || sharedAssets.GetProjectRootDirectory() != original.resources->GetProjectRootDirectory())
                return fail("Unpacked resources belong to another project.");
            for (const auto &object : original.resources->GetAssetCatalog()->GetObjects())
            {
                if (object.identity.assetId != owner || object.identity.localObjectId == 0) continue;
                std::string logical;
                if (!assets::SerializeAssetReference(object.identity, logical, errorMessage)) return false;
                const auto mapped = authoredReferences.find(logical);
                assets::AssetReference identity;
                if (mapped == authoredReferences.end() || !assets::ParseAssetReference(mapped->second, identity) ||
                    identity.assetId == owner || identity.localObjectId != 0) return fail("Unpacked package lacks an authored object mapping.");
                const auto *replacement = catalog->Find(identity);
                if (!replacement || replacement->ownership != assets::AssetOwnership::Authored || replacement->type != object.type ||
                    sharedAssets.ResolveAssetPath(mapped->second).empty()) return fail("Unpacked object mapping differs from the authored catalog.");
            }
            std::string before, verified;
            if (!SceneSerializer::SaveToString(source, before, errorMessage)) return false;
            auto candidate = SceneSerializer::LoadFromString(before, errorMessage);
            if (!candidate || !SceneSerializer::SaveToString(*candidate, verified, errorMessage)) return false;
            if (verified != before) return fail("Unpack preparation could not preserve the complete source scene.");
            candidate->SetFilePath(source.GetFilePath());
            const auto instance = candidate->GetStaticModelInstances().at(rootEntityId);
            for (const auto entityId : instance.state.bindingEntities)
            {
                auto *entity = candidate->FindEntityByID(entityId);
                auto *old = entity ? entity->GetComponent<MeshComponent>() : nullptr;
                if (!old) return fail("Unpack preparation is missing accepted geometry.");
                auto properties = old->Serialize();
                for (auto &property : properties)
                {
                    if (property.name == "ModelAssetId") { property.value.clear(); continue; }
                    if (property.name == "ModelObjectId") { property.value="0"; continue; }
                    if (!property.name.ends_with("Path") && !property.name.ends_with("MaterialAsset") &&
                        property.name != "MeshAssetReference") continue;
                    if (property.value.empty()) continue;
                    const auto logical = instance.resources->PersistAssetPath(property.value);
                    const auto mapped = authoredReferences.find(logical);
                    if (mapped != authoredReferences.end()) property.value=mapped->second;
                    else
                    {
                        assets::AssetReference identity;
                        if (logical.empty() || (assets::ParseAssetReference(logical, identity) && identity.assetId == owner))
                            return fail("Authored override references an unextracted accepted object.");
                        property.value=logical;
                    }
                }
                // Ordinary isolated submesh components use per-submesh overrides.
                // Expand explicit source material-slot edits across matching bindings;
                // existing per-submesh overrides retain their higher precedence.
                const auto indexed = [](const std::string &name, std::string_view prefix, std::size_t &index, std::string_view &field)
                {
                    if (!name.starts_with(prefix)) return false;
                    const auto dot = name.find('.', prefix.size());
                    if (dot == std::string::npos) throw std::runtime_error("Malformed material override property.");
                    const auto parsed = std::from_chars(name.data()+prefix.size(), name.data()+dot, index);
                    if (parsed.ec != std::errc{} || parsed.ptr != name.data()+dot) throw std::runtime_error("Malformed material override index.");
                    field=std::string_view(name).substr(dot+1); return true;
                };
                std::set<std::size_t> overridden;
                for (const auto &property : properties)
                {
                    std::size_t index=0; std::string_view field;
                    if (indexed(property.name, "SubmeshOverrides.", index, field)) overridden.insert(index);
                }
                std::vector<Property> authored;
                for (const auto &property : properties)
                {
                    std::size_t slot=0; std::string_view field;
                    if (!indexed(property.name, "MaterialSlots.", slot, field)) { authored.push_back(property); continue; }
                    for (std::size_t submesh=0; submesh<old->GetMesh()->GetSubmeshCount(); ++submesh)
                        if (old->GetMesh()->GetSubmesh(submesh).materialIndex == slot && !overridden.contains(submesh))
                        {
                            auto converted=property;
                            converted.name="SubmeshOverrides." + std::to_string(submesh) + "." + std::string(field);
                            authored.push_back(std::move(converted));
                        }
                }
                auto replacement = std::make_unique<MeshComponent>(MeshComponentConfig{});
                replacement->Deserialize(authored);
                replacement->SetEnabled(old->IsEnabled());
                if (!replacement->GetMesh() || replacement->GetMesh()->GetSubmeshCount() != instance.state.accepted.submeshCount ||
                    !replacement->GetModelAssetId().empty() || replacement->GetRetainedAssetReader())
                    return fail("Unpacked geometry failed authored resource validation.");
                if (!entity->RemoveComponent(old)) return fail("Cannot replace prepared linked component.");
                entity->AddComponent(replacement.release());
            }
            if (!candidate->DetachStaticModelInstance(rootEntityId, errorMessage) ||
                !SceneSerializer::SaveToString(*candidate, verified, errorMessage)) return false;
            auto roundTrip = SceneSerializer::LoadFromString(verified, errorMessage);
            std::string repeated;
            if (!roundTrip || !SceneSerializer::SaveToString(*roundTrip, repeated, errorMessage) || repeated != verified)
                return fail("Unpacked scene failed authored persistence validation.");
            output=std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &exception) { return fail(std::string("Cannot prepare model unpack: ") + exception.what()); }
    }
}
