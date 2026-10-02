#include "PlutoGE/ui/RmlDocumentEditSession.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char *message)
    { if (!condition) throw std::runtime_error(message); }
    void Write(const std::filesystem::path &path, const std::string &source)
    { std::ofstream(path, std::ios::binary) << source; }
    std::string Read(const std::filesystem::path &path)
    { std::ifstream stream(path, std::ios::binary); return {(std::istreambuf_iterator<char>(stream)), {}}; }

    struct Fixture
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("PlutoGE-rml-session-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Fixture() { std::filesystem::create_directories(root / "UI" / "styles"); }
        ~Fixture() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    };
}

int main() try
{
    using PlutoGE::ui::RmlDocumentEditSession;
    Fixture fixture;
    const auto document = fixture.root / "UI" / "screen.rml";
    const auto sheet = fixture.root / "UI" / "styles" / "shared.rcss";
    const auto imported = fixture.root / "UI" / "styles" / "base.rcss";
    const std::string markup = "<!-- <link href='ignored.rcss'/> -->\r\n<rml><head><link type='text/rcss' href='styles/shared.rcss'/><link href=styles/base.rcss /><link href='styles/shared.rcss'/></head><body/></rml>\r\n";
    Write(document, markup);
    Write(sheet, "body { color: red; }\n");
    Write(imported, "body { margin: 0; }\n");
    RmlDocumentEditSession session;
    session.Open(document, fixture.root);
    Check(session.GetBuffers().size() == 3, "Linked stylesheet discovery or deduplication failed");
    Check(session.GetBuffers()[0].source == markup, "Opening changed formatting or line endings");
    session.SetSource(1, "body { color: blue; }\n");
    Check(Read(sheet).find("red") != std::string::npos, "Unsaved edit touched disk");
    Check(session.GetSourceOverlay().at(std::filesystem::weakly_canonical(sheet).generic_string()).find("blue") != std::string::npos,
        "Overlay did not contain unsaved linked source");
    Check(session.Undo() && !session.IsDirty(), "Undo failed");
    Check(session.Redo() && session.IsDirty(), "Redo failed");
    session.Save();
    Check(!session.IsDirty() && Read(sheet) == "body { color: blue; }\n", "Save failed");
    Check(Read(document) == markup, "Saving styles rewrote untouched markup");
    Check(session.Undo() && session.IsDirty(), "Undo after save lost source history");
    session.Redo();

    session.SetSource(1, "body { color: green; }\n");
    Write(sheet, "body { color: yellow; }\n");
    session.PollExternalChanges();
    Check(session.HasConflicts() && session.GetBuffers()[1].source.find("green") != std::string::npos, "External conflict overwrote unsaved edits");
    bool rejected = false;
    try { session.Save(); } catch (const std::exception &) { rejected = true; }
    Check(rejected && Read(sheet).find("yellow") != std::string::npos, "Conflicting save overwrote disk");
    session.Reload();
    Check(!session.IsDirty() && !session.HasConflicts(), "Explicit reload did not resolve conflict");
    Write(sheet, "body { color: cyan; }\n");
    const auto revision = session.GetRevision();
    session.PollExternalChanges();
    Check(session.GetRevision() > revision && session.GetBuffers()[1].source.find("cyan") != std::string::npos,
        "Clean source did not reload external edit");
    const auto checkpoint = session.GetBuffers()[1].source;
    session.SetSource(1, "/* prefix */" + checkpoint);
    session.SetSource(1, "/* prefix */");
    session.SetSource(1, "");
    Check(session.Undo() && session.Undo() && session.Undo() && session.GetBuffers()[1].source == checkpoint,
        "Insertion, deletion and empty-source undo did not restore the document");
    Check(session.Redo() && session.Redo() && session.Redo() && session.GetBuffers()[1].source.empty(),
        "Source replacement redo failed");
    session.Reload();

    session.SetSource(0, "<rml><head><link href='missing.rcss'/></head><body/></rml>");
    session.RefreshDependencies();
    Check(!session.GetDiagnostics().empty(), "Missing dependency had no diagnostic");
    Check(session.GetBuffers().size() >= 2, "Removing a link discarded open buffers");
    session.SetSource(0, "<rml><head><link href='../../outside.rcss'/></head><body/></rml>");
    session.RefreshDependencies();
    Check(!session.GetDiagnostics().empty(), "Out-of-project source accepted");
    rejected = false;
    try { session.Open(fixture.root / "missing.rml", fixture.root); } catch (const std::exception &) { rejected = true; }
    Check(rejected && session.IsDirty(), "Failed open discarded current session");

    session.Reload();
    std::filesystem::remove(sheet);
    session.PollExternalChanges();
    Check(session.HasConflicts(), "Deleted source was not detected");
    Write(fixture.root / "UI" / "frame.rml", "<template name='frame' content='host'><head><link href='styles/shared.rcss'/><link href='screen.rml'/></head><body><div id='host'/></body></template>");
    Write(document, "<rml><head><link type='text/template' href='frame.rml'/></head><body template='frame'/></rml>");
    Write(sheet, "body { color: red; }");
    RmlDocumentEditSession templates;
    templates.Open(document, fixture.root);
    Check(templates.GetBuffers().size() == 3, "Template dependencies or cycle deduplication failed");
    templates.SetSource(1, "<template name='frame' content='host'><body><div id='host'>Unsaved frame</div></body></template>");
    Check(templates.GetSourceOverlay().at(std::filesystem::weakly_canonical(fixture.root / "UI" / "frame.rml").generic_string()).find("Unsaved frame") != std::string::npos,
        "Unsaved template was absent from preview overlay");
    std::cout << "RML edit session regressions passed\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
