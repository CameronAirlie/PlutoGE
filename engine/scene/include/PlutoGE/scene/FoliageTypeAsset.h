#pragma once

#include "PlutoGE/render/FoliageWind.h"
#include "PlutoGE/render/LodDistanceScale.h"
#include "PlutoGE/scene/water/WaterHabitat.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    // Whether a foliage type casts shadows. Inherit follows the owning FoliageComponent's Cast Shadows, so types
    // written before the setting existed keep drawing as they did.
    enum class FoliageShadowMode : std::uint8_t
    {
        Inherit,
        On,
        Off,
    };
    [[nodiscard]] std::string_view FoliageShadowModeName(FoliageShadowMode mode);
    // Unknown names give Inherit.
    [[nodiscard]] FoliageShadowMode ParseFoliageShadowMode(std::string_view name);

    // Everything that defines one kind of plant, independent of where it is placed. A FoliageType owns one of these;
    // with a non-empty `assetReference` the values come from that .plutofoliage file, and otherwise from the scene.
    struct FoliageTypeAsset
    {
        // The .plutofoliage file these values were loaded from; empty for a type defined in the scene. Not stored in
        // the file itself.
        std::string assetReference;

        // Prototype: what an instance draws.
        std::string sourceMeshPath;
        std::string modelAssetId;
        std::uint64_t modelObjectId = 0;
        // Empty uses the mesh's own materials.
        std::string materialAssetReference;
        // Which mesh parts draw. Empty indices and a negative index draw the whole mesh.
        int submeshIndex = -1;
        std::vector<int> submeshIndices;
        bool useGeneratedLods = false;

        // Placement: how the brush creates new instances. Existing instances keep their transforms.
        float minScale = 0.8f;
        float maxScale = 1.2f;
        bool alignToTerrainNormal = false; // False keeps new instances' up axis on world +Y.
        // Where the plant grows relative to lakes, rivers and the sea: the brush and procedural scatter skip
        // positions it rejects (docs/WATER.md). Anywhere by default, so older types grow as before.
        WaterHabitat waterHabitat{};

        // Rendering.
        // Instances are grouped into cells of this size for collision bodies and editing, and by default for drawing.
        float cellSize = 32.0f;
        // Groups instances into square clusters of this size for drawing instead: each cluster is one render command
        // per mesh part, culled (frustum, draw and shadow distance) as a whole on the CPU. Zero uses cellSize. Larger
        // clusters cost the CPU less per frame (GPU instance culling still culls each instance in the camera passes),
        // but whole clusters within a shadow distance cast, so they can cost shadow rendering more. See
        // RenderClusterSize and docs/FOLIAGE.md.
        float renderClusterSize = 0.0f;
        // Zero inherits the owning foliage component's draw distance.
        float maxDrawDistance = 0.0f;
        // Moves the type's LOD and impostor switches nearer or farther (render/LodDistanceScale.h).
        float lodDistanceScale = render::DefaultLodDistanceScale;
        // How far away instances turn into the mesh's baked impostor cards (Distant Cards), relative to where the
        // import placed the switch: 2 keeps the mesh twice as far, 0.5 swaps to cards at half the distance. Only
        // meshes imported with Bake Impostor have cards.
        float impostorDistanceScale = 1.0f;
        FoliageShadowMode shadows = FoliageShadowMode::Inherit;
        // Zero inherits the owning foliage component's shadow distance. Small plants rarely need shadows far away.
        float maxShadowDistance = 0.0f;

        // Collision.
        bool collisionEnabled = false;
        glm::vec3 collisionCenter{0.0f, 1.0f, 0.0f};
        float collisionRadius = 0.35f;
        float collisionHeight = 2.0f;

        // How the type bends in the scene's wind (WindComponent); off by default, so existing types stay still.
        render::FoliageWindResponse wind{};

        // Interaction: the prefab an instance is swapped for when gameplay needs a full entity there (felling a tree,
        // opening a bush). Empty leaves the type as scenery. See FoliageProxies.h.
        std::string proxyPrefabReference;
        // Game-defined labels saying what gameplay does with the type's instances ("Gather", "Mine"). The engine gives
        // them no meaning: scripts find instances by tag (FindFoliageInstances, FoliageQueries.h), so an interaction
        // system can offer a bush to the player without promoting every plant or giving it collision, and can tell it
        // apart from a tree that is only struck. Sanitized: trimmed, no separators (',' or whitespace inside a tag is
        // dropped), no empties or duplicates, matched exactly.
        std::vector<std::string> interactionTags;

        bool operator==(const FoliageTypeAsset &) const = default;
    };

    // Clamps every value to its valid range. Applied on load and whenever an edited asset is applied.
    void SanitizeFoliageTypeAsset(FoliageTypeAsset &asset);

    // Interaction tags (FoliageTypeAsset::interactionTags) as scenes store them: comma-separated.
    [[nodiscard]] std::string JoinFoliageInteractionTags(const std::vector<std::string> &tags);
    // Splits on commas and whitespace and sanitizes, so "Gather, Fibre" and "Gather,Fibre" give the same tags.
    [[nodiscard]] std::vector<std::string> ParseFoliageInteractionTags(std::string_view text);
    [[nodiscard]] bool HasFoliageInteractionTag(const FoliageTypeAsset &asset, std::string_view tag);

    // The size of the clusters a type's instances draw in: its render cluster size, else its cell size.
    [[nodiscard]] inline float RenderClusterSize(const FoliageTypeAsset &asset) noexcept
    {
        return asset.renderClusterSize > 0.0f ? asset.renderClusterSize : (asset.cellSize > 1.0f ? asset.cellSize : 1.0f);
    }

    // The FoliageComponent `Type.N.<name>` properties that differ between two assets, for recording prefab overrides.
    [[nodiscard]] std::vector<std::string_view> ChangedFoliageTypeProperties(const FoliageTypeAsset &before,
                                                                             const FoliageTypeAsset &after);

    // Version 2 stores the whole asset. Version 1 files hold only the rendering, collision and wind policy: reading
    // one keeps `asset`'s prototype and placement values, so a scene type linked to an old file keeps its mesh.
    bool WriteFoliageTypeAsset(std::ostream &output, const FoliageTypeAsset &asset);
    bool ReadFoliageTypeAsset(std::istream &input, FoliageTypeAsset &asset, std::string *errorMessage = nullptr);

    bool SaveFoliageTypeAsset(const std::string &path, const FoliageTypeAsset &asset, std::string *errorMessage = nullptr);
    bool LoadFoliageTypeAsset(const std::string &path, FoliageTypeAsset &asset, std::string *errorMessage = nullptr);
}
