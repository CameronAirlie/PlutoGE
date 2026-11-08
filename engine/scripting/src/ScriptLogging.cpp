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
    }

    void SetScriptLogSink(ScriptLogSink sink)
    {
        std::lock_guard lock(g_scriptLogMutex);
        g_scriptLogSink = std::move(sink);
    }

    void ClearScriptLogSink()
    {
        SetScriptLogSink({});
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
