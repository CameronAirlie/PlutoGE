#include "PlutoGE/assets/AssetMetadata.h"

#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view Header = "PLUTOASSET\t1";
        constexpr std::size_t MaxMetadataBytes = 1024 * 1024;

        void SetError(std::string *output, std::string value)
        {
            if (output) *output = std::move(value);
        }

        bool IsSingleLine(std::string_view value)
        {
            return value.find_first_of("\r\n\0", 0, 3) == std::string_view::npos;
        }

        class StagingDirectory
        {
        public:
            std::filesystem::path path;
            ~StagingDirectory()
            {
                if (!path.empty())
                {
                    std::error_code ignored;
                    std::filesystem::remove(path / "metadata", ignored);
                    std::filesystem::remove(path, ignored);
                }
            }
        };
    }

    std::string GenerateAssetId()
    {
        std::random_device device;
        std::mt19937_64 random(device());
        std::uniform_int_distribution<std::uint64_t> distribution;
        std::ostringstream output;
        output << std::hex << std::setfill('0') << std::setw(16) << distribution(random)
               << std::setw(16) << distribution(random);
        return output.str();
    }

    std::filesystem::path GetAssetMetadataPath(const std::filesystem::path &assetPath)
    {
        return std::filesystem::path(assetPath.native() + std::filesystem::path(".plutometa").native());
    }

    AssetMetadataStatus ParseAssetMetadata(std::string_view text, AssetMetadata &metadata, std::string *errorMessage)
    {
        if (text.size() > MaxMetadataBytes || text.find('\0') != std::string_view::npos)
        {
            SetError(errorMessage, "Metadata exceeds the size limit or contains a null byte.");
            return AssetMetadataStatus::Invalid;
        }
        std::istringstream input{std::string(text)};
        std::string line;
        if (!std::getline(input, line))
        {
            SetError(errorMessage, "Missing metadata header.");
            return AssetMetadataStatus::Invalid;
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != Header)
        {
            const bool unsupported = line.starts_with("PLUTOASSET\t");
            SetError(errorMessage, unsupported ? "Unsupported metadata schema version." : "Invalid metadata header.");
            return unsupported ? AssetMetadataStatus::UnsupportedVersion : AssetMetadataStatus::Invalid;
        }
        AssetMetadata parsed;
        bool hasId = false;
        bool hasVersion = false;
        bool hasOwnership = false;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto separator = line.find('\t');
            const std::string_view key(line.data(), separator == std::string::npos ? line.size() : separator);
            const std::string_view value = separator == std::string::npos ? std::string_view{} : std::string_view(line).substr(separator + 1);
            if (key == "ID")
            {
                if (hasId || value.empty() || value.find_first_of("\t\r\n ") != std::string_view::npos)
                {
                    SetError(errorMessage, "Missing, duplicate, or invalid metadata ID.");
                    return AssetMetadataStatus::Invalid;
                }
                parsed.id = value;
                hasId = true;
            }
            else if (key == "IMPORTER_VERSION")
            {
                if (value.empty())
                {
                    SetError(errorMessage, "Importer version is required.");
                    return AssetMetadataStatus::Invalid;
                }
                std::uint32_t version = 0;
                const auto result = std::from_chars(value.data(), value.data() + value.size(), version);
                if (hasVersion || result.ec != std::errc{} || result.ptr != value.data() + value.size() || version == 0)
                {
                    SetError(errorMessage, "Duplicate or invalid importer version.");
                    return AssetMetadataStatus::Invalid;
                }
                parsed.importerVersion = version;
                hasVersion = true;
            }
            else if (key == "OWNERSHIP")
            {
                if (hasOwnership || (value != "AUTHORED" && value != "SOURCE"))
                {
                    SetError(errorMessage, "Duplicate or unsupported explicit asset ownership.");
                    return AssetMetadataStatus::Invalid;
                }
                hasOwnership = true;
                parsed.ownership = value == "AUTHORED" ? AssetOwnership::Authored : AssetOwnership::Source;
            }
            else
            {
                parsed.extensionRecords.push_back(line);
            }
        }
        if (!hasId)
        {
            SetError(errorMessage, "Metadata ID is required.");
            return AssetMetadataStatus::Invalid;
        }
        metadata = std::move(parsed);
        if (errorMessage) errorMessage->clear();
        return AssetMetadataStatus::Success;
    }

    AssetMetadataStatus LoadAssetMetadata(const std::filesystem::path &path, AssetMetadata &metadata, std::string *errorMessage)
    {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (status.type() == std::filesystem::file_type::not_found || error == std::errc::no_such_file_or_directory)
        {
            SetError(errorMessage, "Metadata does not exist: " + path.string());
            return AssetMetadataStatus::Missing;
        }
        if (error || status.type() != std::filesystem::file_type::regular)
        {
            SetError(errorMessage, "Metadata is not a readable regular file: " + path.string());
            return AssetMetadataStatus::IoError;
        }
        std::ifstream input(path, std::ios::binary);
        std::string text;
        std::array<char, 4096> buffer;
        while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0)
        {
            text.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
            if (text.size() > MaxMetadataBytes)
            {
                SetError(errorMessage, "Metadata exceeds the size limit: " + path.string());
                return AssetMetadataStatus::Invalid;
            }
        }
        if (!input.eof())
        {
            SetError(errorMessage, "Cannot read metadata: " + path.string());
            return AssetMetadataStatus::IoError;
        }
        const auto result = ParseAssetMetadata(text, metadata, errorMessage);
        if (result != AssetMetadataStatus::Success && errorMessage) *errorMessage += " File: " + path.string();
        return result;
    }

    bool SerializeAssetMetadata(const AssetMetadata &metadata, std::string &text, std::string *errorMessage)
    {
        if (metadata.id.empty() || !IsSingleLine(metadata.id) ||
            metadata.id.find_first_of("\t ") != std::string::npos || metadata.importerVersion == 0)
        {
            SetError(errorMessage, "Invalid metadata identity or importer version.");
            return false;
        }
        std::ostringstream output;
        output << Header << '\n' << "ID\t" << metadata.id << '\n'
               << "IMPORTER_VERSION\t" << metadata.importerVersion << '\n';
        if (metadata.ownership == AssetOwnership::Authored) output << "OWNERSHIP\tAUTHORED\n";
        else if (metadata.ownership == AssetOwnership::Source) output << "OWNERSHIP\tSOURCE\n";
        else if (metadata.ownership != AssetOwnership::Unclassified)
        {
            SetError(errorMessage, "Imported ownership must be derived from a model manifest.");
            return false;
        }
        for (const auto &line : metadata.extensionRecords)
        {
            if (!IsSingleLine(line))
            {
                SetError(errorMessage, "Metadata extension records must contain a single line.");
                return false;
            }
            output << line << '\n';
        }
        auto serialized = output.str();
        AssetMetadata validated;
        if (ParseAssetMetadata(serialized, validated, errorMessage) != AssetMetadataStatus::Success) return false;
        text = std::move(serialized);
        return true;
    }

    bool SaveAssetMetadata(const std::filesystem::path &path, const AssetMetadata &metadata,
                           AssetMetadataWriteMode mode, std::string *errorMessage)
    {
        std::string text;
        if (!SerializeAssetMetadata(metadata, text, errorMessage)) return false;
        if (mode == AssetMetadataWriteMode::ReplaceExisting)
        {
            AssetMetadata existing;
            if (LoadAssetMetadata(path, existing, errorMessage) != AssetMetadataStatus::Success) return false;
            if (existing.id != metadata.id)
            {
                SetError(errorMessage, "Cannot replace metadata with a different identity: " + path.string());
                return false;
            }
        }
        StagingDirectory staging;
        std::error_code error;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto candidate = path.parent_path() / (".pluto-metadata-" + GenerateAssetId());
            if (std::filesystem::create_directory(candidate, error))
            {
                staging.path = candidate;
                break;
            }
            if (error)
            {
                SetError(errorMessage, "Cannot stage metadata: " + error.message());
                return false;
            }
        }
        if (staging.path.empty())
        {
            SetError(errorMessage, "Cannot allocate metadata staging directory.");
            return false;
        }
        const auto temporary = staging.path / "metadata";
        std::ofstream output(temporary, std::ios::binary);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        if (!output)
        {
            SetError(errorMessage, "Cannot finish metadata staging file: " + path.string());
            return false;
        }
#ifdef _WIN32
        const DWORD flags = MOVEFILE_WRITE_THROUGH |
                            (mode == AssetMetadataWriteMode::ReplaceExisting ? MOVEFILE_REPLACE_EXISTING : 0);
        if (!MoveFileExW(temporary.c_str(), path.c_str(), flags))
            error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
        if (mode == AssetMetadataWriteMode::CreateOnly)
            std::filesystem::create_hard_link(temporary, path, error);
        else
            std::filesystem::rename(temporary, path, error);
#endif
        if (error)
        {
            SetError(errorMessage, "Cannot publish metadata: " + path.string() + ": " + error.message());
            return false;
        }
        if (errorMessage) errorMessage->clear();
        return true;
    }
}
