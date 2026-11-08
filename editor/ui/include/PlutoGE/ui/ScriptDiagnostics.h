#pragma once

#include "PlutoGE/ui/ConsoleLog.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::ui
{
    // One compiler message: "File.cs(12,5): error CS0103: The name 'x' does not exist ... [Project.csproj]".
    struct CompilerDiagnostic
    {
        ConsoleSeverity severity = ConsoleSeverity::Error;
        ConsoleFileLocation location;
        std::string code;    // "CS0103"
        std::string message; // Without the trailing "[project]" MSBuild appends.
    };

    // Extracts errors and warnings from `dotnet build` output. MSBuild repeats every diagnostic in its summary;
    // duplicates are dropped, keeping the first occurrence's order.
    [[nodiscard]] std::vector<CompilerDiagnostic> ParseCompilerDiagnostics(std::string_view output);

    // "Assets/Scripts/Player.cs(12,5): CS0103 The name 'x' does not exist in the current context"
    [[nodiscard]] std::string FormatCompilerDiagnostic(const CompilerDiagnostic &diagnostic,
                                                       const std::filesystem::path &projectRoot = {});

    // The source position of the first stack frame in a .NET exception ("   at A.B() in C:\x\File.cs:line 42").
    // When `preferredRoot` is set, the first frame inside it wins (the game's own code rather than engine code);
    // otherwise, or when no frame is inside it, the first frame with a file.
    [[nodiscard]] std::optional<ConsoleFileLocation> FindExceptionSourceLocation(std::string_view exception,
                                                                                const std::filesystem::path &preferredRoot = {});

    // First line of a multi-line text, without the trailing carriage return.
    [[nodiscard]] std::string_view FirstLineOf(std::string_view text);
}
