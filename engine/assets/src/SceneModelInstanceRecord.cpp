#include "PlutoGE/assets/SceneModelInstanceRecord.h"

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view Prefix = "MODEL_INSTANCE\t1\t";
        constexpr std::size_t MaxBytes = (64 * 1024 * 1024 - Prefix.size()) / 2;
        int Nibble(char value)
        { return value >= '0' && value <= '9' ? value - '0' : value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1; }
    }
    bool SerializeSceneModelInstanceRecord(const StaticModelInstanceState &state,
        std::string &record, std::string *errorMessage)
    {
        std::string bytes;
        if (!SerializeStaticModelInstanceState(state, bytes, errorMessage)) return false;
        if (bytes.size() > MaxBytes)
        { if (errorMessage) *errorMessage = "Generated model scene record exceeds its byte limit."; return false; }
        constexpr char Hex[] = "0123456789abcdef";
        std::string candidate(Prefix);
        candidate.reserve(Prefix.size() + bytes.size() * 2);
        for (const unsigned char byte : bytes) { candidate.push_back(Hex[byte >> 4]); candidate.push_back(Hex[byte & 15]); }
        record = std::move(candidate);
        if (errorMessage) errorMessage->clear();
        return true;
    }
    bool ParseSceneModelInstanceRecord(std::string_view record,
        StaticModelInstanceState &state, std::string *errorMessage)
    {
        if (!record.starts_with(Prefix) || record.size() < Prefix.size() ||
            record.size() - Prefix.size() > MaxBytes * 2 || (record.size() - Prefix.size()) % 2)
        { if (errorMessage) *errorMessage = "Invalid or unsupported generated model instance record."; return false; }
        record.remove_prefix(Prefix.size());
        std::string bytes(record.size() / 2, '\0');
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            const int high = Nibble(record[index * 2]), low = Nibble(record[index * 2 + 1]);
            if (high < 0 || low < 0)
            { if (errorMessage) *errorMessage = "Invalid generated model instance record encoding."; return false; }
            bytes[index] = static_cast<char>((high << 4) | low);
        }
        return ParseStaticModelInstanceState(bytes, state, errorMessage);
    }
}
