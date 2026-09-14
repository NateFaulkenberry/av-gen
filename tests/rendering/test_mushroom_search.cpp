// The hero mushroom search, end to end (Phase 4): sample, build, reject, score, select for diversity,
// render a contact sheet, write the winners.
//
// The pipeline is ADR-173's sampler and ADR-172's banded scoring over the shared `search::` framework.
// Its terminal output is a sheet a person looks at and a JSON table they can argue with: **no
// automated score promotes a candidate to hero on its own.**

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
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

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

void glowmereRig(scene::Scene& scene) {
    // Glowmere's own conditions, not a studio: `docs/glowmere-valley-2/02-research.md` 4.6 warns that
    // a candidate whose emission sits at 0.9 has no halo at all and one at 3.0 has a large one,
    // because bloom's threshold is 1.0 in exposed-linear units -- a cliff that is invisible under a
    // neutral rig and decides how a hero reads in the scene it is for.
    scene.lights.clear();
    scene::PunctualLight moon;
    moon.type = scene::PunctualLight::Type::Directional;
    moon.direction = glm::normalize(glm::vec3(-0.42f, -0.37f, -0.83f));
    moon.color = glm::vec3(0.42f, 0.62f, 1.0f);
    moon.intensity = 4.5f;
    moon.castsShadow = false;
    scene.lights.push_back(moon);
    scene::PunctualLight fill;
    fill.type = scene::PunctualLight::Type::Directional;
    fill.direction = glm::normalize(glm::vec3(0.6f, -0.55f, 0.5f));
    fill.color = glm::vec3(0.38f, 0.58f, 0.80f);
    fill.intensity = 0.35f;
    fill.castsShadow = false;
    scene.lights.push_back(fill);
    scene.environment.stylized = true;
    scene.environment.showSkybox = false;
    scene.environment.backgroundColor = glm::vec3(0.008f, 0.016f, 0.048f);
    scene.post.tonemap = scene::TonemapOperator::AgX; // as the scene uses
    scene.post.chromaRetention = 0.6f;
    scene.post.bloomEnabled = true;
    scene.post.bloomIntensity = 0.18f;
    scene.post.bloomThreshold = 1.0f;
    scene.post.bloomEmissionWeight = 0.75f;
}

void installSubject(scene::Scene& scene, const search::Subject& subject, float azimuthDeg,
                    float elevationDeg) {
    scene.entities.clear();
    scene.meshes.clear();
    ++scene.meshVersion;
    glm::vec3 lo(1e9f);
    glm::vec3 hi(-1e9f);
    for (const search::SubjectPart& part : subject.parts) {
        if (!part.mesh.valid()) continue;
        scene::Entity e;
        e.name = part.role;
        e.mesh = scene.addMesh(part.mesh);
        e.material.baseColor = part.baseColor;
        e.material.emissiveColor = part.emissiveColor;
        e.material.emissiveIntensity = part.emissiveIntensity;
        e.material.roughness = part.roughness;
        e.castsShadow = false;
        scene.entities.push_back(std::move(e));
        const auto b = part.mesh.bounds();
        lo = glm::min(lo, b.first);
        hi = glm::max(hi, b.second);
    }
    const glm::vec3 centre = (lo + hi) * 0.5f;
    const float extent = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) * 0.5f;
    // 3.4 cropped every cap on the first winners' sheet. A contact sheet exists to be compared, so
    // the frame has to hold the whole organism with margin at every candidate's aspect.
    const float d = extent * 5.2f;
    const float az = glm::radians(azimuthDeg);
    const float el = glm::radians(elevationDeg);
    scene.camera.position =
        centre + glm::vec3(std::sin(az) * std::cos(el) * d, std::sin(el) * d, std::cos(az) * std::cos(el) * d);
    scene.camera.target = centre;
    scene.camera.lens.useExplicitFov = true;
    scene.camera.fovYRadians = glm::radians(34.0f);
    scene.camera.nearPlane = 0.02f;
    scene.camera.farPlane = 60.0f;
}

} // namespace

TEST_CASE("the hero mushroom search", "[.search][mushroom]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const organism::MushroomGenerator generator;
    const search::GeneratorSchema& schema = generator.schema();
    REQUIRE(schema.validate().has_value());

    constexpr std::uint32_t kPopulation = 200;
    constexpr std::size_t kWinners = 6;
    // The quality/diversity trade, explicit because hiding it inside an algorithm is how a pipeline
    // ends up with six excellent near-identical mushrooms and no knob to say so (ADR-172 / 4.8).
    constexpr float kAlpha = 0.45f;

    std::vector<search::Candidate> population;
    std::map<std::string, int> rejections;
    population.reserve(kPopulation);

    for (std::uint32_t i = 0; i < kPopulation; ++i) {
        search::Candidate c;
        c.index = i;
        c.parameters = search::sampleAt(schema.parameters, i);
        auto subject = generator.build(c.parameters);
        if (!subject) {
            c.rejected = search::Rejection{"build", subject.error().message};
            rejections[c.rejected->rule]++;
            population.push_back(std::move(c));
            continue;
        }
        for (const search::SubjectPart& part : subject->parts) {
            c.triangles += static_cast<std::uint32_t>(part.mesh.indices.size() / 3);
            if (!c.rejected) {
                if (auto bad = search::meshHygiene(part.mesh, search::HygieneLimits{})) {
                    c.rejected = *bad;
                }
            }
        }
        if (!c.rejected) {
            if (auto bad = organism::mushroomPlausibility(*subject, c.parameters)) {
                c.rejected = *bad;
            }
        }
        if (c.rejected) {
            rejections[c.rejected->rule]++;
            population.push_back(std::move(c));
            continue;
        }
        c.score.components = generator.domainScores(*subject, c.parameters);
        c.features = generator.features(*subject, c.parameters);
        population.push_back(std::move(c));
    }

    // The rejection histogram is the pipeline's most useful diagnostic: a rule firing on 40% of the
    // population is a schema bug reporting itself, not a run of bad luck.
    std::printf("\n===== hero mushroom search: %u candidates =====\n", kPopulation);
    int valid = 0;
    for (const search::Candidate& c : population) {
        if (!c.rejected) ++valid;
    }
    std::printf("  valid %d of %u\n  rejections:\n", valid, kPopulation);
    for (const auto& [rule, n] : rejections) {
        std::printf("    %-16s %4d  (%.1f%%)\n", rule.c_str(), n, 100.0 * n / kPopulation);
    }
    REQUIRE(valid > 0);

    const std::vector<std::size_t> winners = search::selectDiverse(population, kWinners, kAlpha);
    REQUIRE(winners.size() == kWinners);

    std::printf("\n  selected (alpha = %.2f):\n", kAlpha);
    std::printf("    %-6s %7s  %s\n", "index", "overall", "components");
    for (const std::size_t w : winners) {
        const search::Candidate& c = population[w];
        std::printf("    #%-5u %7.3f ", c.index, c.score.overall());
        for (const search::ScoreComponent& comp : c.score.components) {
            std::printf(" %s=%.2f(raw %.3g)", comp.name.c_str(), comp.score, comp.raw);
        }
        std::printf("\n");
    }
    std::fflush(stdout);

    // ---- the contact sheet: the winners large, under Glowmere's own light --------------------
    constexpr std::uint32_t kTile = 420;
    const std::uint32_t cols = 3;
    const std::uint32_t rows = 2;
    std::vector<std::uint8_t> sheet(static_cast<std::size_t>(kTile * cols) * (kTile * rows) * 4, 0);
    const std::uint32_t sheetW = kTile * cols;
    for (std::size_t k = 0; k < winners.size(); ++k) {
        const search::Candidate& c = population[winners[k]];
        auto subject = generator.build(c.parameters);
        REQUIRE(subject.has_value());
        scene::Scene scene;
        glowmereRig(scene);
        // A low three-quarter: the angle a hero mushroom is actually shot from, and the one that
        // shows the underside a smooth dome would hide.
        installSubject(scene, *subject, 38.0f, -7.0f);
        const FrameTime time{};
        auto image = renderer.renderToImage(scene, time, kTile, kTile);
        REQUIRE(image.has_value());
        const std::uint32_t cx = (static_cast<std::uint32_t>(k) % cols) * kTile;
        const std::uint32_t cy = (static_cast<std::uint32_t>(k) / cols) * kTile;
        for (std::uint32_t y = 0; y < kTile; ++y) {
            std::copy_n(&image->rgba[static_cast<std::size_t>(y) * kTile * 4], kTile * 4,
                        &sheet[(static_cast<std::size_t>(cy + y) * sheetW + cx) * 4]);
        }
    }
    const fs::path outDir = fs::path(AVGEN_SOURCE_DIR) / "build";
    std::error_code ec;
    fs::create_directories(outDir, ec);
    REQUIRE(assets::writePng(outDir / "mushroom-winners.png", sheetW, kTile * rows, sheet).has_value());

    // ---- the canonical record ----------------------------------------------------------------
    nlohmann::json doc;
    doc["generator"] = schema.generatorName;
    doc["generatorVersion"] = schema.generatorVersion;
    doc["schemaHash"] = schema.hash();
    doc["population"] = kPopulation;
    doc["alpha"] = kAlpha;
    nlohmann::json heroes = nlohmann::json::array();
    for (const std::size_t w : winners) {
        heroes.push_back(search::candidateToJson(schema, population[w]));
    }
    doc["heroes"] = std::move(heroes);
    const fs::path record = fs::path(AVGEN_SOURCE_DIR) / "examples" / "organisms" / "glowmere2-heroes.json";
    fs::create_directories(record.parent_path(), ec);
    std::ofstream(record) << doc.dump(1);
    std::printf("\n  sheet: %s\n  record: %s\n", (outDir / "mushroom-winners.png").string().c_str(),
                record.string().c_str());
}
