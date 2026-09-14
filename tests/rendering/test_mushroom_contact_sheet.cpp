// The mushroom candidate contact sheet (Phase 4).
//
// The pipeline's terminal output is a grid of thumbnails a person looks at, because ADR-172's rule is
// that every automated aesthetic score is a *rejection* instrument and none of them may promote a
// candidate to hero on its own. This is the machinery that makes the looking possible.
//
// It renders through the ordinary offline path -- `SceneRenderer::renderToImage` -- rather than a
// second capture path, and tiles the results into one PNG.

#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "organism/mushroom.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "search/candidate_search.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

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
        SKIP("no GPU adapter available");
    }
    return std::move(*ctx);
}

// A neutral studio: one key, one fill, a dark ground. Deliberately not Glowmere's rig -- the first
// pass is about silhouette and proportion, where a mask is a mask, and a second pass under the
// scene's own moonlight is what decides whether a candidate is a good *Glowmere* mushroom.
void studioLights(scene::Scene& scene) {
    scene.lights.clear();
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.45f, -0.8f, -0.4f));
    key.color = glm::vec3(0.85f, 0.9f, 1.0f);
    key.intensity = 3.2f;
    key.castsShadow = false;
    scene.lights.push_back(key);
    scene::PunctualLight fill;
    fill.type = scene::PunctualLight::Type::Directional;
    fill.direction = glm::normalize(glm::vec3(0.7f, -0.25f, 0.6f));
    fill.color = glm::vec3(0.45f, 0.55f, 0.8f);
    fill.intensity = 0.9f;
    fill.castsShadow = false;
    scene.lights.push_back(fill);
    scene.environment.stylized = false;
    scene.environment.backgroundColor = glm::vec3(0.035f, 0.04f, 0.06f);
    scene.environment.showSkybox = false;
    // A third directional standing in for ambient: the studio's job is to make a silhouette legible
    // and a shape readable, not to be physically plausible.
    scene::PunctualLight amb;
    amb.type = scene::PunctualLight::Type::Directional;
    amb.direction = glm::normalize(glm::vec3(0.1f, 0.95f, 0.2f));
    amb.color = glm::vec3(0.3f, 0.36f, 0.5f);
    amb.intensity = 0.8f;
    amb.castsShadow = false;
    scene.lights.push_back(amb);
}

// One candidate as a scene: four parts, four entities, so each is separately materialled exactly as
// it will be when it reaches Glowmere Valley 2 as four nodes.
void installSubject(scene::Scene& scene, const search::Subject& subject, float frameRadius) {
    scene.entities.clear();
    scene.meshes.clear();
    ++scene.meshVersion;
    for (const search::SubjectPart& part : subject.parts) {
        if (!part.mesh.valid()) {
            continue;
        }
        scene::Entity e;
        e.name = part.role;
        e.mesh = scene.addMesh(part.mesh);
        e.material.baseColor = part.baseColor;
        e.material.emissiveColor = part.emissiveColor;
        e.material.emissiveIntensity = part.emissiveIntensity;
        e.material.roughness = part.roughness;
        e.material.doubleSided = false;
        e.castsShadow = false;
        scene.entities.push_back(std::move(e));
    }
    // Framed from the *actual* bounds, not from a parameter-derived radius. The first sheet cropped
    // half its candidates, and a contact sheet whose framing varies with the thing being judged is
    // not a comparison.
    glm::vec3 lo(1e9f);
    glm::vec3 hi(-1e9f);
    for (const search::SubjectPart& part : subject.parts) {
        if (!part.mesh.valid()) continue;
        const auto b = part.mesh.bounds();
        lo = glm::min(lo, b.first);
        hi = glm::max(hi, b.second);
    }
    const glm::vec3 centre = (lo + hi) * 0.5f;
    const float extent = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) * 0.5f;
    (void)frameRadius;
    const float d = extent * 3.4f;
    const float az = glm::radians(38.0f);
    const float el = glm::radians(6.0f);
    scene.camera.position = centre + glm::vec3(std::sin(az) * std::cos(el) * d, std::sin(el) * d + extent * 0.35f,
                                               std::cos(az) * std::cos(el) * d);
    scene.camera.target = centre;
    scene.camera.lens.useExplicitFov = true;
    scene.camera.fovYRadians = glm::radians(34.0f);
    scene.camera.nearPlane = 0.02f;
    scene.camera.farPlane = 60.0f;
}

} // namespace

TEST_CASE("mushroom candidates, raw", "[.sheet][mushroom]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const organism::MushroomGenerator generator;
    const search::GeneratorSchema& schema = generator.schema();
    REQUIRE(schema.validate().has_value());

    constexpr std::uint32_t kTile = 300;
    constexpr std::uint32_t kCols = 6;
    constexpr std::uint32_t kRows = 4;
    constexpr std::uint32_t kCount = kCols * kRows;

    std::vector<std::uint8_t> sheet(static_cast<std::size_t>(kTile * kCols) * (kTile * kRows) * 4, 0);
    const std::uint32_t sheetW = kTile * kCols;

    std::printf("\n===== mushroom candidates 0..%u, raw geometry =====\n", kCount - 1);
    int rejected = 0;
    for (std::uint32_t i = 0; i < kCount; ++i) {
        const search::Parameters values = search::sampleAt(schema.parameters, i);
        auto subject = generator.build(values);
        REQUIRE(subject.has_value());

        std::uint32_t triangles = 0;
        std::optional<search::Rejection> bad;
        std::string badPart;
        for (const search::SubjectPart& part : subject->parts) {
            triangles += static_cast<std::uint32_t>(part.mesh.indices.size() / 3);
            if (!bad) {
                bad = search::meshHygiene(part.mesh, search::HygieneLimits{});
                if (bad) {
                    badPart = part.role;
                }
            }
        }
        if (!bad) {
            bad = organism::mushroomPlausibility(*subject, values);
        }
        if (bad) {
            ++rejected;
        }
        std::printf("  #%02u  tris %6u  aspect %.2f  rim %+6.1f  lobes %.0f/%.2f  gills %.0f  %s\n", i,
                    triangles, values[0], values[1], values[8], values[9], values[14],
                    bad ? ("REJECT " + badPart + ": " + bad->detail).c_str() : "");

        scene::Scene scene;
        studioLights(scene);
        installSubject(scene, *subject, subject->framingRadius);
        FixedStepClock clock(60.0);
        clock.restartAt(0.0);
        const FrameTime time{};
        auto image = renderer.renderToImage(scene, time, kTile, kTile);
        REQUIRE(image.has_value());
        const std::uint32_t cx = (i % kCols) * kTile;
        const std::uint32_t cy = (i / kCols) * kTile;
        for (std::uint32_t y = 0; y < kTile; ++y) {
            std::copy_n(&image->rgba[static_cast<std::size_t>(y) * kTile * 4], kTile * 4,
                        &sheet[(static_cast<std::size_t>(cy + y) * sheetW + cx) * 4]);
        }
        std::fflush(stdout);
    }

    const fs::path out = fs::path(AVGEN_SOURCE_DIR) / "build" / "mushroom-sheet-raw.png";
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    REQUIRE(assets::writePng(out, sheetW, kTile * kRows, sheet).has_value());
    std::printf("  %d of %u rejected; sheet at %s\n", rejected, kCount, out.string().c_str());
}
