#pragma once

#include <functional>
#include <string_view>

namespace PlutoGE::scripting
{
    enum class ScriptLogSeverity
    {
        Info,
        Warning,
        Error,
    };

    using ScriptLogSink = std::function<void(ScriptLogSeverity, std::string_view)>;

    void SetScriptLogSink(ScriptLogSink sink);
    void ClearScriptLogSink();
    void DispatchScriptLog(ScriptLogSeverity severity, std::string_view message);
}
