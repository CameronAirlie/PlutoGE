#include "PlutoGE/platform/FilesystemPaths.h"
#include "PlutoGE/asset_import/ImportFileTransaction.h"
#include "PlutoGE/assets/AssetMetadata.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cctype>
#include <set>
#include <sstream>
#include <stdexcept>

namespace PlutoGE::assetimport
{
    namespace
    {
        std::filesystem::path ResolveOutputParent(const std::filesystem::path &path)
        {
            std::filesystem::path resolved;
            std::string error;
            if (!content::ResolveDirectoryForCreation(path, resolved, &error)) throw std::runtime_error(error);
            return resolved;
        }

        bool IsRelativeOutput(const std::filesystem::path &path)
        {
            if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
            for (const auto &part : path)
                if (part == ".." || part == "." || part.empty()) return false;
            return true;
        }
    }

    ImportFileTransaction::ImportFileTransaction(const std::filesystem::path &projectRoot)
    {
        const auto stagingRoot = projectRoot / ".pluto-import-transactions";
        std::filesystem::create_directories(stagingRoot);
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto candidate = stagingRoot / assets::GenerateAssetId();
            if (std::filesystem::create_directory(candidate))
            {
                m_root = candidate;
                m_outputRoot = m_root / "Output";
                std::filesystem::create_directory(m_outputRoot);
                return;
            }
        }
        throw std::runtime_error("Cannot allocate import staging directory.");
    }

    ImportFileTransaction::~ImportFileTransaction()
    {
        std::string error;
        if (!m_accepted && !Rollback(&error))
        {
            std::cerr << error << " Backups retained at " << m_root << '\n';
            return;
        }
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    bool ImportFileTransaction::Publish(const std::filesystem::path &assetRoot,
                                       const std::vector<std::filesystem::path> &relativeOutputs, std::string *errorMessage)
    {
        if (!m_published.empty() || m_accepted)
        {
            if (errorMessage) *errorMessage = "Import transaction was already published.";
            return false;
        }
        std::set<std::string> unique;
        try
        {
            const auto canonicalRoot = std::filesystem::weakly_canonical(assetRoot);
            for (const auto &relative : relativeOutputs)
            {
                auto key = relative.generic_string();
#ifdef _WIN32
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
                if (!IsRelativeOutput(relative) || !unique.insert(key).second ||
                    !std::filesystem::is_regular_file(m_outputRoot / relative))
                    throw std::runtime_error("Invalid, duplicate, or missing staged output: " + relative.string());
                PublishedFile file;
                file.destination = assetRoot / relative;
                file.backup = m_root / "Backup" / relative;
                const auto parent = ResolveOutputParent(file.destination.parent_path());
                const auto within = parent.lexically_relative(canonicalRoot);
                if (within.empty() && parent != canonicalRoot) throw std::runtime_error("Cannot resolve output parent.");
                for (const auto &part : within)
                    if (part == "..") throw std::runtime_error("Import output parent escapes asset root.");
                const auto status = std::filesystem::symlink_status(file.destination);
                if (status.type() != std::filesystem::file_type::not_found && status.type() != std::filesystem::file_type::regular)
                    throw std::runtime_error("Import destination is not a regular file: " + file.destination.string());
                file.hadOriginal = status.type() == std::filesystem::file_type::regular;
                m_published.push_back(std::move(file));
            }
            // Record the whole intent before moving any existing file. Original
            // backups and remaining staged files determine recovery progress.
            std::ofstream journal(m_root / "journal.pending", std::ios::binary);
            journal << "PLUTOIMPORT\t1\n";
            for (std::size_t index = 0; index < relativeOutputs.size(); ++index)
                journal << std::quoted(relativeOutputs[index].generic_string()) << ' ' << m_published[index].hadOriginal << '\n';
            journal.close();
            if (!journal) throw std::runtime_error("Cannot finish import recovery journal.");
            std::filesystem::rename(m_root / "journal.pending", m_root / "journal");
            for (std::size_t index = 0; index < relativeOutputs.size(); ++index)
            {
                auto &published = m_published[index];
                std::filesystem::create_directories(published.destination.parent_path());
                if (published.hadOriginal)
                {
                    std::filesystem::create_directories(published.backup.parent_path());
                    std::filesystem::rename(published.destination, published.backup);
                }
                std::filesystem::rename(m_outputRoot / relativeOutputs[index], published.destination);
                published.installed = true;
            }
        }
        catch (const std::exception &exception)
        {
            std::string rollbackError;
            const bool restored = Rollback(&rollbackError);
            if (errorMessage) *errorMessage = std::string("Cannot publish import: ") + exception.what() +
                                               (restored ? "" : "; " + rollbackError);
            return false;
        }
        return true;
    }

    bool ImportFileTransaction::Rollback(std::string *errorMessage)
    {
        bool success = true;
        for (auto it = m_published.rbegin(); it != m_published.rend(); ++it)
        {
            std::error_code error;
            if (it->installed)
            {
                std::filesystem::remove(it->destination, error);
                if (!error) it->installed = false;
            }
            if (!error && it->hadOriginal && std::filesystem::exists(it->backup, error))
            {
                std::filesystem::rename(it->backup, it->destination, error);
                if (!error) it->hadOriginal = false;
            }
            if (error)
            {
                success = false;
                if (errorMessage) *errorMessage = "Import rollback failed: " + it->destination.string() + ": " + error.message();
            }
        }
        if (success) m_published.clear();
        return success;
    }
    bool ImportFileTransaction::Accept(std::string *errorMessage)
    {
        std::ofstream accepted(m_root / "accepted.pending", std::ios::binary);
        accepted << "PLUTOIMPORT_ACCEPTED\n";
        accepted.close();
        if (!accepted)
        {
            if (errorMessage) *errorMessage = "Cannot record accepted import generation.";
            return false;
        }
        std::error_code error;
        std::filesystem::rename(m_root / "accepted.pending", m_root / "accepted", error);
        if (error)
        {
            if (errorMessage) *errorMessage = "Cannot publish accepted import marker: " + error.message();
            return false;
        }
        m_accepted = true;
        return true;
    }

    bool ImportFileTransaction::Recover(const std::filesystem::path &projectRoot,
                                        const std::filesystem::path &assetRoot, std::string *errorMessage)
    {
        const auto stagingRoot = projectRoot / ".pluto-import-transactions";
        try
        {
            if (!std::filesystem::exists(stagingRoot)) return true;
            const auto canonicalRoot = std::filesystem::weakly_canonical(assetRoot);
            for (const auto &entry : std::filesystem::directory_iterator(stagingRoot))
            {
                if (entry.is_symlink() || !entry.is_directory()) continue;
                const auto root = entry.path();
                if (std::filesystem::is_regular_file(root / "accepted"))
                {
                    std::ifstream accepted(root / "accepted");
                    std::string marker;
                    std::getline(accepted, marker);
                    accepted.close();
                    if (marker != "PLUTOIMPORT_ACCEPTED") throw std::runtime_error("Invalid accepted import marker.");
                    std::filesystem::remove_all(root);
                    continue;
                }
                std::ifstream journal(root / "journal", std::ios::binary);
                if (!journal) continue; // No intent was published; never guess about an unknown directory.
                std::string header;
                std::getline(journal, header);
                if (header != "PLUTOIMPORT\t1") throw std::runtime_error("Invalid import recovery journal.");
                std::vector<std::pair<std::filesystem::path, bool>> intent;
                std::set<std::string> unique;
                std::string line;
                while (std::getline(journal, line))
                {
                    std::istringstream record(line);
                    std::string name;
                    int original = -1;
                    if (!(record >> std::quoted(name) >> original) || (original != 0 && original != 1))
                        throw std::runtime_error("Truncated or invalid import recovery journal.");
                    record >> std::ws;
                    if (!record.eof()) throw std::runtime_error("Trailing import recovery fields.");
                    const auto relative = std::filesystem::u8path(name);
                    auto key = relative.generic_string();
#ifdef _WIN32
                    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
                    if (!IsRelativeOutput(relative) || !unique.insert(key).second)
                        throw std::runtime_error("Invalid or duplicate import recovery path.");
                    const auto parent = ResolveOutputParent((assetRoot / relative).parent_path());
                    const auto within = parent.lexically_relative(canonicalRoot);
                    if (within.empty() && parent != canonicalRoot) throw std::runtime_error("Cannot resolve recovery output parent.");
                    for (const auto &part : within)
                        if (part == "..") throw std::runtime_error("Recovery output escapes asset root.");
                    intent.emplace_back(relative, original);
                }
                if (!journal.eof()) throw std::runtime_error("Cannot read import recovery journal.");
                journal.close();
                for (auto it = intent.rbegin(); it != intent.rend(); ++it)
                {
                    const auto destination = assetRoot / it->first;
                    const auto backup = root / "Backup" / it->first;
                    if (it->second && std::filesystem::is_regular_file(backup))
                    {
                        std::filesystem::remove(destination);
                        std::filesystem::create_directories(destination.parent_path());
                        std::filesystem::rename(backup, destination);
                    }
                    else if (!it->second && !std::filesystem::exists(root / "Output" / it->first))
                    {
                        std::filesystem::remove(destination);
                    }
                }
                std::filesystem::remove_all(root);
            }
        }
        catch (const std::exception &exception)
        {
            if (errorMessage) *errorMessage = std::string("Import recovery failed; backups retained: ") + exception.what();
            return false;
        }
        return true;
    }

}
