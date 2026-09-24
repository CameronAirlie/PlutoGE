#include "PlutoGE/assets/LoadingScreenAsset.h"
#include <filesystem>
#include <istream>
#include <ostream>
#include <utility>

namespace PlutoGE::assets
{
    namespace
    {
        bool Valid(const LoadingScreenAsset &asset)
        {
            if (!asset.document.starts_with("project://") || asset.document.size() > 4095 ||
                asset.document.find_first_of("\r\n\t") != std::string::npos ||
                asset.document.find('\0') != std::string::npos) return false;
            const std::filesystem::path path(asset.document.substr(10));
            if (path.has_root_path() || path.extension() != ".rml") return false;
            for (const auto &part : path) if (part == "..") return false;
            if (asset.controller.size() > 1024) return false;
            for (unsigned char c : asset.controller)
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '+')) return false;
            return true;
        }
    }
    bool ReadLoadingScreenAsset(std::istream &input, LoadingScreenAsset &asset)
    {
        LoadingScreenAsset parsed;
        std::string line;
        const auto readLine = [&] {
            if (!std::getline(input, line)) return false;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        };
        if (!readLine() || line != "LoadingScreenVersion=1") return false;
        unsigned seen = 0;
        while (readLine())
        {
            if (line.empty()) continue;
            if (line.size() > 8192) return false;
            const auto separator = line.find('=');
            if (separator == std::string::npos) return false;
            const auto key = line.substr(0, separator);
            unsigned field = 0;
            if (key == "Document") { field = 1; parsed.document = line.substr(separator + 1); }
            else if (key == "Controller") { field = 2; parsed.controller = line.substr(separator + 1); }
            else return false;
            if (seen & field) return false;
            seen |= field;
        }
        if (input.bad() || !Valid(parsed)) return false;
        asset = std::move(parsed);
        return true;
    }
    bool WriteLoadingScreenAsset(std::ostream &output, const LoadingScreenAsset &asset)
    {
        if (!Valid(asset)) return false;
        output << "LoadingScreenVersion=1\nDocument=" << asset.document
               << "\nController=" << asset.controller << '\n';
        return static_cast<bool>(output);
    }
}
