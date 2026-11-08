#pragma once

#include <glm/glm.hpp>

namespace PlutoGE::scene
{
    // Where two colliders first touched, from the point of view of the entity receiving the event.
    struct CollisionContact
    {
        glm::vec3 point{0.0f};
        // Surface normal of the other collider at the contact, pointing towards the receiving entity.
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        // Total impulse the solver applied at the contact this step (N*s); 0 for kinematic contacts.
        float impulse = 0.0f;
    };
}
