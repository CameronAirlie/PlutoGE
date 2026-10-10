#pragma once
#include <map>
#include <string>
#include <string_view>

namespace PlutoGE::assets
{
    struct ModelImportSettings;
    // Validated one-hop aliases target primary node keys. Alias keys cannot also
    // own an object ID, so chains/cycles and multiple primary IDs are impossible.
    class ModelNodeAliasIndex
    {
    public:
        bool Build(const ModelImportSettings &settings, std::string *errorMessage = nullptr);
        std::string_view Resolve(std::string_view key) const;
    private:
        std::map<std::string, std::string, std::less<>> m_aliases;
    };
}
