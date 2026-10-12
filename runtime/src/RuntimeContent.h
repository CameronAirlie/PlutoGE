#pragma once
#include "PlutoGE/platform/ContentPack.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace PlutoGE
{
    /// Owns process-local packed content mounts and their materialized managed files.
    class RuntimeContent
    {
    public:
        RuntimeContent() = default;
        RuntimeContent(const RuntimeContent &) = delete;
        RuntimeContent &operator=(const RuntimeContent &) = delete;
        ~RuntimeContent()
        {
            content::UnmountAll();
            if (!m_directory.empty()) { std::error_code error; std::filesystem::remove_all(m_directory, error); }
        }
        std::filesystem::path MountProject(const std::filesystem::path &packPath)
        {
            if (!m_directory.empty()) throw std::logic_error("Runtime content already mounted");
            const auto pack = std::filesystem::absolute(packPath).lexically_normal();
            const auto unique = pack.stem().string() + "-" +
                std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            const auto directory = std::filesystem::temp_directory_path() / "PlutoGE" / "Content" / unique;
            std::filesystem::create_directories(directory.parent_path());
            if (!std::filesystem::create_directory(directory)) throw std::runtime_error("Cannot reserve runtime content directory");
            m_directory = directory;
            std::string error;
            if (!content::Mount(pack, m_directory, &error)) throw std::runtime_error(error);
            auto mountsPath = pack; mountsPath.replace_extension(".plutomounts");
            std::ifstream mounts(mountsPath);
            std::string name;
            while (std::getline(mounts, name))
            {
                if (!name.empty() && name.back() == '\r') name.pop_back();
                if (name.empty() || name.front() == '#') continue;
                const auto relative = std::filesystem::u8path(name);
                if (relative.has_parent_path() || relative.is_absolute() || relative.extension() != ".plutopack" ||
                    !content::Mount(pack.parent_path() / relative, m_directory, &error))
                    throw std::runtime_error("Cannot mount patch pack " + name + ": " + error);
            }
            for (const auto &path : content::Files(m_directory))
            {
                auto extension = path.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (extension == ".dll" || extension == ".pdb" || extension == ".plutoinput" || extension == ".json")
                    if (!content::Materialize(path, &error)) throw std::runtime_error(error);
            }
            auto manifest = pack.filename(); manifest.replace_extension(".plutoproject");
            return m_directory / manifest;
        }
    private:
        std::filesystem::path m_directory;
    };
}
