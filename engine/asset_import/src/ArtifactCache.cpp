#include "PlutoGE/asset_import/ArtifactCache.h"
#include "PlutoGE/asset_import/ProjectImportLock.h"
#include "PlutoGE/assets/AssetMetadata.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace PlutoGE::assetimport
{
    namespace
    {
        std::mutex CachePublicationMutex;
        constexpr std::uintmax_t MaxManifestBytes = 16 * 1024 * 1024;

        std::string Utf8(const std::filesystem::path &path)
        {
            const auto text = path.generic_u8string();
            return {reinterpret_cast<const char *>(text.data()), text.size()};
        }

        bool RecordText(std::string_view text)
        {
            return text.find_first_of("\r\n") == std::string_view::npos && text.find('\0') == std::string_view::npos;
        }

        std::string OutputKey(const std::filesystem::path &path)
        {
            auto key = Utf8(path);
#ifdef _WIN32
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
            return key;
        }

        bool Relative(const std::filesystem::path &path)
        {
            if (!RecordText(Utf8(path)) || path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
            for (const auto &part : path)
                if (part == ".." || part == "." || part.empty()) return false;
            return true;
        }

        void Frame(content::ContentHasher &hasher, std::string_view text)
        {
            std::array<std::byte, 8> size;
            const auto count = static_cast<std::uint64_t>(text.size());
            for (std::size_t index = 0; index < size.size(); ++index)
                size[index] = static_cast<std::byte>((count >> (index * 8)) & 255);
            hasher.Update(size);
            hasher.Update(std::as_bytes(std::span(text.data(), text.size())));
        }

        class StagingDirectory
        {
        public:
            std::filesystem::path path;
            ~StagingDirectory()
            {
                if (!path.empty()) { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
            }
        };

        void WriteManifest(const std::filesystem::path &path, const ArtifactManifest &manifest)
        {
            std::ofstream output(path, std::ios::binary);
            output << "PLUTOARTIFACT\t1\n" << "KEY " << content::DigestToHex(manifest.key) << '\n';
            output << "RECIPE " << std::quoted(manifest.recipe.importer) << ' ' << manifest.recipe.version << ' '
                   << std::quoted(manifest.recipe.target) << ' ' << content::DigestToHex(manifest.recipe.settings) << '\n';
            for (const auto &input : manifest.recipe.inputs)
                output << "INPUT " << std::quoted(input.identity) << ' ' << std::quoted(Utf8(input.path)) << ' '
                       << content::DigestToHex(input.digest) << '\n';
            for (const auto &file : manifest.outputs)
                output << "OUTPUT " << std::quoted(Utf8(file.relativePath)) << ' ' << content::DigestToHex(file.digest) << '\n';
            output.close();
            if (!output) throw std::runtime_error("Cannot finish artifact manifest.");
        }

        bool SameOutputs(const ArtifactManifest &a, const ArtifactManifest &b)
        {
            if (a.outputs.size() != b.outputs.size()) return false;
            for (std::size_t index = 0; index < a.outputs.size(); ++index)
                if (a.outputs[index].relativePath != b.outputs[index].relativePath || a.outputs[index].digest != b.outputs[index].digest) return false;
            return true;
        }
    }

    bool ComputeArtifactKey(const ArtifactRecipe &recipe, content::ContentDigest &key, std::string *errorMessage)
    {
        if (recipe.importer.empty() || recipe.version == 0 || recipe.target.empty() ||
            !RecordText(recipe.importer) || !RecordText(recipe.target))
        {
            if (errorMessage) *errorMessage = "Artifact importer, version, and target are required.";
            return false;
        }
        auto inputs = recipe.inputs;
        std::sort(inputs.begin(), inputs.end(), [](const auto &a, const auto &b) { return a.identity < b.identity; });
        content::ContentHasher hasher;
        Frame(hasher, "PlutoGE artifact recipe 1");
        Frame(hasher, recipe.importer);
        Frame(hasher, std::to_string(recipe.version));
        Frame(hasher, recipe.target);
        Frame(hasher, content::DigestToHex(recipe.settings));
        std::set<std::string> identities;
        for (const auto &input : inputs)
        {
            if (input.identity.empty() || !RecordText(input.identity) || !input.path.is_absolute() ||
                !RecordText(Utf8(input.path)) || !identities.insert(input.identity).second)
            {
                if (errorMessage) *errorMessage = "Artifact input identities must be nonempty and unique, with an absolute source path.";
                return false;
            }
            Frame(hasher, input.identity);
            Frame(hasher, Utf8(input.path.lexically_normal()));
            Frame(hasher, content::DigestToHex(input.digest));
        }
        key = hasher.Finalize();
        if (errorMessage) errorMessage->clear();
        return true;
    }

    bool AreArtifactInputsCurrent(const ArtifactRecipe &recipe, std::string *errorMessage)
    {
        for (const auto &input : recipe.inputs)
        {
            content::ContentDigest actual;
            if (!content::HashFileContent(input.path, actual, errorMessage)) return false;
            if (actual != input.digest)
            {
                if (errorMessage) *errorMessage = "Import input changed: " + input.path.string();
                return false;
            }
        }
        if (errorMessage) errorMessage->clear();
        return true;
    }

    bool ValidateArtifactPublication(const ArtifactManifest &generation, const std::filesystem::path &outputRoot,
                                     std::string *errorMessage)
    {
        try
        {
            if (!AreArtifactInputsCurrent(generation.recipe, errorMessage)) return false;
            std::set<std::string> paths;
            for (const auto &output : generation.outputs)
            {
                if (!Relative(output.relativePath) || !paths.insert(OutputKey(output.relativePath)).second)
                    throw std::runtime_error("Invalid or duplicate published artifact path.");
                auto path = outputRoot;
                for (const auto &part : output.relativePath)
                {
                    path /= part;
                    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
                        throw std::runtime_error("Published artifact traverses a symbolic link: " + path.string());
                }
                content::ContentDigest actual;
                if (!std::filesystem::is_regular_file(path) || !content::HashFileContent(path, actual) || actual != output.digest)
                    throw std::runtime_error("Published artifact changed before acceptance: " + path.string());
            }
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &exception)
        {
            if (errorMessage) *errorMessage = exception.what();
            return false;
        }
    }

    std::filesystem::path ArtifactCache::GetDirectory(const content::ContentDigest &key) const
    {
        return m_root / content::DigestToHex(key);
    }

    ArtifactCacheStatus ArtifactCache::Find(const content::ContentDigest &key, ArtifactManifest &manifest, std::string *errorMessage) const
    {
        return ReadGeneration(key, manifest, true, errorMessage);
    }

    ArtifactCacheStatus ArtifactCache::ReadGeneration(const content::ContentDigest &key, ArtifactManifest &manifest,
                                                      bool verifyOutputs, std::string *errorMessage) const
    {
        auto corrupt = [&](const std::string &message)
        {
            if (errorMessage) *errorMessage = message;
            return ArtifactCacheStatus::Corrupt;
        };
        try
        {
            const auto directory = GetDirectory(key);
            if (!std::filesystem::exists(directory))
            {
                if (errorMessage) errorMessage->clear();
                return ArtifactCacheStatus::Missing;
            }
            const auto path = directory / "manifest";
            if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > MaxManifestBytes)
                return corrupt("Missing or oversized artifact manifest.");
            std::ifstream input(path, std::ios::binary);
            std::string header;
            std::getline(input, header);
            if (header != "PLUTOARTIFACT\t1") return corrupt("Unsupported artifact manifest schema.");
            ArtifactManifest parsed;
            bool hasKey = false;
            bool hasRecipe = false;
            std::set<std::string> outputs;
            std::string line;
            while (std::getline(input, line))
            {
                std::istringstream record(line);
                std::string tag;
                std::string digest;
                record >> tag;
                if (tag == "KEY")
                {
                    if (hasKey || !(record >> digest) || !content::ParseContentDigest(digest, parsed.key)) return corrupt("Invalid artifact key.");
                    hasKey = true;
                }
                else if (tag == "RECIPE")
                {
                    if (hasRecipe || !(record >> std::quoted(parsed.recipe.importer) >> parsed.recipe.version >> std::quoted(parsed.recipe.target) >> digest) ||
                        !content::ParseContentDigest(digest, parsed.recipe.settings)) return corrupt("Invalid artifact recipe.");
                    hasRecipe = true;
                }
                else if (tag == "INPUT")
                {
                    ArtifactInput source;
                    std::string pathText;
                    if (!(record >> std::quoted(source.identity) >> std::quoted(pathText) >> digest) ||
                        !content::ParseContentDigest(digest, source.digest)) return corrupt("Invalid artifact input.");
                    source.path = std::filesystem::u8path(pathText);
                    parsed.recipe.inputs.push_back(std::move(source));
                }
                else if (tag == "OUTPUT")
                {
                    ArtifactOutput output;
                    std::string relative;
                    if (!(record >> std::quoted(relative) >> digest) || !content::ParseContentDigest(digest, output.digest))
                        return corrupt("Invalid artifact output.");
                    output.relativePath = std::filesystem::u8path(relative);
                    if (!Relative(output.relativePath) || !outputs.insert(OutputKey(output.relativePath)).second) return corrupt("Invalid or duplicate artifact output path.");
                    parsed.outputs.push_back(std::move(output));
                }
                else return corrupt("Unknown artifact manifest record.");
                record >> std::ws;
                if (!record.eof()) return corrupt("Trailing artifact manifest fields.");
            }
            content::ContentDigest expected;
            if (!input.eof() || !hasKey || !hasRecipe || parsed.outputs.empty() || parsed.key != key ||
                !ComputeArtifactKey(parsed.recipe, expected) || expected != key) return corrupt("Artifact recipe/key mismatch.");
            std::sort(parsed.outputs.begin(), parsed.outputs.end(), [](const auto &a, const auto &b) { return a.relativePath < b.relativePath; });
            if (verifyOutputs) for (const auto &output : parsed.outputs)
            {
                const auto file = directory / "Files" / output.relativePath;
                content::ContentDigest actual;
                if (!std::filesystem::is_regular_file(file) || !content::HashFileContent(file, actual) || actual != output.digest)
                    return corrupt("Artifact file is missing or corrupt: " + output.relativePath.string());
            }
            manifest = std::move(parsed);
            if (errorMessage) errorMessage->clear();
            return ArtifactCacheStatus::Hit;
        }
        catch (const std::exception &exception)
        {
            if (errorMessage) *errorMessage = std::string("Cannot read artifact cache: ") + exception.what();
            return ArtifactCacheStatus::IoError;
        }
    }

    ArtifactCacheStatus ArtifactCache::FindMatching(const std::function<bool(const ArtifactManifest &)> &matches,
                                                    ArtifactManifest &manifest, std::string *errorMessage) const
    {
        try
        {
            if (!std::filesystem::exists(m_root))
            {
                if (errorMessage) errorMessage->clear();
                return ArtifactCacheStatus::Missing;
            }
            std::vector<std::filesystem::path> directories;
            for (const auto &entry : std::filesystem::directory_iterator(m_root))
                if (!entry.is_symlink() && entry.is_directory()) directories.push_back(entry.path());
            std::sort(directories.begin(), directories.end());
            for (const auto &directory : directories)
            {
                content::ContentDigest key;
                if (!content::ParseContentDigest(directory.filename().string(), key)) continue;
                ArtifactManifest candidate;
                if (ReadGeneration(key, candidate, false, nullptr) != ArtifactCacheStatus::Hit || !matches(candidate)) continue;
                if (Find(key, candidate) != ArtifactCacheStatus::Hit) continue;
                manifest = std::move(candidate);
                if (errorMessage) errorMessage->clear();
                return ArtifactCacheStatus::Hit;
            }
            if (errorMessage) errorMessage->clear();
            return ArtifactCacheStatus::Missing;
        }
        catch (const std::exception &exception)
        {
            if (errorMessage) *errorMessage = std::string("Cannot search artifact cache: ") + exception.what();
            return ArtifactCacheStatus::IoError;
        }
    }

    bool ArtifactCache::RememberRequestGeneration(std::string_view requestIdentity, const content::ContentDigest &key, std::string *errorMessage) const
    {
        if (requestIdentity.empty())
        {
            if (errorMessage) *errorMessage = "Request identity cannot be empty.";
            return false;
        }
        try
        {
            const auto requestKey = content::HashContent(std::as_bytes(std::span(requestIdentity.data(), requestIdentity.size())));
            const auto requests = m_root / "Requests-v1";
            // A 128-bit bucket keeps Windows paths short. Bucket collisions only
            // add candidates: request matching and full generation hashes remain authoritative.
            const auto directory = requests / content::DigestToHex(requestKey).substr(0, 32);
            std::error_code inspectionError;
            if (std::filesystem::is_symlink(requests, inspectionError)) return false;
            std::filesystem::create_directories(requests);
            if (std::filesystem::is_symlink(directory, inspectionError)) return false;
            std::filesystem::create_directory(directory);
            // An empty directory is an immutable marker. Concurrent writers can
            // add different keys without losing one another's hints.
            const auto marker = directory / content::DigestToHex(key);
            std::filesystem::create_directory(marker);
            const bool stored = !std::filesystem::is_symlink(marker) && std::filesystem::is_directory(marker);
            if (errorMessage) *errorMessage = stored ? "" : "Invalid request hint marker.";
            return stored;
        }
        catch (const std::exception &error)
        {
            if (errorMessage) *errorMessage = std::string("Cannot store request hint: ") + error.what();
            return false;
        }
    }

    ArtifactCacheStatus ArtifactCache::FindMatchingForRequest(std::string_view requestIdentity,
        const std::function<bool(const ArtifactManifest &)> &matches,
        ArtifactManifest &manifest, std::string *errorMessage) const
    {
        if (!requestIdentity.empty())
        {
            try
            {
                const auto requestKey = content::HashContent(std::as_bytes(std::span(requestIdentity.data(), requestIdentity.size())));
                const auto requests = m_root / "Requests-v1";
                const auto directory = requests / content::DigestToHex(requestKey).substr(0, 32);
                std::vector<std::filesystem::path> markers;
                if (!std::filesystem::is_symlink(requests) && !std::filesystem::is_symlink(directory) && std::filesystem::is_directory(directory))
                    for (const auto &entry : std::filesystem::directory_iterator(directory))
                        if (!entry.is_symlink() && entry.is_directory()) markers.push_back(entry.path());
                std::sort(markers.begin(), markers.end());
                for (const auto &marker : markers)
                {
                    content::ContentDigest key;
                    if (!content::ParseContentDigest(marker.filename().string(), key)) continue;
                    ArtifactManifest candidate;
                    if (ReadGeneration(key, candidate, false, nullptr) != ArtifactCacheStatus::Hit || !matches(candidate)) continue;
                    if (Find(key, candidate) != ArtifactCacheStatus::Hit) continue;
                    manifest = std::move(candidate);
                    if (errorMessage) errorMessage->clear();
                    return ArtifactCacheStatus::Hit;
                }
            }
            catch (const std::exception &) { /* Hints are optional; search authoritative generations. */ }
        }
        const auto status = FindMatching(matches, manifest, errorMessage);
        if (status == ArtifactCacheStatus::Hit) RememberRequestGeneration(requestIdentity, manifest.key);
        return status;
    }

    bool ArtifactCache::Store(const ArtifactRecipe &recipe, const std::filesystem::path &sourceRoot,
                              const std::vector<std::filesystem::path> &relativeOutputs, ArtifactManifest &manifest,
                              std::string *errorMessage) const
    {
        std::lock_guard lock(CachePublicationMutex);
        try
        {
            ArtifactManifest candidate;
            candidate.recipe = recipe;
            if (!ComputeArtifactKey(recipe, candidate.key, errorMessage) || !AreArtifactInputsCurrent(recipe, errorMessage)) return false;
            if (relativeOutputs.empty()) throw std::runtime_error("Artifact outputs cannot be empty.");
            std::filesystem::create_directories(m_root);
            ProjectImportLock cacheLock;
            if (!cacheLock.TryAcquire(m_root, errorMessage)) return false;
            StagingDirectory staging;
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                const auto path = m_root / (".staging-" + assets::GenerateAssetId());
                if (std::filesystem::create_directory(path)) { staging.path = path; break; }
            }
            if (staging.path.empty()) throw std::runtime_error("Cannot allocate artifact staging directory.");
            std::set<std::string> unique;
            for (const auto &relative : relativeOutputs)
            {
                if (!Relative(relative) || !unique.insert(OutputKey(relative)).second) throw std::runtime_error("Invalid or duplicate artifact output path.");
                const auto destination = staging.path / "Files" / relative;
                std::filesystem::create_directories(destination.parent_path());
                std::filesystem::copy_file(sourceRoot / relative, destination);
                ArtifactOutput output{.relativePath = relative};
                if (!content::HashFileContent(destination, output.digest, errorMessage)) return false;
                candidate.outputs.push_back(std::move(output));
            }
            std::sort(candidate.outputs.begin(), candidate.outputs.end(), [](const auto &a, const auto &b) { return a.relativePath < b.relativePath; });
            if (!AreArtifactInputsCurrent(recipe, errorMessage)) return false;
            WriteManifest(staging.path / "manifest", candidate);
            ArtifactManifest existing;
            const auto status = Find(candidate.key, existing);
            if (status == ArtifactCacheStatus::Hit)
            {
                if (!SameOutputs(candidate, existing)) throw std::runtime_error("Importer produced different output for the same artifact key.");
                manifest = std::move(existing);
                if (errorMessage) errorMessage->clear();
                return true;
            }
            if (status == ArtifactCacheStatus::IoError) throw std::runtime_error("Existing artifact cache cannot be read.");
            const auto destination = GetDirectory(candidate.key);
            if (status == ArtifactCacheStatus::Corrupt)
                std::filesystem::rename(destination, m_root / (".corrupt-" + assets::GenerateAssetId()));
            std::error_code error;
            std::filesystem::rename(staging.path, destination, error);
            if (error)
            {
                // Another process may have published an equivalent generation.
                if (Find(candidate.key, existing) != ArtifactCacheStatus::Hit || !SameOutputs(candidate, existing))
                    throw std::runtime_error("Cannot publish artifact generation: " + error.message());
            }
            manifest = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &exception)
        {
            if (errorMessage) *errorMessage = std::string("Cannot store artifact generation: ") + exception.what();
            return false;
        }
    }
}
