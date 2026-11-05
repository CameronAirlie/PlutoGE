#include "PlutoGE/platform/ContentPack.h"
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
               << "MAX_DRAW_DISTANCE\t" << asset.maxDrawDistance << '\n'
               << "COLLISION_ENABLED\t" << (asset.collisionEnabled ? 1 : 0) << '\n'
               << "COLLISION_CENTER\t" << asset.collisionCenter.x << '\t' << asset.collisionCenter.y << '\t' << asset.collisionCenter.z << '\n'
               << "COLLISION_CAPSULE\t" << asset.collisionRadius << '\t' << asset.collisionHeight << '\n'
               << "WIND_ENABLED\t" << (asset.wind.enabled ? 1 : 0) << '\n'
               << "WIND_RESPONSE\t" << asset.wind.stiffness << '\t' << asset.wind.flutter << '\t'
               << asset.wind.bendHeight << '\t' << asset.wind.pivotHeight << '\n';
        return output.good();
    }

    bool LoadFoliageTypeAsset(const std::string &path, FoliageTypeAsset &asset, std::string *errorMessage)
    {
        PlutoGE::content::InputFile input(path);
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
            else if (key == "MAX_DRAW_DISTANCE") stream >> loaded.maxDrawDistance;
            else if (key == "COLLISION_ENABLED") { int enabled = 0; stream >> enabled; loaded.collisionEnabled = enabled != 0; }
            else if (key == "COLLISION_CENTER") stream >> loaded.collisionCenter.x >> loaded.collisionCenter.y >> loaded.collisionCenter.z;
            else if (key == "COLLISION_CAPSULE") stream >> loaded.collisionRadius >> loaded.collisionHeight;
            // Version 1 files written before wind have neither record and load with wind off.
            else if (key == "WIND_ENABLED") { int enabled = 0; stream >> enabled; loaded.wind.enabled = enabled != 0; }
            else if (key == "WIND_RESPONSE")
                stream >> loaded.wind.stiffness >> loaded.wind.flutter >> loaded.wind.bendHeight >> loaded.wind.pivotHeight;
        }
        loaded.assetReference = asset.assetReference;
        loaded.cellSize = (std::max)(loaded.cellSize, 1.0f);
        loaded.maxDrawDistance = (std::max)(loaded.maxDrawDistance, 0.0f);
        loaded.collisionRadius = (std::max)(loaded.collisionRadius, 0.01f);
        loaded.collisionHeight = (std::max)(loaded.collisionHeight, loaded.collisionRadius * 2.0f);
        loaded.wind = loaded.wind.Sanitized();
        asset = std::move(loaded);
        return true;
    }
}
