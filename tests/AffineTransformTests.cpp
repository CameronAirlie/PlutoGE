#include "PlutoGE/scene/AffineTransform.h"
#include "PlutoGE/assets/SceneFormat.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
    bool Near(const glm::mat4 &a, const glm::mat4 &b)
    {
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > 0.00001f * std::max(1.0f, std::abs(a[c][r]))) return false;
        return true;
    }
}
int main() try
{
    using namespace PlutoGE::scene;
    Require(PlutoGE::assets::SceneFormatVersion("SCENE\t2\r\nENTITY") == 2 &&
        PlutoGE::assets::SceneFormatVersion("SCENE\t99\n") == 0 &&
        PlutoGE::assets::SceneFormatVersion("\xef\xbb\xbfSCENE\t1\n") == 1, "Scene header codec failed");
    Transform source{{3.1234567f, -2, 5}, {23, -37, 19}, {-2, 3, 0.5f}};
    glm::mat4 shear(1);
    shear[1][0] = 0.75f;
    shear[2][1] = -0.25f;
    const auto matrix = ComposeLocalTransform(source, shear);
    Transform controls;
    glm::mat4 correction;
    Require(FactorLocalTransform(matrix, controls, correction) && IsLinearTransformCorrection(correction) &&
        Near(matrix, ComposeLocalTransform(controls, correction)), "Affine factorization lost shear/reflection");
    glm::mat4 parsed;
    const auto text = SerializeLinearTransformCorrection(correction);
    Require(ParseLinearTransformCorrection(text, parsed) && parsed == correction, "Correction codec is not lossless");
    const auto before = correction;
    for (const auto *bad : {"", "1,0,0", "1,0,0,0,1,0,0,0,1,", "nan,0,0,0,1,0,0,0,1", "1x,0,0,0,1,0,0,0,1"})
        Require(!ParseLinearTransformCorrection(bad, correction) && correction == before, "Invalid codec input changed output");
    for (int invalid = 0; invalid < 3; ++invalid)
    {
        auto bad = matrix;
        if (invalid == 0) bad[0] = glm::vec4(0);
        if (invalid == 1) bad[0][3] = 1;
        if (invalid == 2) bad[1][1] = std::numeric_limits<float>::infinity();
        const auto priorPosition = controls.position;
        Require(!FactorLocalTransform(bad, controls, correction) && controls.position == priorPosition && correction == before,
            "Invalid matrix partially changed factorization output");
    }
    auto moved = controls;
    moved.position += glm::vec3(10, 0, 0);
    Require(Near(ComposeLocalTransform(moved, correction), glm::translate(glm::mat4(1), glm::vec3(10, 0, 0)) * matrix),
        "Position edits changed the retained linear basis");
    Require(FactorLocalTransform(glm::scale(glm::mat4(1), glm::vec3(0.00001f)), controls, correction),
        "Uniform small affine input was rejected");
    std::cout << "Affine transform tests passed\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
