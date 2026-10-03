// The tiny-scale transform bug (Sonic Abstract, PROGRESS-abstract.md engine need 5): Transform::fromMatrix fell back
// to the identity when glm::decompose refused a matrix with a determinant under float epsilon, so an object shrunk
// to a scale of 0.001 drew at full size and unrotated.

#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/quaternion.hpp>

using namespace avgen;
using Catch::Approx;

TEST_CASE("A tiny uniform scale survives a matrix round trip with its rotation", "[transform][tinyscale]") {
    for (const float s : {1.0f, 0.1f, 0.01f, 0.001f, 1e-5f}) {
        INFO("scale " << s);
        scene::Transform in;
        in.position = {1.0f, 2.0f, 3.0f};
        in.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        in.scale = glm::vec3(s);
        const scene::Transform out = scene::Transform::fromMatrix(in.matrix());
        CHECK(out.position.x == Approx(1.0f));
        CHECK(out.position.z == Approx(3.0f));
        for (int i = 0; i < 3; ++i) {
            CHECK(out.scale[i] == Approx(s).epsilon(1e-3));
        }
        CHECK(std::abs(glm::dot(out.rotation, in.rotation)) == Approx(1.0f).margin(1e-4));
    }
}

TEST_CASE("A tiny scale with a mirror and a zero scale decompose without blowing up", "[transform][tinyscale]") {
    scene::Transform in;
    in.rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    in.scale = {-0.001f, 0.001f, 0.002f};
    const glm::mat4 m = in.matrix();
    const scene::Transform out = scene::Transform::fromMatrix(m);
    const glm::mat4 back = out.matrix();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK(back[c][r] == Approx(m[c][r]).margin(1e-7));
        }
    }
    scene::Transform zero;
    zero.position = {4.0f, 0.0f, 0.0f};
    zero.scale = glm::vec3(0.0f);
    const scene::Transform z = scene::Transform::fromMatrix(zero.matrix());
    CHECK(z.scale == glm::vec3(0.0f));
    CHECK(z.position.x == Approx(4.0f));
}
