#include "PlutoGE/ui/ProjectScaffold.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <system_error>

namespace PlutoGE::ui
{
    namespace
    {
        // Visual Studio's project type for SDK-style C# projects.
        constexpr std::string_view kCSharpProjectTypeGuid = "9A19103F-16F7-4668-BE54-9A1E7A4F7556";
        // Solutions are a Windows-native format; Visual Studio writes them with CRLF.
        constexpr std::string_view kSolutionNewLine = "\r\n";
        constexpr std::array<std::string_view, 2> kSolutionConfigurations{"Debug|Any CPU", "Release|Any CPU"};

        std::uint64_t Fnv1a64(std::string_view text, std::uint64_t hash)
        {
            for (const unsigned char c : text)
            {
                hash ^= c;
                hash *= 0x100000001b3ULL;
            }
            return hash;
        }

        // A stable, name-based (version 5 layout) GUID such as "1A2B3C4D-...", so regenerating a solution
        // never changes its text.
        std::string MakeStableProjectGuid(std::string_view projectName)
        {
            const std::uint64_t high = Fnv1a64(projectName, 0xcbf29ce484222325ULL);
            const std::uint64_t low = Fnv1a64(projectName, high ^ 0x9e3779b97f4a7c15ULL);
            std::array<std::uint8_t, 16> bytes{};
            for (int i = 0; i < 8; ++i)
            {
                bytes[i] = static_cast<std::uint8_t>(high >> (56 - 8 * i));
                bytes[8 + i] = static_cast<std::uint8_t>(low >> (56 - 8 * i));
            }
            bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x50);
            bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);

            constexpr std::string_view kHex = "0123456789ABCDEF";
            std::string guid;
            guid.reserve(36);
            for (std::size_t i = 0; i < bytes.size(); ++i)
            {
                if (i == 4 || i == 6 || i == 8 || i == 10)
                    guid += '-';
                guid += kHex[bytes[i] >> 4];
                guid += kHex[bytes[i] & 0x0F];
            }
            return guid;
        }

        std::string WithSeparator(std::string_view path, char separator)
        {
            std::string result(path);
            std::replace_if(result.begin(), result.end(), [](char c) { return c == '/' || c == '\\'; }, separator);
            return result;
        }

        std::string_view FileNameOf(std::string_view genericPath)
        {
            const auto slash = genericPath.find_last_of('/');
            return slash == std::string_view::npos ? genericPath : genericPath.substr(slash + 1);
        }

        std::vector<SolutionProject> Normalized(std::span<const SolutionProject> projects)
        {
            std::vector<SolutionProject> result;
            result.reserve(projects.size());
            for (const auto &project : projects)
                result.push_back({project.name, WithSeparator(project.path, '/')});
            return result;
        }

        // Quoted values on a solution line, in order: Project("{type}") = "name", "path", "{guid}".
        std::vector<std::string_view> QuotedValues(std::string_view line)
        {
            std::vector<std::string_view> values;
            for (std::size_t open = line.find('"'); open != std::string_view::npos; open = line.find('"', open))
            {
                const auto close = line.find('"', open + 1);
                if (close == std::string_view::npos)
                    break;
                values.push_back(line.substr(open + 1, close - open - 1));
                open = close + 1;
            }
            return values;
        }

        bool WriteTextFile(const std::filesystem::path &path, std::string_view text, std::string *errorMessage)
        {
            std::ofstream output(path, std::ios::out | std::ios::trunc | std::ios::binary);
            output << text;
            if (output)
                return true;
            if (errorMessage)
                *errorMessage = "Could not write " + path.string();
            return false;
        }

        bool WriteIfMissing(const std::filesystem::path &path, const std::string &text, std::string *errorMessage)
        {
            std::error_code error;
            if (std::filesystem::exists(path, error))
                return true;
            std::filesystem::create_directories(path.parent_path(), error);
            return WriteTextFile(path, text, errorMessage);
        }
    }

    std::filesystem::path GetScriptEnginePropsPath(const std::filesystem::path &scriptProjectPath)
    {
        auto path = scriptProjectPath;
        path.replace_extension(".engine.props");
        return path;
    }

    std::filesystem::path GetScriptSolutionPath(const std::filesystem::path &scriptProjectPath)
    {
        // "Demo.Scripts.csproj" -> "Demo.Scripts" -> "Demo.sln"
        return scriptProjectPath.parent_path() / std::filesystem::path(scriptProjectPath.stem()).replace_extension(".sln");
    }

    std::string BuildScriptSolutionText(std::span<const SolutionProject> projects)
    {
        const auto nl = kSolutionNewLine;
        std::vector<std::string> guids;
        guids.reserve(projects.size());
        for (const auto &project : projects)
            guids.push_back(MakeStableProjectGuid(project.name));

        std::string text;
        text += nl;
        text += "Microsoft Visual Studio Solution File, Format Version 12.00";
        text += nl;
        text += "# Visual Studio Version 17";
        text += nl;
        text += "VisualStudioVersion = 17.0.31903.59";
        text += nl;
        text += "MinimumVisualStudioVersion = 10.0.40219.1";
        text += nl;
        for (std::size_t i = 0; i < projects.size(); ++i)
        {
            text += "Project(\"{" + std::string(kCSharpProjectTypeGuid) + "}\") = \"" + projects[i].name + "\", \"" +
                    WithSeparator(projects[i].path, '\\') + "\", \"{" + guids[i] + "}\"";
            text += nl;
            text += "EndProject";
            text += nl;
        }

        text += "Global";
        text += nl;
        text += "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution";
        text += nl;
        for (const auto configuration : kSolutionConfigurations)
        {
            text += "\t\t" + std::string(configuration) + " = " + std::string(configuration);
            text += nl;
        }
        text += "\tEndGlobalSection";
        text += nl;
        text += "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution";
        text += nl;
        for (const auto &guid : guids)
        {
            for (const auto configuration : kSolutionConfigurations)
            {
                for (const std::string_view kind : {".ActiveCfg", ".Build.0"})
                {
                    text += "\t\t{" + guid + "}." + std::string(configuration) + std::string(kind) + " = " + std::string(configuration);
                    text += nl;
                }
            }
        }
        text += "\tEndGlobalSection";
        text += nl;
        text += "\tGlobalSection(SolutionProperties) = preSolution";
        text += nl;
        text += "\t\tHideSolutionNode = FALSE";
        text += nl;
        text += "\tEndGlobalSection";
        text += nl;
        text += "EndGlobal";
        text += nl;
        return text;
    }

    std::vector<SolutionProject> ReadSolutionProjects(std::string_view solutionText)
    {
        std::vector<SolutionProject> projects;
        std::size_t lineStart = 0;
        while (lineStart < solutionText.size())
        {
            auto lineEnd = solutionText.find('\n', lineStart);
            if (lineEnd == std::string_view::npos)
                lineEnd = solutionText.size();
            const auto line = solutionText.substr(lineStart, lineEnd - lineStart);
            lineStart = lineEnd + 1;

            if (!line.starts_with("Project("))
                continue;
            const auto values = QuotedValues(line);
            if (values.size() >= 3)
                projects.push_back({std::string(values[1]), WithSeparator(values[2], '/')});
        }
        return projects;
    }

    bool UpdateScriptSolution(const std::filesystem::path &solutionPath,
                              std::span<const SolutionProject> projects,
                              std::span<const std::string> editorManagedFileNames,
                              std::string *errorMessage)
    {
        std::error_code error;
        if (std::filesystem::exists(solutionPath, error))
        {
            std::ifstream input(solutionPath, std::ios::in | std::ios::binary);
            const std::string existingText((std::istreambuf_iterator<char>(input)), {});
            const auto existing = ReadSolutionProjects(existingText);
            if (existing == Normalized(projects))
                return true;

            const bool editorOwned = std::all_of(existing.begin(), existing.end(), [&](const SolutionProject &project)
            {
                const auto fileName = FileNameOf(project.path);
                return std::find(editorManagedFileNames.begin(), editorManagedFileNames.end(), fileName) != editorManagedFileNames.end();
            });
            if (!editorOwned)
                return true;
        }
        return WriteTextFile(solutionPath, BuildScriptSolutionText(projects), errorMessage);
    }

    std::string BuildScriptEnginePropsText(const ScriptProjectLayout &layout)
    {
        std::string text;
        text += "<!-- Generated by the PlutoGE editor for this machine. Not committed; the editor rewrites it. -->\n";
        text += "<Project>\n";
        text += "  <PropertyGroup>\n";
        text += "    <PlutoGEScriptCore>" + layout.scriptCoreReference + "</PlutoGEScriptCore>\n";
        text += "    <PlutoGEScriptCoreRuntime>" + layout.scriptCoreRuntimeDirectory + "</PlutoGEScriptCoreRuntime>\n";
        text += "    <PlutoGEScriptCoreIsProject>" + std::string(layout.scriptCoreIsProject ? "true" : "false") +
                "</PlutoGEScriptCoreIsProject>\n";
        text += "  </PropertyGroup>\n";
        text += "</Project>\n";
        return text;
    }

    std::string BuildScriptProjectText(const ScriptProjectLayout &layout)
    {
        std::string text;
        text += "<Project Sdk=\"Microsoft.NET.Sdk\">\n";
        text += "  <!-- Generated by the PlutoGE editor; safe to commit. Put your own settings and package references in\n";
        text += "       $(MSBuildProjectName).custom.props or .custom.targets, which the editor never changes. -->\n";
        text += "  <Import Project=\"$(MSBuildProjectName).engine.props\" Condition=\"Exists('$(MSBuildProjectName).engine.props')\" />\n";
        text += "  <Import Project=\"$(MSBuildProjectName).custom.props\" Condition=\"Exists('$(MSBuildProjectName).custom.props')\" />\n";
        text += "  <PropertyGroup>\n";
        text += "    <TargetFramework>net8.0</TargetFramework>\n";
        text += "    <ImplicitUsings>enable</ImplicitUsings>\n";
        text += "    <Nullable>enable</Nullable>\n";
        text += "    <EnableDefaultItems>false</EnableDefaultItems>\n";
        text += "    <CopyLocalLockFileAssemblies>true</CopyLocalLockFileAssemblies>\n";
        text += "    <AssemblyName>" + layout.rootNamespace + ".Scripts</AssemblyName>\n";
        text += "    <RootNamespace>" + layout.rootNamespace + ".Scripts</RootNamespace>\n";
        text += "    <OutputPath>" + layout.managedOutputPath + "/</OutputPath>\n";
        text += "    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n";
        text += "  </PropertyGroup>\n";
        text += "  <ItemGroup>\n";
        text += "    <Compile Include=\"" + layout.sourcePattern + "\" />\n";
        text += "  </ItemGroup>\n";
        // Source checkouts reference the ScriptCore project; installed editors reference the packaged assembly, so
        // MSBuild never tries to write bin/obj folders inside a read-only installation.
        text += "  <ItemGroup Condition=\"'$(PlutoGEScriptCoreIsProject)' == 'true'\">\n";
        text += "    <ProjectReference Include=\"$(PlutoGEScriptCore)\" />\n";
        text += "  </ItemGroup>\n";
        text += "  <ItemGroup Condition=\"'$(PlutoGEScriptCoreIsProject)' != 'true'\">\n";
        text += "    <Reference Include=\"PlutoGE.ScriptCore\">\n";
        text += "      <HintPath>$(PlutoGEScriptCore)</HintPath>\n";
        text += "      <Private>true</Private>\n";
        text += "    </Reference>\n";
        text += "  </ItemGroup>\n";
        text += "  <Target Name=\"CopyScriptCoreRuntimeFiles\" AfterTargets=\"Build\">\n";
        text += "    <PropertyGroup>\n";
        text += "      <_PlutoGERuntimeFiles Condition=\"'$(PlutoGEScriptCoreIsProject)' == 'true'\">$(PlutoGEScriptCoreRuntime)/bin/$(Configuration)/$(TargetFramework)</_PlutoGERuntimeFiles>\n";
        text += "      <_PlutoGERuntimeFiles Condition=\"'$(PlutoGEScriptCoreIsProject)' != 'true'\">$(PlutoGEScriptCoreRuntime)</_PlutoGERuntimeFiles>\n";
        text += "    </PropertyGroup>\n";
        text += "    <ItemGroup>\n";
        text += "      <ScriptCoreRuntimeFiles Include=\"$(_PlutoGERuntimeFiles)/PlutoGE.ScriptCore.runtimeconfig.json\" />\n";
        text += "      <ScriptCoreRuntimeFiles Include=\"$(_PlutoGERuntimeFiles)/PlutoGE.ScriptCore.deps.json\" Condition=\"Exists('$(_PlutoGERuntimeFiles)/PlutoGE.ScriptCore.deps.json')\" />\n";
        text += "    </ItemGroup>\n";
        text += "    <Copy SourceFiles=\"@(ScriptCoreRuntimeFiles)\" DestinationFolder=\"$(OutputPath)\" SkipUnchangedFiles=\"true\" Condition=\"@(ScriptCoreRuntimeFiles) != ''\" />\n";
        text += "  </Target>\n";
        text += "  <Import Project=\"$(MSBuildProjectName).custom.targets\" Condition=\"Exists('$(MSBuildProjectName).custom.targets')\" />\n";
        text += "</Project>\n";
        return text;
    }

    std::string BuildGitIgnoreText()
    {
        return "# PlutoGE: generated, cached and per-user files\n"
               "Library/\n"
               "Build/\n"
               ".plutoge-editor/\n"
               ".plutoge-cache/\n"
               "Assets/Managed/\n"
               "obj/\n"
               "bin/\n"
               "# This machine's engine location for the script project (the .csproj itself can be committed)\n"
               "*.engine.props\n"
               "# The editor's solution can list this machine's engine ScriptCore project; it is regenerated on open\n"
               "/*.sln\n"
               "# IDE state\n"
               ".vs/\n"
               "*.csproj.user\n"
               ".idea/\n"
               ".vscode/*\n"
               "!.vscode/launch.json\n";
    }

    std::string BuildGitAttributesText()
    {
        return "* text=auto\n"
               "\n"
               "# Large binary sources go to Git LFS when it is installed (run `git lfs install` once).\n"
               "*.fbx filter=lfs diff=lfs merge=lfs -text\n"
               "*.glb filter=lfs diff=lfs merge=lfs -text\n"
               "*.bin filter=lfs diff=lfs merge=lfs -text\n"
               "*.png filter=lfs diff=lfs merge=lfs -text\n"
               "*.jpg filter=lfs diff=lfs merge=lfs -text\n"
               "*.jpeg filter=lfs diff=lfs merge=lfs -text\n"
               "*.tga filter=lfs diff=lfs merge=lfs -text\n"
               "*.psd filter=lfs diff=lfs merge=lfs -text\n"
               "*.exr filter=lfs diff=lfs merge=lfs -text\n"
               "*.hdr filter=lfs diff=lfs merge=lfs -text\n"
               "*.wav filter=lfs diff=lfs merge=lfs -text\n"
               "*.ogg filter=lfs diff=lfs merge=lfs -text\n"
               "*.mp3 filter=lfs diff=lfs merge=lfs -text\n"
               "\n"
               "# Asset identity files are small text the editor reads directly; they must never become LFS pointers.\n"
               "*.plutometa text -filter\n";
    }

    bool WriteNewProjectFiles(const std::filesystem::path &projectRoot, std::string *errorMessage)
    {
        static constexpr const char *kLaunchJson =
            "{\n"
            "  \"version\": \"0.2.0\",\n"
            "  \"configurations\": [\n"
            "    {\n"
            "      \"name\": \"Attach to PlutoGE editor\",\n"
            "      \"type\": \"coreclr\",\n"
            "      \"request\": \"attach\",\n"
            "      \"processName\": \"PlutoGEEditor.exe\"\n"
            "    }\n"
            "  ]\n"
            "}\n";
        return WriteIfMissing(projectRoot / ".gitignore", BuildGitIgnoreText(), errorMessage) &&
               WriteIfMissing(projectRoot / ".gitattributes", BuildGitAttributesText(), errorMessage) &&
               WriteIfMissing(projectRoot / ".vscode" / "launch.json", kLaunchJson, errorMessage);
    }
}
