#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace PlutoGE::content
{
    struct PackOptions { bool compress = true; };
    // Files are stored in independently compressed 256 KiB blocks. CRCs detect
    // accidental corruption; they do not authenticate untrusted publishers.
    bool WritePack(const std::filesystem::path &directory, const std::filesystem::path &output,
                   const PackOptions &options = {}, std::string *error = nullptr);

    class Pack
    {
    public:
        static std::shared_ptr<Pack> Open(const std::filesystem::path &path, std::string *error = nullptr);
        std::vector<std::string> Files() const;
        bool Contains(const std::string &path) const;
        std::uint64_t Size(const std::string &path) const;
        bool Read(const std::string &path, std::uint64_t offset, std::span<char> bytes, std::string *error = nullptr) const;
        bool Verify(std::string *error = nullptr) const;
        bool ExtractTo(const std::filesystem::path &directory, std::string *error = nullptr) const;
    private:
        struct Impl;
        std::shared_ptr<Impl> m_impl;
    };

    // Later mounts override earlier mounts at the same virtual root. Mount and
    // unmount before loading assets; already loaded engine caches are unchanged.
    bool Mount(const std::filesystem::path &pack, const std::filesystem::path &root, std::string *error = nullptr);
    void UnmountAll();
    bool IsMounted(const std::filesystem::path &path);
    bool Exists(const std::filesystem::path &path);
    bool IsRegularFile(const std::filesystem::path &path, std::error_code &error);
    std::uintmax_t FileSize(const std::filesystem::path &path, std::error_code &error);
    std::filesystem::file_time_type LastWriteTime(const std::filesystem::path &path, std::error_code &error);
    std::vector<std::filesystem::path> Files(const std::filesystem::path &root);
    bool ReadFile(const std::filesystem::path &path, std::string &bytes, std::string *error = nullptr);
    // Compatibility escape hatch for APIs such as hostfxr that need real files.
    bool Materialize(const std::filesystem::path &path, std::string *error = nullptr);

    // Loose files retain ordinary file streaming; packed files decode only the
    // requested asset into memory. Binary mode preserves bytes exactly.
    class InputFile : public std::istream
    {
    public:
        explicit InputFile(const std::filesystem::path &path, std::ios::openmode mode = std::ios::in);
        bool is_open() const { return m_open; }
        void close();
    private:
        std::filebuf m_file;
        std::stringbuf m_memory;
        bool m_open = false;
    };
}
