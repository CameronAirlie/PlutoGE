#include "PlutoGE/scene/CameraTagFilter.h"

#include "PlutoGE/scene/Entity.h"

#include <algorithm>

namespace PlutoGE::scene
{
    namespace
    {
        std::vector<std::string> Normalize(std::vector<std::string> tags)
        {
            std::erase_if(tags, [](const std::string &tag) { return tag.empty(); });
            std::vector<std::string> unique;
            unique.reserve(tags.size());
            for (auto &tag : tags)
                if (std::find(unique.begin(), unique.end(), tag) == unique.end())
                    unique.push_back(std::move(tag));
            return unique;
        }

        bool HasAnyTag(const Entity &entity, const std::vector<std::string> &tags)
        {
            return std::ranges::any_of(entity.GetTags(), [&tags](const std::string &tag)
                                       { return std::find(tags.begin(), tags.end(), tag) != tags.end(); });
        }

        bool HierarchyHasAnyTag(const Entity *entity, const std::vector<std::string> &tags)
        {
            for (; entity; entity = entity->GetParent())
                if (HasAnyTag(*entity, tags))
                    return true;
            return false;
        }
    }

    CameraTagFilter::CameraTagFilter(std::vector<std::string> includedTags, std::vector<std::string> excludedTags)
        : m_includedTags(Normalize(std::move(includedTags))), m_excludedTags(Normalize(std::move(excludedTags)))
    {
    }

    void CameraTagFilter::SetIncludedTags(std::vector<std::string> tags)
    {
        m_includedTags = Normalize(std::move(tags));
    }

    void CameraTagFilter::SetExcludedTags(std::vector<std::string> tags)
    {
        m_excludedTags = Normalize(std::move(tags));
    }

    bool CameraTagFilter::Accepts(const Entity *entity) const
    {
        if (Excludes(entity))
            return false;
        return m_includedTags.empty() || HierarchyHasAnyTag(entity, m_includedTags);
    }

    bool CameraTagFilter::Excludes(const Entity *entity) const
    {
        return !m_excludedTags.empty() && HierarchyHasAnyTag(entity, m_excludedTags);
    }

    std::vector<std::string> CameraTagFilter::ParseTagList(std::string_view text)
    {
        std::vector<std::string> tags;
        while (!text.empty())
        {
            const auto separator = text.find(',');
            auto token = text.substr(0, separator);
            const auto first = token.find_first_not_of(" \t");
            const auto last = token.find_last_not_of(" \t");
            if (first != std::string_view::npos)
                tags.emplace_back(token.substr(first, last - first + 1));
            if (separator == std::string_view::npos)
                break;
            text.remove_prefix(separator + 1);
        }
        return Normalize(std::move(tags));
    }

    std::string CameraTagFilter::FormatTagList(const std::vector<std::string> &tags)
    {
        std::string text;
        for (const auto &tag : tags)
        {
            if (!text.empty())
                text += ", ";
            text += tag;
        }
        return text;
    }
}
