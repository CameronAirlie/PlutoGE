#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

namespace PlutoGE::ui
{
    struct RecoverySettings
    {
        bool enabled = true;
        int intervalSeconds = 120;
        int retainedBackups = 10;
    };
    struct RecoveryBackup
    {
        std::filesystem::path path;
        std::string source;
        std::int64_t created = 0;
    };
    // Filesystem-only storage. Scene serialization and user approval belong to the editor.
    class SceneRecovery
    {
    public:
        static std::filesystem::path Directory(const std::filesystem::path &manifest);
        static bool ReadSettings(const std::filesystem::path &directory, RecoverySettings &settings, std::string &error);
        static bool WriteSettings(const std::filesystem::path &directory, const RecoverySettings &settings, std::string &error);
        static bool Save(const std::filesystem::path &directory, const std::string &source, const std::string &scene,
                         int retain, std::string &error);
        static std::vector<RecoveryBackup> List(const std::filesystem::path &directory, std::vector<std::string> &errors);
        static bool Read(const RecoveryBackup &backup, std::string &scene, std::string &error);
    };
}
