#include "PlutoGE/ui/SceneRecovery.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
    struct Scratch
    {
        const std::filesystem::path parent = std::filesystem::absolute(std::filesystem::temp_directory_path()).lexically_normal();
        const std::filesystem::path root = parent / ("PlutoGE-recovery-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Scratch() { std::filesystem::create_directories(root); }
        ~Scratch()
        {
            if (root.parent_path() == parent && root.filename().string().starts_with("PlutoGE-recovery-tests-"))
            { std::error_code ec; std::filesystem::remove_all(root, ec); }
        }
    };
}
int main()
{
    try
    {
        using namespace PlutoGE::ui;
        Scratch scratch;
        const auto directory = SceneRecovery::Directory(scratch.root / "Game.plutoproject");
        const auto other = SceneRecovery::Directory(scratch.root / "Other.plutoproject");
        std::string error;
        RecoverySettings settings;
        Require(SceneRecovery::ReadSettings(directory, settings, error) && settings.enabled, "Default settings failed");
        settings.intervalSeconds = 30; settings.retainedBackups = 2;
        Require(SceneRecovery::WriteSettings(directory, settings, error), "Settings save failed");
        settings.enabled = false;
        Require(SceneRecovery::WriteSettings(directory, settings, error), "Atomic settings replacement failed");
        RecoverySettings loaded;
        Require(SceneRecovery::ReadSettings(directory, loaded, error) && !loaded.enabled && loaded.intervalSeconds == 30, "Settings roundtrip failed");
        settings.intervalSeconds = -1;
        Require(!SceneRecovery::WriteSettings(directory, settings, error), "Invalid settings accepted");
        Require(SceneRecovery::ReadSettings(directory, loaded, error) && loaded.intervalSeconds == 30, "Invalid save damaged settings");
        std::vector<std::string> issues;
        Require(SceneRecovery::List(directory, issues).empty(), "Unexpected backup");
        Require(SceneRecovery::Save(directory, "", "untitled scene", 2, error), "Untitled backup failed");
        auto backups = SceneRecovery::List(directory, issues);
        Require(backups.size() == 1 && backups[0].source.empty(), "Unsaved source was not retained");
        std::string data;
        Require(SceneRecovery::Read(backups[0], data, error) && data == "untitled scene", "Backup read failed");
        const auto oldest = backups[0].path;
        Require(SceneRecovery::Save(directory, "C:/Scene with spaces.plutoscene", "second", 2, error), "Second save failed");
        Require(SceneRecovery::Save(directory, "C:/Scene with spaces.plutoscene", "third", 2, error), "Third save failed");
        backups = SceneRecovery::List(directory, issues);
        Require(backups.size() == 2 && !std::filesystem::exists(oldest), "Rotation failed");
        Require(SceneRecovery::Read(backups[0], data, error) && data == "third", "Newest ordering failed");
        Require(SceneRecovery::List(other, issues).empty(), "Projects share backups");
        std::ofstream(directory / "interrupted.tmp") << "incomplete";
        Require(SceneRecovery::List(directory, issues).size() == 2 && issues.empty(), "Interrupted publication surfaced as backup");
        const auto corrupt = backups[0];
        {
            std::fstream file(corrupt.path, std::ios::in | std::ios::out | std::ios::binary);
            file.seekp(-1, std::ios::end); file.put('X');
        }
        data = "unchanged";
        Require(!SceneRecovery::Read(corrupt, data, error) && data == "unchanged", "Corruption changed recovery output");
        std::filesystem::resize_file(backups[1].path, 4);
        Require(!SceneRecovery::Read(backups[1], data, error), "Truncated header accepted");
        SceneRecovery::List(directory, issues);
        Require(!issues.empty(), "Malformed backup not reported");
        std::filesystem::remove(corrupt.path);
        Require(!SceneRecovery::Read(corrupt, data, error), "Missing backup accepted");
        const auto blocked = scratch.root / "file";
        std::ofstream(blocked) << "file";
        Require(!SceneRecovery::Save(blocked / "child", "", "data", 2, error), "Failed writes reported success");
        std::ofstream(directory / "settings") << "malformed";
        Require(!SceneRecovery::ReadSettings(directory, loaded, error), "Malformed settings accepted");
        std::cout << "PASS: recovery settings, atomic writes, rotation, isolation, interruption, corruption and missing files\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
