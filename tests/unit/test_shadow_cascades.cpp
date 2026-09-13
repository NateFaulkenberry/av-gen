// The cascade fit's arithmetic, without a device (ADR-112).
//
// The claim ADR-112 rests on is that the *coarsest* cascade's texel is set by the shadowed range
// and by almost nothing else -- not by the split lambda, not by the cascade count -- so shortening
// the range is the only lever on it short of spending more texels. That claim is checked here
// against the real `fitDirectionalCascade` rather than against the approximation the range rule is
// derived from, because an approximation that is only ever compared with itself is not evidence.

#include "rendering/shadow_math.hpp"

#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace avgen::rendering;

namespace {

constexpr float kNear = 0.5f;
constexpr float kFovY = 0.9f;

glm::mat4 invViewProjFor(float near, float far, float aspect = 16.0f / 9.0f) {
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 2.0f, -1.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(kFovY, aspect, near, far);
    proj[1][1] *= -1.0f; // the engine's convention; irrelevant to a corner-interpolation test
    return glm::inverse(proj * view);
}

// The world size of the coarsest cascade's texel, from the real fit.
float coarsestTexel(float range, std::uint32_t cascades, std::uint32_t resolution, float lambda,
                    float aspect = 16.0f / 9.0f) {
    const glm::mat4 inv = invViewProjFor(kNear, range, aspect);
    const std::vector<float> splits = cascadeSplits(kNear, range, cascades, lambda);
    float worst = 0.0f;
    float nearDepth = kNear;
    for (std::uint32_t c = 0; c < cascades; ++c) {
        const ShadowView v = fitDirectionalCascade(inv, kNear, range, nearDepth, splits[c],
                                                   glm::vec3(0.3f, -0.8f, -0.5f), resolution, range);
        worst = std::max(worst, v.texelWorldSize);
        nearDepth = splits[c];
    }
    return worst;
}

} // namespace

TEST_CASE("the coarsest cascade texel is set by the range, not by the split scheme", "[shadows][cascades]") {
    // The premise of ADR-112's range rule, stated as a measurement. If the split lambda or the
    // cascade count could fix the coarsest texel, shortening the range would be the wrong answer.
    const float base = coarsestTexel(500.0f, 3, 2048, 0.85f);
    REQUIRE(base > 0.0f);

    // Lambda from almost uniform to fully logarithmic: the coarsest texel barely notices.
    for (const float lambda : {0.5f, 0.7f, 0.85f, 0.95f, 1.0f}) {
        const float t = coarsestTexel(500.0f, 3, 2048, lambda);
        INFO("lambda " << lambda << ": coarsest texel " << t * 100.0f << " cm against " << base * 100.0f);
        CHECK(t > base * 0.85f);
        CHECK(t < base * 1.15f);
    }
    // Nor does the cascade count. Four cascades cost four depth passes and four atlas layers and
    // buy nothing at all at the far end.
    for (const std::uint32_t cascades : {2u, 3u, 4u}) {
        const float t = coarsestTexel(500.0f, cascades, 2048, 0.85f);
        INFO("cascades " << cascades << ": coarsest texel " << t * 100.0f << " cm");
        CHECK(t > base * 0.85f);
        CHECK(t < base * 1.15f);
    }

    // The range does. This is the negative control for the two loops above: they assert that
    // something does *not* move, which is a claim anyone can satisfy by measuring nothing, so the
    // same measurement has to move when the thing that should move it moves.
    const float shorter = coarsestTexel(100.0f, 3, 2048, 0.85f);
    INFO("range 500 m -> " << base * 100.0f << " cm, range 100 m -> " << shorter * 100.0f << " cm");
    CHECK(shorter < base * 0.35f);
}

TEST_CASE("the range rule hits the texel it aims at", "[shadows][cascades]") {
    // `directionalShadowRange` inverts an approximation (texel = 2 * 0.8 * range / resolution). What
    // matters is whether the range it returns really produces the texel it was asked for, measured
    // through the fit itself.
    for (const std::uint32_t resolution : {1024u, 2048u, 4096u}) {
        for (const float target : {0.04f, 0.08f, 0.2f}) {
            const float range = directionalShadowRange(kNear, 1e6f, 1e6f, resolution, target);
            const float texel = coarsestTexel(range, 3, resolution, 0.85f);
            INFO("resolution " << resolution << ", target " << target * 100.0f << " cm: range "
                               << range << " m gives " << texel * 100.0f << " cm");
            // At the 16:9 the constant was fitted to, within a few percent.
            CHECK(texel > target * 0.9f);
            CHECK(texel < target * 1.1f);
        }
    }
    // Off 16:9 the constant is no longer exact, because the rule is not told the aspect and a wider
    // frustum has a wider bounding sphere for the same depth slice. The claim is that it stays
    // close enough to be choosing a sensible range, over every aspect anyone shoots at -- the
    // square frame of an installation and the 2.39:1 of a scope frame.
    const float range = directionalShadowRange(kNear, 1e6f, 1e6f, 2048, 0.08f);
    for (const float aspect : {1.0f, 4.0f / 3.0f, 16.0f / 9.0f, 2.39f}) {
        const float texel = coarsestTexel(range, 3, 2048, 0.85f, aspect);
        INFO("aspect " << aspect << ": coarsest texel " << texel * 100.0f << " cm against a target of 8");
        CHECK(texel > 0.08f * 0.7f);
        CHECK(texel < 0.08f * 1.4f);
    }
}

TEST_CASE("the range rule only ever shortens the range", "[shadows][cascades]") {
    // A room-sized scene already asks for less than the rule allows, and must come out untouched --
    // the rule is a ceiling on how far shadows reach, never a reason to reach further. This is what
    // keeps every scene that was fine before ADR-112 fine after it.
    for (const float sceneRadius : {0.5f, 2.0f, 10.0f, 40.0f, 200.0f, 2000.0f}) {
        const float without = directionalShadowRange(kNear, 5000.0f, sceneRadius, 2048, 0.0f);
        const float with = directionalShadowRange(kNear, 5000.0f, sceneRadius, 2048, 0.08f);
        INFO("scene radius " << sceneRadius << ": " << without << " m -> " << with << " m");
        CHECK(with <= without + 1e-3f);
        CHECK(with > 0.0f);
    }
    // And it does bite on a wide one, or it would not be worth having.
    CHECK(directionalShadowRange(kNear, 5000.0f, 1000.0f, 2048, 0.08f) <
          directionalShadowRange(kNear, 5000.0f, 1000.0f, 2048, 0.0f) * 0.2f);
    // A target of zero is the pre-ADR-112 behaviour, exactly.
    CHECK(directionalShadowRange(kNear, 5000.0f, 1000.0f, 2048, 0.0f) == 3000.0f);

    // Including for a camera whose far plane is closer than the rule's own floor of twenty near
    // planes -- a macro shot, or anything with a deliberately shallow depth range. The floor must
    // not reach past the far plane, because that would be the rule lengthening the range.
    for (const float cameraFar : {1.0f, 3.0f, 8.0f, 40.0f}) {
        const float without = directionalShadowRange(kNear, cameraFar, 1000.0f, 2048, 0.0f);
        const float with = directionalShadowRange(kNear, cameraFar, 1000.0f, 2048, 0.08f);
        INFO("camera far " << cameraFar << ": " << without << " m -> " << with << " m");
        CHECK(with <= without + 1e-3f);
        CHECK(with <= std::max(cameraFar, kNear * 4.0f) + 1e-3f);
    }
}

TEST_CASE("the range rule cannot collapse the range to nothing", "[shadows][cascades]") {
    // A pathological target or a one-texel map must still leave a usable range rather than a
    // degenerate matrix: the floor is twenty near planes, which is the floor the world rule uses.
    for (const float target : {1e-6f, 1e-3f, 0.01f}) {
        for (const std::uint32_t resolution : {1u, 16u, 1024u}) {
            const float range = directionalShadowRange(kNear, 5000.0f, 1000.0f, resolution, target);
            INFO("target " << target << ", resolution " << resolution << ": range " << range);
            CHECK(range >= kNear * 20.0f);
            CHECK(std::isfinite(range));
        }
    }
    CHECK(std::isfinite(directionalShadowRange(kNear, 5000.0f, 1000.0f, 0, 0.08f)));
}

TEST_CASE("a non-finite frustum still reaches the renderer's guard", "[shadows][cascades]") {
    // `ShadowRenderer::update` refuses a cascade whose matrix came out non-finite and says so in the
    // log. That guard is only worth anything if a bad input still produces a non-finite matrix
    // rather than, say, a silently zeroed one -- so this pins the premise, not the guard.
    glm::mat4 bad = invViewProjFor(kNear, 500.0f);
    bad[2][2] = std::numeric_limits<float>::quiet_NaN();
    const ShadowView view = fitDirectionalCascade(bad, kNear, 500.0f, kNear, 100.0f,
                                                  glm::vec3(0.3f, -0.8f, -0.5f), 2048, 500.0f);
    bool anyNonFinite = false;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            anyNonFinite = anyNonFinite || !std::isfinite(view.viewProj[col][row]);
        }
    }
    CHECK(anyNonFinite);

    // And the good input it is contrasted with is entirely finite, or the check above would pass
    // for a fit that always returns NaN.
    const ShadowView good = fitDirectionalCascade(invViewProjFor(kNear, 500.0f), kNear, 500.0f, kNear,
                                                  100.0f, glm::vec3(0.3f, -0.8f, -0.5f), 2048, 500.0f);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            REQUIRE(std::isfinite(good.viewProj[col][row]));
        }
    }
}

TEST_CASE("cascades stay snapped to their own texel grid", "[shadows][cascades]") {
    // ADR-081: the cascade centre is quantised to a light-space grid anchored at the world origin,
    // so the map re-rasterises onto the same texels frame after frame and edges do not crawl. ADR-112
    // changes the *range*, which changes the splits, which changes the texel size -- so this is
    // re-pinned here rather than assumed to have survived.
    //
    // The property, stated exactly: as the camera slides, a fixed world point's position in the
    // shadow map moves by a whole number of texels. Not "the matrix does not move" -- it does, the
    // window follows the camera -- but that it only ever moves in texel steps, which is what stops
    // the rasterisation landing differently on the same geometry.
    const float range = directionalShadowRange(kNear, 5000.0f, 400.0f, 2048, 0.08f);
    const std::vector<float> splits = cascadeSplits(kNear, range, 3);
    const glm::vec3 light(0.3f, -0.8f, -0.5f);
    constexpr std::uint32_t kResolution = 2048;

    auto fitAt = [&](float dx) {
        const glm::mat4 view = glm::lookAt(glm::vec3(dx, 2.0f, 0.0f), glm::vec3(dx, 2.0f, -1.0f),
                                           glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(kFovY, 16.0f / 9.0f, kNear, range);
        proj[1][1] *= -1.0f;
        return fitDirectionalCascade(glm::inverse(proj * view), kNear, range, kNear, splits[0], light,
                                      kResolution, range);
    };
    // Where a fixed world point lands in the shadow map, in texels.
    auto texelOf = [&](const ShadowView& v, const glm::vec3& world) {
        const glm::vec4 clip = v.viewProj * glm::vec4(world, 1.0f);
        return glm::vec2(clip.x / clip.w * 0.5f + 0.5f, 0.5f - clip.y / clip.w * 0.5f) *
               static_cast<float>(kResolution);
    };

    const ShadowView base = fitAt(0.0f);
    const float texel = base.texelWorldSize;
    REQUIRE(texel > 0.0f);
    const glm::vec3 probe(3.0f, 0.0f, -12.0f);
    const glm::vec2 reference = texelOf(base, probe);

    float worstError = 0.0f;
    bool moved = false;
    for (int step = 1; step <= 40; ++step) {
        const ShadowView v = fitAt(texel * 0.37f * static_cast<float>(step));
        const glm::vec2 delta = texelOf(v, probe) - reference;
        const glm::vec2 error(delta.x - std::round(delta.x), delta.y - std::round(delta.y));
        worstError = std::max({worstError, std::abs(error.x), std::abs(error.y)});
        moved = moved || std::abs(delta.x) + std::abs(delta.y) > 0.5f;
    }
    INFO("worst departure from a whole texel over the slide: " << worstError << " texels");
    CHECK(worstError < 0.05f);
    // The negative control: a fit that never moved would pass the line above trivially.
    CHECK(moved);
}
