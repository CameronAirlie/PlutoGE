#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace PlutoGE::scripting
{
    // An exception thrown by a script callback, with enough context for a tool to point at its source.
    struct ScriptErrorReport
    {
        std::string scriptClass;
        std::string callback;      // "OnCreate", "OnUpdate", ...
        uint32_t entityId = 0;     // 0 when the script has no owner.
        std::string entityName;
        std::string exception;     // Full managed exception text, including the stack trace.
        uint64_t occurrences = 1;  // How many times this error has been raised so far (see ScriptErrorThrottle).
    };

    // "PlayerInput.OnUpdate on 'Player' (entity 4) threw: System.NullReferenceException: ..."
    [[nodiscard]] std::string FormatScriptErrorReport(const ScriptErrorReport &report);

    using ScriptErrorListener = std::function<void(const ScriptErrorReport &)>;

    // A listener receives every report that passes the throttle. Without one, reports go to the script log
    // as errors, so headless hosts still print them.
    void SetScriptErrorListener(ScriptErrorListener listener);
    void ClearScriptErrorListener();
    void DispatchScriptError(const ScriptErrorReport &report);

    // Keeps a script that throws every frame from flooding the log: the first occurrence of an error is reported,
    // then repeats are reported only at 10, 100, 1000, ... occurrences. Errors are keyed by callback and the first
    // line of the exception, so a different failure in the same callback is reported immediately.
    class ScriptErrorThrottle
    {
    public:
        // Records an occurrence; returns the running count when it should be reported, otherwise 0.
        [[nodiscard]] uint64_t Record(std::string_view callback, std::string_view exception);
        void Reset() { m_counts.clear(); }

    private:
        std::unordered_map<std::string, uint64_t> m_counts;
    };
}
