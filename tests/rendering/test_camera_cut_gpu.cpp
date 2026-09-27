// ADR-912: a cut is drawn as a first frame.
//
// The renderer cannot tell a cut from a very fast move: both are a large change of view-projection
// between two frames. It used to treat every cut as motion -- only a seek reset its history -- so
// the first frame of every shot was blurred along a "movement" from the old camera to the new one,
// at the blur's full length. Measured on GV3's final: in 7 of its first 8 cuts the new shot's first
// frame had 20-40% of its neighbours' sharpness. The scene now says so (`Scene::camera.cutSerial`),
// and the renderer drops its motion history when the serial changes.
//
// The claim is checked against the one frame that is certainly right: the same pose drawn by a
// fresh renderer, which has no history to smear. The control arm is the same two frames without the
// serial changing, which is the renderer as it was.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "support/image_diff.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

scene::MeshData boxMesh(glm::vec3 half) {
    scene::MeshData m;
    const glm::vec3 normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : normals) {
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const glm::vec3 c = normal * half;
        const glm::vec3 du = u * half;
        const glm::vec3 dv = v * half;
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({c - du - dv, normal, {0, 0}});
        m.vertices.push_back({c + du - dv, normal, {1, 0}});
        m.vertices.push_back({c + du + dv, normal, {1, 1}});
        m.vertices.push_back({c - du + dv, normal, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

// A still field of small self-lit boxes on black: every edge in the frame is a box's, so the
// frame's sharpness is a direct reading of how much of it the blur smeared.
scene::Scene field() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.lens.shutterAngle = 180.0f;
    s.post.motionBlurAmount = 1.0f;
    s.post.motionBlurSamples = 16;
    // No bloom: its pyramid lifts the whole frame off black around 49 emitters, and then a lit pixel
    // is no longer a box's.
    s.post.bloomEnabled = false;
    const auto box = s.addMesh(boxMesh({0.12f, 0.12f, 0.12f}));
    for (int j = -3; j <= 3; ++j) {
        for (int i = -3; i <= 3; ++i) {
            auto& e = s.addEntity("box", box);
            e.transform.position = glm::vec3(0.8f * static_cast<float>(i), 0.8f * static_cast<float>(j), 0.0f);
            e.material.baseColor = glm::vec3(0.0f);
            e.material.emissiveColor = glm::vec3(1.0f);
            e.material.emissiveIntensity = 4.0f;
            e.material.unlit = true;
        }
    }
    return s;
}

// How far the frame's light is spread: the pixels any box lights at all. A still frame lights the
// boxes and nothing else; motion blur smears each one along its screen motion and lights a streak.
// (A gradient-energy "sharpness" was tried first and read the smear as SHARPER -- a streak of a small
// bright box has two long crisp sides -- which is why this counts spread rather than edges.)
int litPixels(const gpu::Image8& img, int threshold = 24) {
    int lit = 0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        lit += img.rgba[i] > threshold ? 1 : 0;
    }
    return lit;
}

void pose(scene::Scene& s, glm::vec3 eye, glm::vec3 target) {
    s.camera.position = eye;
    s.camera.target = target;
}

} // namespace

TEST_CASE("the first frame after a cut is drawn as a first frame: as sharp as a still, no smear",
          "[gpu][motion][blur][cut][adr912]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    // Two cameras on the same field: the outgoing shot square on, the incoming one 1.9 m across,
    // 0.8 m up and turned -- the kind of change a cut is, and far more than any frame of motion.
    const glm::vec3 eyeA(0.0f, 0.0f, 8.0f);
    const glm::vec3 aimA(0.0f, 0.0f, 0.0f);
    const glm::vec3 eyeB(1.9f, 0.8f, 7.2f);
    const glm::vec3 aimB(0.4f, 0.2f, 0.0f);
    FrameTime t0;
    t0.frameIndex = 0;
    t0.renderTime = 0.0;
    t0.deltaTime = 1.0 / 30.0;
    FrameTime t1 = t0;
    t1.frameIndex = 1;
    t1.renderTime = 1.0 / 30.0;

    // The reference: the incoming pose drawn by a renderer that has never drawn anything else.
    scene::Scene still = field();
    pose(still, eyeB, aimB);
    rendering::SceneRenderer fresh(*ctx, shaders);
    REQUIRE(fresh.init().has_value());
    const auto reference = fresh.renderToImage(still, t1, 256, 256);
    REQUIRE(reference.has_value());

    // The cut: A, then B with the serial changed -- what the composition publishes at a cut.
    scene::Scene cutScene = field();
    rendering::SceneRenderer cutRenderer(*ctx, shaders);
    REQUIRE(cutRenderer.init().has_value());
    pose(cutScene, eyeA, aimA);
    REQUIRE(cutRenderer.renderToImage(cutScene, t0, 256, 256).has_value());
    pose(cutScene, eyeB, aimB);
    cutScene.camera.cutSerial = 1;
    const auto cut = cutRenderer.renderToImage(cutScene, t1, 256, 256);
    REQUIRE(cut.has_value());

    // Control: the same two frames with no cut announced -- the renderer as it was.
    scene::Scene motionScene = field();
    rendering::SceneRenderer motionRenderer(*ctx, shaders);
    REQUIRE(motionRenderer.init().has_value());
    pose(motionScene, eyeA, aimA);
    REQUIRE(motionRenderer.renderToImage(motionScene, t0, 256, 256).has_value());
    pose(motionScene, eyeB, aimB);
    const auto smeared = motionRenderer.renderToImage(motionScene, t1, 256, 256);
    REQUIRE(smeared.has_value());

    const int spreadStill = litPixels(*reference);
    const int spreadCut = litPixels(*cut);
    const int spreadSmeared = litPixels(*smeared);
    const testing::ByteDiff cutVsStill = testing::byteDiff(cut->rgba, reference->rgba);
    const testing::ByteDiff smearVsStill = testing::byteDiff(smeared->rgba, reference->rgba);
    INFO("lit pixels: still " << spreadStill << ", after the cut " << spreadCut << ", the same change drawn as motion "
                              << spreadSmeared << "; cut vs still: " << cutVsStill.describe()
                              << "; motion vs still: " << smearVsStill.describe());
    REQUIRE(spreadStill > 500);
    // The control shows the defect this fixes: the change drawn as motion is a smear, its light spread
    // well past the boxes.
    CHECK(spreadSmeared > (3 * spreadStill) / 2);
    CHECK_FALSE(smearVsStill.identical());
    // The cut IS the still, pixel for pixel: no history of any kind crossed it, so it has a still's
    // sharpness by construction rather than by a threshold.
    CHECK(cutVsStill.identical());
    CHECK(gpu::hashImage(*cut) == gpu::hashImage(*reference));
    CHECK(spreadCut == spreadStill);
    CHECK(cutRenderer.stats().cameraCuts == 1);
    CHECK(motionRenderer.stats().cameraCuts == 0);

    // A repeat of the cut frame with the same serial is not a second cut, and the frame after the cut
    // is motion again: a small move from B blurs, because the history restarted at B rather than
    // stopping.
    const glm::vec3 eyeC = eyeB + glm::vec3(0.35f, 0.0f, 0.0f);
    pose(cutScene, eyeC, aimB + glm::vec3(0.35f, 0.0f, 0.0f));
    FrameTime t2 = t1;
    t2.frameIndex = 2;
    t2.renderTime = 2.0 / 30.0;
    const auto after = cutRenderer.renderToImage(cutScene, t2, 256, 256);
    REQUIRE(after.has_value());
    CHECK(cutRenderer.stats().cameraCuts == 1);
    scene::Scene stillC = field();
    pose(stillC, eyeC, aimB + glm::vec3(0.35f, 0.0f, 0.0f));
    rendering::SceneRenderer freshC(*ctx, shaders);
    REQUIRE(freshC.init().has_value());
    const auto referenceC = freshC.renderToImage(stillC, t2, 256, 256);
    REQUIRE(referenceC.has_value());
    INFO("the frame after the cut lights " << litPixels(*after) << " pixels against its still's "
                                               << litPixels(*referenceC));
    CHECK(litPixels(*after) > (litPixels(*referenceC) * 11) / 10);
}
