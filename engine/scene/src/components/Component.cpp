#include "PlutoGE/scene/components/Component.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace PlutoGE::scene
{
    ComponentTypeID GetOrCreateComponentTypeID(const char *typeName)
    {
        static std::mutex registryMutex;
        static std::unordered_map<std::string, ComponentTypeID> registry;
        static ComponentTypeID nextTypeID = 0;

        const std::lock_guard lock(registryMutex);
        const std::string key = typeName ? typeName : "";
        if (const auto found = registry.find(key); found != registry.end())
        {
            return found->second;
        }

        const ComponentTypeID typeID = nextTypeID++;
        registry.emplace(key, typeID);
        return typeID;
    }
}
