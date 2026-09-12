// GPU skinning (ADR-086). Three claims, each checked against pixels:
//
//   1. A skinned mesh whose palette is the identity renders *byte-identically* to the same mesh
//      drawn statically. pbr_skinned.wgsl includes pbr.wgsl rather than copying it, so this is the
//      test that the shared shading stayed shared.
//   2. Posing the rig changes the image, and the same timeline second always gives the same image.
//   3. The joint palette reaches the GPU: skinned draws are recorded in every pass that draws
//      entities, and the upload is the size the rig is, not the size the ceiling is.
//
// The `[.perf]` case at the bottom prints the cost of a crowd; it never runs in CI.

#include "assets/gltf_loader.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/animation.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

using namespace avgen;

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

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
}

FrameTime frameAt(std::uint64_t index, double fps = 60.0) {
    FrameTime t;
    t.frameIndex = index;
    t.renderTime = static_cast<double>(index) / fps;
    t.deltaTime = 1.0 / fps;
    return t;
}

// A tall box, subdivided along Y so a bend has something to bend.
scene::MeshData barMesh(int segments) {
    scene::MeshData m;
    const float half = 0.35f;
    const float height = 3.0f;
    for (int s = 0; s <= segments; ++s) {
        const float y = height * static_cast<float>(s) / static_cast<float>(segments);
        const float v = static_cast<float>(s) / static_cast<float>(segments);
        const glm::vec3 corners[4] = {
            {-half, y, -half}, {half, y, -half}, {half, y, half}, {-half, y, half}};
        const glm::vec3 normals[4] = {{-0.7f, 0, -0.7f}, {0.7f, 0, -0.7f}, {0.7f, 0, 0.7f}, {-0.7f, 0, 0.7f}};
        for (int c = 0; c < 4; ++c) {
            m.vertices.push_back({corners[c], glm::normalize(normals[c]), {static_cast<float>(c) * 0.25f, v}});
        }
    }
    for (int s = 0; s < segments; ++s) {
        const auto a = static_cast<std::uint32_t>(s * 4);
        const auto b = static_cast<std::uint32_t>((s + 1) * 4);
        for (std::uint32_t c = 0; c < 4; ++c) {
            const std::uint32_t n = (c + 1) % 4;
            m.indices.insert(m.indices.end(), {a + c, b + c, b + n, a + c, b + n, a + n});
        }
    }
    return m;
}

// Two joints: a root at the base and a tip at y = 1.5. Vertices are weighted by height, so
// rotating the tip joint bends the top half of the bar.
scene::Skeleton twoJointRig() {
    scene::Skeleton s;
    s.name = "bar";
    scene::Joint root;
    root.name = "root";
    root.parent = -1;
    s.joints.push_back(root);
    scene::Joint tip;
    tip.name = "tip";
    tip.parent = 0;
    tip.rest.position = {0.0f, 1.5f, 0.0f};
    s.joints.push_back(tip);
    s.palette = {0, 1};
    s.inverseBind = {glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.5f, 0.0f))};
    return s;
}

void weightByHeight(scene::MeshData& mesh) {
    mesh.skin.assign(mesh.vertices.size(), scene::SkinInfluence{});
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const float t = std::clamp(mesh.vertices[i].position.y / 3.0f, 0.0f, 1.0f);
        mesh.skin[i].joints[0] = 0;
        mesh.skin[i].joints[1] = 1;
        mesh.skin[i].weights = {1.0f - t, t, 0.0f, 0.0f};
    }
}

// One bar, lit the same way whether it is skinned or not.
scene::Scene barScene(bool skinned) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.02f, 0.02f, 0.03f);
    s.environment.showSkybox = false;
    s.environment.sky.enabled = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {2.6f, 2.2f, 4.6f};
    s.camera.target = {0.0f, 1.5f, 0.0f};
    s.camera.fovYRadians = 0.8f;

    scene::MeshData mesh = barMesh(12);
    if (skinned) {
        weightByHeight(mesh);
    }
    const scene::MeshId id = s.addMesh(std::move(mesh));
    scene::Entity& e = s.addEntity("bar", id);
    e.material.baseColor = {0.7f, 0.55f, 0.35f};
    e.material.roughness = 0.45f;
    e.material.metallic = 0.0f;
    if (skinned) {
        scene::SkinnedRig rig;
        rig.name = "bar";
        rig.skeleton = twoJointRig();
        rig.pose = scene::restPose(rig.skeleton);
        scene::skinningPalette(rig.skeleton, rig.pose, rig.scratchModel, rig.palette);
        rig.previousPalette = rig.palette;
        s.rigs.push_back(std::move(rig));
        e.rig = 0;
    }
    scene::PunctualLight key;
    key.name = "key";
    key.direction = glm::normalize(glm::vec3(-0.5f, -0.8f, -0.4f));
    key.intensity = 4.0f;
    key.castsShadow = true;
    s.addLight(key);
    return s;
}

std::size_t differingPixels(const gpu::Image8& a, const gpu::Image8& b, int tolerance = 0) {
    REQUIRE(a.rgba.size() == b.rgba.size());
    std::size_t count = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        for (std::size_t c = 0; c < 3; ++c) {
            if (std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c])) > tolerance) {
                ++count;
                break;
            }
        }
    }
    return count;
}

std::filesystem::path alienPath() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "imported" / "alien.gltf";
}

} // namespace

TEST_CASE("a skinned mesh at rest shades exactly like a static one", "[gpu][skinning]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    // A renderer each, driven through the same frames: the occlusion pass keeps a temporal history,
    // so two scenes compared inside one renderer would differ over who was drawn first rather than
    // over what was drawn.
    const auto renderBar = [&](bool skinned, rendering::SkinningStats& peak) {
        const scene::Scene s = barScene(skinned);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        std::optional<gpu::Image8> image;
        for (std::uint64_t i = 0; i < 4; ++i) {
            auto frame = renderer.renderToImage(s, frameAt(i), 512, 384);
            REQUIRE(frame.has_value());
            image = std::move(*frame);
            const rendering::SkinningStats& now = renderer.stats().skinning;
            peak.rigs = std::max(peak.rigs, now.rigs);
            peak.joints = std::max(peak.joints, now.joints);
            peak.uploadBytes = std::max(peak.uploadBytes, now.uploadBytes);
            peak.draws = std::max(peak.draws, now.draws);
            if (i > 0) {
                // A rig that has not re-posed must not be re-uploaded: the whole point of keying
                // the upload on the palette version.
                CHECK(now.uploadBytes == 0);
            }
        }
        return *image;
    };
    rendering::SkinningStats withSkin;
    rendering::SkinningStats withoutSkin;
    const gpu::Image8 skinnedImage = renderBar(true, withSkin);
    const gpu::Image8 staticImage = renderBar(false, withoutSkin);
    const gpu::Image8* a = &skinnedImage;
    const gpu::Image8* b = &staticImage;
    CHECK(withoutSkin.rigs == 0);
    CHECK(withoutSkin.draws == 0);

    // The rest pose makes every joint matrix the identity, so the vertex stage moves nothing and
    // the two paths must agree pixel for pixel -- which they can, because the fragment stage is
    // literally the same function.
    CHECK(differingPixels(*a, *b) == 0);
    // ...and the skinned frame really did go through the skinned pipeline.
    CHECK(withSkin.rigs == 1);
    CHECK(withSkin.joints == 4); // two joints, current and previous
    CHECK(withSkin.draws >= 3); // shadow view, depth prepass, scene pass
    CHECK(withSkin.uploadBytes == 256); // one aligned slice, not the 256-joint ceiling
}

TEST_CASE("posing a rig moves the pixels, and the same second gives the same pixels",
          "[gpu][skinning]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s = barScene(true);
    renderer.setDiagnosticEntity("bar");
    auto rest = renderer.renderToImage(s, frameAt(1), 512, 384);
    REQUIRE(rest.has_value());
    REQUIRE(renderer.diagnosticObject("bar") != nullptr);
    CHECK(renderer.diagnosticObject("bar")->rigIndex == 0);
    CHECK(renderer.diagnosticObject("bar")->jointCount == 2);

    // Bend the tip joint 40 degrees about +Z.
    s.rigs[0].pose.local[1].rotation = glm::angleAxis(glm::radians(40.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    scene::skinningPalette(s.rigs[0].skeleton, s.rigs[0].pose, s.rigs[0].scratchModel, s.rigs[0].palette);
    ++s.rigs[0].paletteVersion;
    // Twice, because the occlusion pass carries a temporal history: the first frame after a change
    // is still resolving the one before it, and comparing across that says nothing about skinning.
    auto settling = renderer.renderToImage(s, frameAt(2), 512, 384);
    REQUIRE(settling.has_value());
    auto bent = renderer.renderToImage(s, frameAt(2), 512, 384);
    REQUIRE(bent.has_value());
    // A bend of that size over a 512x384 frame moves thousands of pixels, not a handful.
    CHECK(differingPixels(*rest, *bent) > 2000);

    // Rendering the same scene at the same frame time again is byte-identical.
    auto again = renderer.renderToImage(s, frameAt(2), 512, 384);
    REQUIRE(again.has_value());
    CHECK(differingPixels(*bent, *again) == 0);
}

TEST_CASE("skinning uploads palettes independently for same-version scenes", "[gpu][skinning][forensics]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    auto rest = barScene(true);
    auto bent = barScene(true);
    bent.rigs[0].pose.local[1].rotation = glm::angleAxis(glm::radians(40.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    scene::skinningPalette(bent.rigs[0].skeleton, bent.rigs[0].pose, bent.rigs[0].scratchModel,
                           bent.rigs[0].palette);
    REQUIRE(rest.rigs[0].paletteVersion == bent.rigs[0].paletteVersion);

    const auto first = renderer.renderToImage(rest, frameAt(0), 512, 384);
    REQUIRE(first.has_value());
    const auto second = renderer.renderToImage(bent, frameAt(0), 512, 384);
    REQUIRE(second.has_value());
    CHECK(differingPixels(*first, *second) > 2000);
    CHECK(renderer.stats().skinning.uploadBytes > 0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("non-finite joint palettes are rejected before GPU upload", "[gpu][skinning][validation]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    auto scene = barScene(true);
    scene.rigs[0].palette[0][0][0] = std::numeric_limits<float>::quiet_NaN();
    scene.rigs[0].previousPalette[0][0][0] = std::numeric_limits<float>::infinity();
    auto image = renderer.renderToImage(scene, frameAt(0), 256, 256);
    REQUIRE(image.has_value());
    CHECK(renderer.stats().skinning.rigs == 0);
    CHECK(renderer.stats().skinning.uploadBytes == 0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the alien renders posed, and the pose comes only from the timeline", "[gpu][skinning]") {
    if (!std::filesystem::is_regular_file(alienPath())) {
        SKIP("assets/imported/alien.gltf is not present");
    }
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s;
    REQUIRE(assets::loadGltf(alienPath(), s).has_value());
    REQUIRE(s.rigs.size() == 1);
    // The asset is authored in centimetres; 0.01 makes a 1.2 m character.
    for (scene::Entity& e : s.entities) {
        e.transform.scale = glm::vec3(0.01f);
    }
    s.camera.position = {1.3f, 0.9f, 2.2f};
    s.camera.target = {0.0f, 0.6f, 0.0f};
    s.environment.showSkybox = false;
    s.environment.sky.enabled = false;
    s.environment.environmentIntensity = 0.4f;
    scene::PunctualLight key;
    key.name = "key";
    key.direction = glm::normalize(glm::vec3(-0.4f, -0.9f, -0.5f));
    key.intensity = 4.0f;
    s.addLight(key);
    REQUIRE(s.rigs[0].player.play("Run", 0.0, 0.0f));

    // Two frame sequences over the same timeline: a 60 fps one and a 24 fps one. Both are asked
    // for the pose at exactly 0.5 s, and the frames must match bit for bit.
    const auto renderAtHalf = [&](double fps, std::uint64_t frames) {
        for (std::uint64_t i = 0; i < frames; ++i) {
            FrameTime t = frameAt(i, fps);
            scene::updateRigs(s, t);
        }
        FrameTime at;
        at.renderTime = 0.5;
        at.deltaTime = 1.0 / fps;
        at.frameIndex = 7; // deliberately the same index in both runs: only the rate differs
        scene::updateRigs(s, at);
        return renderer.renderToImage(s, at, 480, 360);
    };
    auto fast = renderAtHalf(60.0, 30);
    REQUIRE(fast.has_value());
    auto slow = renderAtHalf(24.0, 12);
    REQUIRE(slow.has_value());
    CHECK(differingPixels(*fast, *slow) == 0);

    // The character is actually on screen: a decent share of the frame is not the background.
    std::size_t lit = 0;
    for (std::size_t i = 0; i + 3 < fast->rgba.size(); i += 4) {
        if (fast->rgba[i] + fast->rgba[i + 1] + fast->rgba[i + 2] > 60) {
            ++lit;
        }
    }
    CHECK(lit > (fast->rgba.size() / 4) / 40); // more than 2.5% of the frame
    CHECK(renderer.stats().skinning.rigs == 1);
    CHECK(renderer.stats().skinning.draws > 0);

    // And the run cycle actually changes the picture over time.
    FrameTime later;
    later.renderTime = 0.72;
    later.frameIndex = 7;
    later.deltaTime = 1.0 / 60.0;
    scene::updateRigs(s, later);
    auto moved = renderer.renderToImage(s, later, 480, 360);
    REQUIRE(moved.has_value());
    CHECK(differingPixels(*fast, *moved) > 1000);
}

// ---- cost ---------------------------------------------------------------------------------
//
//   avgen_render_tests "[.perf][skinning]"

TEST_CASE("skinning cost against the same scene unskinned", "[.perf][skinning]") {
    if (!std::filesystem::is_regular_file(alienPath())) {
        SKIP("assets/imported/alien.gltf is not present");
    }
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);

    constexpr int kWarmup = 20;
    constexpr int kMeasured = 60;
    constexpr std::uint32_t kWidth = 1920;
    constexpr std::uint32_t kHeight = 1080;

    const auto build = [&](int count, bool skinned) {
        scene::Scene s;
        scene::Scene asset;
        REQUIRE(assets::loadGltf(alienPath(), asset).has_value());
        s.environment.showSkybox = false;
        s.environment.sky.enabled = false;
        s.environment.environmentIntensity = 0.3f;
        s.camera.position = {0.0f, 1.4f, 6.0f};
        s.camera.target = {0.0f, 0.7f, 0.0f};
        for (auto& m : asset.scene::Scene::meshes) {
            s.meshes.push_back(m);
        }
        ++s.meshVersion;
        for (int i = 0; i < count; ++i) {
            const float a = static_cast<float>(i) * 2.39996f;
            const float r = 0.6f + 0.35f * static_cast<float>(i % 7);
            if (skinned) {
                scene::SkinnedRig rig = asset.rigs[0];
                rig.name = "rig" + std::to_string(i);
                rig.player.play("Run", 0.13 * static_cast<double>(i), 0.0f);
                s.rigs.push_back(std::move(rig));
            }
            for (const scene::Entity& src : asset.entities) {
                scene::Entity e = src;
                e.name = "alien" + std::to_string(i);
                e.mesh = src.mesh;
                e.rig = skinned ? static_cast<scene::RigId>(s.rigs.size() - 1) : scene::kInvalidRig;
                e.transform.scale = glm::vec3(0.01f);
                e.transform.position = {std::cos(a) * r, 0.0f, std::sin(a) * r - 1.0f};
                s.entities.push_back(std::move(e));
            }
        }
        scene::PunctualLight key;
        key.name = "key";
        key.direction = glm::normalize(glm::vec3(-0.4f, -0.9f, -0.5f));
        key.intensity = 4.0f;
        key.castsShadow = true;
        s.addLight(key);
        return s;
    };

    const auto median = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v.empty() ? -1.0 : v[v.size() / 2];
    };
    // The GPU timestamp counter is quantised at 65,536 ns, and every source of noise on this
    // machine makes a frame slower rather than faster, so the *minimum* is the honest estimate of
    // what the work costs and the median says what a run of it looks like. Both are printed.
    const auto smallest = [](const std::vector<double>& v) {
        return v.empty() ? -1.0 : *std::min_element(v.begin(), v.end());
    };

    const auto run = [&](int count, bool skinned, float hz = 0.0f) {
        scene::Scene s = build(count, skinned);
        for (scene::SkinnedRig& rig : s.rigs) {
            rig.updateHz = hz;
            rig.nearDistance = hz > 0.0f ? -1.0f : 15.0f; // force the rate for the measurement
        }
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        std::vector<double> gpu;
        std::vector<double> poseUs;
        double uploadBytes = 0.0;
        for (int i = 0; i < kWarmup + kMeasured; ++i) {
            FrameTime t = frameAt(static_cast<std::uint64_t>(i));
            const auto begin = std::chrono::steady_clock::now();
            const scene::RigStats rigs = scene::updateRigs(s, t);
            const double us =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
            (void)rigs;
            auto image = renderer.renderToImage(s, t, kWidth, kHeight);
            REQUIRE(image.has_value());
            if (i < kWarmup) {
                continue;
            }
            poseUs.push_back(us);
            uploadBytes = renderer.stats().skinning.uploadBytes;
            if (renderer.stats().gpuFrameMs >= 0.0) {
                gpu.push_back(renderer.stats().gpuFrameMs);
            }
        }
        char label[32];
        std::snprintf(label, sizeof(label), "%s%s", skinned ? "skinned" : "static ",
                      hz > 0.0f ? " @20Hz" : "");
        std::printf("%3d %-14s  GPU frame min %6.2f / med %6.2f ms   pose min %6.1f / med %6.1f us   "
                    "palette upload %6.0f B\n",
                    count, label, smallest(gpu), median(gpu), smallest(poseUs), median(poseUs), uploadBytes);
    };

    std::printf("\nADR-086 skinning cost, %ux%u, %d frames after %d warm-up\n", kWidth, kHeight, kMeasured,
                kWarmup);
    for (const int count : {1, 8, 32}) {
        run(count, false);
        run(count, true);
    }
    // What the pose-rate policy buys: the same crowd, posed on a 20 Hz grid instead of every frame.
    run(32, true, 20.0f);
}
