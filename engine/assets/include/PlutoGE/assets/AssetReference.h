#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace PlutoGE::assets
{
    // Stable project identity. Physical paths and import-generation keys do not
    // belong here. Engine built-ins continue to use their engine:// namespace.
    struct AssetReference
    {
        std::string assetId;
        std::uint64_t localObjectId = 0;

        bool operator==(const AssetReference &) const = default;
        [[nodiscard]] bool IsEmpty() const noexcept { return assetId.empty(); }
    };

    // Canonical encoding: asset://<percent-encoded owner ID>#<decimal local ID>.
    // An empty reference serializes to an empty string. Parse failure leaves
    // output unchanged. Legacy project:// and engine:// paths are not decoded
    // here: their resolution requires a catalog and an explicit compatibility policy.
    bool SerializeAssetReference(const AssetReference &reference, std::string &text,
                                 std::string *errorMessage = nullptr);
    bool ParseAssetReference(std::string_view text, AssetReference &reference,
                             std::string *errorMessage = nullptr);
}
