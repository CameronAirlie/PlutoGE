#include "PlutoGE/assets/Project.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <system_error>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view kProjectHeader = "PLUTOPROJECT";
        constexpr int kProjectVersion = 1;

        std::filesystem::path NormalizeAbsolutePath(const std::filesystem::path &path)
        {
            std::error_code errorCode;
            if (path.is_absolute())
            {
                return path.lexically_normal();
            }

            return std::filesystem::absolute(path, errorCode).lexically_normal();
        }

        bool TryMakeRelativePath(const std::filesystem::path &target,
                                 const std::filesystem::path &base,
                                 std::filesystem::path &relativePath)
        {
            std::error_code errorCode;
            relativePath = std::filesystem::relative(NormalizeAbsolutePath(target), NormalizeAbsolutePath(base), errorCode);
            if (errorCode || relativePath.empty())
            {
                return false;
            }

            const auto normalizedRelativePath = relativePath.lexically_normal();
            const auto genericRelativePath = normalizedRelativePath.generic_string();
            if (genericRelativePath == "." || genericRelativePath.rfind("../", 0) == 0 || genericRelativePath == "..")
            {
                return false;
            }

            relativePath = normalizedRelativePath;
            return true;
        }

        std::string EscapeText(std::string_view text)
        {
            std::string escaped;
            escaped.reserve(text.size());

            for (const char character : text)
            {
                switch (character)
                {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    escaped.push_back(character);
                    break;
                }
            }

            return escaped;
        }

        std::vector<std::string> SplitEscaped(std::string_view text, char delimiter)
        {
            std::vector<std::string> parts;
            std::string current;
            bool escaping = false;

            for (const char character : text)
            {
                if (escaping)
                {
                    switch (character)
                    {
                    case 'n':
                        current.push_back('\n');
                        break;
                    case 't':
                        current.push_back('\t');
                        break;
                    case '\\':
                        current.push_back('\\');
                        break;
                    default:
                        current.push_back(character);
                        break;
                    }

                    escaping = false;
                    continue;
                }

                if (character == '\\')
                {
                    escaping = true;
                    continue;
                }

                if (character == delimiter)
                {
                    parts.push_back(current);
                    current.clear();
                    continue;
                }

                current.push_back(character);
            }

            parts.push_back(current);
            return parts;
        }

        void SetError(std::string *errorMessage, std::string message)
        {
            if (errorMessage)
            {
                *errorMessage = std::move(message);
            }
        }

        bool ParseInteger(std::string_view value, int &parsedValue)
        {
            const auto *begin = value.data();
            const auto *end = value.data() + value.size();
            const auto result = std::from_chars(begin, end, parsedValue);
            return result.ec == std::errc() && result.ptr == end;
        }

        bool ParseUnsignedInteger(std::string_view value, std::uintmax_t &parsedValue)
        {
            const auto *begin = value.data();
            const auto *end = value.data() + value.size();
            const auto result = std::from_chars(begin, end, parsedValue);
            return result.ec == std::errc() && result.ptr == end;
        }

        std::string GetExecutableExtension()
        {
#ifdef _WIN32
            return ".exe";
#else
            return {};
#endif
        }

        bool CopyDirectoryRecursive(const std::filesystem::path &sourceDirectory,
                                    const std::filesystem::path &destinationDirectory,
                                    std::string *errorMessage)
        {
            if (!std::filesystem::exists(sourceDirectory))
            {
                return true;
            }

            std::error_code errorCode;
            std::filesystem::create_directories(destinationDirectory, errorCode);
            if (errorCode)
            {
                SetError(errorMessage, "Failed to create export asset directory: " + destinationDirectory.string());
                return false;
            }

            for (std::filesystem::recursive_directory_iterator iterator(sourceDirectory, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
                 iterator != end;
                 iterator.increment(errorCode))
            {
                if (errorCode)
                {
                    SetError(errorMessage, "Failed while enumerating project assets in: " + sourceDirectory.string());
                    return false;
                }

                const auto &sourcePath = iterator->path();
                const auto relativePath = std::filesystem::relative(sourcePath, sourceDirectory, errorCode);
                if (errorCode)
                {
                    SetError(errorMessage, "Failed to build export path for: " + sourcePath.string());
                    return false;
                }

                const auto destinationPath = destinationDirectory / relativePath;
                if (iterator->is_directory())
                {
                    errorCode.clear();
                    std::filesystem::create_directories(destinationPath, errorCode);
                    if (errorCode)
                    {
                        SetError(errorMessage, "Failed to create export directory: " + destinationPath.string() + " (" + errorCode.message() + ")");
                        return false;
                    }
                    continue;
                }

                const auto normalizedSourcePath = NormalizeAbsolutePath(sourcePath);
                const auto normalizedDestinationPath = NormalizeAbsolutePath(destinationPath);
                if (normalizedSourcePath == normalizedDestinationPath)
                {
                    continue;
                }

                errorCode.clear();
                std::filesystem::create_directories(destinationPath.parent_path(), errorCode);
                if (errorCode)
                {
                    SetError(errorMessage, "Failed to create export asset parent directory: " + destinationPath.parent_path().string() + " (" + errorCode.message() + ")");
                    return false;
                }

                errorCode.clear();
                if (std::filesystem::exists(destinationPath))
                {
                    std::filesystem::remove(destinationPath, errorCode);
                    if (errorCode)
                    {
                        SetError(errorMessage, "Failed to replace existing export asset: " + destinationPath.string() + " (" + errorCode.message() + ")");
                        return false;
                    }
                }

                errorCode.clear();
                std::filesystem::copy_file(sourcePath,
                                           destinationPath,
                                           std::filesystem::copy_options::none,
                                           errorCode);
                if (errorCode)
                {
                    SetError(errorMessage,
                             "Failed to copy asset to export: " + sourcePath.string() + " -> " + destinationPath.string() + " (" + errorCode.message() + ")");
                    return false;
                }
            }

            return true;
        }
    }

    Project::Project(std::filesystem::path manifestPath, ProjectManifest manifest)
        : m_manifestPath(NormalizeAbsolutePath(manifestPath)),
          m_rootDirectory(m_manifestPath.parent_path()),
          m_manifest(std::move(manifest))
    {
    }

    std::unique_ptr<Project> Project::Create(const std::filesystem::path &manifestPath,
                                             std::string projectName,
                                             std::string *errorMessage)
    {
        const auto normalizedManifestPath = NormalizeAbsolutePath(manifestPath);
        std::error_code errorCode;
        std::filesystem::create_directories(normalizedManifestPath.parent_path(), errorCode);
        if (errorCode)
        {
            SetError(errorMessage, "Failed to create project directory: " + normalizedManifestPath.parent_path().string());
            return nullptr;
        }

        ProjectManifest manifest;
        if (!projectName.empty())
        {
            manifest.name = std::move(projectName);
        }
        manifest.windowTitle = manifest.name;

        auto project = std::make_unique<Project>(normalizedManifestPath, manifest);
        std::filesystem::create_directories(project->GetAssetDirectoryPath(), errorCode);
        if (errorCode)
        {
            SetError(errorMessage, "Failed to create project asset directory: " + project->GetAssetDirectoryPath().string());
            return nullptr;
        }

        std::filesystem::create_directories(project->GetAssetDirectoryPath() / "Scenes", errorCode);
        if (errorCode)
        {
            SetError(errorMessage, "Failed to create default Scenes directory.");
            return nullptr;
        }

        project->RefreshAssetRegistry();
        if (!project->Save(errorMessage))
        {
            return nullptr;
        }

        return project;
    }

    std::unique_ptr<Project> Project::Load(const std::filesystem::path &manifestPath,
                                           std::string *errorMessage)
    {
        const auto normalizedManifestPath = NormalizeAbsolutePath(manifestPath);
        std::ifstream input(normalizedManifestPath);
        if (!input.is_open())
        {
            SetError(errorMessage, "Failed to open project manifest for reading.");
            return nullptr;
        }

        ProjectManifest manifest;
        bool hasValidHeader = false;

        std::string line;
        while (std::getline(input, line))
        {
            const auto tokens = SplitEscaped(line, '\t');
            if (tokens.empty())
            {
                continue;
            }

            if (tokens[0] == kProjectHeader && tokens.size() >= 2)
            {
                int version = 0;
                if (!ParseInteger(tokens[1], version) || version != kProjectVersion)
                {
                    SetError(errorMessage, "Unsupported project manifest version.");
                    return nullptr;
                }
                hasValidHeader = true;
                continue;
            }

            if (tokens[0] == "NAME" && tokens.size() >= 2)
            {
                manifest.name = tokens[1];
                continue;
            }

            if (tokens[0] == "ASSET_DIR" && tokens.size() >= 2)
            {
                manifest.assetDirectory = tokens[1];
                continue;
            }

            if (tokens[0] == "STARTUP_SCENE" && tokens.size() >= 2)
            {
                manifest.startupScene = tokens[1];
                continue;
            }

            if (tokens[0] == "WINDOW_TITLE" && tokens.size() >= 2)
            {
                manifest.windowTitle = tokens[1];
                continue;
            }

            if (tokens[0] == "WINDOW_SIZE" && tokens.size() >= 3)
            {
                ParseInteger(tokens[1], manifest.windowWidth);
                ParseInteger(tokens[2], manifest.windowHeight);
                continue;
            }

            if (tokens[0] == "VSYNC" && tokens.size() >= 2)
            {
                manifest.vSyncEnabled = tokens[1] == "1" || tokens[1] == "true";
                continue;
            }

            if (tokens[0] == "ASSET" && tokens.size() >= 3)
            {
                ProjectAssetEntry entry;
                entry.reference = tokens[1];
                ParseUnsignedInteger(tokens[2], entry.size);
                manifest.assetEntries.push_back(std::move(entry));
                continue;
            }
        }

        if (!hasValidHeader)
        {
            SetError(errorMessage, "Invalid project manifest header.");
            return nullptr;
        }

        return std::make_unique<Project>(normalizedManifestPath, std::move(manifest));
    }

    bool Project::IsProjectAssetReference(std::string_view reference)
    {
        return reference.rfind(kProjectAssetScheme, 0) == 0;
    }

    bool Project::IsEngineAssetReference(std::string_view reference)
    {
        return reference.rfind(kEngineAssetScheme, 0) == 0;
    }

    std::vector<std::string> Project::GetBuiltinAssetReferences()
    {
        return {
            std::string(kBuiltinCubeMeshReference),
            std::string(kBuiltinDefaultMaterialReference),
        };
    }

    bool Project::Save(std::string *errorMessage) const
    {
        std::ofstream output(m_manifestPath, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            SetError(errorMessage, "Failed to open project manifest for writing.");
            return false;
        }

        output << kProjectHeader << '\t' << kProjectVersion << '\n';
        output << "NAME\t" << EscapeText(m_manifest.name) << '\n';
        output << "ASSET_DIR\t" << EscapeText(m_manifest.assetDirectory) << '\n';
        output << "STARTUP_SCENE\t" << EscapeText(m_manifest.startupScene) << '\n';
        output << "WINDOW_TITLE\t" << EscapeText(m_manifest.windowTitle) << '\n';
        output << "WINDOW_SIZE\t" << m_manifest.windowWidth << '\t' << m_manifest.windowHeight << '\n';
        output << "VSYNC\t" << (m_manifest.vSyncEnabled ? 1 : 0) << '\n';
        for (const auto &assetEntry : m_manifest.assetEntries)
        {
            output << "ASSET\t"
                   << EscapeText(assetEntry.reference) << '\t'
                   << assetEntry.size << '\n';
        }

        return true;
    }

    void Project::RefreshAssetRegistry()
    {
        m_manifest.assetEntries.clear();

        const auto assetDirectoryPath = GetAssetDirectoryPath();
        if (!std::filesystem::exists(assetDirectoryPath))
        {
            return;
        }

        std::error_code errorCode;
        for (std::filesystem::recursive_directory_iterator iterator(assetDirectoryPath, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
             iterator != end;
             iterator.increment(errorCode))
        {
            if (errorCode || !iterator->is_regular_file())
            {
                continue;
            }

            ProjectAssetEntry entry;
            entry.reference = MakeAssetReference(iterator->path());
            entry.size = iterator->file_size(errorCode);
            if (!entry.reference.empty())
            {
                m_manifest.assetEntries.push_back(std::move(entry));
            }
            errorCode.clear();
        }

        std::sort(m_manifest.assetEntries.begin(),
                  m_manifest.assetEntries.end(),
                  [](const ProjectAssetEntry &left, const ProjectAssetEntry &right)
                  {
                      return left.reference < right.reference;
                  });
    }

    std::string Project::MakeAssetReference(const std::filesystem::path &filePath) const
    {
        const auto genericPath = filePath.generic_string();
        if (IsProjectAssetReference(genericPath) || IsEngineAssetReference(genericPath))
        {
            return genericPath;
        }

        std::filesystem::path relativePath;
        if (TryMakeRelativePath(filePath, GetAssetDirectoryPath(), relativePath))
        {
            return std::string(kProjectAssetScheme) + relativePath.generic_string();
        }

        return NormalizeAbsolutePath(filePath).string();
    }

    std::filesystem::path Project::ResolveAssetReference(std::string_view reference) const
    {
        if (!IsProjectAssetReference(reference))
        {
            return std::filesystem::path(reference);
        }

        const auto relativePath = std::filesystem::path(reference.substr(kProjectAssetScheme.size()));
        return (GetAssetDirectoryPath() / relativePath).lexically_normal();
    }

    bool Project::IsInAssetDirectory(const std::filesystem::path &filePath) const
    {
        std::filesystem::path relativePath;
        return TryMakeRelativePath(filePath, GetAssetDirectoryPath(), relativePath);
    }

    std::filesystem::path Project::GetAssetDirectoryPath() const
    {
        return (m_rootDirectory / m_manifest.assetDirectory).lexically_normal();
    }

    std::filesystem::path GetRuntimeManifestPathForExecutable(const std::filesystem::path &executablePath)
    {
        return executablePath.parent_path() / (executablePath.stem().string() + ".plutoproject");
    }

    std::filesystem::path FindRuntimeExecutable(const std::filesystem::path &searchRoot)
    {
        const std::string runtimeFileName = "PlutoGERuntime" + GetExecutableExtension();
        std::filesystem::path bestMatch;
        std::error_code errorCode;

        for (std::filesystem::recursive_directory_iterator iterator(searchRoot, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
             iterator != end;
             iterator.increment(errorCode))
        {
            if (errorCode || !iterator->is_regular_file())
            {
                continue;
            }

            const auto path = iterator->path();
            if (path.filename().string() != runtimeFileName)
            {
                continue;
            }

            if (bestMatch.empty())
            {
                bestMatch = path;
                continue;
            }

            std::error_code timeErrorCode;
            const auto bestWriteTime = std::filesystem::last_write_time(bestMatch, timeErrorCode);
            const auto candidateWriteTime = std::filesystem::last_write_time(path, timeErrorCode);
            if (!timeErrorCode && candidateWriteTime > bestWriteTime)
            {
                bestMatch = path;
            }
        }

        return bestMatch;
    }

    bool ExportStandaloneProject(const Project &project,
                                 const std::filesystem::path &destinationExecutablePath,
                                 const std::filesystem::path &runtimeExecutablePath,
                                 std::string *errorMessage)
    {
        const auto normalizedRuntimeExecutablePath = NormalizeAbsolutePath(runtimeExecutablePath);
        if (!std::filesystem::exists(normalizedRuntimeExecutablePath))
        {
            SetError(errorMessage, "Runtime executable was not found: " + normalizedRuntimeExecutablePath.string());
            return false;
        }

        const auto normalizedDestinationExecutablePath = NormalizeAbsolutePath(destinationExecutablePath);
        std::error_code errorCode;
        std::filesystem::create_directories(normalizedDestinationExecutablePath.parent_path(), errorCode);
        if (errorCode)
        {
            SetError(errorMessage, "Failed to create export directory: " + normalizedDestinationExecutablePath.parent_path().string());
            return false;
        }

        std::filesystem::copy_file(normalizedRuntimeExecutablePath,
                                   normalizedDestinationExecutablePath,
                                   std::filesystem::copy_options::overwrite_existing,
                                   errorCode);
        if (errorCode)
        {
            SetError(errorMessage, "Failed to copy runtime executable into export directory.");
            return false;
        }

        const auto exportedAssetDirectory = normalizedDestinationExecutablePath.parent_path() / project.GetManifest().assetDirectory;
        if (!CopyDirectoryRecursive(project.GetAssetDirectoryPath(), exportedAssetDirectory, errorMessage))
        {
            return false;
        }

        Project exportedProject(GetRuntimeManifestPathForExecutable(normalizedDestinationExecutablePath), project.GetManifest());
        if (!exportedProject.Save(errorMessage))
        {
            return false;
        }

        return true;
    }
}