#include "PlutoGE/ui/ViewportBookmarks.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    struct TemporaryDirectory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("PlutoGE-bookmarks-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporaryDirectory()
        {
            // Only remove the two known files created by this test.
            std::error_code ignored;
            std::filesystem::remove(path / "views.bookmarks", ignored);
            std::filesystem::remove(path / "invalid.bookmarks", ignored);
            std::filesystem::remove(path, ignored);
        }
    };
}

int main()
{
    using namespace PlutoGE::ui;
    try
    {
        TemporaryDirectory temp;
        const auto path = temp.path / "views.bookmarks";
        std::string error;
        std::vector<ViewportBookmark> views{{.name = "Old view"}};
        Require(ViewportBookmarks::Load(path, views, error) && views.empty(), "Missing file is not empty");
        views.push_back({.name = "Road \\\"start\\\"", .position = {0.12345678f, -10, 100}, .yaw = 270,
                         .pitch = -90, .fov = 60, .nearPlane = 0.2f, .farPlane = 2500, .moveSpeed = 22,
                         .orthographic = true, .orthographicSize = 44});
        Require(ViewportBookmarks::Save(path, views, error), error);
        std::vector<ViewportBookmark> loaded;
        Require(ViewportBookmarks::Load(path, loaded, error), error);
        Require(loaded.size() == 1 && loaded[0].name == views[0].name && loaded[0].position == views[0].position &&
                    loaded[0].orthographic && loaded[0].orthographicSize == 44 && loaded[0].pitch == -90 &&
                    loaded[0].fov == 60 && loaded[0].farPlane == 2500 && loaded[0].moveSpeed == 22,
                "Bookmark did not round-trip exactly");
        views[0].name = "Renamed";
        views[0].position = {1, 2, 3};
        Require(ViewportBookmarks::Save(path, views, error), "Replacing existing bookmarks failed");
        Require(ViewportBookmarks::Load(path, loaded, error) && loaded[0].name == "Renamed" && loaded[0].position.x == 1,
                "Replacement did not persist");
        auto invalid = views;
        invalid.push_back(views.front());
        Require(!ViewportBookmarks::Save(path, invalid, error), "Duplicate name accepted");
        invalid = views;
        invalid[0].nearPlane = invalid[0].farPlane;
        Require(!ViewportBookmarks::Save(path, invalid, error), "Invalid lens accepted");
        invalid = views;
        invalid[0].position.x = std::numeric_limits<float>::infinity();
        Require(!ViewportBookmarks::Save(path, invalid, error), "Nonfinite position accepted");
        Require(ViewportBookmarks::Load(path, loaded, error) && loaded[0].name == "Renamed",
                "Rejected save destroyed the previous file");
        const auto invalidPath = temp.path / "invalid.bookmarks";
        {
            std::ofstream file(invalidPath);
            file << "PLUTOGE_VIEWPORT_BOOKMARKS 1\n\"Broken\" 1 2\n";
        }
        Require(!ViewportBookmarks::Load(invalidPath, loaded, error) && loaded[0].name == "Renamed",
                "Malformed load changed the active collection");
        {
            std::ofstream file(invalidPath, std::ios::binary);
            file << "PLUTOGE_VIEWPORT_BOOKMARKS 1\r\n\"CRLF\" 1 2 3 0 0 45 0.1 100 6 0 10\r\n";
        }
        Require(ViewportBookmarks::Load(invalidPath, loaded, error) && loaded[0].name == "CRLF", "CRLF file failed");
        Require(ViewportBookmarks::Save(path, {}, error), "Delete-all save failed");
        Require(ViewportBookmarks::Load(path, loaded, error) && loaded.empty(), "Deleted views returned");
        std::cout << "PASS: viewport bookmark persistence, replacement, validation and malformed files\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
