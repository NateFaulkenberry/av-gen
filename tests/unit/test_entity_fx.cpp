// FXL (per-entity effect lanes) and LIGHTMOD's pool, on the CPU (Effect Library Wave 1, 1.4/1.5/1.9).
//
// What the builder has to get right without a GPU:
//
//   * the §7 fold rules on one owner -- gains multiply, added emission and rims sum, tints multiply,
//     bloom share takes the max -- whatever order the effects were listed in;
//   * one record per owner, and exactly the owner's drawn range pointed at it: nothing outside it;
//   * the exclusive band sub-block, refused by name for the second holder;
//   * every status the builder owns (Disabled, Dormant, Drawn, Dropped, Partial) with its reason;
//   * activation and timing honoured by the generic envelope, and a pulse that is a function of
//     the second (the same played or scrubbed);
//   * the 16-light pool: the 17th spill loses its light, keeps its glow, and says Partial and why;
//   * the new types attach to entities and to nothing else.
//
// Each invariant guard here was broken in src/ and seen red before it was trusted (ADR-182); the
// report of the change that added this file says which.

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_lights.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/entity_fx.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;
using world::EffectKind;
using world::EffectOwner;
using world::EffectStatus;

namespace {

// A scene of named nodes, each with a range in `scene.entities` and a box, standing still.
class FakeScene final : public world::EffectSceneQuery {
public:
    struct Node {
        std::uint32_t first = 0;
        std::uint32_t count = 1;
        glm::vec3 centre{0.0f};
        float half = 1.0f;
        std::uint32_t firstProcedural = 0; // a procedural node: its range in scene.procedurals
        std::uint32_t proceduralCount = 0;
    };
    std::map<std::string, Node, std::less<>> nodes;

    [[nodiscard]] bool nodePosition(std::string_view name, glm::vec3& out) const override {
        const auto it = nodes.find(name);
        if (it == nodes.end()) {
            return false;
        }
        out = it->second.centre;
        return true;
    }
    [[nodiscard]] bool nodeView(std::string_view name, world::NodeView& out) const override {
        const auto it = nodes.find(name);
        if (it == nodes.end()) {
            return false;
        }
        out = world::NodeView{};
        out.world = glm::mat4(1.0f);
        out.world[3] = glm::vec4(it->second.centre, 1.0f);
        out.boundsMin = it->second.centre - glm::vec3(it->second.half);
        out.boundsMax = it->second.centre + glm::vec3(it->second.half);
        out.hasBounds = it->second.count > 0 || it->second.proceduralCount > 0;
        out.firstEntity = it->second.first;
        out.entityCount = it->second.count;
        out.firstProcedural = it->second.firstProcedural;
        out.proceduralCount = it->second.proceduralCount;
        return true;
    }
};

world::EffectInstance make(EffectKind kind, const std::string& owner, const std::string& id) {
    world::EffectInstance e = world::makeEffect(kind, id);
    e.id = id;
    e.owner = EffectOwner::entity(owner);
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    return e;
}

struct Built {
    world::EntityFxFrame frame;
    std::vector<EffectStatus> status;
    std::vector<std::string> reasons;
};

Built build(const std::vector<world::EffectInstance>& effects, const FakeScene& scene, double seconds = 1.0,
            glm::vec3 camera = glm::vec3(0.0f, 0.0f, 50.0f)) {
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.scene = &scene;
    ctx.cameraPosition = camera;
    Built b;
    b.status.assign(effects.size(), EffectStatus::Dormant);
    b.reasons.assign(effects.size(), std::string());
    world::buildEntityFxFrame(effects, ctx, b.frame, {}, b.status, b.reasons);
    return b;
}

const glm::vec4& lane(const Built& b, std::size_t entity, world::EntityFxLaneIndex l) {
    const std::uint32_t r = b.frame.recordFor(entity);
    REQUIRE(r != 0);
    REQUIRE(r < b.frame.records.size());
    return b.frame.records[r].lanes[l];
}

FakeScene twoOwners() {
    FakeScene s;
    s.nodes["rook"] = {0, 1, glm::vec3(-5.0f, 0.0f, 0.0f), 1.0f};
    s.nodes["ufo"] = {3, 2, glm::vec3(5.0f, 2.0f, 0.0f), 1.5f};
    return s;
}

} // namespace

TEST_CASE("FXL fold rules: two Glows on one owner multiply gains and sum their rims", "[effects][fxl]") {
    const FakeScene scene = twoOwners();
    std::vector<world::EffectInstance> effects{make(EffectKind::Glow, "ufo", "g1"), make(EffectKind::Glow, "ufo", "g2")};
    effects[0].values.setFloat("glow/gain", 2.0f);
    effects[0].values.setFloat("glow/glow", 1.0f);
    effects[0].values.setColor("glow/tint", {1.0f, 0.0f, 0.0f});
    effects[0].values.setFloat("glow/rim", 3.0f);
    effects[0].values.setColor("glow/rimColor", {1.0f, 1.0f, 1.0f});
    effects[1].values.setFloat("glow/gain", 3.0f);
    effects[1].values.setFloat("glow/glow", 2.0f);
    effects[1].values.setColor("glow/tint", {0.0f, 0.0f, 1.0f});
    effects[1].values.setFloat("glow/rim", 5.0f);
    effects[1].values.setColor("glow/rimColor", {1.0f, 1.0f, 1.0f});

    const Built b = build(effects, scene);
    CHECK(b.status[0] == EffectStatus::Drawn);
    CHECK(b.status[1] == EffectStatus::Drawn);
    // One owner, one record (plus the neutral record 0), shared by both of its entities.
    REQUIRE(b.frame.records.size() == 2);
    CHECK(b.frame.recordFor(3) == 1);
    CHECK(b.frame.recordFor(4) == 1);

    const glm::vec4 a = lane(b, 3, world::kFxLaneA);
    CHECK(a.x == Approx(6.0f)); // gains multiply: 2 x 3
    CHECK(a.w == Approx(1.0f)); // the record knows its own index
    const glm::vec4 add = lane(b, 3, world::kFxLaneAdd);
    CHECK(add.r == Approx(1.0f)); // added emission sums: red 1 + blue 2
    CHECK(add.b == Approx(2.0f));
    const glm::vec4 rim = lane(b, 3, world::kFxLaneRim);
    CHECK(rim.g == Approx(8.0f)); // rims sum: 3 + 5

    SECTION("the result does not depend on which was listed first") {
        std::vector<world::EffectInstance> swapped{effects[1], effects[0]};
        const Built c = build(swapped, scene);
        for (int l = 0; l < 4; ++l) {
            const glm::vec4 x = lane(b, 3, static_cast<world::EntityFxLaneIndex>(l));
            const glm::vec4 y = lane(c, 3, static_cast<world::EntityFxLaneIndex>(l));
            CHECK(x.x == Approx(y.x));
            CHECK(x.y == Approx(y.y));
            CHECK(x.z == Approx(y.z));
            CHECK(x.w == Approx(y.w));
        }
    }
}

TEST_CASE("FXL fold: a neutral contribution changes nothing, and the rules are the §7 table", "[effects][fxl]") {
    world::EntityLaneContribution into;
    into.gain = 2.0f;
    into.ownTint = {0.5f, 1.0f, 1.0f};
    into.add = {1.0f, 0.0f, 0.0f};
    into.rim = {1.0f, 1.0f, 1.0f};
    into.rimPower = 2.0f;
    into.bloomShare = 0.3f;
    const world::EntityLaneContribution before = into;
    world::foldEntityLanes(into, world::EntityLaneContribution{});
    CHECK(into.gain == before.gain);
    CHECK(into.ownTint == before.ownTint);
    CHECK(into.add == before.add);
    CHECK(into.rim == before.rim);
    CHECK(into.rimPower == Approx(before.rimPower));
    CHECK(into.bloomShare == before.bloomShare);

    world::EntityLaneContribution c;
    c.gain = 0.5f;
    c.ownTint = {1.0f, 0.5f, 1.0f};
    c.bloomShare = 0.8f;
    c.rim = {3.0f, 3.0f, 3.0f};
    c.rimPower = 6.0f;
    world::foldEntityLanes(into, c);
    CHECK(into.gain == Approx(1.0f));             // multiply
    CHECK(into.ownTint.x == Approx(0.5f));        // multiply
    CHECK(into.ownTint.y == Approx(0.5f));
    CHECK(into.bloomShare == Approx(0.8f));       // max
    CHECK(into.rim.x == Approx(4.0f));            // sum
    CHECK(into.rimPower == Approx(5.0f));         // strength-weighted: (1*2 + 3*6) / 4
}

TEST_CASE("FXL: each owner's record covers its own drawn range and nothing else", "[effects][fxl]") {
    const FakeScene scene = twoOwners();
    std::vector<world::EffectInstance> effects{make(EffectKind::Glow, "ufo", "a"), make(EffectKind::Glow, "rook", "b")};
    effects[0].values.setFloat("glow/gain", 4.0f);
    effects[1].values.setFloat("glow/gain", 7.0f);
    const Built b = build(effects, scene);
    REQUIRE(b.frame.records.size() == 3);
    CHECK(b.frame.recordFor(0) != 0);                       // rook
    CHECK(b.frame.recordFor(1) == 0);                       // between the two ranges
    CHECK(b.frame.recordFor(2) == 0);
    CHECK(b.frame.recordFor(3) != 0);                       // ufo
    CHECK(b.frame.recordFor(4) == b.frame.recordFor(3));
    CHECK(b.frame.recordFor(5) == 0);                       // past the last range
    CHECK(b.frame.recordFor(99) == 0);
    CHECK(b.frame.recordFor(0) != b.frame.recordFor(3));    // independent
    CHECK(lane(b, 0, world::kFxLaneA).x == Approx(7.0f));
    CHECK(lane(b, 3, world::kFxLaneA).x == Approx(4.0f));
    // The flags say "on", and record 0 stays neutral.
    CHECK(lane(b, 3, world::kFxLaneA).z != 0.0f);
    for (const glm::vec4& l : b.frame.records[0].lanes) {
        CHECK(l == glm::vec4(0.0f));
    }
}

TEST_CASE("FXL: nothing live means no records at all", "[effects][fxl][gate]") {
    const FakeScene scene = twoOwners();
    std::vector<world::EffectInstance> effects{make(EffectKind::Glow, "ufo", "a"), make(EffectKind::Pulse, "ufo", "b")};
    effects[0].enabled = false;
    effects[1].activation = world::Activation::Window;
    effects[1].timing.windowStart = 10.0;
    effects[1].timing.windowSeconds = 5.0;
    const Built before = build(effects, scene, 2.0);
    CHECK(before.frame.empty());
    CHECK(before.frame.records.empty());
    CHECK(before.frame.lights.count == 0);
    CHECK(before.status[0] == EffectStatus::Disabled);
    CHECK(before.status[1] == EffectStatus::Dormant);
    // The control: inside the window the pulse is drawn, so "empty" above was the window's doing.
    const Built during = build(effects, scene, 12.0);
    CHECK(during.status[1] == EffectStatus::Drawn);
    CHECK_FALSE(during.frame.empty());
}

TEST_CASE("FXL: the activation envelope fades a Glow towards neutral", "[effects][fxl]") {
    const FakeScene scene = twoOwners();
    std::vector<world::EffectInstance> effects{make(EffectKind::Glow, "ufo", "a")};
    effects[0].values.setFloat("glow/gain", 5.0f);
    effects[0].timing.fadeIn = 2.0;
    const float early = lane(build(effects, scene, 0.5), 3, world::kFxLaneA).x;
    const float mid = lane(build(effects, scene, 1.0), 3, world::kFxLaneA).x;
    const float late = lane(build(effects, scene, 3.0), 3, world::kFxLaneA).x;
    CHECK(early > 1.0f);
    CHECK(early < mid);
    CHECK(mid < late);
    CHECK(late == Approx(5.0f));
}

TEST_CASE("Pulse: whole-object gain follows the waveform and is a function of the second", "[effects][fxl]") {
    const FakeScene scene = twoOwners();
    std::vector<world::EffectInstance> effects{make(EffectKind::Pulse, "ufo", "p")};
    effects[0].values.setFloat("pulse/rate", 1.0f);
    effects[0].values.setFloat("pulse/peak", 4.0f);
    effects[0].values.setFloat("pulse/depth", 0.75f);
    effects[0].values.setFloat("pulse/waveform", static_cast<float>(world::FxWaveform::Sine));
    // Sine starts at its trough and crests half a cycle later.
    CHECK(lane(build(effects, scene, 3.0), 3, world::kFxLaneA).x == Approx(1.0f));  // 4 * (1 - 0.75)
    CHECK(lane(build(effects, scene, 3.5), 3, world::kFxLaneA).x == Approx(4.0f));
    CHECK(lane(build(effects, scene, 3.25), 3, world::kFxLaneA).x == Approx(2.5f)); // halfway up
    // Seek-exact: the same second twice, whatever came between, is the same record.
    const glm::vec4 x = lane(build(effects, scene, 37.37), 3, world::kFxLaneA);
    static_cast<void>(build(effects, scene, 5.0));
    const glm::vec4 y = lane(build(effects, scene, 37.37), 3, world::kFxLaneA);
    CHECK(x == y);
}

TEST_CASE("Pulse and Glow on one owner both act, and the pulse multiplies the glow", "[effects][fxl]") {
    const FakeScene scene = twoOwners();
    std::vector<world::EffectInstance> effects{make(EffectKind::Glow, "ufo", "g"), make(EffectKind::Pulse, "ufo", "p")};
    effects[0].values.setFloat("glow/gain", 2.0f);
    effects[1].values.setFloat("pulse/rate", 1.0f);
    effects[1].values.setFloat("pulse/peak", 3.0f);
    effects[1].values.setFloat("pulse/depth", 1.0f);
    const Built crest = build(effects, scene, 0.5);
    CHECK(crest.status[0] == EffectStatus::Drawn);
    CHECK(crest.status[1] == EffectStatus::Drawn);
    CHECK(lane(crest, 3, world::kFxLaneA).x == Approx(6.0f)); // 2 x 3
    // The glow's added light stays in the record; the gain is what the pulse took to zero.
    const Built trough = build(effects, scene, 1.0);
    CHECK(lane(trough, 3, world::kFxLaneA).x == Approx(0.0f).margin(1e-5));
    CHECK(lane(trough, 3, world::kFxLaneAdd).g > 0.0f);
}

TEST_CASE("Pulse: a travelling band is an exclusive sub-block, and the second one says whose it is",
          "[effects][fxl]") {
    const FakeScene scene = twoOwners();
    std::vector<world::EffectInstance> effects{make(EffectKind::Pulse, "ufo", "climb"),
                                               make(EffectKind::Pulse, "ufo", "fall"),
                                               make(EffectKind::Pulse, "rook", "other")};
    for (auto& e : effects) {
        e.values.setFloat("pulse/mode", 1.0f); // Travelling
    }
    effects[0].name = "Climb";
    const Built b = build(effects, scene, 0.3);
    CHECK(b.status[0] == EffectStatus::Drawn);
    CHECK(b.status[1] == EffectStatus::Dropped);
    CHECK(b.reasons[1].find("Climb") != std::string::npos);
    CHECK(b.reasons[1].find("ufo") != std::string::npos);
    // A band on a DIFFERENT owner is not a conflict.
    CHECK(b.status[2] == EffectStatus::Drawn);
    CHECK(b.frame.dropped == 1);
    // The band sub-block is populated along the owner's up axis, across its box.
    const glm::vec4 axis = lane(b, 3, world::kFxLaneBandAxis);
    const float uLow = glm::dot(glm::vec3(axis), glm::vec3(5.0f, 0.5f, 0.0f)) + axis.w;  // box: y in [0.5, 3.5]
    const float uHigh = glm::dot(glm::vec3(axis), glm::vec3(5.0f, 3.5f, 0.0f)) + axis.w;
    CHECK(uLow == Approx(0.0f).margin(1e-4));
    CHECK(uHigh == Approx(1.0f).margin(1e-4));
    CHECK((static_cast<std::uint32_t>(lane(b, 3, world::kFxLaneA).z) & world::kFxBand) != 0u);
}

TEST_CASE("FXL: an owner that draws nothing is Dropped with a reason", "[effects][fxl]") {
    FakeScene scene = twoOwners();
    scene.nodes["lamp"] = {7, 0, glm::vec3(0.0f), 0.0f};
    std::vector<world::EffectInstance> effects{make(EffectKind::BloomSource, "lamp", "b")};
    const Built b = build(effects, scene);
    CHECK(b.status[0] == EffectStatus::Dropped);
    CHECK(b.reasons[0].find("lamp") != std::string::npos);
    CHECK(b.frame.empty());
}

TEST_CASE("Bloom Source: several on one owner take the maximum share", "[effects][fxl]") {
    const FakeScene scene = twoOwners();
    std::vector<world::EffectInstance> effects{make(EffectKind::BloomSource, "ufo", "a"),
                                               make(EffectKind::BloomSource, "ufo", "b")};
    effects[0].values.setFloat("bloomSource/weight", 0.3f);
    effects[1].values.setFloat("bloomSource/weight", 0.7f);
    const Built b = build(effects, scene);
    const glm::vec4 a = lane(b, 3, world::kFxLaneA);
    CHECK(a.y == Approx(0.7f));
    CHECK(a.x == Approx(1.0f)); // it does not make anything brighter
    CHECK((static_cast<std::uint32_t>(a.z) & world::kFxBloomShare) != 0u);
    CHECK((static_cast<std::uint32_t>(a.z) & world::kFxRecord) == 0u); // no added light, no rim
}

TEST_CASE("LIGHTMOD: 17 spill lights -- 16 fit, the 17th glow still draws and says why its light did not",
          "[effects][fxl][lightmod]") {
    FakeScene scene;
    std::vector<world::EffectInstance> effects;
    for (int i = 0; i < 17; ++i) {
        const std::string owner = "saucer" + std::to_string(i);
        // Further from the camera as i grows, so the ranking is by distance and the loser is the
        // farthest -- which is what "projected intensity" means with equal intensities.
        scene.nodes[owner] = {static_cast<std::uint32_t>(i), 1, glm::vec3(0.0f, 0.0f, -10.0f * static_cast<float>(i)), 1.0f};
        world::EffectInstance e = make(EffectKind::Glow, owner, "glow" + std::to_string(i));
        e.values.setBool("glow/spill", true);
        e.values.setFloat("glow/gain", 1.0f);
        effects.push_back(e);
    }
    const Built b = build(effects, scene, 1.0, glm::vec3(0.0f, 0.0f, 10.0f));
    CHECK(b.frame.lights.count == world::kEffectLightBudget);
    CHECK(b.frame.lights.dropped == 1);
    for (int i = 0; i < 16; ++i) {
        INFO("glow " << i);
        CHECK(b.status[static_cast<std::size_t>(i)] == EffectStatus::Drawn);
    }
    CHECK(b.status[16] == EffectStatus::Partial);
    CHECK(b.reasons[16].find("budget (16)") != std::string::npos);
    // It is still in the lanes: Partial is "drew, without its light".
    CHECK(b.frame.recordFor(16) != 0);
    // The kept lights are the owners' centres, in the glow's colour, reaching 3x the owner's radius.
    const world::EffectLight& first = b.frame.lights.lights[0];
    CHECK(first.position.z == Approx(0.0f));
    CHECK(first.range == Approx(3.0f * std::sqrt(3.0f)));
    CHECK(first.intensity > 0.0f);

    SECTION("with the spill off there is no light and no Partial") {
        std::vector<world::EffectInstance> quiet = effects;
        for (auto& e : quiet) {
            e.values.setBool("glow/spill", false);
        }
        const Built q = build(quiet, scene, 1.0, glm::vec3(0.0f, 0.0f, 10.0f));
        CHECK(q.frame.lights.count == 0);
        CHECK(q.status[16] == EffectStatus::Drawn);
    }
}

TEST_CASE("LIGHTMOD: the spill follows the owner's folded gain, so a Pulse pulses the light", "[effects][fxl][lightmod]") {
    const FakeScene scene = twoOwners();
    std::vector<world::EffectInstance> effects{make(EffectKind::Glow, "ufo", "g"), make(EffectKind::Pulse, "ufo", "p")};
    effects[0].values.setBool("glow/spill", true);
    effects[0].values.setFloat("glow/gain", 1.0f);
    effects[0].values.setFloat("glow/spillIntensity", 10.0f);
    effects[1].values.setFloat("pulse/rate", 1.0f);
    effects[1].values.setFloat("pulse/peak", 2.0f);
    effects[1].values.setFloat("pulse/depth", 1.0f);
    const Built crest = build(effects, scene, 0.5);
    const Built trough = build(effects, scene, 1.0);
    REQUIRE(crest.frame.lights.count == 1);
    REQUIRE(trough.frame.lights.count == 1);
    CHECK(crest.frame.lights.lights[0].intensity == Approx(20.0f));
    CHECK(trough.frame.lights.lights[0].intensity == Approx(0.0f).margin(1e-5));
}

TEST_CASE("The FXL types attach to entities and to nothing else", "[effects][fxl][targets]") {
    for (const EffectKind kind : {EffectKind::Glow, EffectKind::Pulse, EffectKind::BloomSource}) {
        const world::EffectSchema* schema = world::effectSchema(kind);
        REQUIRE(schema != nullptr);
        INFO(schema->key);
        std::vector<world::EffectInstance> effects;
        CHECK(world::addEffect(effects, EffectOwner::entity("ufo"), kind).has_value());
        CHECK_FALSE(world::addEffect(effects, EffectOwner::world(), kind).has_value());
        CHECK_FALSE(world::addEffect(effects, EffectOwner::camera(), kind).has_value());
        CHECK_FALSE(world::addEffect(effects, EffectOwner::light("key"), kind).has_value());
        CHECK(effects.size() == 1);
        CHECK(schema->resolve.bucket == world::EffectBucket::EntityLanes);
        CHECK(schema->resolve.lanes != nullptr);
        CHECK(schema->stage == world::RenderStage::Material);
    }
}

TEST_CASE("The pulse waveforms are 0 at both ends of a cycle and reach 1", "[effects][fxl]") {
    for (std::size_t w = 0; w < world::kFxWaveformCount; ++w) {
        const auto wave = static_cast<world::FxWaveform>(w);
        INFO("waveform " << w);
        CHECK(world::pulseWave(wave, 0.0f) == Approx(0.0f).margin(0.02));
        CHECK(world::pulseWave(wave, 0.999f) == Approx(0.0f).margin(0.05));
        float peak = 0.0f;
        for (int i = 0; i < 1000; ++i) {
            const float v = world::pulseWave(wave, static_cast<float>(i) / 1000.0f);
            CHECK(v >= -1e-5f);
            CHECK(v <= 1.0f + 1e-5f);
            peak = std::max(peak, v);
        }
        CHECK(peak > 0.93f);
    }
}

TEST_CASE("FXL: a procedural owner (the Glowmere saucer's kind of node) is Drawn, and its parts share the record",
          "[effects][fxl]") {
    FakeScene scene = twoOwners();
    // No scene entities at all: the node draws through scene.procedurals 2..4 (an asset and its two
    // other material parts), which is what Glowmere's `visitor` is.
    scene.nodes["visitor"] = {0, 0, glm::vec3(0.0f, 20.0f, 0.0f), 4.0f, 2, 3};
    std::vector<world::EffectInstance> effects{make(EffectKind::Glow, "visitor", "g"),
                                               make(EffectKind::Pulse, "visitor", "p")};
    effects[0].values.setFloat("glow/gain", 3.0f);
    const Built b = build(effects, scene);
    CHECK(b.status[0] == EffectStatus::Drawn);
    CHECK(b.status[1] == EffectStatus::Drawn);
    CHECK(b.reasons[0].empty());
    const std::uint32_t r = b.frame.recordForProcedural(2);
    REQUIRE(r != 0);
    CHECK(b.frame.recordForProcedural(3) == r);
    CHECK(b.frame.recordForProcedural(4) == r);
    CHECK(b.frame.recordForProcedural(1) == 0); // not this node's
    CHECK(b.frame.recordForProcedural(5) == 0);
    CHECK(b.frame.recordFor(0) == 0);           // and no scene entity picked it up
    CHECK(b.frame.records[r].lanes[world::kFxLaneA].z != 0.0f);
    // A node with neither entities nor procedurals is still the named drop.
    scene.nodes["empty"] = {0, 0, glm::vec3(0.0f), 1.0f, 0, 0};
    const Built e = build({make(EffectKind::Glow, "empty", "x")}, scene);
    CHECK(e.status[0] == EffectStatus::Dropped);
}
