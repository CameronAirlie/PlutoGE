#include "PlutoGE/assets/AssetReference.h"

#include <charconv>
#include <system_error>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view Prefix = "asset://";
        constexpr std::size_t MaxIdBytes = 1024;

        bool IsUnreserved(unsigned char value)
        {
            return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                   (value >= '0' && value <= '9') || value == '-' || value == '_' || value == '.' || value == '~';
        }

        bool IsValidId(std::string_view id)
        {
            if (id.empty() || id.size() > MaxIdBytes) return false;
            for (const unsigned char value : id)
                if (value <= 32 || value == 127) return false;
            return true;
        }

        int HexValue(char value)
        {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            return -1;
        }

        bool Fail(std::string *error)
        {
            if (error) *error = "Invalid stable asset reference.";
            return false;
        }
    }

    bool SerializeAssetReference(const AssetReference &reference, std::string &text, std::string *errorMessage)
    {
        if (reference.IsEmpty())
        {
            if (reference.localObjectId != 0) return Fail(errorMessage);
            text.clear();
        }
        else
        {
            if (!IsValidId(reference.assetId)) return Fail(errorMessage);
            constexpr char Hex[] = "0123456789ABCDEF";
            std::string encoded(Prefix);
            for (const unsigned char value : reference.assetId)
            {
                if (IsUnreserved(value)) encoded += static_cast<char>(value);
                else
                {
                    encoded += '%';
                    encoded += Hex[value >> 4];
                    encoded += Hex[value & 15];
                }
            }
            encoded += '#';
            encoded += std::to_string(reference.localObjectId);
            text = std::move(encoded);
        }
        if (errorMessage) errorMessage->clear();
        return true;
    }

    bool ParseAssetReference(std::string_view text, AssetReference &reference, std::string *errorMessage)
    {
        if (text.empty())
        {
            reference = {};
            if (errorMessage) errorMessage->clear();
            return true;
        }
        if (!text.starts_with(Prefix) || text.size() > Prefix.size() + MaxIdBytes * 3 + 21) return Fail(errorMessage);
        text.remove_prefix(Prefix.size());
        const auto separator = text.find('#');
        if (separator == std::string_view::npos) return Fail(errorMessage);
        const auto id = text.substr(0, separator);
        const auto local = text.substr(separator + 1);
        if (local.empty() || (local.size() > 1 && local.front() == '0')) return Fail(errorMessage);
        AssetReference parsed;
        const auto result = std::from_chars(local.data(), local.data() + local.size(), parsed.localObjectId);
        if (result.ec != std::errc{} || result.ptr != local.data() + local.size()) return Fail(errorMessage);
        for (std::size_t index = 0; index < id.size(); ++index)
        {
            if (id[index] == '%')
            {
                if (index + 2 >= id.size()) return Fail(errorMessage);
                const int high = HexValue(id[index + 1]);
                const int low = HexValue(id[index + 2]);
                if (high < 0 || low < 0) return Fail(errorMessage);
                parsed.assetId += static_cast<char>((high << 4) | low);
                index += 2;
            }
            else
            {
                if (!IsUnreserved(static_cast<unsigned char>(id[index]))) return Fail(errorMessage);
                parsed.assetId += id[index];
            }
        }
        if (!IsValidId(parsed.assetId)) return Fail(errorMessage);
        reference = std::move(parsed);
        if (errorMessage) errorMessage->clear();
        return true;
    }
}
