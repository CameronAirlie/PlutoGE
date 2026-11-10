#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/scene/FoliageTypeAsset.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <ostream>
#include <sstream>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr std::string_view kHeaderPrefix = "PLUTOFOLIAGE\t";
        constexpr int kCurrentVersion = 2;

        void SetError(std::string *error, std::string value) { if (error) *error = std::move(value); }

        float Finite(float value, float fallback) { return std::isfinite(value) ? value : fallback; }

        // A reference holds no tabs or line breaks, so a record's value is the rest of its line.
        std::string RestOfLine(std::stringstream &stream)
        {
            std::string value;
            std::getline(stream, value);
            if (!value.empty() && value.back() == '\r') value.pop_back();
            return value;
        }

        std::vector<int> ParseIndices(std::stringstream &stream)
        {
            std::vector<int> indices;
            int value = 0;
            while (stream >> value) indices.push_back(value);
            return indices;
        }

        // Prototype and placement records only exist from version 2.
        void CopyPrototypeAndPlacement(const FoliageTypeAsset &from, FoliageTypeAsset &to)
        {
            to.sourceMeshPath = from.sourceMeshPath;
            to.modelAssetId = from.modelAssetId;
            to.modelObjectId = from.modelObjectId;
            to.materialAssetReference = from.materialAssetReference;
            to.submeshIndex = from.submeshIndex;
            to.submeshIndices = from.submeshIndices;
            to.useGeneratedLods = from.useGeneratedLods;
            to.minScale = from.minScale;
            to.maxScale = from.maxScale;
            to.alignToTerrainNormal = from.alignToTerrainNormal;
        }
    }

    void SanitizeFoliageTypeAsset(FoliageTypeAsset &asset)
    {
        const float minScale = (std::max)(Finite(asset.minScale, 1.0f), 0.0001f);
        const float maxScale = (std::max)(Finite(asset.maxScale, 1.0f), 0.0001f);
        asset.minScale = (std::min)(minScale, maxScale);
        asset.maxScale = (std::max)(minScale, maxScale);
        asset.cellSize = (std::max)(Finite(asset.cellSize, 32.0f), 1.0f);
        asset.maxDrawDistance = (std::max)(Finite(asset.maxDrawDistance, 0.0f), 0.0f);
        asset.lodDistanceScale = render::SanitizeLodDistanceScale(asset.lodDistanceScale);
        asset.collisionRadius = (std::max)(Finite(asset.collisionRadius, 0.35f), 0.01f);
        asset.collisionHeight = (std::max)(Finite(asset.collisionHeight, 2.0f), asset.collisionRadius * 2.0f);
        asset.wind = asset.wind.Sanitized();
    }

    std::vector<std::string_view> ChangedFoliageTypeProperties(const FoliageTypeAsset &before, const FoliageTypeAsset &after)
    {
        std::vector<std::string_view> changed;
        const auto add = [&](bool differs, std::initializer_list<std::string_view> names)
        {
            if (differs) changed.insert(changed.end(), names);
        };
        add(before.assetReference != after.assetReference, {"AssetReference"});
        add(before.sourceMeshPath != after.sourceMeshPath || before.modelAssetId != after.modelAssetId ||
                before.modelObjectId != after.modelObjectId,
            {"SourceMesh", "ModelAssetId", "ModelObjectId"});
        add(before.materialAssetReference != after.materialAssetReference, {"MaterialAsset"});
        add(before.submeshIndex != after.submeshIndex || before.submeshIndices != after.submeshIndices,
            {"SubmeshIndex", "SubmeshIndices"});
        add(before.useGeneratedLods != after.useGeneratedLods, {"UseGeneratedLods"});
        add(before.minScale != after.minScale, {"MinScale"});
        add(before.maxScale != after.maxScale, {"MaxScale"});
        add(before.alignToTerrainNormal != after.alignToTerrainNormal, {"AlignToTerrainNormal"});
        add(before.cellSize != after.cellSize, {"CellSize"});
        add(before.maxDrawDistance != after.maxDrawDistance, {"MaxDrawDistance"});
        add(before.lodDistanceScale != after.lodDistanceScale, {"LodDistanceScale"});
        add(before.collisionEnabled != after.collisionEnabled, {"CollisionEnabled"});
        add(before.collisionCenter != after.collisionCenter, {"CollisionCenter"});
        add(before.collisionRadius != after.collisionRadius, {"CollisionRadius"});
        add(before.collisionHeight != after.collisionHeight, {"CollisionHeight"});
        add(before.wind.enabled != after.wind.enabled, {"WindEnabled"});
        add(before.wind.stiffness != after.wind.stiffness, {"WindStiffness"});
        add(before.wind.flutter != after.wind.flutter, {"WindFlutter"});
        add(before.wind.bendHeight != after.wind.bendHeight, {"WindBendHeight"});
        add(before.wind.pivotHeight != after.wind.pivotHeight, {"WindPivotHeight"});
        add(before.proxyPrefabReference != after.proxyPrefabReference, {"ProxyPrefab"});
        return changed;
    }

    bool WriteFoliageTypeAsset(std::ostream &output, const FoliageTypeAsset &asset)
    {
        output << kHeaderPrefix << kCurrentVersion << '\n';
        if (!asset.sourceMeshPath.empty()) output << "MESH\t" << asset.sourceMeshPath << '\n';
        if (!asset.modelAssetId.empty())
            output << "MODEL_OBJECT\t" << asset.modelObjectId << '\t' << asset.modelAssetId << '\n';
        if (!asset.materialAssetReference.empty()) output << "MATERIAL\t" << asset.materialAssetReference << '\n';
        if (!asset.submeshIndices.empty())
        {
            output << "SUBMESHES";
            for (const int index : asset.submeshIndices) output << '\t' << index;
            output << '\n';
        }
        else if (asset.submeshIndex >= 0)
        {
            output << "SUBMESHES\t" << asset.submeshIndex << '\n';
        }
        output << "GENERATED_LODS\t" << (asset.useGeneratedLods ? 1 : 0) << '\n'
               << "SCALE_RANGE\t" << asset.minScale << '\t' << asset.maxScale << '\n'
               << "ALIGN_TO_NORMAL\t" << (asset.alignToTerrainNormal ? 1 : 0) << '\n'
               << "CELL_SIZE\t" << asset.cellSize << '\n'
               << "MAX_DRAW_DISTANCE\t" << asset.maxDrawDistance << '\n'
               << "LOD_DISTANCE_SCALE\t" << asset.lodDistanceScale << '\n'
               << "COLLISION_ENABLED\t" << (asset.collisionEnabled ? 1 : 0) << '\n'
               << "COLLISION_CENTER\t" << asset.collisionCenter.x << '\t' << asset.collisionCenter.y << '\t' << asset.collisionCenter.z << '\n'
               << "COLLISION_CAPSULE\t" << asset.collisionRadius << '\t' << asset.collisionHeight << '\n'
               << "WIND_ENABLED\t" << (asset.wind.enabled ? 1 : 0) << '\n'
               << "WIND_RESPONSE\t" << asset.wind.stiffness << '\t' << asset.wind.flutter << '\t'
               << asset.wind.bendHeight << '\t' << asset.wind.pivotHeight << '\n';
        // Readers that predate proxies skip the record, so it needs no new version.
        if (!asset.proxyPrefabReference.empty()) output << "PROXY_PREFAB\t" << asset.proxyPrefabReference << '\n';
        return output.good();
    }

    bool ReadFoliageTypeAsset(std::istream &input, FoliageTypeAsset &asset, std::string *errorMessage)
    {
        std::string line;
        if (!std::getline(input, line))
        {
            SetError(errorMessage, "Empty foliage type asset.");
            return false;
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        int version = 0;
        if (line.rfind(kHeaderPrefix, 0) == 0)
        {
            std::stringstream header(line.substr(kHeaderPrefix.size()));
            header >> version;
        }
        if (version < 1 || version > kCurrentVersion)
        {
            SetError(errorMessage, "Unsupported foliage type asset header: " + line);
            return false;
        }

        FoliageTypeAsset loaded;
        if (version == 1) CopyPrototypeAndPlacement(asset, loaded);
        while (std::getline(input, line))
        {
            std::stringstream stream(line);
            std::string key;
            std::getline(stream, key, '\t');
            if (key == "MESH") loaded.sourceMeshPath = RestOfLine(stream);
            else if (key == "MODEL_OBJECT")
            {
                stream >> loaded.modelObjectId;
                stream.ignore(1);
                loaded.modelAssetId = RestOfLine(stream);
            }
            else if (key == "MATERIAL") loaded.materialAssetReference = RestOfLine(stream);
            else if (key == "SUBMESHES")
            {
                loaded.submeshIndices = ParseIndices(stream);
                loaded.submeshIndex = loaded.submeshIndices.size() == 1 ? loaded.submeshIndices.front() : -1;
            }
            else if (key == "GENERATED_LODS") { int enabled = 0; stream >> enabled; loaded.useGeneratedLods = enabled != 0; }
            else if (key == "SCALE_RANGE") stream >> loaded.minScale >> loaded.maxScale;
            else if (key == "ALIGN_TO_NORMAL") { int enabled = 0; stream >> enabled; loaded.alignToTerrainNormal = enabled != 0; }
            else if (key == "CELL_SIZE") stream >> loaded.cellSize;
            else if (key == "MAX_DRAW_DISTANCE") stream >> loaded.maxDrawDistance;
            // Version 1 files written before LOD distance scales have no record and load with the default.
            else if (key == "LOD_DISTANCE_SCALE") stream >> loaded.lodDistanceScale;
            else if (key == "COLLISION_ENABLED") { int enabled = 0; stream >> enabled; loaded.collisionEnabled = enabled != 0; }
            else if (key == "COLLISION_CENTER") stream >> loaded.collisionCenter.x >> loaded.collisionCenter.y >> loaded.collisionCenter.z;
            else if (key == "COLLISION_CAPSULE") stream >> loaded.collisionRadius >> loaded.collisionHeight;
            // Version 1 files written before wind have neither record and load with wind off.
            else if (key == "WIND_ENABLED") { int enabled = 0; stream >> enabled; loaded.wind.enabled = enabled != 0; }
            else if (key == "WIND_RESPONSE")
                stream >> loaded.wind.stiffness >> loaded.wind.flutter >> loaded.wind.bendHeight >> loaded.wind.pivotHeight;
            else if (key == "PROXY_PREFAB") loaded.proxyPrefabReference = RestOfLine(stream);
        }
        loaded.assetReference = asset.assetReference;
        SanitizeFoliageTypeAsset(loaded);
        asset = std::move(loaded);
        return true;
    }

    bool SaveFoliageTypeAsset(const std::string &path, const FoliageTypeAsset &asset, std::string *errorMessage)
    {
        // Serialize first, so an invalid asset cannot truncate the file that is already there.
        std::ostringstream serialized;
        if (!WriteFoliageTypeAsset(serialized, asset))
        {
            SetError(errorMessage, "Failed to serialize foliage type asset: " + path);
            return false;
        }
        std::ofstream output(path, std::ios::trunc);
        if (!output) { SetError(errorMessage, "Failed to create foliage type asset: " + path); return false; }
        output << serialized.str();
        output.close();
        if (!output) { SetError(errorMessage, "Failed to write foliage type asset: " + path); return false; }
        return true;
    }

    bool LoadFoliageTypeAsset(const std::string &path, FoliageTypeAsset &asset, std::string *errorMessage)
    {
        PlutoGE::content::InputFile input(path);
        if (!input)
        {
            SetError(errorMessage, "Could not open foliage type asset: " + path);
            return false;
        }
        std::string error;
        if (!ReadFoliageTypeAsset(input, asset, &error))
        {
            SetError(errorMessage, "Invalid foliage type asset " + path + ": " + error);
            return false;
        }
        return true;
    }
}
