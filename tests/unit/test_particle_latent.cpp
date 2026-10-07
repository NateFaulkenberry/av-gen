// ADR-1140 (a latent SDF force on GPU particles), ADR-1141 (a render-transient particle density
// volume) and ADR-1142 (an SDF object drawing a density volume's iso-surface sharpened toward its own
// tree): the CPU half. The file representation round-trips and is refused by name when it is wrong;
// a reference that names nothing is refused at load (ADR-704: a binding that silently answers 0 is the
// defect); the parameters reach the scene; and the CPU references the GPU passes are compared against
// (scene/particle_latent.cpp) do what the ADRs say. The GPU half is tests/rendering/test_particle_latent_gpu.cpp.

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/particle_io.hpp"
#include "scene/particle_latent.hpp"
#include "scene/particles.hpp"
#include "scene/sdf_object.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <numeric>
#include <string>
#include <vector>

using namespace avgen;
using json = nlohmann::json;

namespace {

json sphereSdf(float radius = 1.0f, bool visible = false) {
    return json::parse(R"({"tree": {"root": {"kind": "sphere", "radius": )" + std::to_string(radius) +
                       R"(}}, "boundsMin": [-2, -2, -2], "boundsMax": [2, 2, 2], "visible": )" +
                       (visible ? "true" : "false") + "}");
}

json latentParticles(const std::string& sdfName) {
    json p = json::parse(R"({"capacity": 4096, "shape": "box", "extent": [2, 2, 2], "spawnRate": 0,
        "density": {"boundsMin": [-2, -2, -2], "boundsMax": [2, 2, 2], "resolution": 32, "weight": 2.0}})");
    p["latent"] = json{{"sdf", sdfName}, {"coherence", 0.8}, {"width", 0.1}, {"strength", 1.5}, {"flow", 0.25},
                       {"release", 9.0}};
    return p;
}

json sceneWith(const json& nodes) {
    return json{{"format", "avgen-scene"}, {"version", 1}, {"name", "astral"}, {"nodes", nodes}};
}

json particlesNode(const std::string& name, const json& particles) {
    return json{{"name", name}, {"kind", "particles"}, {"particles", particles}};
}

json sdfNode(const std::string& name, const json& sdf) { return json{{"name", name}, {"kind", "sdf"}, {"sdf", sdf}}; }

Result<std::unique_ptr<scene::Composition>> load(const json& document) {
    static assets::AssetRegistry registry{testsupport::processTempDir()};
    return scene::Composition::fromJson(document, registry);
}

} // namespace

// ---- ADR-1140 / ADR-1141: the particle system's two blocks ----------------------------------------

TEST_CASE("a particle system's latent and density blocks round-trip", "[particles][latent][density]") {
    auto read = scene::particlesFromJson(latentParticles("mask"));
    REQUIRE(read);
    CHECK(read->latent.active());
    CHECK(read->latent.sdf == "mask");
    CHECK(read->latent.coherence == Catch::Approx(0.8f));
    CHECK(read->latent.width == Catch::Approx(0.1f));
    CHECK(read->latent.strength == Catch::Approx(1.5f));
    CHECK(read->latent.flow == Catch::Approx(0.25f));
    CHECK(read->latent.release == Catch::Approx(9.0f));
    CHECK(read->density.enabled);
    CHECK(read->density.resolution == 32);
    CHECK(read->density.weight == Catch::Approx(2.0f));
    CHECK(read->density.boundsMin == glm::vec3(-2.0f));
    // write -> read -> write is a fixpoint, so nothing is written that is not read back.
    const json first = scene::particlesToJson(*read);
    auto again = scene::particlesFromJson(first);
    REQUIRE(again);
    CHECK(scene::particlesToJson(*again) == first);
    CHECK(first.at("latent").at("sdf") == "mask");
    CHECK(first.at("density").at("resolution") == 32);
}

TEST_CASE("a particle system without the blocks writes neither key", "[particles][latent][density]") {
    const json written = scene::particlesToJson(scene::ParticleSystem{});
    CHECK_FALSE(written.contains("latent"));
    CHECK_FALSE(written.contains("density"));
    auto read = scene::particlesFromJson(written);
    REQUIRE(read);
    CHECK_FALSE(read->latent.active());
    CHECK_FALSE(read->density.enabled);
}

TEST_CASE("a malformed latent or density block is refused by name", "[particles][latent][density]") {
    auto refused = [](const json& particles, const std::string& needle) {
        auto r = scene::particlesFromJson(particles);
        INFO(particles.dump());
        REQUIRE_FALSE(r);
        INFO(r.error().message);
        CHECK(r.error().message.find(needle) != std::string::npos);
    };
    refused(json::parse(R"({"latent": {"coherence": 0.5}})"), "sdf");
    refused(json::parse(R"({"latent": {"sdf": "m", "coherence": 1.5}})"), "coherence");
    refused(json::parse(R"({"latent": {"sdf": "m", "width": 0.9}})"), "width");
    refused(json::parse(R"({"latent": {"sdf": "m", "pull": 1}})"), "pull");
    refused(json::parse(R"({"latent": "mask"})"), "latent");
    refused(json::parse(R"({"density": {"boundsMin": [0, 0, 0]}})"), "boundsMax");
    refused(json::parse(R"({"density": {"boundsMin": [0, 0, 0], "boundsMax": [1, 1, 1], "resolution": 512}})"),
            "resolution");
    refused(json::parse(R"({"density": {"boundsMin": [0, 0, 0], "boundsMax": [1, 0, 1]}})"), "bounds");
    refused(json::parse(R"({"density": {"boundsMin": [0, 0, 0], "boundsMax": [1, 1, 1], "weight": -1}})"), "weight");
}

TEST_CASE("the density cap is a memory budget the message states", "[particles][density]") {
    // ADR-1150: plus the coarse occupancy grid, one rgba16float texel per 8^3 block
    CHECK(scene::densityCoarseResolution(128) == 16);
    CHECK(scene::densityCoarseResolution(130) == 17);
    CHECK(scene::densityMemoryBytes(128) == 128ull * 128 * 128 * 12 + 16ull * 16 * 16 * 8);
    CHECK(scene::densityMemoryBytes(scene::kMaxDensityResolution) == (192ull << 20) + 32ull * 32 * 32 * 8);
    CHECK((scene::densityMemoryBytes(scene::kMaxDensityResolution) >> 20) == 192); // the refusal's MiB figure
}

// ---- ADR-1142: the SDF object's density source ----------------------------------------------------

TEST_CASE("an sdf object's density source round-trips and is absent by default", "[sdf][density]") {
    json j = sphereSdf(1.0f, true);
    j["name"] = "body";
    j["density"] = json{{"particles", "matter"}, {"iso", 1.4}, {"sharpness", 0.75}};
    auto read = scene::SdfObject::fromJson(j);
    REQUIRE(read);
    CHECK(read->density.active());
    CHECK(read->density.particles == "matter");
    CHECK(read->density.iso == Catch::Approx(1.4f));
    CHECK(read->density.sharpness == Catch::Approx(0.75f));
    const json first = read->toJson();
    auto again = scene::SdfObject::fromJson(first);
    REQUIRE(again);
    CHECK(again->toJson() == first);

    auto plain = scene::SdfObject::fromJson(sphereSdf());
    REQUIRE(plain);
    CHECK_FALSE(plain->density.active());
    CHECK_FALSE(plain->toJson().contains("density"));
}

TEST_CASE("a malformed density source is refused by name", "[sdf][density]") {
    auto refused = [](json j, const std::string& needle) {
        auto r = scene::SdfObject::fromJson(j);
        INFO(j.dump());
        REQUIRE_FALSE(r);
        INFO(r.error().message);
        CHECK(r.error().message.find(needle) != std::string::npos);
    };
    json j = sphereSdf();
    j["density"] = json{{"iso", 1.0}};
    refused(j, "particles");
    j["density"] = json{{"particles", "m"}, {"iso", 0.0}};
    refused(j, "iso");
    j["density"] = json{{"particles", "m"}, {"sharpness", 2.0}};
    refused(j, "sharpness");
    j["density"] = json{{"particles", "m"}, {"level", 1.0}};
    refused(j, "level");
    j["density"] = json{{"particles", "m"}};
    j["renderMode"] = "mesh";
    refused(j, "raymarch");
}

// ---- references are resolved at load --------------------------------------------------------------

TEST_CASE("a scene whose latent and density references resolve loads", "[composition][latent][density]") {
    json body = sphereSdf(1.0f, true);
    body["density"] = json{{"particles", "matter"}, {"iso", 1.0}, {"sharpness", 0.5}};
    auto comp = load(sceneWith(json::array({sdfNode("mask", sphereSdf()), particlesNode("matter", latentParticles("mask")),
                                            sdfNode("body", body)})));
    INFO((comp ? std::string() : comp.error().message));
    REQUIRE(comp);
    params::ParameterSet parameters;
    params::Modulator modulator;
    (*comp)->attach(parameters, modulator);
    (*comp)->update({});
    const scene::Scene& sc = (*comp)->scene();
    REQUIRE(sc.particles.size() == 1);
    CHECK(sc.particles[0].latent.sdf == "mask");
    REQUIRE(sc.sdfs.size() == 2);
    CHECK(sc.sdfs[1].density.particles == "matter");
}

TEST_CASE("a latent naming no sdf node is refused at load", "[composition][latent]") {
    auto missing = load(sceneWith(json::array({particlesNode("matter", latentParticles("nowhere"))})));
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message.find("latent.sdf 'nowhere'") != std::string::npos);
    // A node of the wrong kind is not an SDF either.
    auto wrongKind = load(sceneWith(json::array({particlesNode("other", json::object()),
                                                 particlesNode("matter", latentParticles("other"))})));
    REQUIRE_FALSE(wrongKind);
    CHECK(wrongKind.error().message.find("names no sdf node") != std::string::npos);
}

TEST_CASE("a latent the particle interpreter cannot evaluate is refused at load unless its object compiles",
          "[composition][latent]") {
    // Ten nested translations need a point stack of ten; the interpreter has eight (kMaxSdfStack). ADR-1145: a
    // compiled object's latent runs a compiled variant of the force, so the same tree, interpreted, is refused
    // and, compiled, loads.
    json root = json{{"kind", "sphere"}, {"radius", 0.5}};
    for (int i = 0; i < 10; ++i) {
        root = json{{"kind", "translate"}, {"translation", {0.02, 0.0, 0.0}}, {"children", json::array({root})}};
    }
    json deep = json{{"tree", {{"root", root}}},
                     {"boundsMin", {-2, -2, -2}},
                     {"boundsMax", {2, 2, 2}},
                     {"compile", true},
                     {"visible", false}};
    REQUIRE(scene::SdfObject::fromJson(deep)); // loadable as a compiled object
    auto compiled = load(sceneWith(json::array({sdfNode("deep", deep), particlesNode("matter", latentParticles("deep"))})));
    INFO((compiled ? std::string() : compiled.error().message));
    CHECK(compiled);
    deep["compile"] = false;
    auto refused = load(sceneWith(json::array({sdfNode("deep", deep), particlesNode("matter", latentParticles("deep"))})));
    REQUIRE_FALSE(refused);
    INFO(refused.error().message);
    CHECK(refused.error().message.find("deep") != std::string::npos);
}

TEST_CASE("a density source naming nothing, or a system without a volume, is refused at load", "[composition][density]") {
    json body = sphereSdf(1.0f, true);
    body["density"] = json{{"particles", "ghost"}};
    auto missing = load(sceneWith(json::array({sdfNode("body", body)})));
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message.find("density.particles 'ghost'") != std::string::npos);

    body["density"] = json{{"particles", "plain"}};
    auto noVolume = load(sceneWith(json::array({particlesNode("plain", json::object()), sdfNode("body", body)})));
    REQUIRE_FALSE(noVolume);
    CHECK(noVolume.error().message.find("has no density volume") != std::string::npos);
}

// ---- parameters -----------------------------------------------------------------------------------

TEST_CASE("the latent, density and density-source levels are registered parameters that reach the scene",
          "[composition][latent][density][params]") {
    json body = sphereSdf(1.0f, true);
    body["density"] = json{{"particles", "matter"}, {"iso", 1.0}, {"sharpness", 0.5}};
    auto comp = load(sceneWith(json::array({sdfNode("mask", sphereSdf()), particlesNode("matter", latentParticles("mask")),
                                            sdfNode("body", body), particlesNode("plain", json::object())})));
    REQUIRE(comp);
    params::ParameterSet parameters;
    params::Modulator modulator;
    (*comp)->attach(parameters, modulator);
    for (const char* path : {"particles/matter/latent/coherence", "particles/matter/latent/strength",
                             "particles/matter/latent/flow", "particles/matter/latent/release",
                             "particles/matter/latent/width", "particles/matter/density/weight",
                             "sdf/body/density/iso", "sdf/body/density/sharpness"}) {
        INFO(path);
        CHECK(parameters.find(path) != nullptr);
    }
    // Absent blocks register nothing, so a scene without them lists exactly what it always did.
    for (const char* path : {"particles/plain/latent/coherence", "particles/plain/density/weight",
                             "sdf/mask/density/iso"}) {
        INFO(path);
        CHECK(parameters.find(path) == nullptr);
    }
    // Seeded from the file, and a change reaches the flattened scene.
    auto* coherence = dynamic_cast<params::Parameter<float>*>(parameters.find("particles/matter/latent/coherence"));
    REQUIRE(coherence != nullptr);
    CHECK(coherence->value() == Catch::Approx(0.8f));
    coherence->setBase(0.25f);
    coherence->resetFinal(); // what the frame loop does before modulation
    auto* sharpness = dynamic_cast<params::Parameter<float>*>(parameters.find("sdf/body/density/sharpness"));
    REQUIRE(sharpness != nullptr);
    sharpness->setBase(0.9f);
    sharpness->resetFinal(); // what the frame loop does before modulation
    auto* weight = dynamic_cast<params::Parameter<float>*>(parameters.find("particles/matter/density/weight"));
    REQUIRE(weight != nullptr);
    weight->setBase(3.0f);
    weight->resetFinal(); // what the frame loop does before modulation
    (*comp)->update({});
    const scene::Scene& sc = (*comp)->scene();
    CHECK(sc.particles[0].latent.coherence == Catch::Approx(0.25f));
    CHECK(sc.particles[0].density.weight == Catch::Approx(3.0f));
    CHECK(sc.sdfs[1].density.sharpness == Catch::Approx(0.9f));
}

// ---- the CPU references ---------------------------------------------------------------------------

TEST_CASE("coherence 1 binds every particle and coherence 0 binds none", "[particles][latent]") {
    for (const float width : {0.02f, 0.08f, 0.25f}) {
        for (int i = 0; i < 1000; ++i) {
            const float seed = static_cast<float>(i) / 1000.0f;
            const float theta = scene::latentTheta(seed, width);
            INFO("width " << width << " seed " << seed);
            CHECK(theta >= width - 1e-6f);
            CHECK(theta <= 1.0f - width + 1e-6f);
            CHECK(scene::latentBinding(theta, 1.0f, width) == Catch::Approx(1.0f));
            CHECK(scene::latentBinding(theta, 0.0f, width) == 0.0f);
        }
    }
    // In between, the bound share follows coherence (the curve an author reasons about).
    auto boundShare = [](float c) {
        double sum = 0.0;
        for (int i = 0; i < 1000; ++i) {
            sum += scene::latentBinding(scene::latentTheta(static_cast<float>(i) / 1000.0f, 0.08f), c, 0.08f);
        }
        return sum / 1000.0;
    };
    CHECK(boundShare(0.3f) < boundShare(0.5f));
    CHECK(boundShare(0.5f) == Catch::Approx(0.5).margin(0.05));
    CHECK(boundShare(0.5f) < boundShare(0.7f));
}

TEST_CASE("the latent stiffness grows with coherence and stays stable on a long frame", "[particles][latent]") {
    CHECK(scene::latentStiffness(1.0f, 0.0f, 1.0f / 60.0f) == Catch::Approx(18.0f));
    CHECK(scene::latentStiffness(1.0f, 1.0f, 1.0f / 60.0f) == Catch::Approx(88.0f));
    CHECK(scene::latentStiffness(2.0f, 1.0f, 1.0f / 60.0f) == Catch::Approx(176.0f));
    CHECK(scene::latentStiffness(50.0f, 1.0f, 0.1f) == Catch::Approx(80.0f)); // 0.8 / dt^2
    CHECK(scene::latentStiffness(0.0f, 1.0f, 1.0f / 60.0f) == 0.0f);
}

TEST_CASE("the latent projection lands on the SDF's zero set", "[particles][latent][sdf]") {
    auto obj = scene::SdfObject::fromJson(sphereSdf(1.25f));
    REQUIRE(obj);
    std::vector<spatial::SdfNodeGpu> program;
    REQUIRE(spatial::packSdfTree(obj->tree, program) > 0);
    const float eps = scene::latentGradientEpsilon(obj->boundsMin, obj->boundsMax);
    for (const glm::vec3 p : {glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(0.2f, -0.1f, 0.3f), glm::vec3(-1.0f, 2.0f, 0.5f)}) {
        const scene::LatentSample s = scene::latentProject(program, p, 0.0, eps);
        INFO(p.x << " " << p.y << " " << p.z);
        CHECK(glm::length(s.projection) == Catch::Approx(1.25f).margin(1e-3));
        CHECK(s.distance == Catch::Approx(glm::length(p) - 1.25f).margin(1e-3));
        CHECK(glm::dot(s.normal, glm::normalize(p)) == Catch::Approx(1.0f).margin(1e-4));
    }
    // One step pulls a bound particle toward the surface, and does nothing to an unbound one.
    scene::LatentStep step;
    step.coherence = 1.0f;
    step.prevCoherence = 1.0f;
    step.epsilon = eps;
    const glm::vec3 outside(2.0f, 0.0f, 0.0f);
    const glm::vec3 v = scene::latentVelocityStep(step, program, outside, glm::vec3(0.0f), 0.37f);
    CHECK(v.x < 0.0f);
    CHECK(std::abs(v.y) < 1e-4f);
    step.coherence = 0.0f;
    step.prevCoherence = 0.0f;
    CHECK(scene::latentVelocityStep(step, program, outside, glm::vec3(0.5f), 0.37f) == glm::vec3(0.5f));
    // A coherence drop throws the matter it releases along the normal.
    step.prevCoherence = 1.0f;
    const glm::vec3 thrown = scene::latentVelocityStep(step, program, outside, glm::vec3(0.0f), 0.37f);
    CHECK(glm::length(thrown) > 1.0f);
    CHECK(std::abs(glm::dot(glm::normalize(thrown), glm::vec3(1.0f, 0.0f, 0.0f))) == Catch::Approx(1.0f).margin(1e-4));
}

TEST_CASE("the density splat conserves mass and the blur is a normalised binomial", "[particles][density]") {
    const glm::vec3 lo(-1.0f);
    const glm::vec3 hi(1.0f);
    const int res = 16;
    std::vector<glm::vec3> positions;
    for (int i = 0; i < 500; ++i) {
        const float a = static_cast<float>(i) * 0.61803f;
        positions.emplace_back(0.7f * std::sin(a * 3.1f), 0.7f * std::cos(a * 1.7f), 0.7f * std::sin(a * 0.9f + 1.0f));
    }
    std::vector<std::uint32_t> grid;
    scene::splatDensity(positions, lo, hi, res, grid);
    const double fixedTotal = std::accumulate(grid.begin(), grid.end(), 0.0);
    // Eight rounded corner weights per particle: within a quantum per corner of exactly one particle each.
    CHECK(fixedTotal / scene::kDensityFixedScale == Catch::Approx(500.0).margin(500.0 * 8.0 / scene::kDensityFixedScale));
    const std::vector<float> rho = scene::resolveDensity(grid, res, 2.0f);
    const double total = std::accumulate(rho.begin(), rho.end(), 0.0);
    // The blur only moves mass (nothing here is within a cell of the clamped edge): weight 2 doubles it.
    CHECK(total == Catch::Approx(2.0 * fixedTotal / scene::kDensityFixedScale).epsilon(1e-4));
    // A particle outside the bounds deposits nothing.
    std::vector<glm::vec3> outside{glm::vec3(5.0f), glm::vec3(-0.999f)};
    scene::splatDensity(outside, lo, hi, res, grid);
    CHECK(std::accumulate(grid.begin(), grid.end(), 0.0) == 0.0);
}
