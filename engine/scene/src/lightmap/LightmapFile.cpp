#include "PlutoGE/scene/LightmapFile.h"

#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/Project.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr std::array<char, 8> kMagic{'P', 'L', 'M', 'A', 'P', 0, 0, 0};

        void Fail(std::string *error, std::string message)
        {
            if (error)
                *error = std::move(message);
        }

        bool KnownEncoding(std::uint32_t encoding)
        {
            return encoding == static_cast<std::uint32_t>(render::LightmapEncoding::IrradianceShadowMask);
        }
    }

    bool WriteLightmapFile(const std::filesystem::path &path, const render::LightmapAtlas &atlas, std::string *error)
    {
        if (!atlas.IsValid() || atlas.width > kMaxLightmapAtlasSize || atlas.height > kMaxLightmapAtlasSize)
        {
            Fail(error, "Invalid lightmap atlas.");
            return false;
        }
        std::error_code ignored;
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ignored);
        // Write beside the target and swap in, so a failed write never leaves a truncated lightmap behind.
        auto temporary = path;
        temporary += ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            const std::array<std::uint32_t, 4> header{kLightmapFileVersion, static_cast<std::uint32_t>(atlas.encoding),
                                                      atlas.width, atlas.height};
            file.write(kMagic.data(), kMagic.size());
            file.write(reinterpret_cast<const char *>(header.data()), sizeof(header));
            file.write(reinterpret_cast<const char *>(atlas.texels.data()),
                       static_cast<std::streamsize>(atlas.texels.size() * sizeof(std::uint16_t)));
            if (!file)
            {
                Fail(error, "Could not write " + temporary.string());
                return false;
            }
        }
        std::filesystem::rename(temporary, path, ignored);
        if (ignored)
        {
            std::filesystem::remove(path, ignored);
            std::error_code renamed;
            std::filesystem::rename(temporary, path, renamed);
            if (renamed)
            {
                Fail(error, "Could not replace " + path.string() + ": " + renamed.message());
                return false;
            }
        }
        return true;
    }

    std::optional<render::LightmapAtlas> ReadLightmapFile(const std::filesystem::path &path, std::string *error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            Fail(error, "Could not open " + path.string());
            return std::nullopt;
        }
        std::array<char, 8> magic{};
        std::array<std::uint32_t, 4> header{};
        file.read(magic.data(), magic.size());
        file.read(reinterpret_cast<char *>(header.data()), sizeof(header));
        if (!file || magic != kMagic)
        {
            Fail(error, path.string() + " is not a lightmap.");
            return std::nullopt;
        }
        const auto [version, encoding, width, height] = header;
        if (version != kLightmapFileVersion || !KnownEncoding(encoding) || width == 0 || height == 0 ||
            width > kMaxLightmapAtlasSize || height > kMaxLightmapAtlasSize)
        {
            Fail(error, path.string() + " has an unsupported lightmap version, encoding or size.");
            return std::nullopt;
        }
        render::LightmapAtlas atlas;
        atlas.width = width;
        atlas.height = height;
        atlas.encoding = static_cast<render::LightmapEncoding>(encoding);
        atlas.texels.resize(std::size_t(width) * height * 4u);
        file.read(reinterpret_cast<char *>(atlas.texels.data()),
                  static_cast<std::streamsize>(atlas.texels.size() * sizeof(std::uint16_t)));
        if (!file)
        {
            Fail(error, path.string() + " is truncated.");
            return std::nullopt;
        }
        return atlas;
    }

    std::optional<std::string> SaveLightmapAsset(assets::Project &project, const render::LightmapAtlas &atlas,
                                                 const std::string &name, const std::string &folder, std::string *error)
    {
        if (name.empty() || name.find_first_of("/\\:") != std::string::npos)
        {
            Fail(error, "Invalid lightmap name.");
            return std::nullopt;
        }
        const auto path = project.GetAssetDirectoryPath() / std::filesystem::path(folder).relative_path() /
                          (name + kLightmapFileExtension);
        if (!WriteLightmapFile(path, atlas, error))
            return std::nullopt;
        // A stable identity sidecar on first write; rebakes keep the existing ID.
        assets::AssetMetadata metadata;
        const auto metadataPath = assets::GetAssetMetadataPath(path);
        const auto status = assets::LoadAssetMetadata(metadataPath, metadata);
        if (status == assets::AssetMetadataStatus::Missing)
        {
            metadata = {};
            metadata.id = assets::GenerateAssetId();
            if (!assets::SaveAssetMetadata(metadataPath, metadata, assets::AssetMetadataWriteMode::CreateOnly, error))
                return std::nullopt;
        }
        else if (status != assets::AssetMetadataStatus::Success)
        {
            Fail(error, "Unreadable asset metadata " + metadataPath.string());
            return std::nullopt;
        }
        if (!project.RefreshAssetEntry(path, error))
            return std::nullopt;
        auto reference = project.MakeAssetReference(path);
        // Scenes name the file only inside baked data, which the build's dependency scan does not read.
        auto &include = project.GetManifest().buildAlwaysInclude;
        if (std::find(include.begin(), include.end(), reference) == include.end())
            include.push_back(reference);
        if (!project.Save(error))
            return std::nullopt;
        return reference;
    }
}
