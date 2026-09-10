#include "PlutoGE/platform/ContentPack.h"
#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace PlutoGE::content;
void Require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
void Write(const fs::path &path, const std::string &data)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out.write(data.data(), data.size());
    Require(bool(out), "Fixture write");
}
template<class T> void Put(std::ostream &out, T value)
{ for (unsigned i = 0; i < sizeof(T); ++i) out.put(static_cast<char>(value >> (8 * i))); }
void Legacy(const fs::path &path, std::string name, std::string bytes)
{
    std::ofstream out(path, std::ios::binary); out.write("PLUTOPK1", 8);
    Put(out, std::uint32_t{1}); Put(out, std::uint32_t{1}); Put(out, static_cast<std::uint32_t>(name.size())); Put(out, static_cast<std::uint64_t>(bytes.size()));
    for (auto &c : name) c ^= 0xA7; for (auto &c : bytes) c ^= 0xA7;
    out << name << bytes;
}
int main()
{
    auto root = fs::temp_directory_path() / ("pluto-pack-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path root; ~Cleanup() { UnmountAll(); std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
    try
    {
        const auto source = root / "source", packed = root / "game.plutopack", mount = root / "virtual";
        const std::string large(700000, 'x');
        std::string random(600000, '\0'); std::mt19937 rng(42); for (auto &c : random) c = static_cast<char>(rng());
        Write(source / "Assets/large.bin", large); Write(source / "Assets/random.bin", random);
        Write(source / "Assets/empty", ""); Write(source / "game.plutoproject", "one\r\ntwo\r\n");
        std::string error;
        InputFile absent(""); Require(!absent && !IsMounted(""), "Empty optional asset path must fail without throwing");
        Require(WritePack(source, packed, {}, &error), error);
        Require(fs::file_size(packed) < large.size() + random.size() / 2, "Compression did not save space");
        auto pack = Pack::Open(packed, &error); Require(bool(pack), error); Require(pack->Verify(&error), error);
        std::string range(100, '\0');
        Require(pack->Read("Assets/random.bin", 262130, range, &error), error);
        Require(range == random.substr(262130, range.size()), "Cross-block random access");
        Require(!pack->Read("Assets/random.bin", random.size() - 1, range, &error), "Out of bounds accepted");
        Require(Mount(packed, mount, &error), error);
        std::string bytes; Require(ReadFile(mount / "Assets/large.bin", bytes, &error) && bytes == large, "Mounted read");
        Require(!fs::exists(mount), "Mount extracted assets");
        Require(Exists(mount / "Assets") && Exists(mount / "Assets/empty"), "Virtual existence");
        InputFile text(mount / "game.plutoproject"); std::string line; std::getline(text, line);
#ifdef _WIN32
        Require(line == "one", "Text-mode CRLF handling");
#else
        Require(line == "one\r", "POSIX text semantics");
#endif
        InputFile binary(mount / "Assets/random.bin", std::ios::binary);
        binary.seekg(262130); binary.read(range.data(), range.size()); Require(range == random.substr(262130, range.size()), "Stream seeking");
        Write(root / "patch/Assets/large.bin", "patched");
        Require(WritePack(root / "patch", root / "patch.plutopack", {}, &error), error);
        Require(Mount(root / "patch.plutopack", mount, &error), error);
        Require(ReadFile(mount / "Assets/large.bin", bytes, &error) && bytes == "patched", "Overlay precedence");
        Require(ReadFile(mount / "Assets/random.bin", bytes, &error) && bytes == random, "Overlay base fallback");
        Require(Files(mount).size() == 4, "Overlay enumeration duplicated entries");
        Write(mount / "unlisted", "loose"); Require(!ReadFile(mount / "unlisted", bytes), "Mounted namespace fell back to loose file");
        Require(Materialize(mount / "Assets/large.bin", &error), error);
        Require(fs::exists(mount / "Assets/large.bin") && !fs::exists(mount / "Assets/random.bin"), "Selective extraction");
        UnmountAll();
        Require(WritePack(source, root / "raw.plutopack", {.compress = false}, &error), error);
        auto raw = Pack::Open(root / "raw.plutopack", &error); Require(raw && raw->Verify(&error), error);
        // Index damage is rejected at mount; payload damage is detected on read.
        fs::copy_file(packed, root / "bad-index.plutopack");
        { std::fstream out(root / "bad-index.plutopack", std::ios::binary | std::ios::in | std::ios::out); out.seekp(-1, std::ios::end); out.put('!'); }
        Require(!Pack::Open(root / "bad-index.plutopack", &error), "Bad index accepted");
        fs::copy_file(root / "raw.plutopack", root / "bad-data.plutopack");
        { std::fstream out(root / "bad-data.plutopack", std::ios::binary | std::ios::in | std::ios::out); out.seekp(40); out.put('!'); }
        auto damaged = Pack::Open(root / "bad-data.plutopack", &error);
        Require(damaged && !damaged->Verify(&error), "Payload damage accepted");
        Require(damaged->Read("Assets/random.bin", 262130, range, &error) && range == random.substr(262130, range.size()), "Unrelated asset was read while seeking");
        Require(pack->ExtractTo(root / "extracted", &error), error);
        Require(ReadFile(root / "extracted/Assets/random.bin", bytes, &error) && bytes == random, "Explicit extraction roundtrip");
        const auto unicode = fs::path(u8"Assets/\u00e9\u65e5.bin");
        Write(root / "unicode" / unicode, "unicode");
        Require(WritePack(root / "unicode", root / "unicode.plutopack", {}, &error), error);
        Require(Mount(root / "unicode.plutopack", root / fs::path(u8"mounted-\u65e5"), &error), error);
        Require(ReadFile(root / fs::path(u8"mounted-\u65e5") / unicode, bytes, &error) && bytes == "unicode", "UTF-8 pack path");
        UnmountAll();
        Legacy(root / "legacy.plutopack", "Assets/old", "old data");
        auto legacy = Pack::Open(root / "legacy.plutopack", &error); Require(bool(legacy), error);
        bytes.resize(8); Require(legacy->Read("Assets/old", 0, bytes, &error) && bytes == "old data", "Legacy read");
        for (const auto &unsafe : {"../escape", "/absolute", "C:drive", "a/../../escape", "a\\escape", "a/NUL", "a/../b"})
        { Legacy(root / "unsafe.plutopack", unsafe, "x"); Require(!Pack::Open(root / "unsafe.plutopack"), "Unsafe path accepted"); }
        fs::resize_file(root / "legacy.plutopack", 18); Require(!Pack::Open(root / "legacy.plutopack"), "Truncation accepted");
        Require(!WritePack(source, source / "self.plutopack", {}, &error), "Recursive output accepted");
        Require(!WritePack(source, source / "..archive.plutopack", {}, &error), "Dot-prefixed recursive output accepted");
        auto before = fs::file_size(packed); Require(!WritePack(root / "missing", packed, {}, &error), "Missing source accepted");
        Require(fs::file_size(packed) == before && Pack::Open(packed)->Verify(), "Failed export damaged old pack");
        Require(WritePack(source, packed, {}, &error), "Repeat export: " + error);
        std::cout << "Content pack tests passed\n";
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
