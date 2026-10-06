// THE ASTRAL FORGE iteration 3, the production look (ADR-1150..1153): the parts with no GPU in them.
//   ADR-1150: the coarse occupancy grid's size (its memory is in test_particle_latent.cpp's budget case).
//   ADR-1151: `environment.bands` -- the file block, its refusals, its parameters, the packed lanes and the
//             CPU twin of the shader's band radiance.
//   ADR-1152: a material's `engraving` -- the file block on an SDF object, its refusals (including on the
//             owners that cannot draw it), its parameters, and the line field's coordinates.
//   ADR-1153: `shape2d: "flake"` and its `flake` block on a particle system, and its parameters.
// The GPU half is tests/rendering/test_astral_production_look_gpu.cpp.

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/material_engraving.hpp"
#include "scene/particle_io.hpp"
#include "scene/particles.hpp"
#include "scene/procedural.hpp"
#include "scene/reflection_bands.hpp"
#include "scene/sdf_object.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

using namespace avgen;
using json = nlohmann::json;
using Catch::Approx;

namespace {

Result<std::unique_ptr<scene::Composition>> load(const json& document) {
    static assets::AssetRegistry registry{testsupport::processTempDir()};
    return scene::Composition::fromJson(document, registry);
}

json bandsBlock() {
    return json::parse(R"({"phase": 0.5, "rotation": 0.25, "gain": 1.5,
        "softbox": {"intensity": 0.55, "azimuth": 0.8, "elevation": 0.5, "falloff": 2.6, "skyFill": 0.08},
        "strips": [{"axis": [0, 1, 0], "offset": 0.35, "width": 0.035, "intensity": 2.4, "warmth": 0.8},
                   {"axis": [1, 0, 0], "offset": 0.0, "width": 0.02, "intensity": 1.5, "segments": 5, "rate": 0.12}]})");
}

json engravedSdf() {
    return json::parse(R"({"name": "plate", "tree": {"root": {"kind": "sphere", "radius": 1.0}},
        "boundsMin": [-1.5, -1.5, -1.5], "boundsMax": [1.5, 1.5, 1.5],
        "material": {"baseColor": [0.4, 0.4, 0.42], "metallic": 1, "roughness": 0.2,
                     "engraving": {"depth": 0.3, "crawl": 0.05, "grating": 1.0, "spacing": 1600, "panels": 0.55,
                                   "layers": [{"family": "rosette", "center": [0.3, 0.2, 0.9], "petals": 12,
                                               "frequency": 14, "inner": 0.12, "outer": 0.5, "depth": 0.35},
                                              {"family": "contour", "center": [0, 0.2, -3], "frequency": 7},
                                              {"family": "engine", "axis": [0.94, 0.3, 0.17], "frequency": 9,
                                               "weight": 0.25}]}}})");
}

} // namespace

// ---- ADR-1150 ----------------------------------------------------------------------------------------

TEST_CASE("the coarse occupancy grid has one texel per 8^3 block, rounded up", "[particles][density][adr1150]") {
    CHECK(scene::densityCoarseResolution(4) == 1);
    CHECK(scene::densityCoarseResolution(8) == 1);
    CHECK(scene::densityCoarseResolution(9) == 2);
    CHECK(scene::densityCoarseResolution(192) == 24);
    CHECK(scene::densityCoarseResolution(256) == 32);
}

// ---- ADR-1151: reflection-only bands -----------------------------------------------------------------

TEST_CASE("an environment's bands block reads, writes back exactly and refuses what it does not know",
          "[bands][adr1151]") {
    auto b = scene::readReflectionBands(bandsBlock());
    INFO((b ? std::string() : b.error().message));
    REQUIRE(b);
    CHECK(b->enabled);
    CHECK(b->phase == Approx(0.5f));
    CHECK(b->gain == Approx(1.5f));
    REQUIRE(b->strips.size() == 2);
    CHECK(b->strips[0].warmth == Approx(0.8f));
    CHECK(b->strips[0].segments == 0);
    CHECK(b->strips[1].segments == 5);
    CHECK(b->strips[1].rate == Approx(0.12f));
    CHECK(b->strips[1].warmth == Approx(0.5f)); // the default, kept
    const json written = scene::reflectionBandsToJson(*b);
    auto again = scene::readReflectionBands(written);
    REQUIRE(again);
    CHECK(scene::reflectionBandsToJson(*again) == written);

    const auto refused = [](const char* text, const char* needle) {
        auto r = scene::readReflectionBands(json::parse(text));
        REQUIRE_FALSE(r);
        INFO(r.error().message);
        CHECK(r.error().message.find(needle) != std::string::npos);
    };
    refused(R"({"strobe": 1})", "strobe");
    refused(R"({"softbox": {"size": 1}})", "size");
    refused(R"({"strips": [{"axis": [0, 0, 0]}]})", "axis");
    refused(R"({"strips": [{"width": 0}]})", "width");
    refused(R"({"strips": [{"segments": 1.5}]})", "segments");
    refused(R"({"strips": [{}, {}, {}, {}, {}]})", "at most 4");
    refused(R"({"strips": [{"intensity": -1}]})", "intensity");
}

TEST_CASE("absent bands pack to zero lanes, and the band radiance follows the strips", "[bands][adr1151]") {
    const scene::ReflectionBands off;
    const scene::ReflectionBandLanes zero = scene::packReflectionBands(off);
    CHECK(zero.info == glm::vec4(0.0f));
    CHECK(zero.soft2 == glm::vec4(0.0f)); // soft2.w is the lit shader's gate
    CHECK(scene::reflectionBandRadiance(zero, glm::vec3(0, 1, 0), 0.05f, true) == glm::vec3(0.0f));

    scene::ReflectionBands one;
    one.enabled = true;
    scene::ReflectionBand ring;
    ring.axis = glm::vec3(0.0f, 1.0f, 0.0f);
    ring.offset = 0.5f; // the ring at 30 degrees of elevation
    ring.width = 0.03f;
    ring.intensity = 2.0f;
    one.strips.push_back(ring);
    const scene::ReflectionBandLanes lanes = scene::packReflectionBands(one);
    CHECK(lanes.soft2.w == 1.0f);
    const float s30 = std::sqrt(0.75f);
    const glm::vec3 onRing(s30, 0.5f, 0.0f);
    const glm::vec3 offRing(1.0f, 0.0f, 0.0f);
    const glm::vec3 lit = scene::reflectionBandRadiance(lanes, onRing, 0.0f, false);
    CHECK(lit.g == Approx(2.0f * 0.915f).epsilon(1e-4)); // the peak: intensity x the band colour (warmth 0.5)
    CHECK(glm::length(scene::reflectionBandRadiance(lanes, offRing, 0.0f, false)) < 1e-6f);
    // A rough reflector sees a wider, dimmer band with the same energy (sigma^2 = width^2 + alpha^2).
    const glm::vec3 rough = scene::reflectionBandRadiance(lanes, onRing, 0.04f, false);
    CHECK(rough.g == Approx(lit.g * 0.03f / std::sqrt(0.03f * 0.03f + 0.04f * 0.04f)).epsilon(1e-4));
    // The whole rig turns about +Y: a ring about +X, turned a quarter turn, lies about -Z.
    scene::ReflectionBands turned = one;
    turned.strips[0].axis = glm::vec3(1.0f, 0.0f, 0.0f);
    turned.strips[0].offset = 0.0f;
    turned.rotation = 0.5f * 3.14159265f;
    const scene::ReflectionBandLanes tl = scene::packReflectionBands(turned);
    CHECK(glm::length(scene::reflectionBandRadiance(tl, glm::vec3(1.0f, 0.0f, 0.0f), 0.0f, false)) > 1.0f);
    CHECK(glm::length(scene::reflectionBandRadiance(tl, glm::vec3(0.0f, 0.0f, 1.0f), 0.0f, false)) < 1e-6f);
    // A segmented strip's dashes move with the phase.
    scene::ReflectionBands dashed = one;
    dashed.strips[0].segments = 4;
    dashed.strips[0].rate = 0.125f;
    const glm::vec3 probe(s30, 0.5f, 0.0f);
    dashed.phase = 0.0f;
    const float a = scene::reflectionBandRadiance(scene::packReflectionBands(dashed), probe, 0.0f, false).g;
    dashed.phase = 1.0f; // a quarter of a dash period further round
    const float b = scene::reflectionBandRadiance(scene::packReflectionBands(dashed), probe, 0.0f, false).g;
    CHECK(std::abs(a - b) > 0.1f);
}

TEST_CASE("a scene's bands are parameters that reach the environment, and absent bands register none",
          "[bands][adr1151][composition]") {
    json doc = {{"format", "avgen-scene"}, {"version", 1}, {"name", "bands"},
                {"environment", {{"background", {0, 0, 0}}, {"bands", bandsBlock()}}}};
    auto comp = load(doc);
    INFO((comp ? std::string() : comp.error().message));
    REQUIRE(comp);
    params::ParameterSet parameters;
    params::Modulator modulator;
    (*comp)->attach(parameters, modulator);
    for (const char* path : {"scene/bands/phase", "scene/bands/rotation", "scene/bands/gain",
                             "scene/bands/softbox/intensity", "scene/bands/softbox/azimuth",
                             "scene/bands/softbox/elevation", "scene/bands/0/intensity", "scene/bands/1/offset",
                             "scene/bands/1/width", "scene/bands/1/warmth"}) {
        INFO(path);
        CHECK(parameters.find(path) != nullptr);
    }
    auto* phase = parameters.findAs<float>("scene/bands/phase");
    REQUIRE(phase != nullptr);
    CHECK(phase->value() == Approx(0.5f));
    phase->setBase(3.0f);
    phase->resetFinal();
    (*comp)->update({});
    CHECK((*comp)->scene().environment.bands.enabled);
    CHECK((*comp)->scene().environment.bands.phase == Approx(3.0f));
    // The save writes the block back (with the parameter's base), so it round-trips.
    const json saved = (*comp)->toJson();
    REQUIRE(saved.at("environment").contains("bands"));
    CHECK(saved.at("environment").at("bands").at("phase") == Approx(3.0f));

    json plain = {{"format", "avgen-scene"}, {"version", 1}, {"name", "plain"},
                  {"environment", {{"background", {0, 0, 0}}}}};
    auto none = load(plain);
    REQUIRE(none);
    params::ParameterSet p2;
    params::Modulator m2;
    (*none)->attach(p2, m2);
    CHECK(p2.find("scene/bands/phase") == nullptr);
    (*none)->update({});
    CHECK_FALSE((*none)->scene().environment.bands.enabled);
    CHECK_FALSE((*none)->toJson().at("environment").contains("bands"));
}

// ---- ADR-1152: engraving -------------------------------------------------------------------------------

TEST_CASE("an SDF material's engraving reads, writes back exactly and is refused by name when wrong",
          "[engraving][adr1152]") {
    auto o = scene::SdfObject::fromJson(engravedSdf());
    INFO((o ? std::string() : o.error().message));
    REQUIRE(o);
    const scene::Engraving& e = o->material.engraving;
    REQUIRE(e.enabled());
    REQUIRE(e.layers.size() == 3);
    CHECK(e.layers[0].family == scene::EngravingFamily::Rosette);
    CHECK(e.layers[1].family == scene::EngravingFamily::Contour);
    CHECK(e.layers[1].petals == Approx(7.0f)); // a contour's default
    CHECK(e.layers[2].family == scene::EngravingFamily::Engine);
    CHECK(e.layers[2].weight == Approx(0.25f));
    const json written = o->toJson();
    auto again = scene::SdfObject::fromJson(written);
    REQUIRE(again);
    CHECK(again->toJson() == written);

    const auto refused = [](const json& engraving, const char* needle) {
        json j = engravedSdf();
        j["material"]["engraving"] = engraving;
        auto r = scene::SdfObject::fromJson(j);
        REQUIRE_FALSE(r);
        INFO(r.error().message);
        CHECK(r.error().message.find(needle) != std::string::npos);
    };
    refused(json::parse(R"({"layers": [{"family": "spiral"}]})"), "spiral");
    refused(json::parse(R"({"layers": [{"family": "rosette", "colour": 1}]})"), "colour");
    refused(json::parse(R"({"layers": []})"), "layers");
    refused(json::parse(R"({"depth": 0.3})"), "layers");
    refused(json::parse(R"({"layers": [{"family": "engine", "axis": [0, 0, 0]}]})"), "axis");
    refused(json::parse(R"({"layers": [{"family": "rosette", "inner": 0.5, "outer": 0.2}]})"), "outer");
    refused(json::parse(R"({"layers": [{"family": "rosette", "frequency": 0}]})"), "frequency");

    // An SDF object meshed for the entity path cannot be engraved: refused, not silently plain.
    json meshed = engravedSdf();
    meshed["renderMode"] = "mesh";
    auto m = scene::SdfObject::fromJson(meshed);
    if (m) {
        auto ok = m->validate();
        REQUIRE_FALSE(ok);
        CHECK(ok.error().message.find("engraving") != std::string::npos);
    } else {
        CHECK(m.error().message.find("engraving") != std::string::npos);
    }
    // Without the block the material writes no key.
    json plain = engravedSdf();
    plain["material"].erase("engraving");
    auto p = scene::SdfObject::fromJson(plain);
    REQUIRE(p);
    CHECK_FALSE(p->toJson().at("material").contains("engraving"));
}

TEST_CASE("an engraving on an owner that cannot draw it is refused by name", "[engraving][adr1152]") {
    json proc = json::parse(R"({"source": {"kind": "sphere", "radius": 1.0}, "distribution": {"kind": "single"},
        "material": {"baseColor": [1, 1, 1], "engraving": {"layers": [{"family": "rosette"}]}}})");
    auto r = scene::ProceduralGeometry::fromJson(proc);
    REQUIRE_FALSE(r);
    CHECK(r.error().message.find("SDF objects only") != std::string::npos);
}

TEST_CASE("an engraving's knobs are parameters, registered only when it is authored", "[engraving][adr1152][params]") {
    auto o = scene::SdfObject::fromJson(engravedSdf());
    REQUIRE(o);
    params::ParameterSet params;
    auto p = scene::registerSdfParameters(params, *o, "sdf/plate/");
    for (const char* path : {"sdf/plate/material/engraving/depth", "sdf/plate/material/engraving/crawl",
                             "sdf/plate/material/engraving/grating", "sdf/plate/material/engraving/spacing",
                             "sdf/plate/material/engraving/panels", "sdf/plate/material/engraving/0/frequency",
                             "sdf/plate/material/engraving/2/weight"}) {
        INFO(path);
        CHECK(params.find(path) != nullptr);
    }
    auto* weight = params.findAs<float>("sdf/plate/material/engraving/2/weight");
    REQUIRE(weight != nullptr);
    weight->setBase(0.75f);
    params.resetFinals();
    scene::SdfObject live = *o;
    scene::applySdfParameters(p, *o, live);
    CHECK(live.material.engraving.layers[2].weight == Approx(0.75f));
    scene::unregisterSdfParameters(params, p);

    json plain = engravedSdf();
    plain["material"].erase("engraving");
    auto q = scene::SdfObject::fromJson(plain);
    REQUIRE(q);
    params::ParameterSet p2;
    auto pp = scene::registerSdfParameters(p2, *q, "sdf/plate/");
    CHECK(p2.find("sdf/plate/material/engraving/depth") == nullptr);
    scene::unregisterSdfParameters(p2, pp);
}

TEST_CASE("the engraving's surface coordinates: rosette angle and radius, contour shells, engine lines",
          "[engraving][adr1152]") {
    scene::EngravingLayer rosette;
    rosette.center = glm::vec3(1.0f, 0.0f, 0.0f);
    rosette.inner = 0.1f;
    rosette.frequency = 20.0f;
    const auto a = scene::engravingUv(rosette, glm::vec3(1.0f, 0.5f, 0.3f), 0.0f);
    CHECK(a.angular);
    CHECK(a.u == Approx(0.5f * 3.14159265f).epsilon(1e-5));
    CHECK(a.v == Approx(0.5f));
    CHECK(a.amp == Approx(1.0f)); // past inner + 6 line spacings
    CHECK(scene::engravingUv(rosette, glm::vec3(1.0f, 0.05f, 0.0f), 0.0f).amp == 0.0f); // the polished centre
    // The crawl turns the rosette.
    CHECK(scene::engravingUv(rosette, glm::vec3(1.0f, 0.5f, 0.3f), 0.25f).u == Approx(a.u + 0.25f));

    scene::EngravingLayer contour;
    contour.family = scene::EngravingFamily::Contour;
    contour.center = glm::vec3(0.0f, 0.0f, -3.0f);
    const auto c = scene::engravingUv(contour, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
    CHECK(c.v == Approx(std::sqrt(10.0f)));

    scene::EngravingLayer engine;
    engine.family = scene::EngravingFamily::Engine;
    engine.axis = glm::vec3(2.0f, 0.0f, 0.0f);
    const auto e = scene::engravingUv(engine, glm::vec3(0.7f, 0.2f, 0.0f), 0.0f);
    CHECK_FALSE(e.angular);
    CHECK(e.v == Approx(0.7f)); // the distance along the (normalised) axis
    CHECK(e.u == Approx(0.6f)); // 3 x the distance along the perpendicular (+Y here)
}

// ---- ADR-1153: glint flakes ---------------------------------------------------------------------------

TEST_CASE("a flake system reads and writes its flake block, and only a flake system may carry one",
          "[particles][flake][adr1153]") {
    json j = json::parse(R"({"capacity": 1024, "shape2d": "flake",
        "flake": {"metal": [0.5, 0.5, 0.55], "temper": 45, "glint": 0.02, "tumble": 0.6, "free": 0.15,
                  "bound": 0.9, "latentNormal": 0.8, "sparkle": 0.01, "sparkleGain": 0.4}})");
    auto s = scene::particlesFromJson(j);
    INFO((s ? std::string() : s.error().message));
    REQUIRE(s);
    CHECK(s->shape2d == scene::ParticleShape::Flake);
    CHECK(s->flake.temper == Approx(45.0f));
    CHECK(s->flake.latentNormal == Approx(0.8f));
    const json written = scene::particlesToJson(*s);
    CHECK(written.at("shape2d") == "flake");
    auto again = scene::particlesFromJson(written);
    REQUIRE(again);
    CHECK(scene::particlesToJson(*again) == written);

    json noShape = j;
    noShape.erase("shape2d");
    auto r1 = scene::particlesFromJson(noShape);
    REQUIRE_FALSE(r1);
    CHECK(r1.error().message.find("shape2d") != std::string::npos);
    json badKey = j;
    badKey["flake"]["shine"] = 1;
    auto r2 = scene::particlesFromJson(badKey);
    REQUIRE_FALSE(r2);
    CHECK(r2.error().message.find("shine") != std::string::npos);
    json badRange = j;
    badRange["flake"]["latentNormal"] = 2;
    REQUIRE_FALSE(scene::particlesFromJson(badRange));
    // A round system writes no flake block.
    auto round = scene::particlesFromJson(json::parse(R"({"capacity": 16})"));
    REQUIRE(round);
    CHECK_FALSE(scene::particlesToJson(*round).contains("flake"));
}

TEST_CASE("a flake system's knobs are parameters; other systems do not get them", "[particles][flake][adr1153][params]") {
    json doc = {{"format", "avgen-scene"}, {"version", 1}, {"name", "flakes"},
                {"nodes", json::array({json{{"name", "dust"}, {"kind", "particles"},
                                            {"particles", json::parse(R"({"capacity": 64, "shape2d": "flake",
                                                "flake": {"temper": 40}})")}},
                                       json{{"name", "plain"}, {"kind", "particles"}, {"particles", json::object()}}})}};
    auto comp = load(doc);
    INFO((comp ? std::string() : comp.error().message));
    REQUIRE(comp);
    params::ParameterSet parameters;
    params::Modulator modulator;
    (*comp)->attach(parameters, modulator);
    for (const char* path : {"particles/dust/flake/temper", "particles/dust/flake/glint", "particles/dust/flake/tumble",
                             "particles/dust/flake/free", "particles/dust/flake/bound", "particles/dust/flake/sparkle"}) {
        INFO(path);
        CHECK(parameters.find(path) != nullptr);
    }
    CHECK(parameters.find("particles/plain/flake/temper") == nullptr);
    auto* temper = parameters.findAs<float>("particles/dust/flake/temper");
    REQUIRE(temper != nullptr);
    CHECK(temper->value() == Approx(40.0f));
    temper->setBase(70.0f);
    temper->resetFinal();
    (*comp)->update({});
    CHECK((*comp)->scene().particles[0].flake.temper == Approx(70.0f));
}
