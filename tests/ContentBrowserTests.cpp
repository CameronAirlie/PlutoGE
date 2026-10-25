#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/ui/contentbrowser/ContentCommands.h"
#include "PlutoGE/ui/contentbrowser/ContentFileOperations.h"
#include "PlutoGE/ui/contentbrowser/ContentFormatting.h"
#include "PlutoGE/ui/contentbrowser/ContentNavigationHistory.h"
#include "PlutoGE/ui/contentbrowser/ContentPaths.h"
#include "PlutoGE/ui/contentbrowser/ContentSelection.h"
#include "PlutoGE/ui/contentbrowser/ContentSorting.h"
#include "PlutoGE/ui/SystemShell.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace PlutoGE;
    namespace fs = std::filesystem;

    void Require(bool value, const std::string &message)
    {
        if (!value) throw std::runtime_error(message);
    }

    void Write(const fs::path &path, const std::string &text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }

    std::string Read(const fs::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), {}};
    }

    struct Scratch
    {
        fs::path root = fs::temp_directory_path() /
                        ("PlutoGE-content-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Scratch() { fs::create_directories(root / "Assets"); }
        ~Scratch()
        {
            std::error_code error;
            fs::remove_all(root, error);
        }
    };

    void TestPaths()
    {
        Require(ui::NormalizeContentFolder("Models/./Cars/") == "Models/Cars" && ui::NormalizeContentFolder(".").empty(),
                "Folders must normalize to trimmed generic paths");
        Require(ui::GetContentFolderParent("A/B/C") == "A/B" && ui::GetContentFolderParent("A").empty() &&
                    ui::GetContentFolderName("A/B/C") == "C" && ui::JoinContentFolder("", "A") == "A" &&
                    ui::JoinContentFolder("A", "B") == "A/B",
                "Folder parent, name and join must agree");
        Require(ui::IsContentFolderAncestorOrSelf("", "X") && ui::IsContentFolderAncestorOrSelf("A", "A/B") &&
                    !ui::IsContentFolderAncestorOrSelf("A", "AB") && !ui::IsContentFolderAncestorOrSelf("A/B", "A"),
                "Ancestry must respect path components");
        Require(ui::RemapContentFolder("A/B/C", "A/B", "Z") == "Z/C" && ui::RemapContentFolder("AB/C", "A", "Z") == "AB/C" &&
                    ui::RemapContentFolder("A/x.png", "A", "B") == "B/x.png",
                "Remapping must only rewrite moved prefixes");
        Require(!ui::ValidateContentItemName("").empty() && !ui::ValidateContentItemName("a/b").empty() &&
                    !ui::ValidateContentItemName("..").empty() && !ui::ValidateContentItemName("name.").empty() &&
                    !ui::ValidateContentItemName("con.txt").empty() && ui::ValidateContentItemName("Rock 02.png").empty(),
                "Name validation must reject separators, reserved and trailing-dot names");
        Require(ui::PathToUtf8(ui::PathFromUtf8("Ünïcode/名前")) == "Ünïcode/名前", "UTF-8 paths must round-trip");
    }

    void TestSorting()
    {
        Require(ui::CompareNatural("Item 2", "item 10") < 0 && ui::CompareNatural("b", "A") > 0 &&
                    ui::CompareNatural("x007", "x7") != 0 && ui::CompareNatural("Same", "Same") == 0,
                "Natural ordering must be case-insensitive and numeric-aware");
        const ui::ContentSortFields folder{.folder = true, .name = "Zed"};
        const ui::ContentSortFields big{.name = "a", .typeName = "Mesh", .modified = 5, .size = 900};
        const ui::ContentSortFields small{.name = "b", .typeName = "Material", .modified = 9, .size = 10};
        using Key = ui::ContentSortKey;
        Require(ui::ContentSortsBefore(folder, big, {Key::Name, true}) && ui::ContentSortsBefore(folder, big, {Key::Name, false}),
                "Folders must lead in either direction");
        Require(ui::ContentSortsBefore(small, big, {Key::Size, true}) && ui::ContentSortsBefore(big, small, {Key::Size, false}) &&
                    ui::ContentSortsBefore(big, small, {Key::Modified, true}) && ui::ContentSortsBefore(small, big, {Key::Type, true}),
                "Each sort key must order by its own field");
        Require(ui::FormatContentSize(512) == "512 B" && ui::FormatContentSize(1536) == "1.5 KB" &&
                    ui::FormatContentSize(50ull * 1024 * 1024) == "50 MB",
                "Sizes must use binary units");
    }

    void TestSelectionAndHistory()
    {
        const std::vector<std::string> shown{"a", "b", "c", "d"};
        ui::ContentSelection selection;
        selection.Click("b", false, false, shown);
        selection.Click("d", false, true, shown);
        Require(selection.Keys() == std::vector<std::string>({"b", "c", "d"}) && *selection.Primary() == "d",
                "Shift must select the shown range from the anchor");
        selection.Click("c", true, false, shown);
        Require(!selection.Contains("c") && selection.Size() == 2, "Ctrl must toggle membership");
        selection.Remap([](const std::string &key) { return key == "b" ? std::string("B") : key; });
        selection.Prune([](const std::string &key) { return key != "d"; });
        Require(selection.Keys() == std::vector<std::string>({"B"}), "Remap and prune must follow moves and deletions");

        ui::ContentNavigationHistory history;
        history.Visit("A");
        history.Visit("A/B");
        history.Visit("A/B");
        Require(history.Back() == "A" && history.CanGoForward() && history.Forward() == "A/B" && !history.CanGoForward(),
                "History must record each visit once and walk both ways");
        history.Back();
        history.Visit("C");
        Require(!history.CanGoForward(), "Visiting after going back must discard forward entries");
        history.Remap([](const std::string &folder) { return folder == "C" ? std::string("A") : folder; });
        Require(history.Current() == "A" && history.Back().empty(), "Remapping must collapse neighbouring duplicates");
    }

    void TestCommands()
    {
        std::set<int> seen;
        for (const auto &info : ui::GetContentCommands())
        {
            Require(seen.insert(static_cast<int>(info.command)).second, "Commands must be listed once");
            Require(&ui::GetContentCommandInfo(info.command) == &info && info.label && *info.label, "Commands must resolve to themselves");
            Require((info.chord == ImGuiKey_None) == (info.shortcutText == nullptr), "Shortcut text must match the chord");
        }
        Require(seen.size() == static_cast<std::size_t>(ui::ContentCommand::Refresh) + 1, "Every command needs a table entry");
        Require(ui::MakeFileUri("/tmp/a b#c").ends_with("/tmp/a%20b%23c"), "File URIs must percent-encode reserved bytes");
    }

    void TestFileOperations()
    {
        Scratch scratch;
        assets::Project project(scratch.root / "Content.plutoproject", assets::ProjectManifest{});
        const auto assetsRoot = scratch.root / "Assets";
        const ui::ContentFileOperations operations(project);

        auto created = operations.CreateFolder(assetsRoot);
        auto second = operations.CreateFolder(assetsRoot);
        Require(created.Succeeded() && second.Succeeded() && created.results.front().filename() == "New Folder" &&
                    second.results.front().filename() == "New Folder 2",
                "New folders must get unique names");
        Require(!operations.CreateFolder(scratch.root).Succeeded() && !operations.CreateFolder(assetsRoot, "bad/name").Succeeded(),
                "Folders must stay inside the asset directory and use valid names");

        Write(assetsRoot / "Rock.plutomaterial", "Color=1,1,1,1\n");
        assets::AssetMetadata identity{.id = "rock-material"};
        std::string error;
        Require(assets::SaveAssetMetadata(assets::GetAssetMetadataPath(assetsRoot / "Rock.plutomaterial"), identity, assets::AssetMetadataWriteMode::CreateOnly, &error), error);
        auto renamed = operations.Rename(assetsRoot / "Rock.plutomaterial", "Stone.plutomaterial");
        Require(renamed.Succeeded() && fs::exists(assetsRoot / "Stone.plutomaterial") &&
                    fs::exists(assets::GetAssetMetadataPath(assetsRoot / "Stone.plutomaterial")) &&
                    !fs::exists(assetsRoot / "Rock.plutomaterial") && renamed.moved.size() == 1,
                "Renames must carry the identity sidecar: " + (renamed.errors.empty() ? std::string{} : renamed.errors.front()));
        Require(!operations.Rename(assetsRoot / "Stone.plutomaterial", "New Folder").Succeeded(), "Renames must not replace items");

        const auto folder = assetsRoot / "New Folder";
        const std::vector<fs::path> moveItems{assetsRoot / "Stone.plutomaterial"};
        auto moved = operations.Move(moveItems, folder);
        Require(moved.Succeeded() && fs::exists(folder / "Stone.plutomaterial") && moved.moved.front().second == folder / "Stone.plutomaterial",
                "Moves must relocate items into the destination folder");
        Require(operations.Move(std::vector<fs::path>{folder / "Stone.plutomaterial"}, folder).results.empty(),
                "Moving into the current parent must be a no-op");
        const std::vector<fs::path> folderItem{folder};
        Require(!operations.Move(folderItem, folder).Succeeded(), "A folder must not move into itself");

        Write(folder / "Ground.plutomesh", "mesh");
        Write(folder / "Ground.plutomesh.materials", "overrides");
        Require(assets::SaveAssetMetadata(assets::GetAssetMetadataPath(folder / "Ground.plutomesh"), {.id = "ground-mesh"}, assets::AssetMetadataWriteMode::CreateOnly, &error), error);
        const std::vector<fs::path> meshItem{folder / "Ground.plutomesh"};
        auto copied = operations.Copy(meshItem, folder);
        const auto copy = folder / "Ground Copy.plutomesh";
        Require(copied.Succeeded() && copied.results.front() == copy && Read(copy) == "mesh" &&
                    Read(folder / "Ground Copy.plutomesh.materials") == "overrides" && !fs::exists(assets::GetAssetMetadataPath(copy)),
                "Copies must bring material overrides and drop identity sidecars");
        auto folderCopy = operations.Copy(folderItem, assetsRoot);
        Require(folderCopy.Succeeded() && fs::exists(assetsRoot / "New Folder Copy" / "Ground.plutomesh") &&
                    !fs::exists(assets::GetAssetMetadataPath(assetsRoot / "New Folder Copy" / "Ground.plutomesh")),
                "Folder copies must be recursive and drop every identity sidecar");
        Require(!operations.Copy(folderItem, folder).Succeeded(), "A folder must not copy into itself");

        const std::vector<fs::path> nested{folder, folder / "Ground.plutomesh", assetsRoot / "New Folder 2"};
        Require(ui::ContentFileOperations::RemoveNested(nested).size() == 2, "Items inside listed folders must be dropped");

        const std::vector<fs::path> deleteItems{folder / "Ground.plutomesh"};
        auto deleted = operations.Delete(deleteItems);
        Require(deleted.Succeeded() && !fs::exists(folder / "Ground.plutomesh") && !fs::exists(folder / "Ground.plutomesh.materials") &&
                    !fs::exists(assets::GetAssetMetadataPath(folder / "Ground.plutomesh")) && deleted.moved.front().second.empty(),
                "Deletes must remove the asset and its sidecars");
        const std::vector<fs::path> forbidden{assetsRoot, scratch.root / "Content.plutoproject"};
        Require(operations.Delete(forbidden).errors.size() == 2 && fs::exists(assetsRoot), "The asset root and outside files must be protected");

        const auto outside = scratch.root / "Outside";
        Write(outside / "Texture.png", "png");
        Write(outside / "Pack" / "Inner.txt", "inner");
        Write(outside / "Pack" / "Inner.txt.plutometa", "foreign identity");
        Write(folder / "Texture.png", "existing");
        const std::vector<fs::path> external{outside / "Texture.png", outside / "Pack"};
        auto imported = operations.Import(external, folder);
        Require(imported.Succeeded() && Read(folder / "Texture 2.png") == "png" && Read(folder / "Texture.png") == "existing" &&
                    fs::exists(folder / "Pack" / "Inner.txt") && !fs::exists(folder / "Pack" / "Inner.txt.plutometa"),
                "Imports must keep names, avoid collisions and drop foreign identities");
    }
}

int main()
try
{
    TestPaths();
    TestSorting();
    TestSelectionAndHistory();
    TestCommands();
    TestFileOperations();
    std::cout << "Content browser tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
