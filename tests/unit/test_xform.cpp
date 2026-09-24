// XFORM (Effect Library Wave 2): the render-transform offset layer and its five types.
//
// What is proved here, on the CPU, through the engine's own frame (`Engine::update`: the Geometry
// stage before the flatten, the flatten composing the offset into the owner's transform):
//   * the owner is drawn offset and its CHILD follows it (the reason XFORM runs before the flatten);
//   * the offset is visual-only: `nodeWorldTransform` (the simulation's, the rigs', HIST's) is not
//     moved, and HIST records the pre-offset path;
//   * the effects that read the drawn view (`nodeView`) see the offset;
//   * with no live XFORM instance the flatten's matrices are bit-identical;
//   * offsets compose in stack order: translations commute, rotations do not;
//   * a keyed owner with Float + Orbit stacked is the same played and scrubbed, bit for bit;
//   * Shake decays over its activation's age; Bounce preserves volume.
// The velocity target (`prevModel`) is proved on pixels in tests/rendering/test_xform_gpu.cpp.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "scene/composition.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/transform_frame.hpp"

#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace avgen;

namespace {

void frameAt(app::Engine& engine, long long frame) {
    engine.update(FrameTime{static_cast<double>(frame) / 60.0, frame == 0 ? 0.0 : 1.0 / 60.0,
                            static_cast<std::uint64_t>(frame)});
}

world::EffectInstance motion(world::EffectKind kind, const std::string& owner, const char* style = nullptr) {
    world::EffectInstance e = world::makeEffect(kind, world::effectSchema(kind)->displayName);
    e.id.clear();
    e.owner = world::EffectOwner::entity(owner);
    if (style != nullptr) {
        REQUIRE(world::applyEffectStyle(e, kind, style));
    }
    return e;
}

// A craft with a lamp hanging off it (a child node, 2 m to its side), and an unrelated rock.
void installCraftAndLamp(app::Engine& engine) {
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(R"({ "format": "avgen-scene", "version": 1,
        "name": "xform", "nodes": [
          { "kind": "orb", "name": "craft", "position": [0, 5, 0] },
          { "kind": "orb", "name": "lamp", "parent": "craft", "position": [2, 0, 0] },
          { "kind": "orb", "name": "rock", "position": [-6, 1, 3] } ] })"))
                .has_value());
}

std::vector<std::string> install(app::Engine& engine, std::vector<world::EffectInstance> effects) {
    std::vector<world::EffectInstance> list;
    for (world::EffectInstance& e : effects) {
        REQUIRE(world::insertEffect(list, std::move(e)).has_value());
    }
    REQUIRE(engine.setEffects(list).has_value());
    std::vector<std::string> ids;
    for (const world::EffectInstance& e : engine.effects()) {
        ids.push_back(e.id);
    }
    return ids;
}

glm::mat4 drawn(const app::Engine& engine, const char* node) {
    world::NodeView view;
    REQUIRE(engine.composition()->nodeView(node, view));
    return view.world;
}

scene::Transform simulated(const app::Engine& engine, const char* node) {
    const scene::CompositionNode* n = engine.composition()->findNode(node);
    REQUIRE(n != nullptr);
    return engine.composition()->nodeWorldTransform(*n);
}

glm::vec3 translation(const glm::mat4& m) { return glm::vec3(m[3]); }

bool sameBytes(const glm::mat4& a, const glm::mat4& b) { return std::memcmp(&a, &b, sizeof(glm::mat4)) == 0; }

// Every drawn entity matrix of the scene, as bytes.
std::vector<glm::mat4> entityMatrices(const app::Engine& engine) {
    std::vector<glm::mat4> out;
    for (const scene::Entity& e : engine.scene().entities) {
        out.push_back(e.transform.matrix());
    }
    return out;
}

} // namespace

TEST_CASE("XFORM: the owner is drawn offset, its child follows, and the simulation does not move",
          "[xform][effects]") {
    app::Engine engine(app::EngineMode::Offline);
    installCraftAndLamp(engine);
    world::EffectInstance orbit = motion(world::EffectKind::Orbit, "craft");
    orbit.values.setFloat("orbit/radius", 3.0f);
    orbit.values.setFloat("orbit/face", 1.0f); // turn with the orbit, so the child swings round too
    orbit.values.setFloat("orbit/tilt", 0.0f); // the factory's Satellite look tilts it 35 degrees
    const auto ids = install(engine, {orbit});
    for (long long f = 0; f <= 150; ++f) {
        frameAt(engine, f);
    }
    CHECK(engine.effectStatus(ids[0]) == world::EffectStatus::Drawn);

    // Visual-only: where the simulation, the rigs and HIST think the craft is has not moved.
    CHECK(simulated(engine, "craft").position == glm::vec3(0.0f, 5.0f, 0.0f));

    // Drawn: on the circle, 3 m from its authored position, in the horizontal plane.
    const glm::mat4 craft = drawn(engine, "craft");
    const glm::vec3 d = translation(craft) - glm::vec3(0.0f, 5.0f, 0.0f);
    CHECK(glm::length(d) == Catch::Approx(3.0f).margin(1e-4));
    CHECK(d.y == Catch::Approx(0.0f).margin(1e-5));

    // The child: exactly where the drawn craft's matrix puts its local (2, 0, 0) -- it went round
    // with the craft, rather than staying beside the authored spot.
    const glm::mat4 lamp = drawn(engine, "lamp");
    const glm::vec3 expected = glm::vec3(craft * glm::vec4(2.0f, 0.0f, 0.0f, 1.0f));
    INFO("lamp " << translation(lamp).x << "," << translation(lamp).y << "," << translation(lamp).z << " expected "
                 << expected.x << "," << expected.y << "," << expected.z);
    CHECK(glm::length(translation(lamp) - expected) < 1e-4f);
    CHECK(glm::length(translation(lamp) - glm::vec3(2.0f, 5.0f, 0.0f)) > 1.0f);

    // The entities the renderer draws (and takes `prevModel` from) are the drawn ones.
    world::NodeView view;
    REQUIRE(engine.composition()->nodeView("lamp", view));
    REQUIRE(view.entityCount >= 1);
    const glm::vec3 entityAt = engine.scene().entities[view.firstEntity].transform.position;
    CHECK(glm::length(entityAt - expected) < 1e-3f);
}

TEST_CASE("XFORM: with no live instance the flatten's matrices are bit-identical", "[xform][effects][gate]") {
    const auto run = [](std::vector<world::EffectInstance> effects) {
        app::Engine engine(app::EngineMode::Offline);
        installCraftAndLamp(engine);
        if (!effects.empty()) {
            install(engine, std::move(effects));
        }
        for (long long f = 0; f <= 90; ++f) {
            frameAt(engine, f);
        }
        return entityMatrices(engine);
    };
    const std::vector<glm::mat4> none = run({});
    REQUIRE(none.size() >= 3);

    // A disabled Float on the craft is not a live instance: nothing moves, to the bit.
    world::EffectInstance off = motion(world::EffectKind::Float, "craft");
    off.enabled = false;
    const std::vector<glm::mat4> disabled = run({off});
    REQUIRE(disabled.size() == none.size());
    for (std::size_t i = 0; i < none.size(); ++i) {
        INFO("entity " << i);
        CHECK(sameBytes(none[i], disabled[i]));
    }

    // A live Float on the ROCK moves the rock and nothing else, to the bit.
    const std::vector<glm::mat4> rockOnly = run({motion(world::EffectKind::Float, "rock")});
    std::size_t moved = 0;
    for (std::size_t i = 0; i < none.size(); ++i) {
        moved += sameBytes(none[i], rockOnly[i]) ? 0u : 1u;
    }
    CHECK(moved == 1); // the control: the offset is real, and it is the rock's alone

    // And the composition's own answer: with an empty frame the drawn transform IS the simulated one.
    app::Engine engine(app::EngineMode::Offline);
    installCraftAndLamp(engine);
    frameAt(engine, 0);
    for (const auto& node : engine.composition()->nodes()) {
        const scene::Transform a = engine.composition()->nodeWorldTransform(*node);
        const scene::Transform b = engine.composition()->nodeDrawnWorldTransform(*node);
        INFO(node->name);
        CHECK(std::memcmp(&a, &b, sizeof(scene::Transform)) == 0);
    }
}

TEST_CASE("XFORM: offsets compose in stack order -- translations commute, rotations do not",
          "[xform][effects][stack]") {
    world::EffectInstance orbit = motion(world::EffectKind::Orbit, "craft", "Guardian Orb"); // turns and banks
    orbit.id = "orbit";
    world::EffectInstance bob = motion(world::EffectKind::Float, "craft", "Buoy"); // rocks
    bob.id = "float";
    const std::vector<world::EffectInstance> effects{orbit, bob};
    world::EffectContext ctx;
    ctx.seconds = 3.3;

    world::TransformFrame ab;
    world::TransformFrame ba;
    std::vector<world::EffectStatus> status(2, world::EffectStatus::Dormant);
    std::vector<std::string> reasons(2);
    const std::vector<std::uint32_t> orderAB{0, 1};
    const std::vector<std::uint32_t> orderBA{1, 0};
    world::buildTransformFrame(effects, ctx, ab, orderAB, status, reasons);
    world::buildTransformFrame(effects, ctx, ba, orderBA, status, reasons);
    REQUIRE(ab.count == 1);
    REQUIRE(ba.count == 1);
    CHECK(status[0] == world::EffectStatus::Drawn);
    CHECK(status[1] == world::EffectStatus::Drawn);

    // Translations add, and IEEE addition of two terms commutes exactly.
    CHECK(ab.offsets[0].translation == ba.offsets[0].translation);
    // Rotations compose in stack order and do not commute: the two orders are measurably different.
    const float angle = glm::angle(glm::inverse(ab.offsets[0].localRotation) * ba.offsets[0].localRotation);
    INFO("angle between the two stack orders: " << angle);
    CHECK(angle > 1e-3f);

    // And the rule itself: top of the stack outermost.
    world::TransformContribution co;
    world::TransformContribution cf;
    REQUIRE(world::transformProducer(world::EffectKind::Orbit)->produce(orbit, ctx, ctx.seconds, co));
    REQUIRE(world::transformProducer(world::EffectKind::Float)->produce(bob, ctx, ctx.seconds, cf));
    const glm::quat want = glm::normalize(co.rotation * cf.rotation);
    CHECK(glm::angle(glm::inverse(want) * ab.offsets[0].localRotation) < 1e-5f);

    SECTION("through the engine: reordering the stack turns the craft differently in the same place") {
        const auto drawnWith = [](std::vector<world::EffectInstance> list) {
            app::Engine engine(app::EngineMode::Offline);
            installCraftAndLamp(engine);
            for (auto& e : list) {
                e.id.clear();
            }
            install(engine, std::move(list));
            for (long long f = 0; f <= 200; ++f) {
                frameAt(engine, f);
            }
            return drawn(engine, "craft");
        };
        const glm::mat4 first = drawnWith({orbit, bob});
        const glm::mat4 second = drawnWith({bob, orbit});
        CHECK(glm::length(translation(first) - translation(second)) < 1e-5f);
        const glm::quat qa = glm::quat_cast(glm::mat3(first));
        const glm::quat qb = glm::quat_cast(glm::mat3(second));
        CHECK(glm::angle(glm::inverse(qa) * qb) > 1e-3f);
    }
}

namespace {

// `installFlownCraft` (test_ufo_stack_demo.cpp), with the craft carrying Float and Orbit stacked, a
// lamp hanging off it, and a Trail reading its history -- motion the Engine's seek replays exactly.
void installFlownFloatingCraft(app::Engine& engine) {
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(R"({ "format": "avgen-scene", "version": 1,
        "name": "flown", "nodes": [
          { "kind": "orb", "name": "craft", "position": [0, 5, 0] },
          { "kind": "orb", "name": "lamp", "parent": "craft", "position": [0, -2, 1] } ] })"))
                .has_value());
    install(engine, {motion(world::EffectKind::Float, "craft", "Dream Float"),
                     motion(world::EffectKind::Orbit, "craft", "Guardian Orb"),
                     motion(world::EffectKind::Trail, "craft", "UFO Wake")});
    for (const world::EffectInstance& e : engine.effects()) {
        engine.addDefaultEffectRoutes(e.id);
    }
    params::Track fly;
    fly.target = "nodes/craft/position";
    fly.keys.push_back(params::Key{.time = 0.0, .value = {0.0f, 5.0f, 0.0f, 0.0f}});
    fly.keys.push_back(params::Key{.time = 4.0, .value = {30.0f, 9.0f, -10.0f, 0.0f}});
    fly.keys.push_back(params::Key{.time = 8.0, .value = {-20.0f, 6.0f, 25.0f, 0.0f}});
    engine.timeline().addTrack(fly);
    REQUIRE(engine.timeline().bind(engine.params()).has_value());
}

} // namespace

TEST_CASE("XFORM: Float + Orbit on a keyed owner are the same played and scrubbed",
          "[xform][effects][seek][determinism]") {
    constexpr long long kTarget = 330; // 5.5 s, mid-flight
    app::Engine played(app::EngineMode::Offline);
    installFlownFloatingCraft(played);
    for (long long f = 0; f <= kTarget + 1; ++f) {
        frameAt(played, f);
    }
    app::Engine scrubbed(app::EngineMode::Offline);
    installFlownFloatingCraft(scrubbed);
    frameAt(scrubbed, 0);
    scrubbed.seekSeconds(static_cast<double>(kTarget) / 60.0);
    frameAt(scrubbed, kTarget + 1);

    for (const world::EffectInstance& e : played.effects()) {
        INFO(e.id);
        CHECK(played.effectStatus(e.id) == world::EffectStatus::Drawn);
        CHECK(scrubbed.effectStatus(e.id) == world::EffectStatus::Drawn);
    }
    // The control: the offset is live -- the craft is drawn well away from where it is simulated.
    const glm::vec3 sim = simulated(played, "craft").position;
    REQUIRE(glm::length(translation(drawn(played, "craft")) - sim) > 0.5f);

    // The drawn matrices, bit for bit: the owner, its child, every entity the renderer will draw.
    CHECK(sameBytes(drawn(played, "craft"), drawn(scrubbed, "craft")));
    CHECK(sameBytes(drawn(played, "lamp"), drawn(scrubbed, "lamp")));
    const auto a = entityMatrices(played);
    const auto b = entityMatrices(scrubbed);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        INFO("entity " << i);
        CHECK(sameBytes(a[i], b[i]));
    }
    // The trail on top of it, too.
    const auto& ra = played.scene().ribbons;
    const auto& rb = scrubbed.scene().ribbons;
    REQUIRE_FALSE(ra.vertices.empty());
    REQUIRE(ra.vertices.size() == rb.vertices.size());
    CHECK(std::memcmp(ra.vertices.data(), rb.vertices.data(), ra.vertices.size() * sizeof(ra.vertices[0])) == 0);

    // HIST records the PRE-offset path (visual-only offsets; the replay does not run effects), so its
    // newest sample is where the craft is simulated, not where it is drawn.
    const world::HistoryBank& hist = played.historyBank();
    const std::size_t ring = hist.find("craft");
    REQUIRE(ring < hist.ringCount());
    REQUIRE(hist.sampleCount(ring) > 0);
    const glm::vec3 newest = hist.sample(ring, hist.sampleCount(ring) - 1).position;
    CHECK(glm::length(newest - sim) < 1e-4f);
}

TEST_CASE("XFORM: Shake settles over its activation's age; continuous Shake does not",
          "[xform][effects][shake]") {
    world::EffectInstance impact = motion(world::EffectKind::Shake, "craft", "Impact");
    impact.id = "impact";
    impact.activation = world::Activation::Window;
    impact.timing.windowStart = 1.0;
    impact.timing.windowSeconds = 10.0;
    impact.timing.fadeIn = 0.0;
    impact.timing.fadeOut = 0.0;
    const world::TransformProducer* shake = world::transformProducer(world::EffectKind::Shake);
    REQUIRE(shake != nullptr);

    world::EffectContext ctx;
    ctx.seconds = 0.5;
    CHECK(world::transformGate(impact, ctx).envelope == 0.0f); // before the window: nothing
    CHECK(world::transformRecords(impact, ctx) == 0);

    // The age is `t - t0`, with t0 the window's start.
    ctx.seconds = 1.25;
    const world::TransformGate gate = world::transformGate(impact, ctx);
    CHECK(gate.age == Catch::Approx(0.25));
    // Peak |offset| over a short span early in the pass and late in it: the late one has decayed.
    const auto peak = [&](double from) {
        float most = 0.0f;
        for (int i = 0; i < 60; ++i) {
            world::EffectContext c;
            c.seconds = from + i / 120.0;
            const world::TransformGate g = world::transformGate(impact, c);
            world::TransformContribution out;
            if (shake->produce(impact, c, g.age, out)) {
                most = std::max(most, glm::length(out.translation));
            }
        }
        return most;
    };
    const float early = peak(1.0);
    const float late = peak(3.0);
    INFO("early peak " << early << " m, late peak " << late << " m");
    CHECK(early > 0.05f);
    CHECK(late < early * 0.01f);

    // Frame-rate independence: the same second is the same displacement whatever came before it.
    world::TransformContribution x;
    world::TransformContribution y;
    ctx.seconds = 1.4;
    REQUIRE(shake->produce(impact, ctx, world::transformGate(impact, ctx).age, x));
    REQUIRE(shake->produce(impact, ctx, world::transformGate(impact, ctx).age, y));
    CHECK(x.translation == y.translation);

    // Rumble (decay 0) keeps shaking.
    world::EffectInstance rumble = motion(world::EffectKind::Shake, "craft", "Rumble");
    rumble.id = "rumble";
    world::EffectContext far;
    far.seconds = 400.0;
    world::TransformContribution r;
    REQUIRE(shake->produce(rumble, far, world::transformGate(rumble, far).age, r));
    CHECK(glm::length(r.translation) > 0.0f);
}

TEST_CASE("XFORM: Bounce squashes on contact and stretches in flight, preserving volume",
          "[xform][effects][bounce]") {
    world::EffectInstance hop = motion(world::EffectKind::Bounce, "craft", "Beat Hop");
    hop.id = "hop";
    const world::TransformProducer* bounce = world::transformProducer(world::EffectKind::Bounce);
    REQUIRE(bounce != nullptr);
    float minY = 10.0f;
    float maxY = 0.0f;
    for (int i = 0; i < 120; ++i) {
        world::EffectContext ctx;
        ctx.seconds = 10.0 + i / 240.0;
        world::TransformContribution out;
        REQUIRE(bounce->produce(hop, ctx, 0.0, out));
        INFO("t " << ctx.seconds);
        CHECK(out.scale.x * out.scale.y * out.scale.z == Catch::Approx(1.0f).margin(1e-4));
        CHECK(out.translation.y >= -1e-6f);
        minY = std::min(minY, out.scale.y);
        maxY = std::max(maxY, out.scale.y);
    }
    CHECK(minY < 0.85f); // squashed at some contact
    CHECK(maxY > 1.05f); // stretched in some flight
}
