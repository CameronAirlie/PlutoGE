#include "PlutoGE/asset_import/AssetMigrationBackup.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/ProjectAssetLock.h"
#include "PlutoGE/platform/FilesystemPaths.h"
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace PlutoGE::assetimport
{
    namespace
    {
        constexpr std::size_t MaxFiles = 4096, MaxManifestBytes = 4 * 1024 * 1024;
        std::string Utf8(const std::filesystem::path &path)
        {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char *>(value.data()), value.size()};
        }
        void CheckRelative(const std::filesystem::path &path)
        {
            const auto value = Utf8(path);
            if (path.empty() || path.has_root_path() || value.size() > 65536 ||
                value.find_first_of("\r\n\t\0", 0, 4) != std::string::npos)
                throw std::runtime_error("Invalid migration backup relative path.");
            for (const auto &part : path)
            {
                if (part == "." || part == ".." || part.empty()) throw std::runtime_error("Backup path contains traversal or empty segments.");
#ifdef _WIN32
                const auto component = Utf8(part);
                if (component.ends_with('.') || component.ends_with(' ') || component.find(':') != std::string::npos)
                    throw std::runtime_error("Backup path contains an ambiguous Windows component.");
#endif
            }
        }
        void OrdinaryChain(const std::filesystem::path &root, const std::filesystem::path &relative, bool file)
        {
            auto path = root;
            if (!std::filesystem::is_directory(std::filesystem::symlink_status(path)))
                throw std::runtime_error("Backup root must be an ordinary directory.");
            for (const auto &part : relative)
            {
                path /= part;
                const auto status = std::filesystem::symlink_status(path);
                if (std::filesystem::is_symlink(status) ||
                    (path == root / relative && file ? !std::filesystem::is_regular_file(status) : !std::filesystem::is_directory(status)))
                    throw std::runtime_error("Backup inputs must use ordinary files and directories.");
            }
            if (!content::IsPathWithinDirectory(std::filesystem::canonical(root / relative), std::filesystem::canonical(root), true))
                throw std::runtime_error("Backup path escapes its root.");
        }
        void CheckHash(const std::filesystem::path &path, const content::ContentDigest &expected)
        {
            content::ContentDigest actual;
            std::string error;
            if (!content::HashFileContent(path, actual, &error) || actual != expected)
                throw std::runtime_error("Migration backup hash mismatch: " + path.string());
        }
        void CheckCancelled(std::stop_token stop)
        { if (stop.stop_requested()) throw std::runtime_error("Migration backup cancelled."); }
        void CreateOrdinaryDirectories(const std::filesystem::path &root, const std::filesystem::path &relative)
        {
            auto current = root;
            OrdinaryChain(root, {}, false);
            for (const auto &part : relative)
            {
                if (part.empty()) continue;
                current /= part;
                std::filesystem::create_directory(current);
                if (!std::filesystem::is_directory(std::filesystem::symlink_status(current)))
                    throw std::runtime_error("Backup directory is not ordinary.");
            }
        }
    }

    bool VerifyAssetMigrationBackup(const std::filesystem::path &directory, MigrationBackup &output,
        std::string *errorMessage, std::stop_token stop)
    {
        try
        {
            CheckCancelled(stop);
            const auto root = std::filesystem::absolute(directory).lexically_normal();
            OrdinaryChain(root, "manifest", true);
            std::error_code markerError;
            const auto marker = std::filesystem::symlink_status(root / "INCOMPLETE", markerError);
            if ((markerError && markerError != std::errc::no_such_file_or_directory) || marker.type() != std::filesystem::file_type::not_found)
                throw std::runtime_error("Migration backup is incomplete or its marker cannot be inspected.");
            const auto size = std::filesystem::file_size(root / "manifest");
            if (size > MaxManifestBytes) throw std::runtime_error("Backup manifest exceeds its limit.");
            std::string manifestBytes(static_cast<std::size_t>(size), '\0');
            std::ifstream manifestFile(root / "manifest", std::ios::binary);
            manifestFile.read(manifestBytes.data(), static_cast<std::streamsize>(manifestBytes.size()));
            if (!manifestFile || manifestFile.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Backup manifest changed while reading.");
            std::istringstream input(manifestBytes);
            std::string header, line;
            if (!std::getline(input, header) || header != "PLUTOMIGRATIONBACKUP\t1") throw std::runtime_error("Unsupported migration backup manifest.");
            MigrationBackup candidate{root, {}};
            std::set<std::filesystem::path> paths;
            std::size_t bytes = header.size() + 1;
            while (std::getline(input, line))
            {
                CheckCancelled(stop);
                bytes += line.size() + 1;
                if (bytes > MaxManifestBytes || candidate.files.size() >= MaxFiles) throw std::runtime_error("Backup manifest exceeds inventory limits.");
                std::istringstream record(line);
                std::string token, name, digest, extra;
                if (!(record >> token >> std::quoted(name) >> digest) || token != "FILE" || record >> extra)
                    throw std::runtime_error("Malformed migration backup record.");
                MigrationBackupFile file{std::filesystem::u8path(name), {}};
                CheckRelative(file.projectRelativePath);
                if (!paths.insert(file.projectRelativePath).second || !content::ParseContentDigest(digest, file.contentHash))
                    throw std::runtime_error("Duplicate backup location or invalid digest.");
                const auto relative = std::filesystem::path("Files") / file.projectRelativePath;
                OrdinaryChain(root, relative, true);
                CheckHash(root / relative, file.contentHash);
                candidate.files.push_back(std::move(file));
            }
            if (!input.eof() || candidate.files.empty()) throw std::runtime_error("Unreadable or empty backup manifest.");
            CheckHash(root / "manifest", content::HashContent(std::as_bytes(std::span(manifestBytes.data(), manifestBytes.size()))));
            CheckCancelled(stop);
            output = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error) { if (errorMessage) *errorMessage = error.what(); return false; }
    }

    bool CreateAssetMigrationBackup(const assets::Project &project, const std::vector<MigrationBackupFile> &files,
        MigrationBackup &output, std::string *errorMessage, std::stop_token stop)
    {
        std::filesystem::path attempt;
        try
        {
            CheckCancelled(stop);
            if (files.empty() || files.size() > MaxFiles) throw std::runtime_error("Invalid migration backup inventory size.");
            assets::ProjectAssetLock lock;
            std::string error;
            if (!lock.TryAcquire(project.GetRootDirectory(), &error) ||
                !assets::ValidateNoPendingAssetTransactions(project.GetRootDirectory(), &error)) throw std::runtime_error(error);
            const auto root = std::filesystem::canonical(project.GetRootDirectory());
            std::set<std::filesystem::path> paths;
            for (const auto &file : files)
            {
                CheckCancelled(stop); CheckRelative(file.projectRelativePath);
                const auto path = root / file.projectRelativePath;
                if (!paths.insert(file.projectRelativePath).second || assets::IsAssetInfrastructurePath(root, path))
                    throw std::runtime_error("Duplicate or infrastructure backup input.");
                OrdinaryChain(root, file.projectRelativePath, true); CheckHash(path, file.contentHash);
            }
            const auto backups = root / ".pluto-migration-backups";
            std::filesystem::create_directory(backups);
            OrdinaryChain(root, ".pluto-migration-backups", false);
            attempt = backups / assets::GenerateAssetId();
            if (!std::filesystem::create_directory(attempt)) throw std::runtime_error("Backup directory already exists.");
            {
                std::ofstream marker(attempt / "INCOMPLETE", std::ios::binary);
                marker << "Migration backup has not passed verification.\n"; marker.close();
                if (!marker) throw std::runtime_error("Cannot mark incomplete backup.");
            }
            std::ostringstream manifest;
            manifest << "PLUTOMIGRATIONBACKUP\t1\n";
            for (const auto &file : files)
            {
                CheckCancelled(stop);
                const auto source = root / file.projectRelativePath;
                OrdinaryChain(root, file.projectRelativePath, true); CheckHash(source, file.contentHash);
                const auto destination = attempt / "Files" / file.projectRelativePath;
                CreateOrdinaryDirectories(attempt, std::filesystem::path("Files") / file.projectRelativePath.parent_path());
                OrdinaryChain(attempt, std::filesystem::path("Files") / file.projectRelativePath.parent_path(), false);
                if (!std::filesystem::copy_file(source, destination)) throw std::runtime_error("Cannot copy migration backup file.");
                OrdinaryChain(attempt, std::filesystem::path("Files") / file.projectRelativePath, true);
                CheckHash(destination, file.contentHash); CheckHash(source, file.contentHash);
                manifest << "FILE\t" << std::quoted(Utf8(file.projectRelativePath)) << '\t' << content::DigestToHex(file.contentHash) << '\n';
                if (manifest.tellp() > MaxManifestBytes) throw std::runtime_error("Backup manifest exceeds its limit.");
            }
            for (const auto &file : files)
            { CheckCancelled(stop); OrdinaryChain(root, file.projectRelativePath, true); CheckHash(root / file.projectRelativePath, file.contentHash); }
            std::ofstream record(attempt / "manifest", std::ios::binary);
            record << manifest.str(); record.close();
            if (!record) throw std::runtime_error("Cannot write migration backup manifest.");
            CheckCancelled(stop);
            if (!std::filesystem::remove(attempt / "INCOMPLETE")) throw std::runtime_error("Cannot seal migration backup.");
            MigrationBackup verified;
            if (!VerifyAssetMigrationBackup(attempt, verified, &error, stop))
            {
                std::ofstream marker(attempt / "INCOMPLETE", std::ios::binary);
                marker << "Final backup verification failed.\n";
                throw std::runtime_error(error);
            }
            output = std::move(verified);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        { if (errorMessage) *errorMessage = std::string(error.what()) + (attempt.empty() ? "" : " Backup attempt: " + attempt.string()); return false; }
    }
}
