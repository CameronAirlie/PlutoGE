#pragma once

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "PlutoGE/scene/components/Component.h"

namespace PlutoGE::scene
{
    // The encoded text of one heavy property, kept beside the revision it was encoded from so saves that did not touch
    // the data reuse it. A missed revision bump would save stale data, so debug builds re-encode and compare.
    // Serialization runs on the editor's main thread; like the rest of a component, this is not thread-safe.
    class HeavyPropertyCache
    {
    public:
        template <typename Encode>
        [[nodiscard]] std::shared_ptr<const std::string> Get(std::uint64_t revision, Encode &&encode) const
        {
            if (!m_text || m_revision != revision)
            {
                m_text = std::make_shared<const std::string>(encode());
                m_revision = revision;
            }
            else
            {
                assert(*m_text == encode() && "Heavy data changed without bumping its revision");
            }
            return m_text;
        }

    private:
        mutable std::shared_ptr<const std::string> m_text;
        mutable std::uint64_t m_revision = 0;
    };

    // Turns SerializeShared output into what Serialize() returns, for components whose Serialize is built on it.
    inline std::vector<Property> MaterializeSharedProperties(std::vector<Property> properties)
    {
        for (auto &property : properties)
            if (property.sharedValue)
            {
                property.value = *property.sharedValue;
                property.sharedValue.reset();
            }
        return properties;
    }

    // Drops the heavy properties from SerializeShared output.
    inline std::vector<Property> OmitSharedProperties(std::vector<Property> properties)
    {
        std::erase_if(properties, [](const Property &property) { return property.sharedValue != nullptr; });
        return properties;
    }
}
