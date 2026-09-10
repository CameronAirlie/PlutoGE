#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetReferences.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view kMetadataHeader = "PLUTOASSET\t1";
        constexpr std::string_view kCookHeader = "PLUTOCOOK\t1";

        void SetError(std::string *output, std::string value)
        {
            if (output) *output = std::move(value);
        }

        std::string GenerateId()
        {
            std::random_device device;
            std::mt19937_64 random(device());
            std::uniform_int_distribution<std::uint64_t> distribution;
            std::ostringstream output;
            output << std::hex << std::setfill('0') << std::setw(16) << distribution(random)
                   << std::setw(16) << distribution(random);
            return output.str();
        }

        bool ReadMetadata(const std::filesystem::path &path, AssetRecord &record)
        {
            std::ifstream input(path);
            std::string line;
            if (!input || !std::getline(input, line) || line != kMetadataHeader) return false;
            while (std::getline(input, line))
            {
                const auto separator = line.find('\t');
                if (separator == std::string::npos) continue;
                const auto key = line.substr(0, separator);
                const auto value = line.substr(separator + 1);
                if (key == "ID") record.id = value;
                else if (key == "IMPORTER_VERSION")
                {
                    try { record.importerVersion = static_cast<std::uint32_t>(std::stoul(value)); } catch (...) {}
                }
            }
            return !record.id.empty();
        }

        bool WriteMetadata(const std::filesystem::path &path, const AssetRecord &record)
        {
            std::ofstream output(path, std::ios::trunc);
            if (!output) return false;
            output << kMetadataHeader << '\n'
                   << "ID\t" << record.id << '\n'
                   << "IMPORTER_VERSION\t" << record.importerVersion << '\n';
            return output.good();
        }

        std::vector<std::string> DiscoverDependencies(const std::filesystem::path &path, const std::filesystem::path &assetRoot, std::vector<std::string> &errors)
        {
            const auto scan = ScanAssetReferences(path, {}, assetRoot);
            errors = scan.errors;
            std::set<std::string> unique;
            for (const auto &occurrence : scan.occurrences)
                if (Project::IsProjectAssetReference(occurrence.reference)) unique.insert(occurrence.reference);
            return {unique.begin(), unique.end()};
        }

        bool ShouldCook(ProjectAssetType type, const CookOptions &options)
        {
            if (type == ProjectAssetType::Script) return false;
            if (!options.includeSourceAssets && type == ProjectAssetType::Model) return false;
            return true;
        }
    }

    std::filesystem::path AssetDatabase::GetMetadataPath(const std::filesystem::path &assetPath)
    {
        return std::filesystem::path(assetPath.string() + ".plutometa");
    }

    std::uint64_t AssetDatabase::HashFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        std::uint64_t hash = 14695981039346656037ull;
        std::array<char, 64 * 1024> buffer{};
        while (input)
        {
            input.read(buffer.data(), buffer.size());
            for (std::streamsize i = 0; i < input.gcount(); ++i)
            {
                hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
                hash *= 1099511628211ull;
            }
        }
        return hash;
    }

    bool AssetDatabase::Scan(Project &project, std::string *errorMessage)
    {
        m_records.clear(); m_byId.clear(); m_byReference.clear();
        project.RefreshAssetRegistry();
        auto entries = project.GetManifest().assetEntries;
        // Model manifests are hidden from the editor registry, but runtime
        // stable-object resolution still needs them after source models are stripped.
        std::error_code scanError;
        for (std::filesystem::recursive_directory_iterator it(project.GetAssetDirectoryPath(), scanError), end;
             !scanError && it != end; it.increment(scanError))
        {
            if (it->is_regular_file() && it->path().extension() == ".plutomodel")
                entries.push_back({project.MakeAssetReference(it->path()), it->file_size(), ProjectAssetType::Unknown});
        }
        if (scanError) { SetError(errorMessage, "Cannot enumerate model manifests: " + scanError.message()); return false; }
        for (const auto &entry : entries)
        {
            if (!Project::IsProjectAssetReference(entry.reference)) continue;
            const auto path = project.ResolveAssetReference(entry.reference);
            if (path.extension() == ".plutometa" || !std::filesystem::is_regular_file(path)) continue;

            AssetRecord record;
            record.reference = entry.reference;
            record.type = entry.type;
            record.size = entry.size;
            record.contentHash = HashFile(path);
            const auto metadataPath = GetMetadataPath(path);
            if (!ReadMetadata(metadataPath, record))
            {
                record.id = GenerateId();
                if (!WriteMetadata(metadataPath, record))
                {
                    SetError(errorMessage, "Failed to write asset metadata: " + metadataPath.string());
                    return false;
                }
            }
            if (m_byId.contains(record.id))
            {
                record.id = GenerateId();
                if (!WriteMetadata(metadataPath, record)) return false;
            }
            record.dependencies = DiscoverDependencies(path, project.GetAssetDirectoryPath(), record.dependencyScanErrors);
            if (record.type == ProjectAssetType::Model)
            {
                auto modelManifest = path.parent_path() / (path.stem().string() + ".plutomodel");
                if (!std::filesystem::is_regular_file(modelManifest))
                    modelManifest = project.GetAssetDirectoryPath() / "Imported" / path.stem() / (path.stem().string() + ".plutomodel");
                if (std::filesystem::is_regular_file(modelManifest)) record.dependencies.push_back(project.MakeAssetReference(modelManifest));
            }
            m_byId[record.id] = m_records.size();
            m_byReference[record.reference] = m_records.size();
            m_records.push_back(std::move(record));
        }
        std::sort(m_records.begin(), m_records.end(), [](const auto &a, const auto &b) { return a.reference < b.reference; });
        m_byId.clear(); m_byReference.clear();
        for (std::size_t i = 0; i < m_records.size(); ++i) { m_byId[m_records[i].id] = i; m_byReference[m_records[i].reference] = i; }
        project.RefreshAssetRegistry();
        return true;
    }

    const AssetRecord *AssetDatabase::FindById(std::string_view id) const
    {
        const auto found = m_byId.find(std::string(id)); return found == m_byId.end() ? nullptr : &m_records[found->second];
    }
    const AssetRecord *AssetDatabase::FindByReference(std::string_view reference) const
    {
        const auto found = m_byReference.find(std::string(reference)); return found == m_byReference.end() ? nullptr : &m_records[found->second];
    }
    std::string AssetDatabase::ResolveId(std::string_view id) const
    {
        const auto *record = FindById(id); return record ? record->reference : std::string{};
    }

    bool CookProjectContent(Project &project, const std::filesystem::path &destination, const CookOptions &options, std::string *errorMessage)
    {
        AssetDatabase database;
        if (!database.Scan(project, errorMessage)) return false;
        for (const auto &reference : options.alwaysInclude)
        {
            if (!Project::IsProjectAssetReference(reference) || !database.FindByReference(reference))
            {
                SetError(errorMessage, "Always-include asset was not found: " + reference);
                return false;
            }
        }
        std::set<std::string> reachable;
        if (!options.includeUnreferencedAssets)
        {
            std::vector<std::string> pending{project.GetManifest().startupScene, project.GetManifest().scriptAssembly};
            pending.insert(pending.end(), options.alwaysInclude.begin(), options.alwaysInclude.end());
            while (!pending.empty())
            {
                auto reference = std::move(pending.back());
                pending.pop_back();
                if (reference.empty() || !reachable.insert(reference).second) continue;
                if (const auto *record = database.FindByReference(reference))
                {
                    if (!record->dependencyScanErrors.empty())
                    {
                        SetError(errorMessage, "Cannot determine dependencies for " + reference + ": " + record->dependencyScanErrors.front());
                        return false;
                    }
                    pending.insert(pending.end(), record->dependencies.begin(), record->dependencies.end());
                }
                else if (Project::IsProjectAssetReference(reference))
                {
                    SetError(errorMessage, "Missing cook dependency: " + reference);
                    return false;
                }
            }
        }
        std::error_code error;
        std::filesystem::create_directories(destination, error);
        if (error) { SetError(errorMessage, "Failed to create cooked asset directory: " + error.message()); return false; }

        // Preserve source-model IDs even when source bytes are not shipped.
        std::ofstream identities(destination.parent_path() / "PlutoAssetIds.manifest", std::ios::trunc);
        if (!identities) { SetError(errorMessage, "Cannot write asset identity manifest."); return false; }
        for (const auto &record : database.GetRecords()) identities << record.id << '\t' << record.reference << '\n';
        identities.close();
        if (!identities) { SetError(errorMessage, "Cannot finish asset identity manifest."); return false; }
        std::ofstream manifest(destination.parent_path() / "PlutoCook.manifest", std::ios::trunc);
        if (!manifest) { SetError(errorMessage, "Failed to create cook manifest."); return false; }
        manifest << kCookHeader << '\n';
        for (const auto &record : database.GetRecords())
        {
            if (!ShouldCook(record.type, options)) continue;
            const auto assembly = project.ResolveAssetReference(project.GetManifest().scriptAssembly);
            const auto recordPath = project.ResolveAssetReference(record.reference);
            const auto extension = recordPath.extension().string();
            const bool managedCompanion = !project.GetManifest().scriptAssembly.empty() &&
                recordPath.parent_path() == assembly.parent_path() &&
                (extension == ".dll" || extension == ".json" || extension == ".pdb");
            if (!options.includeUnreferencedAssets && !reachable.contains(record.reference) && !managedCompanion) continue;
            auto relative = std::filesystem::path(record.reference.substr(Project::kProjectAssetScheme.size()));
            const auto source = project.ResolveAssetReference(record.reference);
            const auto target = destination / relative;
            std::filesystem::create_directories(target.parent_path(), error);
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
            if (error) { SetError(errorMessage, "Failed to cook asset " + record.reference + ": " + error.message()); return false; }
            manifest << record.id << '\t' << std::hex << std::setw(16) << std::setfill('0') << record.contentHash << std::dec
                     << '\t' << record.size << '\t' << record.reference << '\n';
        }
        return manifest.good();
    }
}
