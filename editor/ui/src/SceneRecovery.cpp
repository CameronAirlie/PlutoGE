#include "PlutoGE/ui/SceneRecovery.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PlutoGE::ui
{
    namespace
    {
        constexpr std::size_t MaxSceneBytes = 256ull * 1024 * 1024;
        std::uint64_t Hash(const std::string &value)
        {
            std::uint64_t hash = 14695981039346656037ull;
            for (unsigned char c : value) { hash ^= c; hash *= 1099511628211ull; }
            return hash;
        }
        bool Valid(const RecoverySettings &s)
        {
            return s.intervalSeconds >= 10 && s.intervalSeconds <= 3600 && s.retainedBackups >= 1 && s.retainedBackups <= 50;
        }
        bool AtomicWrite(const std::filesystem::path &path, const std::string &data, std::string &error)
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) { error = ec.message(); return false; }
            auto temp = path;
            temp += "." + std::to_string(std::random_device{}()) + ".tmp";
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            out.close();
            if (!out) { error = "Failed to write recovery file."; std::filesystem::remove(temp, ec); return false; }
#ifdef _WIN32
            const bool replaced = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
            if (!replaced) error = "Failed to publish recovery file (Windows error " + std::to_string(GetLastError()) + ").";
#else
            std::filesystem::rename(temp, path, ec);
            const bool replaced = !ec;
            if (!replaced) error = ec.message();
#endif
            if (!replaced) std::filesystem::remove(temp, ec);
            return replaced;
        }
        bool Header(std::istream &in, RecoveryBackup &backup, std::size_t &size, std::uint64_t &hash, std::string &error)
        {
            std::string header;
            // Bound metadata independently from the scene payload.
            char c = 0;
            while (in.get(c) && c != '\n' && header.size() < 16384) header.push_back(c);
            std::istringstream fields(header);
            std::string magic;
            if (c != '\n' || !(fields >> magic >> backup.created >> size >> hash >> std::quoted(backup.source)) ||
                magic != "PLUTORECOVERY1" || size == 0 || size > MaxSceneBytes || !(fields >> std::ws).eof())
            { error = "Invalid recovery header."; return false; }
            return true;
        }
    }
    std::filesystem::path SceneRecovery::Directory(const std::filesystem::path &manifest)
    {
        return manifest.parent_path() / ".plutoge-editor" / (manifest.filename().string() + ".recovery");
    }
    bool SceneRecovery::ReadSettings(const std::filesystem::path &directory, RecoverySettings &settings, std::string &error)
    {
        error.clear();
        std::error_code ec;
        const auto path = directory / "settings";
        if (!std::filesystem::exists(path, ec) && !ec) { settings = {}; return true; }
        if (std::filesystem::file_size(path, ec) > 4096 || ec)
        { error = "Recovery settings are unreadable or too large."; return false; }
        std::ifstream in(path);
        std::string magic;
        RecoverySettings staged;
        if (!(in >> magic >> staged.enabled >> staged.intervalSeconds >> staged.retainedBackups) ||
            magic != "PLUTORECOVERYSETTINGS1" || !Valid(staged) || !(in >> std::ws).eof())
        { error = "Invalid or unreadable recovery settings; autosave is disabled until settings are saved."; return false; }
        settings = staged;
        return true;
    }
    bool SceneRecovery::WriteSettings(const std::filesystem::path &directory, const RecoverySettings &settings, std::string &error)
    {
        error.clear();
        if (!Valid(settings)) { error = "Use an interval of 10–3600 seconds and 1–50 backups."; return false; }
        std::ostringstream out;
        out << "PLUTORECOVERYSETTINGS1 " << settings.enabled << ' ' << settings.intervalSeconds << ' ' << settings.retainedBackups << '\n';
        return AtomicWrite(directory / "settings", out.str(), error);
    }
    std::vector<RecoveryBackup> SceneRecovery::List(const std::filesystem::path &directory, std::vector<std::string> &errors)
    {
        errors.clear();
        std::vector<RecoveryBackup> result;
        std::error_code ec;
        if (!std::filesystem::exists(directory, ec) && !ec) return result;
        std::filesystem::directory_iterator it(directory, ec), end;
        while (!ec && it != end)
        {
            const auto path = it->path();
            if (path.extension() == ".recovery" && it->is_regular_file(ec))
            {
                RecoveryBackup backup{path};
                std::size_t size = 0;
                std::uint64_t hash = 0;
                std::string error;
                std::ifstream in(path, std::ios::binary);
                if (Header(in, backup, size, hash, error)) result.push_back(std::move(backup));
                else errors.push_back(path.filename().string() + ": " + error);
            }
            if (!ec) it.increment(ec);
        }
        if (ec) errors.push_back(ec.message());
        std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
            return a.created != b.created ? a.created > b.created : a.path > b.path;
        });
        return result;
    }
    bool SceneRecovery::Read(const RecoveryBackup &backup, std::string &scene, std::string &error)
    {
        error.clear();
        std::ifstream in(backup.path, std::ios::binary);
        RecoveryBackup staged;
        std::size_t size = 0;
        std::uint64_t hash = 0;
        if (!Header(in, staged, size, hash, error)) return false;
        std::string data(size, '\0');
        if (!in.read(data.data(), static_cast<std::streamsize>(size)) || in.peek() != std::char_traits<char>::eof() || Hash(data) != hash)
        { error = "Recovery backup is truncated or corrupt."; return false; }
        scene = std::move(data);
        return true;
    }
    bool SceneRecovery::Save(const std::filesystem::path &directory, const std::string &source, const std::string &scene,
                             int retain, std::string &error)
    {
        error.clear();
        if (scene.empty() || scene.size() > MaxSceneBytes || source.size() > 8192 || source.find_first_of("\r\n") != std::string::npos || retain < 1 || retain > 50)
        { error = "Recovery payload or retention is outside supported limits."; return false; }
        const auto created = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::ostringstream out;
        out << "PLUTORECOVERY1 " << created << ' ' << scene.size() << ' ' << Hash(scene) << ' ' << std::quoted(source) << '\n' << scene;
        const auto path = directory / (std::to_string(created) + "-" + std::to_string(std::random_device{}()) + ".recovery");
        if (!AtomicWrite(path, out.str(), error)) return false;
        std::vector<std::string> errors;
        const auto backups = List(directory, errors);
        for (std::size_t i = static_cast<std::size_t>(retain); i < backups.size(); ++i)
        {
            std::error_code ec;
            std::filesystem::remove(backups[i].path, ec);
            if (ec) errors.push_back("Could not rotate backup: " + ec.message());
        }
        if (!errors.empty()) error = "Backup saved; " + errors.front();
        return true;
    }
}
