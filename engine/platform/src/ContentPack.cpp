#include "PlutoGE/platform/ContentPack.h"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <zlib.h>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace PlutoGE::content
{
    namespace
    {
        constexpr std::uint32_t BlockSize = 256 * 1024;
        constexpr std::uint64_t HeaderSize = 40;
        constexpr std::uint64_t MaxIndexSize = 256 * 1024 * 1024;
        void Fail(const std::string &message) { throw std::runtime_error(message); }
        void Error(std::string *error, const std::exception &e) { if (error) *error = e.what(); }
        template<class T> void Put(std::ostream &out, T value)
        {
            for (unsigned i = 0; i < sizeof(T); ++i) out.put(static_cast<char>(value >> (8 * i)));
            if (!out) Fail("Failed to write content pack.");
        }
        template<class T> T Get(std::istream &in)
        {
            T result = 0;
            for (unsigned i = 0; i < sizeof(T); ++i)
            {
                const auto byte = in.get();
                if (byte == EOF) Fail("Truncated content pack.");
                result |= static_cast<T>(static_cast<unsigned char>(byte)) << (8 * i);
            }
            return result;
        }
        std::uint32_t Crc(std::span<const char> bytes)
        {
            return static_cast<std::uint32_t>(crc32(0, reinterpret_cast<const Bytef *>(bytes.data()), static_cast<uInt>(bytes.size())));
        }
        bool SafePath(const std::string &path)
        {
            if (path.empty() || path.size() > 4096 || path.front() == '/' || path.back() == '/') return false;
            if (path.find_first_of("\\:\0", 0, 3) != std::string::npos) return false;
            std::istringstream parts(path);
            std::string part;
            while (std::getline(parts, part, '/'))
            {
                if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
                for (unsigned char c : part) if (c < 32 || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
                auto stem = part.substr(0, part.find('.'));
                for (auto &c : stem) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
                if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
                    (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '0' && stem[3] <= '9')) return false;
            }
            return true;
        }
        std::string Fold(std::string value)
        {
#ifdef _WIN32
            for (auto &c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
#endif
            return value;
        }
        std::string Key(const std::filesystem::path &path)
        {
            const auto utf8 = std::filesystem::absolute(path).lexically_normal().generic_u8string();
            return Fold(std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size()));
        }
        struct Block { std::uint64_t offset; std::uint32_t stored, raw, crc, codec; };
        struct Entry { std::string name; std::uint64_t size = 0; std::vector<Block> blocks; };
        void AddEntry(std::map<std::string, Entry> &entries, Entry entry)
        {
            if (!SafePath(entry.name)) Fail("Unsafe content pack path: " + entry.name);
            if (!entries.emplace(Fold(entry.name), std::move(entry)).second) Fail("Duplicate content pack path.");
        }
    }
    struct Pack::Impl
    {
        std::filesystem::path path;
        std::map<std::string, Entry> entries;
    };

    std::shared_ptr<Pack> Pack::Open(const std::filesystem::path &path, std::string *error)
    {
        try
        {
            auto pack = std::make_shared<Pack>();
            pack->m_impl = std::make_shared<Impl>();
            auto &impl = *pack->m_impl;
            impl.path = std::filesystem::absolute(path);
            const auto length = std::filesystem::file_size(path);
            std::ifstream input(path, std::ios::binary);
            std::string magic(8, '\0'); input.read(magic.data(), 8);
            const auto version = Get<std::uint32_t>(input);
            if (magic == "PLUTOPK1" && version == 1)
            {
                const auto count = Get<std::uint32_t>(input);
                if (count > length / 13) Fail("Invalid legacy pack file count.");
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    auto nameSize = Get<std::uint32_t>(input);
                    Entry entry; entry.size = Get<std::uint64_t>(input);
                    if (!nameSize || nameSize > 4096) Fail("Invalid legacy pack path length.");
                    entry.name.resize(nameSize); input.read(entry.name.data(), nameSize);
                    if (!input) Fail("Truncated legacy pack path.");
                    for (char &c : entry.name) c = static_cast<char>(static_cast<unsigned char>(c) ^ 0xA7);
                    auto offset = static_cast<std::uint64_t>(input.tellg());
                    if (offset > length || entry.size > length - offset) Fail("Truncated legacy pack payload.");
                    for (std::uint64_t remaining = entry.size; remaining;)
                    {
                        auto size = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, BlockSize));
                        entry.blocks.push_back({offset, size, size, 0, 2}); offset += size; remaining -= size;
                    }
                    input.seekg(static_cast<std::streamoff>(offset));
                    AddEntry(impl.entries, std::move(entry));
                }
                if (static_cast<std::uint64_t>(input.tellg()) != length) Fail("Unexpected legacy pack trailing bytes.");
            }
            else if (magic == "PLUTOPK2" && version == 2)
            {
                if (Get<std::uint32_t>(input) != BlockSize) Fail("Unsupported pack block size.");
                const auto indexOffset = Get<std::uint64_t>(input);
                const auto indexSize = Get<std::uint64_t>(input);
                const auto indexCrc = Get<std::uint32_t>(input);
                const auto count = Get<std::uint32_t>(input);
                if (indexOffset < HeaderSize || indexOffset > length || indexSize != length - indexOffset ||
                    indexSize > MaxIndexSize || count > indexSize / 17) Fail("Invalid pack index bounds.");
                std::string index(static_cast<std::size_t>(indexSize), '\0');
                input.seekg(static_cast<std::streamoff>(indexOffset)); input.read(index.data(), static_cast<std::streamsize>(indexSize));
                if (!input || Crc(index) != indexCrc) Fail("Content pack index checksum mismatch.");
                std::istringstream in(index, std::ios::binary);
                std::uint64_t nextOffset = HeaderSize;
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    auto nameSize = Get<std::uint32_t>(in);
                    if (!nameSize || nameSize > 4096) Fail("Invalid pack path length.");
                    Entry entry; entry.name.resize(nameSize); in.read(entry.name.data(), nameSize);
                    entry.size = Get<std::uint64_t>(in);
                    const auto blockCount = Get<std::uint32_t>(in);
                    if (blockCount != entry.size / BlockSize + (entry.size % BlockSize != 0) || blockCount > indexSize / 24)
                        Fail("Invalid pack block count.");
                    std::uint64_t remaining = entry.size;
                    for (std::uint32_t b = 0; b < blockCount; ++b)
                    {
                        Block block{Get<std::uint64_t>(in), Get<std::uint32_t>(in), Get<std::uint32_t>(in), Get<std::uint32_t>(in), Get<std::uint32_t>(in)};
                        if (block.offset != nextOffset || block.offset > indexOffset || block.stored > indexOffset - block.offset ||
                            block.raw != std::min<std::uint64_t>(remaining, BlockSize) || !block.stored || block.stored > BlockSize ||
                            block.codec > 1 || (block.codec == 0 && block.stored != block.raw)) Fail("Invalid content pack block bounds.");
                        nextOffset += block.stored; remaining -= block.raw; entry.blocks.push_back(block);
                    }
                    AddEntry(impl.entries, std::move(entry));
                }
                if (nextOffset != indexOffset || in.peek() != EOF) Fail("Unexpected data in content pack index.");
            }
            else Fail("Unsupported content pack format.");
            // Reject file/directory collisions before any compatibility extraction.
            for (const auto &[name, entry] : impl.entries)
                for (auto slash = name.find('/'); slash != std::string::npos; slash = name.find('/', slash + 1))
                    if (impl.entries.contains(name.substr(0, slash))) Fail("Content pack file/directory collision.");
            return pack;
        }
        catch (const std::exception &e) { Error(error, e); return {}; }
    }

    std::vector<std::string> Pack::Files() const
    {
        std::vector<std::string> result;
        for (const auto &[key, entry] : m_impl->entries) result.push_back(entry.name);
        return result;
    }
    bool Pack::Contains(const std::string &path) const { return m_impl->entries.contains(Fold(path)); }
    std::uint64_t Pack::Size(const std::string &path) const { return m_impl->entries.at(Fold(path)).size; }
    bool Pack::Read(const std::string &path, std::uint64_t offset, std::span<char> bytes, std::string *error) const
    {
        try
        {
            const auto &entry = m_impl->entries.at(Fold(path));
            if (offset > entry.size || bytes.size() > entry.size - offset) Fail("Content read outside asset bounds: " + path);
            std::ifstream input(m_impl->path, std::ios::binary);
            while (!bytes.empty())
            {
                const auto &block = entry.blocks.at(static_cast<std::size_t>(offset / BlockSize));
                std::string stored(block.stored, '\0'), decoded(block.raw, '\0');
                input.seekg(static_cast<std::streamoff>(block.offset)); input.read(stored.data(), block.stored);
                if (!input) Fail("Truncated pack payload: " + path);
                if (block.codec == 1)
                {
                    uLongf rawSize = block.raw;
                    if (uncompress(reinterpret_cast<Bytef *>(decoded.data()), &rawSize,
                                   reinterpret_cast<const Bytef *>(stored.data()), block.stored) != Z_OK || rawSize != block.raw)
                        Fail("Pack decompression failed: " + path);
                }
                else
                {
                    decoded = std::move(stored);
                    if (block.codec == 2) for (auto &c : decoded) c = static_cast<char>(static_cast<unsigned char>(c) ^ 0xA7);
                }
                if (block.codec != 2 && Crc(decoded) != block.crc) Fail("Pack payload checksum mismatch: " + path);
                const auto start = static_cast<std::size_t>(offset % BlockSize);
                const auto count = std::min(bytes.size(), decoded.size() - start);
                std::copy_n(decoded.data() + start, count, bytes.data()); bytes = bytes.subspan(count); offset += count;
            }
            return true;
        }
        catch (const std::exception &e) { Error(error, e); return false; }
    }
    bool Pack::Verify(std::string *error) const
    {
        std::array<char, BlockSize> bytes;
        for (const auto &[key, entry] : m_impl->entries)
            for (std::uint64_t offset = 0; offset < entry.size; offset += BlockSize)
                if (!Read(entry.name, offset, std::span(bytes).first(static_cast<std::size_t>(std::min<std::uint64_t>(BlockSize, entry.size - offset))), error)) return false;
        return true;
    }

    bool WritePack(const std::filesystem::path &directory, const std::filesystem::path &output, const PackOptions &options, std::string *error)
    {
        // A sibling temporary directory gives this invocation exclusive ownership
        // and permits atomic replacement without deleting an existing good pack.
        std::filesystem::path scratch;
        try
        {
            const auto root = std::filesystem::absolute(directory).lexically_normal();
            const auto destination = std::filesystem::absolute(output).lexically_normal();
            const auto relativeOutput = destination.lexically_relative(root);
            const auto relativeName = relativeOutput.generic_string();
            if (!relativeOutput.empty() && relativeName != ".." && !relativeName.starts_with("../"))
                Fail("Pack output must be outside its source directory.");
            std::filesystem::create_directories(destination.parent_path());
            for (unsigned i = 0; i < 10000; ++i)
            {
                auto candidate = destination; candidate += ".writing-" + std::to_string(i);
                if (std::filesystem::create_directory(candidate)) { scratch = candidate; break; }
            }
            if (scratch.empty()) Fail("Cannot reserve temporary pack output.");
            const auto temporary = scratch / "content";
            std::map<std::string, Entry> entries;
            for (const auto &item : std::filesystem::recursive_directory_iterator(root))
            {
                if (item.is_symlink()) Fail("Symlinks are not supported in content packs.");
                if (!item.is_regular_file()) continue;
                Entry entry; const auto utf8 = item.path().lexically_relative(root).generic_u8string(); entry.name.assign(reinterpret_cast<const char *>(utf8.data()), utf8.size()); entry.size = item.file_size();
                AddEntry(entries, std::move(entry));
            }
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(std::string(HeaderSize, '\0').data(), HeaderSize);
            std::string raw(BlockSize, '\0'), compressed(compressBound(BlockSize), '\0');
            for (auto &[key, entry] : entries)
            {
                std::ifstream in(root / std::filesystem::u8path(entry.name), std::ios::binary);
                if (!in) Fail("Cannot open pack source: " + entry.name);
                for (std::uint64_t remaining = entry.size; remaining;)
                {
                    auto count = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, BlockSize));
                    in.read(raw.data(), count); if (!in) Fail("Cannot read pack source: " + entry.name);
                    Block block{static_cast<std::uint64_t>(out.tellp()), count, count, Crc(std::span(raw.data(), count)), 0};
                    uLongf compressedSize = static_cast<uLongf>(compressed.size());
                    if (options.compress && compress2(reinterpret_cast<Bytef *>(compressed.data()), &compressedSize,
                        reinterpret_cast<const Bytef *>(raw.data()), count, Z_BEST_SPEED) == Z_OK && compressedSize < count)
                    { block.codec = 1; block.stored = static_cast<std::uint32_t>(compressedSize); }
                    out.write(block.codec ? compressed.data() : raw.data(), block.stored);
                    entry.blocks.push_back(block); remaining -= count;
                }
                if (in.peek() != EOF) Fail("Source changed while packing: " + entry.name);
            }
            const auto indexOffset = static_cast<std::uint64_t>(out.tellp());
            std::ostringstream index(std::ios::binary);
            for (const auto &[key, entry] : entries)
            {
                Put(index, static_cast<std::uint32_t>(entry.name.size())); index.write(entry.name.data(), entry.name.size());
                Put(index, entry.size); Put(index, static_cast<std::uint32_t>(entry.blocks.size()));
                for (const auto &block : entry.blocks)
                { Put(index, block.offset); Put(index, block.stored); Put(index, block.raw); Put(index, block.crc); Put(index, block.codec); }
            }
            auto bytes = index.str();
            if (bytes.size() > MaxIndexSize) Fail("Content pack index is too large.");
            out.write(bytes.data(), bytes.size()); out.seekp(0); out.write("PLUTOPK2", 8);
            Put(out, std::uint32_t{2}); Put(out, BlockSize); Put(out, indexOffset); Put(out, static_cast<std::uint64_t>(bytes.size()));
            Put(out, Crc(bytes)); Put(out, static_cast<std::uint32_t>(entries.size())); out.close();
            if (!out) Fail("Failed to finish content pack.");
            auto verification = Pack::Open(temporary, error);
            if (!verification || !verification->Verify(error)) Fail(error && !error->empty() ? *error : "Pack verification failed.");
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) Fail("Cannot replace content pack.");
#else
            std::filesystem::rename(temporary, destination);
#endif
            std::filesystem::remove(scratch);
            return true;
        }
        catch (const std::exception &e)
        {
            if (!scratch.empty()) { std::error_code ignored; std::filesystem::remove_all(scratch, ignored); }
            Error(error, e); return false;
        }
    }

    namespace
    {
        struct Mounted { std::string root; std::shared_ptr<Pack> pack; std::filesystem::file_time_type modified; };
        std::mutex MountMutex;
        std::vector<Mounted> Mounts;
        struct Lookup { bool mounted = false; std::shared_ptr<Pack> pack; std::string relative; std::filesystem::file_time_type modified; };
        Lookup Find(const std::filesystem::path &path)
        {
            if (path.empty()) return {};
            const auto key = Key(path);
            std::lock_guard lock(MountMutex);
            Lookup result;
            for (auto it = Mounts.rbegin(); it != Mounts.rend(); ++it)
            {
                if (key == it->root || key.starts_with(it->root + '/'))
                {
                    result.mounted = true;
                    const auto relative = key == it->root ? std::string{} : key.substr(it->root.size() + 1);
                    if (it->pack->Contains(relative)) return {true, it->pack, relative, it->modified};
                }
            }
            return result;
        }
    }
    bool Mount(const std::filesystem::path &path, const std::filesystem::path &root, std::string *error)
    {
        auto pack = Pack::Open(path, error); if (!pack) return false;
        try
        {
            Mounted mounted{Key(root), pack, std::filesystem::last_write_time(path)};
            std::lock_guard lock(MountMutex); Mounts.push_back(std::move(mounted)); return true;
        }
        catch (const std::exception &e) { Error(error, e); return false; }
    }
    void UnmountAll() { std::lock_guard lock(MountMutex); Mounts.clear(); }
    bool IsMounted(const std::filesystem::path &path) { return Find(path).mounted; }
    bool Exists(const std::filesystem::path &path)
    {
        const auto found = Find(path);
        if (found.pack) return true;
        if (found.mounted) return !Files(path).empty();
        return std::filesystem::exists(path);
    }
    bool IsRegularFile(const std::filesystem::path &path, std::error_code &error)
    {
        const auto found = Find(path);
        if (!found.mounted) return std::filesystem::is_regular_file(path, error);
        error.clear(); return bool(found.pack);
    }
    std::uintmax_t FileSize(const std::filesystem::path &path, std::error_code &error)
    {
        const auto found = Find(path);
        if (!found.mounted) return std::filesystem::file_size(path, error);
        if (!found.pack) { error = std::make_error_code(std::errc::no_such_file_or_directory); return std::uintmax_t(-1); }
        error.clear(); return found.pack->Size(found.relative);
    }
    std::filesystem::file_time_type LastWriteTime(const std::filesystem::path &path, std::error_code &error)
    {
        const auto found = Find(path);
        if (!found.mounted) return std::filesystem::last_write_time(path, error);
        if (!found.pack) { error = std::make_error_code(std::errc::no_such_file_or_directory); return {}; }
        error.clear(); return found.modified;
    }
    std::vector<std::filesystem::path> Files(const std::filesystem::path &root)
    {
        if (root.empty()) return {};
        std::map<std::string, std::filesystem::path> files;
        const auto key = Key(root);
        bool mounted = false;
        {
            std::lock_guard lock(MountMutex);
            for (const auto &mount : Mounts)
            {
                if (key != mount.root && !key.starts_with(mount.root + '/')) continue;
                mounted = true;
                for (const auto &name : mount.pack->Files())
                {
                    auto path = std::filesystem::u8path(mount.root) / std::filesystem::u8path(name);
                    auto fileKey = Key(path);
                    if (fileKey.starts_with(key + '/')) files[fileKey] = path;
                }
            }
        }
        if (!mounted)
        {
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
                if (it->is_regular_file()) files[Key(it->path())] = it->path();
        }
        std::vector<std::filesystem::path> result;
        for (const auto &[key, path] : files) result.push_back(path);
        return result;
    }
    bool ReadFile(const std::filesystem::path &path, std::string &bytes, std::string *error)
    {
        bytes.clear();
        try
        {
            const auto found = Find(path);
            if (found.mounted)
            {
                if (!found.pack) Fail("Asset is absent from mounted content: " + path.string());
                const auto size = found.pack->Size(found.relative);
                if (size > bytes.max_size()) Fail("Asset is too large to load into memory.");
                bytes.resize(static_cast<std::size_t>(size));
                if (!found.pack->Read(found.relative, 0, bytes, error)) { bytes.clear(); return false; }
            }
            else
            {
                std::ifstream in(path, std::ios::binary);
                if (!in) Fail("Cannot open asset: " + path.string());
                bytes.assign(std::istreambuf_iterator<char>(in), {});
                if (in.bad()) Fail("Cannot read asset: " + path.string());
            }
            return true;
        }
        catch (const std::exception &e) { bytes.clear(); Error(error, e); return false; }
    }
    static bool WriteMaterialized(const std::filesystem::path &path, const std::string &bytes, std::string *error)
    {
        try
        {
            // Refuse symlink/reparse redirection in the extraction destination.
            for (auto current = std::filesystem::absolute(path); !current.empty();)
            {
                std::error_code ec;
                if (std::filesystem::is_symlink(std::filesystem::symlink_status(current, ec))) Fail("Symlink in extraction path.");
#ifdef _WIN32
                const auto attributes = GetFileAttributesW(current.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) Fail("Reparse point in extraction path.");
#endif
                auto parent = current.parent_path(); if (parent == current) break; current = parent;
            }
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary | std::ios::trunc); out.write(bytes.data(), bytes.size()); out.close();
            if (!out) Fail("Failed to materialize asset: " + path.string());
            return true;
        }
        catch (const std::exception &e) { Error(error, e); return false; }
    }
    bool Materialize(const std::filesystem::path &path, std::string *error)
    {
        if (!IsMounted(path)) return Exists(path);
        std::string bytes;
        return ReadFile(path, bytes, error) && WriteMaterialized(path, bytes, error);
    }
    bool Pack::ExtractTo(const std::filesystem::path &directory, std::string *error) const
    {
        if (!Verify(error)) return false;
        try
        {
            for (const auto &name : Files())
            {
                std::string bytes;
                if (Size(name) > bytes.max_size()) Fail("Asset too large to extract.");
                bytes.resize(static_cast<std::size_t>(Size(name)));
                if (!Read(name, 0, bytes, error) || !WriteMaterialized(directory / std::filesystem::u8path(name), bytes, error)) return false;
            }
            return true;
        }
        catch (const std::exception &e) { Error(error, e); return false; }
    }
    InputFile::InputFile(const std::filesystem::path &path, std::ios::openmode mode) : std::istream(nullptr)
    {
        if (!IsMounted(path))
        {
            m_open = m_file.open(path, mode | std::ios::in) != nullptr;
            rdbuf(&m_file);
        }
        else
        {
            std::string bytes;
            m_open = ReadFile(path, bytes);
#ifdef _WIN32
            if (!(mode & std::ios::binary))
            {
                std::size_t write = 0;
                for (std::size_t read = 0; read < bytes.size(); ++read)
                    if (!(bytes[read] == '\r' && read + 1 < bytes.size() && bytes[read + 1] == '\n')) bytes[write++] = bytes[read];
                bytes.resize(write);
            }
#endif
            m_memory.str(std::move(bytes)); rdbuf(&m_memory);
        }
        if (!m_open) setstate(std::ios::failbit);
        else if (mode & std::ios::ate) seekg(0, std::ios::end);
    }
    void InputFile::close() { if (m_file.is_open()) m_file.close(); m_open = false; }
}
