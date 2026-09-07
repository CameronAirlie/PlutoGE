#include "PlutoGE/ui/ViewportBookmarks.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <random>
#include <set>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PlutoGE::ui
{
    bool ViewportBookmarks::Validate(const std::vector<ViewportBookmark> &bookmarks, std::string &error)
    {
        error.clear();
        if (bookmarks.size() > MaxBookmarks)
        {
            error = "A project can have at most 128 viewport bookmarks.";
            return false;
        }
        std::set<std::string> names;
        for (const auto &bookmark : bookmarks)
        {
            if (bookmark.name.empty() || bookmark.name.size() > 128 ||
                bookmark.name.find_first_not_of(" \t\r\n") == std::string::npos ||
                std::any_of(bookmark.name.begin(), bookmark.name.end(), [](unsigned char c) { return c < 32; }) ||
                !names.insert(bookmark.name).second)
            {
                error = "Bookmark names must be unique, nonblank, and at most 128 bytes without control characters.";
                return false;
            }
            for (float value : {bookmark.position.x, bookmark.position.y, bookmark.position.z,
                    bookmark.yaw, bookmark.pitch, bookmark.fov, bookmark.nearPlane, bookmark.farPlane,
                    bookmark.moveSpeed, bookmark.orthographicSize})
            {
                if (!std::isfinite(value))
                {
                    error = "Bookmark camera values must be finite.";
                    return false;
                }
            }
            if (bookmark.fov <= 0 || bookmark.fov >= 180 || bookmark.nearPlane <= 0 ||
                bookmark.farPlane <= bookmark.nearPlane || bookmark.moveSpeed < 0.1f ||
                bookmark.moveSpeed > 1000 || bookmark.orthographicSize < 0.01f ||
                bookmark.pitch < -90 || bookmark.pitch > 90)
            {
                error = "Bookmark camera settings are outside the supported range.";
                return false;
            }
        }
        return true;
    }

    bool ViewportBookmarks::Load(const std::filesystem::path &path, std::vector<ViewportBookmark> &output, std::string &error)
    {
        error.clear();
        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        if (ec)
        {
            error = "Cannot inspect bookmark file: " + ec.message();
            return false;
        }
        if (!exists)
        {
            output.clear();
            return true;
        }
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size > 128 * 1024)
        {
            error = "Bookmark file is inaccessible or exceeds 128 KiB.";
            return false;
        }
        std::ifstream input(path);
        input.imbue(std::locale::classic());
        std::string header;
        const bool hasHeader = static_cast<bool>(std::getline(input, header));
        if (!header.empty() && header.back() == '\r') header.pop_back();
        if (!hasHeader || header != "PLUTOGE_VIEWPORT_BOOKMARKS 1")
        {
            error = "Unsupported or malformed viewport bookmark file.";
            return false;
        }
        std::vector<ViewportBookmark> loaded;
        std::string line;
        while (std::getline(input, line))
        {
            if (line.empty()) continue;
            std::istringstream row(line);
            row.imbue(std::locale::classic());
            ViewportBookmark bookmark;
            int orthographic = 0;
            if (!(row >> std::quoted(bookmark.name) >> bookmark.position.x >> bookmark.position.y >> bookmark.position.z
                      >> bookmark.yaw >> bookmark.pitch >> bookmark.fov >> bookmark.nearPlane >> bookmark.farPlane
                      >> bookmark.moveSpeed >> orthographic >> bookmark.orthographicSize) ||
                (orthographic != 0 && orthographic != 1) || !(row >> std::ws).eof())
            {
                error = "Malformed viewport bookmark record.";
                return false;
            }
            bookmark.orthographic = orthographic != 0;
            loaded.push_back(std::move(bookmark));
        }
        if (input.bad() || !Validate(loaded, error))
        {
            if (error.empty()) error = "Could not read viewport bookmarks.";
            return false;
        }
        output = std::move(loaded);
        return true;
    }

    bool ViewportBookmarks::Save(const std::filesystem::path &path, const std::vector<ViewportBookmark> &bookmarks, std::string &error)
    {
        if (!Validate(bookmarks, error)) return false;
        std::error_code ec;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "Cannot create bookmark directory: " + ec.message();
            return false;
        }
        auto temporary = path;
        temporary += "." + std::to_string(std::random_device{}()) + ".tmp";
        {
            std::ofstream output(temporary, std::ios::trunc);
            output.imbue(std::locale::classic());
            output << "PLUTOGE_VIEWPORT_BOOKMARKS 1\n" << std::setprecision(std::numeric_limits<float>::max_digits10);
            for (const auto &b : bookmarks)
                output << std::quoted(b.name) << ' ' << b.position.x << ' ' << b.position.y << ' ' << b.position.z << ' '
                       << b.yaw << ' ' << b.pitch << ' ' << b.fov << ' ' << b.nearPlane << ' ' << b.farPlane << ' '
                       << b.moveSpeed << ' ' << (b.orthographic ? 1 : 0) << ' ' << b.orthographicSize << '\n';
            output.flush();
            output.close();
            if (!output)
            {
                error = "Could not write viewport bookmarks. The previous file is unchanged.";
                std::filesystem::remove(temporary, ec);
                return false;
            }
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
        std::filesystem::rename(temporary, path, ec);
#endif
        if (ec)
        {
            error = "Could not replace viewport bookmarks: " + ec.message();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        return true;
    }
}
