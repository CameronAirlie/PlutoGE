#include "PlutoGE/scene/components/ColliderComponent.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE::scene;

int main()
{
    const auto near = [](float actual, float expected) {
        if (std::abs(actual - expected) > 0.00001f)
            throw std::runtime_error("Incorrect CCD sphere clearance");
    };
    ColliderComponent box;
    // A low car must sweep its height, not the unrelated default sphere radius.
    near(box.GetCcdSweptSphereRadius({1.2f, 0.4f, 2.0f}), 0.16f);
    near(box.GetCcdSweptSphereRadius({-1.2f, 0.4f, -2.0f}), 0.16f);
    box.SetCenter({0.0f, 0.1f, 0.0f});
    box.SetSize({0.92f, 0.75f, 0.88f});
    near(box.GetCcdSweptSphereRadius({1.2f, 0.4f, 2.0f}), 0.088f);
    near(box.GetCcdSweptSphereRadius({1.2f, 0.4f, 2.0f}, box.GetCenter()), 0.12f);
    near(box.GetCcdSweptSphereRadius({1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}), 0.0f);
    near(box.GetCcdSweptSphereRadius({1.0f, 0.01f, 1.0f}), 0.0022f);
    ColliderComponent sphere({.shape = ColliderShape::Sphere});
    near(sphere.GetCcdSweptSphereRadius({2.0f, 2.0f, 2.0f}), 0.8f);
    near(sphere.GetCcdSweptSphereRadius({1.0f, 1.0f, 1.0f}, {0.0f, 0.25f, 0.0f}), 0.2f);
    ColliderComponent capsule({.shape = ColliderShape::Capsule});
    near(capsule.GetCcdSweptSphereRadius({1.0f, 1.0f, 1.0f}, {0.0f, 0.75f, 0.0f}), 0.2f);
    std::cout << "PASS: CCD clearance for low boxes, scaling, offsets, spheres and capsules\n";
}
