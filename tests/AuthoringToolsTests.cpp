#include "PlutoGE/ui/AuthoringRegistry.h"
#include "PlutoGE/ui/RmlSourceTools.h"
#include "PlutoGE/ui/ScriptSourceWatch.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
    template<class F> void Reject(F action) { try { action(); } catch (const std::exception &) { return; } throw std::runtime_error("Invalid operation accepted"); }
}
int main()
{
    using namespace PlutoGE::ui;
    const std::string source = "<!-- <div id='ignored'/> --><rml><head><style>div { color: red; } /* < */</style></head><body id='root'><button id='ok' title='a>b'>OK</button></body></rml>";
    const auto elements = RmlSourceTools::Parse(source);
    Check(elements.size() == 5 && elements[4].id == "ok", "Lexical hierarchy");
    const auto edited = RmlSourceTools::SetAttribute(source, 4, "title", "A & B < C");
    Check(edited.find("title=\"A &amp; B &lt; C\"") != std::string::npos && edited.starts_with("<!--"), "Escaped surgical edit");
    Reject([&] { RmlSourceTools::SetAttribute(source, 4, "id", "root"); });
    Reject([&] { RmlSourceTools::Parse("<rml><body></rml>"); });
    Reject([&] { RmlSourceTools::Parse("<rml><body id='x' id='y'/></rml>"); });
    const auto inserted = RmlSourceTools::Insert("<rml><body/></rml>", 1, "button", "new-button");
    Check(RmlSourceTools::Parse(inserted).size() == 3, "Insert into self-closing parent");
    Check(RmlSourceTools::Remove(inserted, 2) == "<rml><body>\n\n</body></rml>", "Remove subtree");
    Reject([&] { RmlSourceTools::Remove(source, 3); });
    const auto escaped = RmlSourceTools::SetAttribute(source, 4, "title", "A & B");
    Check(RmlSourceTools::Parse(escaped)[4].attributes[1].value == "A & B", "Attribute roundtrip double-escapes entities");
    Check(RmlSourceTools::Decode("&#x1f680;") == "\xf0\x9f\x9a\x80", "Unicode attribute entity");
    Check(RmlSourceTools::SetText(source, 4, "<safe>").find("&lt;safe&gt;") != std::string::npos, "Leaf text injection");
    Reject([&] { RmlSourceTools::SetText(source, 3, "erase children"); });
    Check(RmlSourceTools::SetStyle(source, 4, "width", "240dp").find("width: 240dp;") != std::string::npos, "Style editing");
    ScriptSourceWatch watch;
    const auto now = ScriptSourceWatch::Clock::now();
    ScriptSourceWatch::Snapshot first{{"a.cs", {std::filesystem::file_time_type{}, 10}}};
    Check(!watch.Observe(first, now) && !watch.Ready(now), "Initial watch triggered build");
    first["a.cs"].second = 11;
    Check(watch.Observe(first, now) && !watch.Ready(now + std::chrono::milliseconds(500)), "Watch debounce");
    Check(watch.Ready(now + std::chrono::seconds(1)), "Watch never became ready");
    watch.Acknowledge();
    Check(!watch.Ready(now + std::chrono::seconds(2)), "Failed build would repeat");
    {
        // A project opened after an engine update must rebuild scripts compiled against the old ScriptCore.
        const auto dir = std::filesystem::temp_directory_path() / "PlutoGEAuthoringStaleAssembly";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        const auto assembly = dir / "Game.dll", core = dir / "PlutoGE.ScriptCore.dll";
        std::ofstream(assembly) << "dll";
        std::ofstream(core) << "core";
        const auto built = std::filesystem::last_write_time(assembly);
        std::filesystem::last_write_time(core, built - std::chrono::hours(1));
        ScriptSourceWatch::Snapshot sources{{"Game.cs", {built - std::chrono::hours(2), 1}}};
        Check(!ScriptSourceWatch::IsAssemblyStale(assembly, core, sources), "Fresh assembly reported stale");
        Check(!ScriptSourceWatch::IsAssemblyStale(dir / "Missing.dll", core, {}), "Project without scripts reported stale");
        Check(ScriptSourceWatch::IsAssemblyStale(dir / "Missing.dll", core, sources), "Missing assembly not stale");
        std::filesystem::last_write_time(core, built + std::chrono::hours(1));
        Check(ScriptSourceWatch::IsAssemblyStale(assembly, core, sources), "Assembly older than ScriptCore not stale");
        std::filesystem::last_write_time(core, built - std::chrono::hours(1));
        sources["Game.cs"].first = built + std::chrono::hours(1);
        Check(ScriptSourceWatch::IsAssemblyStale(assembly, core, sources), "Assembly older than sources not stale");
        std::filesystem::remove_all(dir);
        ScriptSourceWatch fresh;
        Check(!fresh.IsPrimed(), "New watch primed");
        fresh.Request(now);
        Check(fresh.Ready(now + std::chrono::seconds(1)), "Requested build never became ready");
    }
    AuthoringRegistry registry;
    registry.Register({"test", "Test", "", AuthoringRegistry::Kind::Command, [](EditorShell &) {}});
    Reject([&] { registry.Register({"test", "Test", "", AuthoringRegistry::Kind::Command, [](EditorShell &) {}}); });
    auto snapshot = registry.List(AuthoringRegistry::Kind::Command);
    registry.Unregister("test");
    Check(snapshot.size() == 1 && registry.List(AuthoringRegistry::Kind::Command).empty(), "Registry snapshots");
    std::cout << "Authoring native tests passed.\n";
}
