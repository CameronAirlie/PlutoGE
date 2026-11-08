#include "PlutoGE/ui/ScriptDiagnostics.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <set>
#include <tuple>

namespace PlutoGE::ui
{
    namespace
    {
        std::string_view TrimView(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        bool ParseInt(std::string_view text, int &value)
        {
            if (text.empty())
                return false;
            return std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{};
        }

        // "<file>(<line>,<column>): <error|warning> <code>: <message>"
        std::optional<CompilerDiagnostic> ParseDiagnosticLine(std::string_view line)
        {
            line = TrimView(line);
            const auto marker = line.find("): ");
            if (marker == std::string_view::npos)
                return std::nullopt;
            const auto open = line.rfind('(', marker);
            if (open == std::string_view::npos || open == 0)
                return std::nullopt;

            const auto position = line.substr(open + 1, marker - open - 1);
            const auto comma = position.find(',');
            CompilerDiagnostic diagnostic;
            if (!ParseInt(position.substr(0, comma), diagnostic.location.line))
                return std::nullopt;
            if (comma != std::string_view::npos && !ParseInt(position.substr(comma + 1), diagnostic.location.column))
                return std::nullopt;

            auto rest = line.substr(marker + 3);
            if (rest.starts_with("error "))
            {
                diagnostic.severity = ConsoleSeverity::Error;
                rest.remove_prefix(6);
            }
            else if (rest.starts_with("warning "))
            {
                diagnostic.severity = ConsoleSeverity::Warning;
                rest.remove_prefix(8);
            }
            else
            {
                return std::nullopt;
            }

            const auto codeEnd = rest.find(": ");
            if (codeEnd == std::string_view::npos)
                return std::nullopt;
            diagnostic.code = std::string(TrimView(rest.substr(0, codeEnd)));
            auto message = TrimView(rest.substr(codeEnd + 2));
            // MSBuild appends " [C:\path\Project.csproj]".
            if (!message.empty() && message.back() == ']')
            {
                if (const auto bracket = message.rfind(" ["); bracket != std::string_view::npos)
                    message = TrimView(message.substr(0, bracket));
            }
            diagnostic.message = std::string(message);
            diagnostic.location.file = std::filesystem::path(std::string(TrimView(line.substr(0, open))));
            return diagnostic;
        }

        bool IsInside(const std::filesystem::path &file, const std::filesystem::path &root)
        {
            if (root.empty())
                return false;
            const auto normalizedFile = file.lexically_normal().generic_string();
            auto normalizedRoot = root.lexically_normal().generic_string();
            if (!normalizedRoot.empty() && normalizedRoot.back() != '/')
                normalizedRoot += '/';
            const auto lower = [](std::string text)
            {
                std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return text;
            };
            return lower(normalizedFile).starts_with(lower(normalizedRoot));
        }
    }

    std::string_view FirstLineOf(std::string_view text)
    {
        const auto end = text.find_first_of("\r\n");
        return end == std::string_view::npos ? text : text.substr(0, end);
    }

    std::vector<CompilerDiagnostic> ParseCompilerDiagnostics(std::string_view output)
    {
        std::vector<CompilerDiagnostic> diagnostics;
        std::set<std::tuple<std::string, int, int, std::string, std::string>> seen;
        while (!output.empty())
        {
            const auto end = output.find('\n');
            const auto line = output.substr(0, end);
            output = end == std::string_view::npos ? std::string_view{} : output.substr(end + 1);
            auto diagnostic = ParseDiagnosticLine(line);
            if (!diagnostic)
                continue;
            auto key = std::make_tuple(diagnostic->location.file.generic_string(), diagnostic->location.line,
                                       diagnostic->location.column, diagnostic->code, diagnostic->message);
            if (seen.insert(std::move(key)).second)
                diagnostics.push_back(std::move(*diagnostic));
        }
        return diagnostics;
    }

    std::string FormatCompilerDiagnostic(const CompilerDiagnostic &diagnostic, const std::filesystem::path &projectRoot)
    {
        std::string file = diagnostic.location.file.generic_string();
        if (IsInside(diagnostic.location.file, projectRoot))
            file = diagnostic.location.file.lexically_normal().lexically_relative(projectRoot.lexically_normal()).generic_string();
        std::string text = file;
        text += '(';
        text += std::to_string(diagnostic.location.line);
        if (diagnostic.location.column > 0)
        {
            text += ',';
            text += std::to_string(diagnostic.location.column);
        }
        text += "): ";
        if (!diagnostic.code.empty())
        {
            text += diagnostic.code;
            text += ' ';
        }
        text += diagnostic.message;
        return text;
    }

    std::optional<ConsoleFileLocation> FindExceptionSourceLocation(std::string_view exception,
                                                                  const std::filesystem::path &preferredRoot)
    {
        std::optional<ConsoleFileLocation> first;
        while (!exception.empty())
        {
            const auto end = exception.find('\n');
            const auto line = TrimView(exception.substr(0, end));
            exception = end == std::string_view::npos ? std::string_view{} : exception.substr(end + 1);
            if (!line.starts_with("at "))
                continue;
            // "at Namespace.Type.Method() in C:\path\File.cs:line 42"
            const auto in = line.rfind(" in ");
            const auto lineMarker = line.rfind(":line ");
            if (in == std::string_view::npos || lineMarker == std::string_view::npos || lineMarker < in)
                continue;
            ConsoleFileLocation location;
            if (!ParseInt(TrimView(line.substr(lineMarker + 6)), location.line))
                continue;
            location.file = std::filesystem::path(std::string(TrimView(line.substr(in + 4, lineMarker - in - 4))));
            if (preferredRoot.empty() || IsInside(location.file, preferredRoot))
                return location;
            if (!first)
                first = std::move(location);
        }
        return first;
    }
}
