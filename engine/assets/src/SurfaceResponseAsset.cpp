#include "PlutoGE/assets/SurfaceResponseAsset.h"
#include "PlutoGE/assets/AssetManager.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace PlutoGE::assets
{
    namespace
    {
        bool ProjectPathIsRelative(const std::string &reference)
        {
            if (!Project::IsProjectAssetReference(reference)) return false;
            const auto path = std::filesystem::path(reference.substr(Project::kProjectAssetScheme.size()));
            if (path.empty() || path.has_root_path()) return false;
            for (const auto &part : path) if (part == "..") return false;
            return true;
        }
        bool ValidReference(const std::string &reference, ProjectAssetType type)
        {
            return reference.empty() || (reference.size() <= 4095 &&
                reference.find_first_of("\r\n\t") == std::string::npos && reference.find('\0') == std::string::npos &&
                (ProjectPathIsRelative(reference) || Project::IsEngineAssetReference(reference)) &&
                Project::GetAssetTypeForReference(reference) == type);
        }
        bool Valid(const SurfaceResponseAsset &asset)
        {
            if (!std::isfinite(asset.friction) || asset.friction < 0 || asset.friction > 10) return false;
            for (const auto *response : {&asset.footstep, &asset.impact})
                if (!ValidReference(response->sound, ProjectAssetType::Audio) ||
                    !ValidReference(response->particles, ProjectAssetType::ParticleSystem) ||
                    !ValidReference(response->decalMaterial, ProjectAssetType::Material)) return false;
            return true;
        }
    }

    bool ReadSurfaceResponseAsset(std::istream &input, SurfaceResponseAsset &asset)
    {
        SurfaceResponseAsset parsed;
        std::string line;
        if (!std::getline(input, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != "SurfaceResponseVersion=1") return false;
        unsigned seen = 0;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (line.size() > 8192) return false;
            const auto separator = line.find('=');
            if (separator == std::string::npos) return false;
            const auto key = line.substr(0, separator), value = line.substr(separator + 1);
            unsigned field = 0;
            if (key == "Friction")
            {
                field = 1;
                std::istringstream number(value);
                if (!(number >> parsed.friction) || !(number >> std::ws).eof()) return false;
            }
            else if (key == "FootstepSound") { field = 2; parsed.footstep.sound = value; }
            else if (key == "FootstepParticles") { field = 4; parsed.footstep.particles = value; }
            else if (key == "FootstepDecal") { field = 8; parsed.footstep.decalMaterial = value; }
            else if (key == "ImpactSound") { field = 16; parsed.impact.sound = value; }
            else if (key == "ImpactParticles") { field = 32; parsed.impact.particles = value; }
            else if (key == "ImpactDecal") { field = 64; parsed.impact.decalMaterial = value; }
            else return false;
            if (seen & field) return false;
            seen |= field;
        }
        if (input.bad() || !Valid(parsed)) return false;
        asset = std::move(parsed);
        return true;
    }

    bool WriteSurfaceResponseAsset(std::ostream &output, const SurfaceResponseAsset &asset)
    {
        if (!Valid(asset)) return false;
        output << "SurfaceResponseVersion=1\nFriction=" << std::setprecision(9) << asset.friction
            << "\nFootstepSound=" << asset.footstep.sound
            << "\nFootstepParticles=" << asset.footstep.particles
            << "\nFootstepDecal=" << asset.footstep.decalMaterial
            << "\nImpactSound=" << asset.impact.sound
            << "\nImpactParticles=" << asset.impact.particles
            << "\nImpactDecal=" << asset.impact.decalMaterial << '\n';
        return output.good();
    }

    SurfaceResponseAsset AssetManager::LoadSurfaceResponseAsset(const std::string &reference, bool *loaded)
    {
        if (loaded) *loaded = false;
        if (!ProjectPathIsRelative(reference) || Project::GetAssetTypeForReference(reference) != ProjectAssetType::SurfaceResponse) return {};
        if (const auto found = m_surfaceResponseCache.find(reference); found != m_surfaceResponseCache.end())
        {
            if (loaded) *loaded = found->second.first;
            return found->second.second;
        }
        std::ifstream input(ResolveAssetPath(reference));
        SurfaceResponseAsset asset;
        const bool success = ReadSurfaceResponseAsset(input, asset);
        m_surfaceResponseCache[reference] = {success, asset};
        if (loaded) *loaded = success;
        return asset;
    }

    bool AssetManager::SaveSurfaceResponseAsset(const std::string &reference, const SurfaceResponseAsset &asset, std::string *error)
    {
        if (error) error->clear();
        auto fail = [&](const char *message) { if (error) *error = message; return false; };
        if (!ProjectPathIsRelative(reference) || Project::GetAssetTypeForReference(reference) != ProjectAssetType::SurfaceResponse)
            return fail("Choose a project .plutosurface asset.");
        std::ostringstream serialized;
        if (!WriteSurfaceResponseAsset(serialized, asset)) return fail("Invalid friction or response asset type.");
        const auto path = std::filesystem::path(ResolveAssetPath(reference));
        if (path.empty()) return fail("Could not resolve surface asset path.");
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return fail("Could not create surface asset directory.");
        std::ofstream output(path, std::ios::trunc);
        output << serialized.str();
        output.close();
        if (!output) return fail("Could not write surface asset.");
        m_surfaceResponseCache[reference] = {true, asset};
        return true;
    }
}
