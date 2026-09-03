#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/AssetDatabase.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

namespace PlutoGE::assets
{
    render::rhi::TemporalUpscalerOptions ProjectManifest::GetTemporalUpscalerOptions() const noexcept
    {
        render::rhi::TemporalUpscalerOptions options;
        if (graphicsApi != render::rhi::GraphicsApi::Vulkan)
            return options;
        options.technology = runtimeUpscaler == RuntimeUpscalerMode::Fsr2
                                 ? render::rhi::TemporalUpscaler::Fsr2
                             : runtimeUpscaler == RuntimeUpscalerMode::Dlss
                                 ? render::rhi::TemporalUpscaler::Dlss
                                 : render::rhi::TemporalUpscaler::None;
        options.quality = runtimeUpscalerQuality;
        options.sharpness = std::clamp(runtimeUpscaleSharpness, 0.0f, 1.0f);
        return options;
    }

    namespace
    {
        constexpr std::string_view kProjectHeader = "PLUTOPROJECT";
        constexpr int kProjectVersion = 1;
        constexpr int kRuntimeSearchAncestorLimit = 8;
        constexpr std::string_view kBundledDotnetRuntimeDirectory = "DotnetRuntime";
        constexpr std::string_view kContentPackMagic = "PLUTOPK1";
        constexpr std::uint32_t kContentPackVersion = 1;
        constexpr std::uint8_t kContentPackXorKey = 0xA7;

        std::filesystem::path NormalizeAbsolutePath(const std::filesystem::path &path)
        {
            std::error_code errorCode;
            if (path.is_absolute())
            {
                return path.lexically_normal();
            }

            return std::filesystem::absolute(path, errorCode).lexically_normal();
        }

        std::string PathToUtf8String(const std::filesystem::path &path)
        {
#ifdef _WIN32
            const auto utf8Path = path.u8string();
            std::string result;
            result.reserve(utf8Path.size());
            for (const auto character : utf8Path)
            {
                result.push_back(static_cast<char>(character));
            }
            return result;
#else
            return path.string();
#endif
        }

        std::string PathToGenericUtf8String(const std::filesystem::path &path)
        {
#ifdef _WIN32
            const auto utf8Path = path.generic_u8string();
            std::string result;
            result.reserve(utf8Path.size());
            for (const auto character : utf8Path)
            {
                result.push_back(static_cast<char>(character));
            }
            return result;
#else
            return path.generic_string();
#endif
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
            const auto genericRelativePath = PathToGenericUtf8String(normalizedRelativePath);
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

        bool ParseFloat(std::string_view value, float &parsedValue)
        {
            std::string buffer(value);
            char *parseEnd = nullptr;
            parsedValue = std::strtof(buffer.c_str(), &parseEnd);
            return parseEnd != nullptr && *parseEnd == '\0';
        }

        std::filesystem::path FindNewestRuntimeExecutableInTree(const std::filesystem::path &searchRoot,
                                                                const std::string &runtimeFileName)
        {
            if (searchRoot.empty() || !std::filesystem::exists(searchRoot))
            {
                return {};
            }

            std::filesystem::path bestMatch;
            const std::filesystem::path runtimeFilePath(runtimeFileName);
            std::error_code errorCode;
            for (std::filesystem::recursive_directory_iterator iterator(searchRoot, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
                 iterator != end;
                 iterator.increment(errorCode))
            {
                if (errorCode)
                {
                    errorCode.clear();
                    continue;
                }

                std::error_code statusErrorCode;
                if (!iterator->is_regular_file(statusErrorCode))
                {
                    continue;
                }

                const auto path = iterator->path();
                if (path.filename() != runtimeFilePath)
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
                SetError(errorMessage, "Failed to create export asset directory: " + PathToUtf8String(destinationDirectory));
                return false;
            }

            for (std::filesystem::recursive_directory_iterator iterator(sourceDirectory, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
                 iterator != end;
                 iterator.increment(errorCode))
            {
                if (errorCode)
                {
                    SetError(errorMessage, "Failed while enumerating project assets in: " + PathToUtf8String(sourceDirectory));
                    return false;
                }

                const auto &sourcePath = iterator->path();
                const auto relativePath = std::filesystem::relative(sourcePath, sourceDirectory, errorCode);
                if (errorCode)
                {
                    SetError(errorMessage, "Failed to build export path for: " + PathToUtf8String(sourcePath));
                    return false;
                }

                const auto destinationPath = destinationDirectory / relativePath;
                if (iterator->is_directory())
                {
                    errorCode.clear();
                    std::filesystem::create_directories(destinationPath, errorCode);
                    if (errorCode)
                    {
                        SetError(errorMessage, "Failed to create export directory: " + PathToUtf8String(destinationPath) + " (" + errorCode.message() + ")");
                        return false;
                    }
                    continue;
                }

                if (sourcePath.extension() == ".cs")
                {
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
                    SetError(errorMessage, "Failed to create export asset parent directory: " + PathToUtf8String(destinationPath.parent_path()) + " (" + errorCode.message() + ")");
                    return false;
                }

                errorCode.clear();
                if (std::filesystem::exists(destinationPath))
                {
                    std::filesystem::remove(destinationPath, errorCode);
                    if (errorCode)
                    {
                        SetError(errorMessage, "Failed to replace existing export asset: " + PathToUtf8String(destinationPath) + " (" + errorCode.message() + ")");
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
                             "Failed to copy asset to export: " + PathToUtf8String(sourcePath) + " -> " + PathToUtf8String(destinationPath) + " (" + errorCode.message() + ")");
                    return false;
                }
            }

            return true;
        }

        bool CopyRuntimeSidecarFiles(const std::filesystem::path &runtimeExecutablePath,
                                     const std::filesystem::path &destinationDirectory,
                                     std::string *errorMessage)
        {
            const auto sourceDirectory = runtimeExecutablePath.parent_path();
            std::error_code errorCode;
            for (std::filesystem::directory_iterator iterator(sourceDirectory, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
                 iterator != end;
                 iterator.increment(errorCode))
            {
                if (errorCode)
                {
                    SetError(errorMessage, "Failed while enumerating runtime dependencies in: " + PathToUtf8String(sourceDirectory));
                    return false;
                }

                std::error_code statusErrorCode;
                if (!iterator->is_regular_file(statusErrorCode))
                {
                    continue;
                }

                const auto sourcePath = iterator->path();
                if (NormalizeAbsolutePath(sourcePath) == NormalizeAbsolutePath(runtimeExecutablePath))
                {
                    continue;
                }

                const auto extension = sourcePath.extension().string();
#ifdef _WIN32
                if (extension != ".dll" && extension != ".DLL")
#else
                if (sourcePath.filename().string().find(".so") == std::string::npos)
#endif
                {
                    continue;
                }

                const auto destinationPath = destinationDirectory / sourcePath.filename();
                errorCode.clear();
                std::filesystem::copy_file(sourcePath,
                                           destinationPath,
                                           std::filesystem::copy_options::overwrite_existing,
                                           errorCode);
                if (errorCode)
                {
                    SetError(errorMessage, "Failed to copy runtime dependency: " + PathToUtf8String(sourcePath) + " (" + errorCode.message() + ")");
                    return false;
                }
            }

            return true;
        }

        std::filesystem::path GetEnvironmentPath(const char *name)
        {
            if (const char *value = std::getenv(name))
            {
                return value;
            }

            return {};
        }

        std::vector<std::filesystem::path> GetDotnetRootCandidates()
        {
            std::vector<std::filesystem::path> candidates;

            if (const auto dotnetRoot = GetEnvironmentPath("DOTNET_ROOT"); !dotnetRoot.empty())
            {
                candidates.emplace_back(dotnetRoot);
            }

#ifdef _WIN32
            if (const auto programFiles = GetEnvironmentPath("ProgramFiles"); !programFiles.empty())
            {
                candidates.emplace_back(programFiles / "dotnet");
            }
#else
            candidates.emplace_back("/usr/share/dotnet");
            candidates.emplace_back("/usr/lib/dotnet");
#endif

            return candidates;
        }

        bool IsDotnetRuntimeRoot(const std::filesystem::path &dotnetRoot)
        {
            const auto fxrDirectory = dotnetRoot / "host" / "fxr";
            const auto sharedRuntimeDirectory = dotnetRoot / "shared" / "Microsoft.NETCore.App";
            if (!std::filesystem::exists(fxrDirectory) || !std::filesystem::exists(sharedRuntimeDirectory))
            {
                return false;
            }

            std::error_code errorCode;
            for (std::filesystem::directory_iterator iterator(fxrDirectory, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
                 iterator != end;
                 iterator.increment(errorCode))
            {
                if (errorCode)
                {
                    errorCode.clear();
                    continue;
                }

                if (iterator->is_directory() &&
#ifdef _WIN32
                    std::filesystem::exists(iterator->path() / "hostfxr.dll")
#else
                    std::filesystem::exists(iterator->path() / "libhostfxr.so")
#endif
                )
                {
                    return true;
                }
            }
            return false;
        }

        std::filesystem::path FindDotnetRuntimeRoot()
        {
            for (const auto &candidate : GetDotnetRootCandidates())
            {
                if (IsDotnetRuntimeRoot(candidate))
                {
                    return NormalizeAbsolutePath(candidate);
                }
            }

            return {};
        }

        bool CopyBundledDotnetRuntime(const std::filesystem::path &destinationDirectory,
                                      std::string *errorMessage)
        {
            const auto dotnetRoot = FindDotnetRuntimeRoot();
            if (dotnetRoot.empty())
            {
                SetError(errorMessage, "Failed to locate a .NET runtime to bundle. Install the .NET runtime or set DOTNET_ROOT before exporting.");
                return false;
            }

            const auto bundledRuntimeDirectory = destinationDirectory / std::filesystem::path(kBundledDotnetRuntimeDirectory);
            const std::array<std::filesystem::path, 2> runtimeSubdirectories{
                std::filesystem::path("host") / "fxr",
                std::filesystem::path("shared") / "Microsoft.NETCore.App",
            };

            for (const auto &runtimeSubdirectory : runtimeSubdirectories)
            {
                const auto sourceDirectory = dotnetRoot / runtimeSubdirectory;
                if (!std::filesystem::exists(sourceDirectory))
                {
                    SetError(errorMessage, "Failed to locate .NET runtime directory for export: " + PathToUtf8String(sourceDirectory));
                    return false;
                }

                if (!CopyDirectoryRecursive(sourceDirectory, bundledRuntimeDirectory / runtimeSubdirectory, errorMessage))
                {
                    return false;
                }
            }

            return true;
        }

        template <typename Value>
        bool WritePackValue(std::ostream &output, Value value)
        {
            output.write(reinterpret_cast<const char *>(&value), sizeof(Value));
            return output.good();
        }

        template <typename Value>
        bool ReadPackValue(std::istream &input, Value &value)
        {
            input.read(reinterpret_cast<char *>(&value), sizeof(Value));
            return input.good();
        }

        bool TransferPackBytes(std::istream &input, std::ostream &output, std::uint64_t byteCount)
        {
            std::array<char, 64 * 1024> buffer{};
            while (byteCount > 0)
            {
                const auto chunkSize = static_cast<std::streamsize>((std::min)(byteCount, static_cast<std::uint64_t>(buffer.size())));
                input.read(buffer.data(), chunkSize);
                if (input.gcount() != chunkSize)
                {
                    return false;
                }
                for (std::streamsize index = 0; index < chunkSize; ++index)
                {
                    buffer[static_cast<std::size_t>(index)] = static_cast<char>(
                        static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(index)]) ^ kContentPackXorKey);
                }
                output.write(buffer.data(), chunkSize);
                if (!output.good())
                {
                    return false;
                }
                byteCount -= static_cast<std::uint64_t>(chunkSize);
            }
            return true;
        }

        bool CreateContentPack(const std::filesystem::path &sourceDirectory,
                               const std::filesystem::path &contentPackPath,
                               std::string *errorMessage)
        {
            std::vector<std::filesystem::path> files;
            std::error_code errorCode;
            for (std::filesystem::recursive_directory_iterator iterator(sourceDirectory, errorCode), end; iterator != end; iterator.increment(errorCode))
            {
                if (errorCode)
                {
                    SetError(errorMessage, "Failed to enumerate cooked content for packing.");
                    return false;
                }
                if (iterator->is_regular_file())
                {
                    files.push_back(iterator->path());
                }
            }
            std::sort(files.begin(), files.end());
            if (files.size() > (std::numeric_limits<std::uint32_t>::max)())
            {
                SetError(errorMessage, "The project contains too many files for the content pack.");
                return false;
            }

            std::ofstream output(contentPackPath, std::ios::binary | std::ios::trunc);
            output.write(kContentPackMagic.data(), static_cast<std::streamsize>(kContentPackMagic.size()));
            if (!WritePackValue(output, kContentPackVersion) || !WritePackValue(output, static_cast<std::uint32_t>(files.size())))
            {
                SetError(errorMessage, "Failed to create the content pack header.");
                return false;
            }

            for (const auto &file : files)
            {
                const auto relativePath = PathToGenericUtf8String(std::filesystem::relative(file, sourceDirectory));
                const auto fileSize = std::filesystem::file_size(file, errorCode);
                if (errorCode || relativePath.empty() || relativePath.size() > (std::numeric_limits<std::uint32_t>::max)())
                {
                    SetError(errorMessage, "Failed to index cooked content: " + PathToUtf8String(file));
                    return false;
                }
                const auto pathLength = static_cast<std::uint32_t>(relativePath.size());
                if (!WritePackValue(output, pathLength) || !WritePackValue(output, static_cast<std::uint64_t>(fileSize)))
                {
                    SetError(errorMessage, "Failed to write the content pack index.");
                    return false;
                }
                std::string encodedPath = relativePath;
                for (char &character : encodedPath)
                {
                    character = static_cast<char>(static_cast<std::uint8_t>(character) ^ kContentPackXorKey);
                }
                output.write(encodedPath.data(), static_cast<std::streamsize>(encodedPath.size()));
                std::ifstream input(file, std::ios::binary);
                if (!input.is_open() || !TransferPackBytes(input, output, fileSize))
                {
                    SetError(errorMessage, "Failed to pack cooked content: " + PathToUtf8String(file));
                    return false;
                }
            }
            return output.good();
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
            SetError(errorMessage, "Failed to create project directory: " + PathToUtf8String(normalizedManifestPath.parent_path()));
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
            SetError(errorMessage, "Failed to create project asset directory: " + PathToUtf8String(project->GetAssetDirectoryPath()));
            return nullptr;
        }

        std::filesystem::create_directories(project->GetAssetDirectoryPath() / "Scenes", errorCode);
        if (errorCode)
        {
            SetError(errorMessage, "Failed to create default Scenes directory.");
            return nullptr;
        }

        std::filesystem::create_directories(project->GetAssetDirectoryPath() / "Meshes", errorCode);
        if (errorCode)
        {
            SetError(errorMessage, "Failed to create default Meshes directory.");
            return nullptr;
        }

        std::filesystem::create_directories(project->GetAssetDirectoryPath() / "Materials", errorCode);
        if (errorCode)
        {
            SetError(errorMessage, "Failed to create default Materials directory.");
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

            if (tokens[0] == "SCRIPT_ASSEMBLY" && tokens.size() >= 2)
            {
                manifest.scriptAssembly = tokens[1];
                // Older Linux editor builds passed project:// references through
                // std::filesystem::path. POSIX path normalization collapsed the
                // scheme and could also prepend the editor working directory,
                // producing values such as /path/to/PlutoGE/project:/Managed/...
                if (!IsProjectAssetReference(manifest.scriptAssembly))
                {
                    constexpr std::string_view collapsedScheme = "project:/";
                    const auto collapsedPosition = manifest.scriptAssembly.find(collapsedScheme);
                    if (collapsedPosition != std::string::npos)
                    {
                        auto relativeReference = manifest.scriptAssembly.substr(collapsedPosition + collapsedScheme.size());
                        while (!relativeReference.empty() && (relativeReference.front() == '/' || relativeReference.front() == '\\'))
                        {
                            relativeReference.erase(relativeReference.begin());
                        }
                        manifest.scriptAssembly = std::string(kProjectAssetScheme) + relativeReference;
                    }
                }
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

            if (tokens[0] == "GRAPHICS_API" && tokens.size() >= 2)
            {
                manifest.graphicsApi = tokens[1] == "Vulkan"
                                           ? render::rhi::GraphicsApi::Vulkan
                                           : render::rhi::GraphicsApi::OpenGL;
                continue;
            }

            if (tokens[0] == "RUNTIME_UPSCALER" && tokens.size() >= 2)
            {
                manifest.runtimeUpscaler = tokens[1] == "Spatial" ? RuntimeUpscalerMode::Spatial
                                           : tokens[1] == "DLSS" ? RuntimeUpscalerMode::Dlss
                                           : tokens[1] == "FSR2" ? RuntimeUpscalerMode::Fsr2
                                                                  : RuntimeUpscalerMode::None;
                continue;
            }

            if ((tokens[0] == "RUNTIME_UPSCALER_QUALITY" || tokens[0] == "RUNTIME_DLSS_QUALITY") &&
                tokens.size() >= 2)
            {
                manifest.runtimeUpscalerQuality = tokens[1] == "Performance" ? render::rhi::UpscalerQuality::Performance
                                              : tokens[1] == "Balanced" ? render::rhi::UpscalerQuality::Balanced
                                              : tokens[1] == "UltraPerformance" ? render::rhi::UpscalerQuality::UltraPerformance
                                              : tokens[1] == "DLAA" ? render::rhi::UpscalerQuality::Dlaa
                                                                    : render::rhi::UpscalerQuality::Quality;
                continue;
            }

            if (tokens[0] == "RUNTIME_RENDER_SCALE" && tokens.size() >= 2)
            {
                ParseFloat(tokens[1], manifest.runtimeRenderScale);
                manifest.runtimeRenderScale = std::clamp(manifest.runtimeRenderScale, 0.5f, 1.0f);
                continue;
            }

            if (tokens[0] == "RUNTIME_UPSCALE_SHARPNESS" && tokens.size() >= 2)
            {
                ParseFloat(tokens[1], manifest.runtimeUpscaleSharpness);
                manifest.runtimeUpscaleSharpness = std::clamp(manifest.runtimeUpscaleSharpness, 0.0f, 1.0f);
                continue;
            }

            if (tokens[0] == "EDITOR_FONT_SIZE" && tokens.size() >= 2)
            {
                ParseFloat(tokens[1], manifest.editorFontSize);
                manifest.editorFontSize = std::clamp(manifest.editorFontSize, 10.0f, 24.0f);
                continue;
            }

            if (tokens[0] == "EDITOR_FONT" && tokens.size() >= 2)
            {
                manifest.editorFont = tokens[1];
                continue;
            }

            if (tokens[0] == "EDITOR_CAMERA_POSITION" && tokens.size() >= 4)
            {
                ParseFloat(tokens[1], manifest.editorCamera.positionX);
                ParseFloat(tokens[2], manifest.editorCamera.positionY);
                ParseFloat(tokens[3], manifest.editorCamera.positionZ);
                continue;
            }

            if (tokens[0] == "EDITOR_CAMERA_SPEED" && tokens.size() >= 2)
            {
                ParseFloat(tokens[1], manifest.editorCamera.moveSpeed);
                manifest.editorCamera.moveSpeed = std::clamp(manifest.editorCamera.moveSpeed, 0.1f, 1000.0f);
                continue;
            }

            if (tokens[0] == "EDITOR_CAMERA_ROTATION" && tokens.size() >= 3)
            {
                ParseFloat(tokens[1], manifest.editorCamera.yawDegrees);
                ParseFloat(tokens[2], manifest.editorCamera.pitchDegrees);
                continue;
            }

            if (tokens[0] == "EDITOR_CAMERA_LENS" && tokens.size() >= 4)
            {
                ParseFloat(tokens[1], manifest.editorCamera.fovY);
                ParseFloat(tokens[2], manifest.editorCamera.nearPlane);
                ParseFloat(tokens[3], manifest.editorCamera.farPlane);
                continue;
            }

            if (tokens[0] == "EDITOR_CAMERA_POST_PROCESS_PRESET" && tokens.size() >= 2)
            {
                manifest.editorCameraPostProcessPreset = tokens[1];
                continue;
            }

            if (tokens[0] == "EDITOR_CAMERA_EFFECT" && tokens.size() >= 3)
            {
                ProjectPostProcessEffect effect;
                effect.typeName = tokens[1];
                effect.enabled = tokens[2] == "1" || tokens[2] == "true";
                manifest.editorCameraPostProcessEffects.push_back(std::move(effect));
                continue;
            }

            if (tokens[0] == "EDITOR_CAMERA_EFFECT_PARAM" && tokens.size() >= 5)
            {
                int effectIndex = -1;
                int parameterType = 0;
                if (!ParseInteger(tokens[1], effectIndex) ||
                    effectIndex < 0 ||
                    static_cast<std::size_t>(effectIndex) >= manifest.editorCameraPostProcessEffects.size())
                {
                    continue;
                }

                ParseInteger(tokens[3], parameterType);

                ProjectPostProcessParameter parameter;
                parameter.name = tokens[2];
                parameter.type = parameterType;
                parameter.value = tokens[4];
                manifest.editorCameraPostProcessEffects[static_cast<std::size_t>(effectIndex)].parameters.push_back(std::move(parameter));
                continue;
            }

            if (tokens[0] == "ASSET" && tokens.size() >= 3)
            {
                ProjectAssetEntry entry;
                entry.reference = tokens[1];
                ParseUnsignedInteger(tokens[2], entry.size);
                entry.type = tokens.size() >= 4 ? Project::ParseAssetTypeName(tokens[3])
                                                : Project::GetAssetTypeForReference(entry.reference);
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
            std::string(kBuiltinSphereMeshReference),
            std::string(kBuiltinPlaneMeshReference),
            std::string(kBuiltinCylinderMeshReference),
            std::string(kBuiltinQuadMeshReference),
            std::string(kBuiltinDefaultMaterialReference),
            std::string(kBuiltinDefaultShadedMaterialReference),
            std::string(kBuiltinDefaultShaderGraphReference),
            std::string(kBuiltinDefaultUnlitShaderGraphReference),
        };
    }

    namespace
    {
        bool EndsWithInsensitive(std::string_view text, std::string_view suffix)
        {
            if (text.size() < suffix.size())
            {
                return false;
            }

            const auto offset = text.size() - suffix.size();
            for (std::size_t index = 0; index < suffix.size(); ++index)
            {
                const auto left = static_cast<unsigned char>(text[offset + index]);
                const auto right = static_cast<unsigned char>(suffix[index]);
                if (std::tolower(left) != std::tolower(right))
                {
                    return false;
                }
            }

            return true;
        }
    }

    ProjectAssetType Project::GetAssetTypeForReference(std::string_view reference)
    {
        if (reference.rfind("engine://builtin/mesh/", 0) == 0)
        {
            return ProjectAssetType::Mesh;
        }
        if (reference.rfind("engine://builtin/material/", 0) == 0)
        {
            return ProjectAssetType::Material;
        }
        if (reference.rfind("engine://builtin/shadergraph/", 0) == 0)
        {
            return ProjectAssetType::ShaderGraph;
        }
        if (EndsWithInsensitive(reference, ".plutoscene"))
        {
            return ProjectAssetType::Scene;
        }
        if (EndsWithInsensitive(reference, ".plutoprefab"))
        {
            return ProjectAssetType::Prefab;
        }
        if (EndsWithInsensitive(reference, ".plutoscriptable"))
        {
            return ProjectAssetType::ScriptableObject;
        }
        if (EndsWithInsensitive(reference, ".cs"))
        {
            return ProjectAssetType::Script;
        }
        if (EndsWithInsensitive(reference, ".dll"))
        {
            return ProjectAssetType::Assembly;
        }
        if (EndsWithInsensitive(reference, ".rml"))
            return ProjectAssetType::RmlDocument;
        if (EndsWithInsensitive(reference, ".plutomaterial") || EndsWithInsensitive(reference, ".mat"))
        {
            return ProjectAssetType::Material;
        }
        if (EndsWithInsensitive(reference, ".plutoshadergraph"))
        {
            return ProjectAssetType::ShaderGraph;
        }
        if (EndsWithInsensitive(reference, ".plutoanimgraph"))
        {
            return ProjectAssetType::AnimationGraph;
        }
        if (EndsWithInsensitive(reference, ".plutoparticles"))
        {
            return ProjectAssetType::ParticleSystem;
        }
        if (EndsWithInsensitive(reference, ".plutopostprocess"))
        {
            return ProjectAssetType::PostProcessPreset;
        }
        if (EndsWithInsensitive(reference, ".plutoinput"))
            return ProjectAssetType::InputMapping;
        if (EndsWithInsensitive(reference, ".plutomesh") || EndsWithInsensitive(reference, ".obj"))
        {
            return ProjectAssetType::Mesh;
        }
        if (EndsWithInsensitive(reference, ".plutoanim"))
        {
            return ProjectAssetType::Animation;
        }
        if (EndsWithInsensitive(reference, ".plutoclip"))
        {
            return ProjectAssetType::AnimationClip;
        }
        if (EndsWithInsensitive(reference, ".gltf") || EndsWithInsensitive(reference, ".glb") || EndsWithInsensitive(reference, ".fbx"))
        {
            return ProjectAssetType::Model;
        }
        if (EndsWithInsensitive(reference, ".png") || EndsWithInsensitive(reference, ".jpg") ||
            EndsWithInsensitive(reference, ".jpeg") || EndsWithInsensitive(reference, ".tga") ||
            EndsWithInsensitive(reference, ".hdr") || EndsWithInsensitive(reference, ".exr"))
        {
            return ProjectAssetType::Texture;
        }
        if (EndsWithInsensitive(reference, ".wav"))
        {
            return ProjectAssetType::Audio;
        }

        return ProjectAssetType::Unknown;
    }

    std::string_view Project::GetAssetTypeName(ProjectAssetType type)
    {
        switch (type)
        {
        case ProjectAssetType::Scene:
            return "Scene";
        case ProjectAssetType::Prefab:
            return "Prefab";
        case ProjectAssetType::Script:
            return "Script";
        case ProjectAssetType::Mesh:
            return "Mesh";
        case ProjectAssetType::Animation:
            return "Animation";
        case ProjectAssetType::AnimationClip:
            return "Animation Clip";
        case ProjectAssetType::Model:
            return "Model";
        case ProjectAssetType::Material:
            return "Material";
        case ProjectAssetType::ShaderGraph:
            return "Shader Graph";
        case ProjectAssetType::AnimationGraph:
            return "Animation Graph";
        case ProjectAssetType::ParticleSystem:
            return "Particle System";
        case ProjectAssetType::PostProcessPreset:
            return "Post Process Preset";
        case ProjectAssetType::Audio:
            return "Audio";
        case ProjectAssetType::Texture:
            return "Texture";
        case ProjectAssetType::Assembly:
            return "Assembly";
        case ProjectAssetType::ScriptableObject:
            return "Scriptable Object";
        case ProjectAssetType::RmlDocument:
            return "RML Document";
        case ProjectAssetType::InputMapping:
            return "Input Mapping";
        case ProjectAssetType::Unknown:
        case ProjectAssetType::Count:
        default:
            return "Unknown";
        }
    }

    ProjectAssetType Project::ParseAssetTypeName(std::string_view typeName)
    {
        if (typeName == "Scene")
            return ProjectAssetType::Scene;
        if (typeName == "Prefab")
            return ProjectAssetType::Prefab;
        if (typeName == "Script")
            return ProjectAssetType::Script;
        if (typeName == "Mesh")
            return ProjectAssetType::Mesh;
        if (typeName == "Animation")
            return ProjectAssetType::Animation;
        if (typeName == "Animation Clip" || typeName == "AnimationClip")
            return ProjectAssetType::AnimationClip;
        if (typeName == "Model")
            return ProjectAssetType::Model;
        if (typeName == "Material")
            return ProjectAssetType::Material;
        if (typeName == "Shader Graph" || typeName == "ShaderGraph")
            return ProjectAssetType::ShaderGraph;
        if (typeName == "Animation Graph" || typeName == "AnimationGraph")
            return ProjectAssetType::AnimationGraph;
        if (typeName == "Particle System" || typeName == "ParticleSystem")
            return ProjectAssetType::ParticleSystem;
        if (typeName == "Post Process Preset" || typeName == "PostProcessPreset")
            return ProjectAssetType::PostProcessPreset;
        if (typeName == "Audio")
            return ProjectAssetType::Audio;
        if (typeName == "Texture")
            return ProjectAssetType::Texture;
        if (typeName == "Assembly")
            return ProjectAssetType::Assembly;
        if (typeName == "Scriptable Object" || typeName == "ScriptableObject")
            return ProjectAssetType::ScriptableObject;
        if (typeName == "RML Document" || typeName == "RmlDocument")
            return ProjectAssetType::RmlDocument;
        if (typeName == "Input Mapping" || typeName == "InputMapping")
            return ProjectAssetType::InputMapping;
        return ProjectAssetType::Unknown;
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
        output << "SCRIPT_ASSEMBLY\t" << EscapeText(m_manifest.scriptAssembly) << '\n';
        output << "WINDOW_TITLE\t" << EscapeText(m_manifest.windowTitle) << '\n';
        output << "WINDOW_SIZE\t" << m_manifest.windowWidth << '\t' << m_manifest.windowHeight << '\n';
        output << "VSYNC\t" << (m_manifest.vSyncEnabled ? 1 : 0) << '\n';
        output << "GRAPHICS_API\t"
               << (m_manifest.graphicsApi == render::rhi::GraphicsApi::Vulkan ? "Vulkan" : "OpenGL") << '\n';
        output << "RUNTIME_UPSCALER\t"
               << (m_manifest.runtimeUpscaler == RuntimeUpscalerMode::Spatial ? "Spatial"
                   : m_manifest.runtimeUpscaler == RuntimeUpscalerMode::Dlss ? "DLSS"
                   : m_manifest.runtimeUpscaler == RuntimeUpscalerMode::Fsr2 ? "FSR2" : "None") << '\n';
        const auto upscalerQuality = m_manifest.runtimeUpscalerQuality == render::rhi::UpscalerQuality::Performance ? "Performance"
                                     : m_manifest.runtimeUpscalerQuality == render::rhi::UpscalerQuality::Balanced ? "Balanced"
                                     : m_manifest.runtimeUpscalerQuality == render::rhi::UpscalerQuality::UltraPerformance ? "UltraPerformance"
                                     : m_manifest.runtimeUpscalerQuality == render::rhi::UpscalerQuality::Dlaa ? "DLAA" : "Quality";
        output << "RUNTIME_UPSCALER_QUALITY\t" << upscalerQuality << '\n';
        output << "RUNTIME_RENDER_SCALE\t" << std::clamp(m_manifest.runtimeRenderScale, 0.5f, 1.0f) << '\n';
        output << "RUNTIME_UPSCALE_SHARPNESS\t" << std::clamp(m_manifest.runtimeUpscaleSharpness, 0.0f, 1.0f) << '\n';
        output << "EDITOR_FONT_SIZE\t" << std::clamp(m_manifest.editorFontSize, 10.0f, 24.0f) << '\n';
        output << "EDITOR_FONT\t" << EscapeText(m_manifest.editorFont) << '\n';
        output << "EDITOR_CAMERA_POSITION\t"
               << m_manifest.editorCamera.positionX << '\t'
               << m_manifest.editorCamera.positionY << '\t'
               << m_manifest.editorCamera.positionZ << '\n';
        output << "EDITOR_CAMERA_SPEED\t" << std::clamp(m_manifest.editorCamera.moveSpeed, 0.1f, 1000.0f) << '\n';
        output << "EDITOR_CAMERA_ROTATION\t"
               << m_manifest.editorCamera.yawDegrees << '\t'
               << m_manifest.editorCamera.pitchDegrees << '\n';
        output << "EDITOR_CAMERA_LENS\t"
               << m_manifest.editorCamera.fovY << '\t'
               << m_manifest.editorCamera.nearPlane << '\t'
               << m_manifest.editorCamera.farPlane << '\n';
        output << "EDITOR_CAMERA_POST_PROCESS_PRESET\t" << EscapeText(m_manifest.editorCameraPostProcessPreset) << '\n';
        for (std::size_t effectIndex = 0; effectIndex < m_manifest.editorCameraPostProcessEffects.size(); ++effectIndex)
        {
            const auto &effect = m_manifest.editorCameraPostProcessEffects[effectIndex];
            output << "EDITOR_CAMERA_EFFECT\t"
                   << EscapeText(effect.typeName) << '\t'
                   << (effect.enabled ? 1 : 0) << '\n';

            for (const auto &parameter : effect.parameters)
            {
                output << "EDITOR_CAMERA_EFFECT_PARAM\t"
                       << effectIndex << '\t'
                       << EscapeText(parameter.name) << '\t'
                       << parameter.type << '\t'
                       << EscapeText(parameter.value) << '\n';
            }
        }
        for (const auto &assetEntry : m_manifest.assetEntries)
        {
            output << "ASSET\t"
                   << EscapeText(assetEntry.reference) << '\t'
                   << assetEntry.size << '\t'
                   << GetAssetTypeName(assetEntry.type) << '\n';
        }

        return true;
    }

    void Project::RefreshAssetRegistry()
    {
        m_manifest.assetEntries.clear();

        for (const auto &builtinReference : GetBuiltinAssetReferences())
        {
            m_manifest.assetEntries.push_back(ProjectAssetEntry{
                .reference = builtinReference,
                .size = 0,
                .type = GetAssetTypeForReference(builtinReference),
            });
        }

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

            if (iterator->path().extension() == ".plutometa" || iterator->path().extension() == ".plutomodel")
            {
                continue;
            }

            ProjectAssetEntry entry;
            entry.reference = MakeAssetReference(iterator->path());
            entry.size = iterator->file_size(errorCode);
            entry.type = GetAssetTypeForReference(entry.reference);
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
        const auto genericPath = PathToGenericUtf8String(filePath);
        if (IsProjectAssetReference(genericPath) || IsEngineAssetReference(genericPath))
        {
            return genericPath;
        }

        std::filesystem::path relativePath;
        if (TryMakeRelativePath(filePath, GetAssetDirectoryPath(), relativePath))
        {
            return std::string(kProjectAssetScheme) + PathToGenericUtf8String(relativePath);
        }

        return PathToUtf8String(NormalizeAbsolutePath(filePath));
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

    std::string Project::FindSceneAssetReference(std::string_view nameOrReference) const
    {
        if (nameOrReference.empty())
            return {};

        const std::string requested(nameOrReference);
        if (GetAssetTypeForReference(requested) == ProjectAssetType::Scene)
        {
            if (IsProjectAssetReference(requested))
                return std::filesystem::exists(ResolveAssetReference(requested)) ? requested : std::string{};
            const std::filesystem::path requestedPath(requested);
            if (requestedPath.is_absolute())
                return std::filesystem::exists(requestedPath) ? requestedPath.lexically_normal().string() : std::string{};
        }

        std::filesystem::path requestedPath(requested);
        std::string requestedFileName = requestedPath.filename().string();
        std::string requestedStem = requestedPath.stem().string();
        if (requestedPath.extension().empty())
            requestedFileName += ".plutoscene";

        for (const auto &entry : m_manifest.assetEntries)
        {
            if (entry.type != ProjectAssetType::Scene)
                continue;
            std::string relative = entry.reference;
            if (IsProjectAssetReference(relative))
                relative.erase(0, kProjectAssetScheme.size());
            const std::filesystem::path entryPath(relative);
            if (relative == requested || entryPath.generic_string() == requested ||
                entryPath.filename().string() == requestedFileName || entryPath.stem().string() == requestedStem)
                return entry.reference;
        }
        return {};
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
        auto manifestFileName = executablePath.stem();
        manifestFileName += ".plutoproject";
        return executablePath.parent_path() / manifestFileName;
    }

    std::filesystem::path GetRuntimeContentPackPathForExecutable(const std::filesystem::path &executablePath)
    {
        auto packFileName = executablePath.stem();
        packFileName += ".plutopack";
        return executablePath.parent_path() / packFileName;
    }

    bool ExtractStandaloneProjectContent(const std::filesystem::path &contentPackPath,
                                         const std::filesystem::path &destinationDirectory,
                                         std::string *errorMessage)
    {
        std::ifstream input(contentPackPath, std::ios::binary);
        std::array<char, 8> magic{};
        input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        std::uint32_t version = 0;
        std::uint32_t fileCount = 0;
        if (!input.good() || std::string_view(magic.data(), magic.size()) != kContentPackMagic ||
            !ReadPackValue(input, version) || version != kContentPackVersion || !ReadPackValue(input, fileCount))
        {
            SetError(errorMessage, "The game content pack is invalid or unsupported.");
            return false;
        }

        const auto normalizedDestination = NormalizeAbsolutePath(destinationDirectory);
        for (std::uint32_t fileIndex = 0; fileIndex < fileCount; ++fileIndex)
        {
            std::uint32_t pathLength = 0;
            std::uint64_t fileSize = 0;
            if (!ReadPackValue(input, pathLength) || !ReadPackValue(input, fileSize) || pathLength == 0 || pathLength > 1024 * 1024)
            {
                SetError(errorMessage, "The game content pack index is damaged.");
                return false;
            }
            std::string relativePath(pathLength, '\0');
            input.read(relativePath.data(), static_cast<std::streamsize>(pathLength));
            for (char &character : relativePath)
            {
                character = static_cast<char>(static_cast<std::uint8_t>(character) ^ kContentPackXorKey);
            }
            const std::filesystem::path relative(relativePath);
            const auto normalizedRelativePath = relative.lexically_normal().generic_string();
            if (!input.good() || relative.is_absolute() || normalizedRelativePath == ".." || normalizedRelativePath.rfind("../", 0) == 0)
            {
                SetError(errorMessage, "The game content pack contains an unsafe path.");
                return false;
            }
            const auto outputPath = (normalizedDestination / relative).lexically_normal();
            std::error_code errorCode;
            std::filesystem::create_directories(outputPath.parent_path(), errorCode);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            if (errorCode || !output.is_open() || !TransferPackBytes(input, output, fileSize))
            {
                SetError(errorMessage, "Failed to unpack game content: " + PathToUtf8String(outputPath));
                return false;
            }
        }
        return true;
    }

    std::filesystem::path FindRuntimeExecutable(const std::filesystem::path &searchRoot)
    {
        const std::string runtimeFileName = "PlutoGERuntime" + GetExecutableExtension();
        auto candidateRoot = NormalizeAbsolutePath(searchRoot);
        for (int depth = 0; depth < kRuntimeSearchAncestorLimit && !candidateRoot.empty(); ++depth)
        {
            if (const auto bestMatch = FindNewestRuntimeExecutableInTree(candidateRoot, runtimeFileName); !bestMatch.empty())
            {
                return bestMatch;
            }

            const auto parentRoot = candidateRoot.parent_path();
            if (parentRoot.empty() || parentRoot == candidateRoot)
            {
                break;
            }

            candidateRoot = parentRoot;
        }

        return {};
    }

    bool ExportStandaloneProject(const Project &project,
                                 const std::filesystem::path &destinationExecutablePath,
                                 const std::filesystem::path &runtimeExecutablePath,
                                 std::string *errorMessage)
    {
        const auto normalizedRuntimeExecutablePath = NormalizeAbsolutePath(runtimeExecutablePath);
        if (!std::filesystem::exists(normalizedRuntimeExecutablePath))
        {
            SetError(errorMessage, "Runtime executable was not found: " + PathToUtf8String(normalizedRuntimeExecutablePath));
            return false;
        }

        const auto &manifest = project.GetManifest();
        if (!manifest.scriptAssembly.empty() && !Project::IsEngineAssetReference(manifest.scriptAssembly))
        {
            const auto scriptAssemblyPath = project.ResolveAssetReference(manifest.scriptAssembly);
            if (scriptAssemblyPath.empty() || !std::filesystem::exists(scriptAssemblyPath))
            {
                SetError(errorMessage,
                         "Project script assembly was not found. Build project scripts before exporting: " +
                             (scriptAssemblyPath.empty() ? manifest.scriptAssembly : PathToUtf8String(scriptAssemblyPath)));
                return false;
            }
        }

        const auto normalizedDestinationExecutablePath = NormalizeAbsolutePath(destinationExecutablePath);
        const std::filesystem::path assetDirectoryPath(manifest.assetDirectory);
        const auto normalizedAssetDirectory = assetDirectoryPath.lexically_normal().generic_string();
        if (assetDirectoryPath.empty() || assetDirectoryPath.is_absolute() || normalizedAssetDirectory == "." ||
            normalizedAssetDirectory == ".." || normalizedAssetDirectory.rfind("../", 0) == 0)
        {
            SetError(errorMessage, "The project asset directory must be a safe relative directory before it can be exported.");
            return false;
        }
        std::error_code errorCode;
        std::filesystem::create_directories(normalizedDestinationExecutablePath.parent_path(), errorCode);
        if (errorCode)
        {
            SetError(errorMessage, "Failed to create export directory: " + PathToUtf8String(normalizedDestinationExecutablePath.parent_path()));
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

        if (!CopyRuntimeSidecarFiles(normalizedRuntimeExecutablePath, normalizedDestinationExecutablePath.parent_path(), errorMessage))
        {
            return false;
        }

        if (!manifest.scriptAssembly.empty() &&
            !CopyBundledDotnetRuntime(normalizedDestinationExecutablePath.parent_path(), errorMessage))
        {
            return false;
        }

        const auto stagingDirectory = normalizedDestinationExecutablePath.parent_path() / (".pluto-pack-" + normalizedDestinationExecutablePath.stem().string());
        std::filesystem::remove_all(stagingDirectory, errorCode);
        errorCode.clear();
        std::filesystem::create_directories(stagingDirectory, errorCode);
        if (errorCode)
        {
            SetError(errorMessage, "Failed to create the content-pack staging directory.");
            return false;
        }
        const auto exportedAssetDirectory = stagingDirectory / project.GetManifest().assetDirectory;
        Project cookProject(project.GetManifestPath(), project.GetManifest());
        if (!CookProjectContent(cookProject, exportedAssetDirectory, {}, errorMessage))
        {
            return false;
        }

        Project exportedProject(stagingDirectory / GetRuntimeManifestPathForExecutable(normalizedDestinationExecutablePath).filename(), project.GetManifest());
        exportedProject.RefreshAssetRegistry();
        if (!exportedProject.Save(errorMessage))
        {
            return false;
        }

        const auto contentPackPath = GetRuntimeContentPackPathForExecutable(normalizedDestinationExecutablePath);
        if (!CreateContentPack(stagingDirectory, contentPackPath, errorMessage))
        {
            std::filesystem::remove_all(stagingDirectory, errorCode);
            return false;
        }
        std::filesystem::remove_all(stagingDirectory, errorCode);

        // Remove files produced by older loose-content exports only after the new pack is complete.
        std::filesystem::remove_all(normalizedDestinationExecutablePath.parent_path() / project.GetManifest().assetDirectory, errorCode);
        std::filesystem::remove(GetRuntimeManifestPathForExecutable(normalizedDestinationExecutablePath), errorCode);

        return true;
    }
}
