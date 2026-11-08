#include "PlutoGE/ui/AssetHotReload.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr auto kQuietPeriod = std::chrono::milliseconds(400);

        std::string LowerExtension(const std::filesystem::path &path)
        {
            auto extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }
    }

    std::optional<HotReloadKind> ClassifyHotReloadPath(const std::filesystem::path &path)
    {
        const auto extension = LowerExtension(path);
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp" ||
            extension == ".hdr" || extension == ".psd" || extension == ".gif")
            return HotReloadKind::Texture;
        if (extension == ".plutomaterial" || extension == ".mat")
            return HotReloadKind::Material;
        if (extension == ".wav" || extension == ".ogg" || extension == ".mp3" || extension == ".flac")
            return HotReloadKind::Audio;
        return std::nullopt;
    }

    AssetFileWatch::Snapshot AssetFileWatch::Capture(const std::filesystem::path &assetRoot)
    {
        Snapshot result;
        std::error_code error;
        if (!std::filesystem::is_directory(assetRoot, error))
            return result;
        for (std::filesystem::recursive_directory_iterator it(assetRoot, std::filesystem::directory_options::skip_permission_denied, error), end;
             it != end; it.increment(error))
        {
            if (error)
                break;
            if (it->is_symlink(error))
            {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_directory(error))
            {
                const auto name = it->path().filename().string();
                // Generated and per-user folders change constantly and hold nothing to reload.
                if (name == "Library" || name == "Build" || name == "Managed" || name == "bin" || name == "obj" || name.starts_with("."))
                    it.disable_recursion_pending();
                continue;
            }
            if (!ClassifyHotReloadPath(it->path()))
                continue;
            std::error_code statError;
            const auto modified = it->last_write_time(statError);
            const auto size = it->file_size(statError);
            if (!statError)
                result.emplace(it->path().generic_string(), Stamp{modified, size});
        }
        return result;
    }

    std::vector<std::string> AssetFileWatch::Observe(Snapshot snapshot, Clock::time_point now)
    {
        if (!m_primed)
        {
            m_primed = true;
            m_committed = std::move(snapshot);
            m_candidate = m_committed;
            return {};
        }
        if (snapshot != m_candidate)
        {
            m_candidate = std::move(snapshot);
            m_candidateSince = now;
            return {};
        }
        if (m_candidate == m_committed || now - m_candidateSince < kQuietPeriod)
            return {};

        std::vector<std::string> changed;
        for (const auto &[path, stamp] : m_candidate)
        {
            const auto previous = m_committed.find(path);
            if (previous == m_committed.end() || previous->second != stamp)
                changed.push_back(path);
        }
        m_committed = m_candidate;
        return changed;
    }
}
