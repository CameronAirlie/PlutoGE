#pragma once

namespace PlutoGE::scene
{
    class Entity;

    class Component
    {
    public:
        virtual ~Component() = default;
        virtual void Update(float deltaTime) = 0;

        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }

        Entity *GetOwner() const { return m_entity; }

    private:
        friend class Entity; // Allow Entity to access private members for managing component ownership
        Entity *m_entity = nullptr;
        bool m_enabled = true;
    };
}