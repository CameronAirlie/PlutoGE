#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace PlutoGE::ui
{
    // Per-user editor directory (%APPDATA%/PlutoGE, $XDG_CONFIG_HOME/PlutoGE or ~/.config/PlutoGE).
    [[nodiscard]] std::filesystem::path GetEditorUserSettingsDirectory();

    // Per-user editor preferences that follow the user between projects: behaviour toggles, tool choices,
    // key bindings. Stored as sorted `key=value` lines so the file stays readable and stable. Keys are
    // dotted by feature ("console.pauseOnError", "scripts.externalEditor").
    //
    // Per-project editor state (layout, camera, bookmarks) belongs in the project's .plutoge-editor folder instead.
    class EditorSettings
    {
    public:
        // The shared instance backed by editor-settings.ini in the user settings directory.
        static EditorSettings &Get();

        explicit EditorSettings(std::filesystem::path file);

        [[nodiscard]] bool GetBool(std::string_view key, bool fallback) const;
        [[nodiscard]] int GetInt(std::string_view key, int fallback) const;
        [[nodiscard]] float GetFloat(std::string_view key, float fallback) const;
        [[nodiscard]] std::string GetString(std::string_view key, std::string_view fallback = {}) const;
        [[nodiscard]] bool Has(std::string_view key) const;

        void SetBool(std::string_view key, bool value);
        void SetInt(std::string_view key, int value);
        void SetFloat(std::string_view key, float value);
        void SetString(std::string_view key, std::string_view value);
        void Remove(std::string_view key);

        // Writes pending changes. Setters only mark the store dirty, so callers flush once per frame (or on
        // exit) instead of rewriting the file while a slider is dragged.
        bool Flush();
        [[nodiscard]] bool IsDirty() const { return m_dirty; }
        [[nodiscard]] const std::filesystem::path &GetPath() const { return m_file; }

    private:
        void Load();
        void Set(std::string_view key, std::string value);

        std::filesystem::path m_file;
        std::map<std::string, std::string, std::less<>> m_values;
        bool m_dirty = false;
    };
}
