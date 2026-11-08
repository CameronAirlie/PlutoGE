#include "PlutoGE/scripting/ScriptLogging.h"

#include <iostream>
#include <mutex>
#include <utility>

namespace PlutoGE::scripting
{
    namespace
    {
        std::mutex g_scriptLogMutex;
        ScriptLogSink g_scriptLogSink;
        ScriptLogContextSink g_scriptLogContextSink;
    }

    void SetScriptLogContextSink(ScriptLogContextSink sink)
    {
        std::lock_guard lock(g_scriptLogMutex);
        g_scriptLogContextSink = std::move(sink);
    }

    void DispatchScriptLog(ScriptLogSeverity severity, std::string_view message, const ScriptLogContext &context)
    {
        if (message.empty())
            return;
        ScriptLogContextSink sink;
        {
            std::lock_guard lock(g_scriptLogMutex);
            sink = g_scriptLogContextSink;
        }
        if (sink)
        {
            sink(severity, message, context);
            return;
        }
        DispatchScriptLog(severity, message);
    }

    void SetScriptLogSink(ScriptLogSink sink)
    {
        std::lock_guard lock(g_scriptLogMutex);
        g_scriptLogSink = std::move(sink);
    }

    void ClearScriptLogSink()
    {
        SetScriptLogSink({});
        SetScriptLogContextSink({});
    }

    void DispatchScriptLog(ScriptLogSeverity severity, std::string_view message)
    {
        if (message.empty())
        {
            return;
        }

        ScriptLogSink sink;
        {
            std::lock_guard lock(g_scriptLogMutex);
            sink = g_scriptLogSink;
        }

        if (sink)
        {
            sink(severity, message);
            return;
        }

        auto &stream = severity == ScriptLogSeverity::Error ? std::cerr : std::cout;
        switch (severity)
        {
        case ScriptLogSeverity::Warning:
            stream << "[Script][Warning] ";
            break;
        case ScriptLogSeverity::Error:
            stream << "[Script][Error] ";
            break;
        case ScriptLogSeverity::Info:
        default:
            stream << "[Script] ";
            break;
        }

        stream << message << std::endl;
    }
}
