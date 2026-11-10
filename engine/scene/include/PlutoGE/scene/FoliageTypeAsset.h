#pragma once

#include "PlutoGE/render/FoliageWind.h"
#include "PlutoGE/render/LodDistanceScale.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
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

        // Rendering.
        float cellSize = 32.0f;
        // Zero inherits the owning foliage component's draw distance.
        float maxDrawDistance = 0.0f;
        // Moves the type's LOD and impostor switches nearer or farther (render/LodDistanceScale.h).
        float lodDistanceScale = render::DefaultLodDistanceScale;

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

        bool operator==(const FoliageTypeAsset &) const = default;
    };

    // Clamps every value to its valid range. Applied on load and whenever an edited asset is applied.
    void SanitizeFoliageTypeAsset(FoliageTypeAsset &asset);

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
