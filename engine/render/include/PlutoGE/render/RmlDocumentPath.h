#pragma once

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
}
