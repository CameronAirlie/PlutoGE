#pragma once

#include <algorithm>
#include <filesystem>
#include <string>

namespace PlutoGE::render
{
    // RmlUi resolves relative sources (such as <img src>) against the document
    // URL, whose directories split only on '/'. Backslashed Windows paths leave
    // that URL without a directory, so relative images never resolve. Pass
    // every document path to RmlUi through this.
    [[nodiscard]] inline std::string ToRmlDocumentPath(const std::filesystem::path &path)
    {
        return path.generic_string();
    }

    // StreamFile performs this escaping for on-disk loads. StreamMemory does
    // not: a Windows drive colon would be parsed as a malformed URL protocol.
    // RmlUi's JoinPath restores the pipe to a colon when resolving resources.
    [[nodiscard]] inline std::string ToRmlMemoryDocumentUrl(const std::filesystem::path &path)
    {
        auto url = ToRmlDocumentPath(path);
        std::replace(url.begin(), url.end(), ':', '|');
        return url;
    }
}
