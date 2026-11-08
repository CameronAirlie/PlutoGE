#pragma once

#include <cstdint>
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

    // Where a script message came from: the entity passed to Debug.Log and the calling source line.
    struct ScriptLogContext
    {
        uint32_t entityId = 0;
        std::string_view file;
        int line = 0;
    };
    using ScriptLogContextSink = std::function<void(ScriptLogSeverity, std::string_view, const ScriptLogContext &)>;

    void SetScriptLogSink(ScriptLogSink sink);
    void ClearScriptLogSink();
    void DispatchScriptLog(ScriptLogSeverity severity, std::string_view message);
    // Hosts that can use the context (the editor Console) register this; others keep receiving plain messages.
    void SetScriptLogContextSink(ScriptLogContextSink sink);
    void DispatchScriptLog(ScriptLogSeverity severity, std::string_view message, const ScriptLogContext &context);
}
