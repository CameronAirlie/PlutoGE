#include "PlutoGE/scripting/ScriptErrors.h"

#include "PlutoGE/scripting/ScriptLogging.h"

#include <mutex>
#include <utility>

namespace PlutoGE::scripting
{
    namespace
    {
        std::mutex g_listenerMutex;
        ScriptErrorListener g_listener;

        bool IsReportedCount(uint64_t count)
        {
            if (count == 1)
                return true;
            while (count % 10 == 0)
                count /= 10;
            return count == 1;
        }

        std::string_view FirstLine(std::string_view text)
        {
            const auto end = text.find_first_of("\r\n");
            return end == std::string_view::npos ? text : text.substr(0, end);
        }
    }

    std::string FormatScriptErrorReport(const ScriptErrorReport &report)
    {
        std::string text = report.scriptClass.empty() ? std::string("Script") : report.scriptClass;
        if (!report.callback.empty())
        {
            text += '.';
            text += report.callback;
        }
        if (report.entityId != 0)
        {
            text += " on '";
            text += report.entityName;
            text += "' (entity ";
            text += std::to_string(report.entityId);
            text += ')';
        }
        text += " threw";
        if (report.occurrences > 1)
        {
            text += " (";
            text += std::to_string(report.occurrences);
            text += " times)";
        }
        text += ": ";
        text += report.exception.empty() ? std::string("unknown error") : report.exception;
        return text;
    }

    void SetScriptErrorListener(ScriptErrorListener listener)
    {
        std::lock_guard lock(g_listenerMutex);
        g_listener = std::move(listener);
    }

    void ClearScriptErrorListener()
    {
        SetScriptErrorListener({});
    }

    void DispatchScriptError(const ScriptErrorReport &report)
    {
        ScriptErrorListener listener;
        {
            std::lock_guard lock(g_listenerMutex);
            listener = g_listener;
        }

        if (listener)
        {
            listener(report);
            return;
        }

        DispatchScriptLog(ScriptLogSeverity::Error, FormatScriptErrorReport(report));
    }

    uint64_t ScriptErrorThrottle::Record(std::string_view callback, std::string_view exception)
    {
        std::string key(callback);
        key += '\n';
        key += FirstLine(exception);
        const uint64_t count = ++m_counts[key];
        return IsReportedCount(count) ? count : 0;
    }
}
