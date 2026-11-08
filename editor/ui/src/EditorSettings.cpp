#include "PlutoGE/ui/EditorSettings.h"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace PlutoGE::ui
{
    namespace
    {
        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        // Values are single-line; escape the characters that would break that.
        std::string Escape(std::string_view value)
        {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char c : value)
            {
                switch (c)
                {
                case '\\': escaped += "\\\\"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                default: escaped += c; break;
                }
            }
            return escaped;
        }

        std::string Unescape(std::string_view value)
        {
            std::string text;
            text.reserve(value.size());
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                if (value[i] != '\\' || i + 1 == value.size())
                {
                    text += value[i];
                    continue;
                }
                const char next = value[++i];
                text += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
            }
            return text;
        }
    }

    std::filesystem::path GetEditorUserSettingsDirectory()
    {
#ifdef _WIN32
        if (const char *appData = std::getenv("APPDATA"); appData != nullptr && appData[0] != '\0')
            return std::filesystem::path(appData) / "PlutoGE";
#else
        if (const char *configHome = std::getenv("XDG_CONFIG_HOME"); configHome != nullptr && configHome[0] != '\0')
            return std::filesystem::path(configHome) / "PlutoGE";
        if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
            return std::filesystem::path(home) / ".config" / "PlutoGE";
#endif
        return std::filesystem::current_path();
    }

    EditorSettings &EditorSettings::Get()
    {
        static EditorSettings settings(GetEditorUserSettingsDirectory() / "editor-settings.ini");
        return settings;
    }

    EditorSettings::EditorSettings(std::filesystem::path file) : m_file(std::move(file))
    {
        Load();
    }

    void EditorSettings::Load()
    {
        m_values.clear();
        m_dirty = false;
        std::ifstream input(m_file);
        std::string line;
        while (std::getline(input, line))
        {
            const std::string_view text = Trim(line);
            if (text.empty() || text.front() == '#' || text.front() == ';')
                continue;
            const auto separator = text.find('=');
            if (separator == std::string_view::npos)
                continue;
            const auto key = Trim(text.substr(0, separator));
            if (!key.empty())
                m_values.insert_or_assign(std::string(key), Unescape(Trim(text.substr(separator + 1))));
        }
    }

    bool EditorSettings::Has(std::string_view key) const
    {
        return m_values.find(key) != m_values.end();
    }

    bool EditorSettings::GetBool(std::string_view key, bool fallback) const
    {
        const auto it = m_values.find(key);
        if (it == m_values.end())
            return fallback;
        if (it->second == "true" || it->second == "1")
            return true;
        if (it->second == "false" || it->second == "0")
            return false;
        return fallback;
    }

    int EditorSettings::GetInt(std::string_view key, int fallback) const
    {
        const auto it = m_values.find(key);
        if (it == m_values.end())
            return fallback;
        int value = fallback;
        const auto *begin = it->second.data();
        const auto *end = begin + it->second.size();
        return std::from_chars(begin, end, value).ec == std::errc{} ? value : fallback;
    }

    float EditorSettings::GetFloat(std::string_view key, float fallback) const
    {
        const auto it = m_values.find(key);
        if (it == m_values.end())
            return fallback;
        float value = fallback;
        const auto *begin = it->second.data();
        const auto *end = begin + it->second.size();
        return std::from_chars(begin, end, value).ec == std::errc{} ? value : fallback;
    }

    std::string EditorSettings::GetString(std::string_view key, std::string_view fallback) const
    {
        const auto it = m_values.find(key);
        return it == m_values.end() ? std::string(fallback) : it->second;
    }

    void EditorSettings::Set(std::string_view key, std::string value)
    {
        if (key.empty())
            return;
        const auto it = m_values.find(key);
        if (it != m_values.end() && it->second == value)
            return;
        m_values.insert_or_assign(std::string(key), std::move(value));
        m_dirty = true;
    }

    void EditorSettings::SetBool(std::string_view key, bool value) { Set(key, value ? "true" : "false"); }
    void EditorSettings::SetInt(std::string_view key, int value) { Set(key, std::to_string(value)); }

    void EditorSettings::SetFloat(std::string_view key, float value)
    {
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        Set(key, std::string(buffer, result.ptr));
    }

    void EditorSettings::SetString(std::string_view key, std::string_view value) { Set(key, std::string(value)); }

    void EditorSettings::Remove(std::string_view key)
    {
        const auto it = m_values.find(key);
        if (it == m_values.end())
            return;
        m_values.erase(it);
        m_dirty = true;
    }

    bool EditorSettings::Flush()
    {
        if (!m_dirty)
            return true;

        std::error_code error;
        std::filesystem::create_directories(m_file.parent_path(), error);
        // Write beside the file and swap it in, so a crash never leaves a truncated settings file.
        auto temporary = m_file;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::out | std::ios::trunc);
            if (!output)
                return false;
            output << "# PlutoGE editor settings\n";
            for (const auto &[key, value] : m_values)
                output << key << '=' << Escape(value) << '\n';
            if (!output)
                return false;
        }
        std::filesystem::rename(temporary, m_file, error);
        if (error)
        {
            std::filesystem::remove(temporary, error);
            return false;
        }
        m_dirty = false;
        return true;
    }
}
