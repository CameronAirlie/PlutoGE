#include "PlutoGE/scene/FoliageTypeAsset.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr const char *kHeader = "PLUTOFOLIAGE\t1";
        void SetError(std::string *error, std::string value) { if (error) *error = std::move(value); }
    }

    bool SaveFoliageTypeAsset(const std::string &path, const FoliageTypeAsset &asset, std::string *errorMessage)
    {
        std::ofstream output(path, std::ios::trunc);
        if (!output) { SetError(errorMessage, "Failed to create foliage type asset: " + path); return false; }
        output << kHeader << '\n'
               << "CELL_SIZE\t" << asset.cellSize << '\n'
               << "COLLISION_ENABLED\t" << (asset.collisionEnabled ? 1 : 0) << '\n'
               << "COLLISION_CENTER\t" << asset.collisionCenter.x << '\t' << asset.collisionCenter.y << '\t' << asset.collisionCenter.z << '\n'
               << "COLLISION_CAPSULE\t" << asset.collisionRadius << '\t' << asset.collisionHeight << '\n';
        return output.good();
    }

    bool LoadFoliageTypeAsset(const std::string &path, FoliageTypeAsset &asset, std::string *errorMessage)
    {
        std::ifstream input(path);
        std::string line;
        if (!input || !std::getline(input, line) || line != kHeader)
        {
            SetError(errorMessage, "Invalid foliage type asset: " + path);
            return false;
        }
        FoliageTypeAsset loaded;
        while (std::getline(input, line))
        {
            std::stringstream stream(line);
            std::string key;
            std::getline(stream, key, '\t');
            if (key == "CELL_SIZE") stream >> loaded.cellSize;
            else if (key == "COLLISION_ENABLED") { int enabled = 0; stream >> enabled; loaded.collisionEnabled = enabled != 0; }
            else if (key == "COLLISION_CENTER") stream >> loaded.collisionCenter.x >> loaded.collisionCenter.y >> loaded.collisionCenter.z;
            else if (key == "COLLISION_CAPSULE") stream >> loaded.collisionRadius >> loaded.collisionHeight;
        }
        loaded.assetReference = asset.assetReference;
        loaded.cellSize = (std::max)(loaded.cellSize, 1.0f);
        loaded.collisionRadius = (std::max)(loaded.collisionRadius, 0.01f);
        loaded.collisionHeight = (std::max)(loaded.collisionHeight, loaded.collisionRadius * 2.0f);
        asset = std::move(loaded);
        return true;
    }
}
