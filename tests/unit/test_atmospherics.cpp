// Atmospheric effects (ADR-230): the CPU half.
//
// What this covers is the part that has to be right before a pixel is worth looking at: the data
// model, the JSON round trip, the lifecycle, the trajectory, the packing, the parameters and the
// presets. The shader half is tests/rendering/test_atmospherics_gpu.cpp.
//
// The determinism cases are the ones that matter most. ADR-091 requires an offline render of second
// N to be byte-identical to a realtime playthrough of second N, and the way that is lost is always
// the same: something integrates a frame delta instead of evaluating a function of the transport
// second. A comet's whole trajectory is the obvious candidate, so it is asserted directly -- the
// same second resolves to the same position however you arrive at it, and at whatever frame rate.

#include "assets/asset_registry.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "world/atmospheric_params.hpp"
#include "world/atmospherics.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// The direction `world::directionFromSky` builds: azimuth clockwise from +Z, elevation above the
// horizon. Recomputed here rather than exported, so the test states the convention it is asserting
// instead of borrowing the implementation's.
glm::vec3 skyDirection(float azimuthDegrees, float elevationDegrees) {
    const float az = glm::radians(azimuthDegrees);
    const float el = glm::radians(elevationDegrees);
    const float horizontal = std::cos(el);
    return glm::vec3(horizontal * std::sin(az), std::sin(el), horizontal * std::cos(az));
}

// An ordinary crossing, of the shape a scene actually authors: a quarter-turn of azimuth, entering
// high and leaving low, with no bow. Deliberately *not* a near-antipodal pair -- see the zenith case
// below for why that is a different question.
world::AtmosphericEffect plainComet(std::string name = "probe") {
    world::AtmosphericEffect e = world::bioluminescentComet(std::move(name));
    e.activation = world::Activation::Window;
    e.timing.windowStart = 0.0;
    e.timing.windowSeconds = 100.0;
    e.timing.delay = 0.0;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    e.timing.lifetime = 0.0;
    e.comet.path.anchor = world::SkyAnchor::World;
    e.comet.path.anchorPosition = glm::vec3(0.0f);
    e.comet.path.startAzimuth = 30.0f;
    e.comet.path.startElevation = 30.0f;
    e.comet.path.endAzimuth = 120.0f;
    e.comet.path.endElevation = 5.0f;
    e.comet.path.distance = 1000.0f;
    e.comet.path.travelSeconds = 10.0f;
    e.comet.path.speedScale = 1.0f;
    e.comet.path.acceleration = 0.0f;
    e.comet.path.curvature = 0.0f;
    e.comet.path.arcLift = 0.0f;
    return e;
}

world::AtmosphericContext contextAt(double seconds) {
    world::AtmosphericContext ctx;
    ctx.seconds = seconds;
    ctx.cameraPosition = glm::vec3(0.0f);
    return ctx;
}

// Resolve one effect and hand back the comet slot.
std::optional<world::ResolvedAtmospheric> resolveComet(const world::AtmosphericEffect& e, double seconds) {
    std::array<world::ResolvedAtmospheric, world::kMaxGpuComets> comets{};
    std::array<world::ResolvedAtmospheric, world::kMaxGpuAuroras> auroras{};
    const std::array<world::AtmosphericEffect, 1> set{e};
    const auto counts = world::resolveAtmosphericEffects(set, contextAt(seconds), comets, auroras);
    if (counts.comets == 0) {
        return std::nullopt;
    }
    return comets[0];
}

} // namespace

TEST_CASE("an atmospheric effect round-trips through JSON", "[world][atmospherics][json]") {
    world::AtmosphericEffect e = world::bioluminescentComet("Hero");
    e.comet.rainbow.enabled = true;
    e.comet.rainbow.scale = 2.25f;
    e.ground.mode = world::GroundGlow::Strong;
    e.ground.color = glm::vec3(0.2f, 0.7f, 0.9f);
    e.aurora.appearance.intensity = 4.5f; // the payload it is *not* using still round-trips

    const auto back = world::AtmosphericEffect::fromJson(e.toJson());
    REQUIRE(back.has_value());
    CHECK(back->name == e.name);
    CHECK(back->kind == world::AtmosphereKind::Comet);
    CHECK(back->style == e.style);
    CHECK(back->comet.rainbow.enabled);
    CHECK_THAT(back->comet.rainbow.scale, WithinAbs(2.25f, 1e-5f));
    CHECK(back->ground.mode == world::GroundGlow::Strong);
    CHECK_THAT(back->ground.color.g, WithinAbs(0.7f, 1e-5f));
    // The unused payload survives, which is the whole reason both live on one struct rather than in
    // a variant: switching an effect's kind must not throw away what the other kind was set to.
    CHECK_THAT(back->aurora.appearance.intensity, WithinAbs(4.5f, 1e-5f));
    CHECK_THAT(back->comet.appearance.tailLength, WithinAbs(e.comet.appearance.tailLength, 1e-3f));
}

TEST_CASE("an atmospheric effect keeps the defaults of everything the file omits",
          "[world][atmospherics][json]") {
    const nlohmann::json minimal = {{"name", "Sparse"}, {"kind", "aurora"}};
    const auto e = world::AtmosphericEffect::fromJson(minimal);
    REQUIRE(e.has_value());
    CHECK(e->kind == world::AtmosphereKind::Aurora);
    CHECK(e->enabled);
    const world::AtmosphericEffect defaults;
    CHECK_THAT(e->aurora.shape.radius, WithinAbs(defaults.aurora.shape.radius, 1e-3f));
    CHECK_THAT(e->aurora.audio.bass, WithinAbs(defaults.aurora.audio.bass, 1e-5f));
    CHECK(e->ground.mode == world::GroundGlow::Off);
}

TEST_CASE("an invalid atmospheric effect is refused rather than clamped", "[world][atmospherics]") {
    SECTION("a name is half of a parameter path") {
        world::AtmosphericEffect e = plainComet("bad/name");
        CHECK_FALSE(e.validate().has_value());
    }
    SECTION("a comet that would not move") {
        world::AtmosphericEffect e = plainComet();
        e.comet.path.endAzimuth = e.comet.path.startAzimuth;
        e.comet.path.endElevation = e.comet.path.startElevation;
        CHECK_FALSE(e.validate().has_value());
    }
    SECTION("an acceleration that would reverse the path") {
        world::AtmosphericEffect e = plainComet();
        // Below -0.5 the reparameterisation stops being monotone: the comet would fly backwards
        // through the second half of its own crossing.
        e.comet.path.acceleration = -0.8f;
        CHECK_FALSE(e.validate().has_value());
    }
    SECTION("more curtains than the shader's loop bound") {
        world::AtmosphericEffect e = world::glowmereAurora();
        e.aurora.shape.curtainCount = 9.0f;
        CHECK_FALSE(e.validate().has_value());
    }
    SECTION("two effects may not share a name") {
        const std::array<world::AtmosphericEffect, 2> both{plainComet("same"), plainComet("same")};
        CHECK_FALSE(world::validateAtmosphericEffects(both).has_value());
    }
}

TEST_CASE("a comet flies a great-circle arc, so its authored bearings mean what they say",
          "[world][atmospherics][trajectory]") {
    const world::AtmosphericEffect e = plainComet();

    const glm::vec3 d0 = skyDirection(30.0f, 30.0f);
    const glm::vec3 d1 = skyDirection(120.0f, 5.0f);
    const float omega = std::acos(glm::dot(d0, d1));

    const auto start = resolveComet(e, 0.0);
    REQUIRE(start.has_value());
    CHECK_THAT(start->omega, WithinRel(omega, 1e-4f));

    SECTION("the path length is the arc, not the chord") {
        // The distinction that makes a speed in metres per second mean anything. The chord between
        // these two bearings is 1383 m and the arc is 1527 m, so a resolver that measured the chord
        // would run the comet 10% slow and its tail length would be wrong by the same amount.
        CHECK_THAT(start->pathLength, WithinRel(omega * 1000.0f, 1e-4f));
        const float chord = glm::length(d1 - d0) * 1000.0f;
        CHECK(start->pathLength > chord * 1.05f);
    }

    SECTION("the distance from the anchor is constant, which is what a chord would not give") {
        // A chord between two points on a sphere passes through the interior; this is the assertion
        // that caught the first implementation, where the comet dived towards the anchor and
        // appeared 18 degrees higher than either end.
        for (const double t : {0.0, 1.0, 2.5, 5.0, 7.5, 9.9}) {
            const auto r = resolveComet(e, t);
            REQUIRE(r.has_value());
            const glm::vec3 p = world::cometPositionAt(*r, r->travelled);
            CHECK_THAT(glm::length(p - r->anchor), WithinRel(1000.0f, 1e-3f));
        }
    }

    SECTION("it starts at the launch bearing and reaches the destination") {
        const auto at0 = resolveComet(e, 0.0);
        REQUIRE(at0.has_value());
        const glm::vec3 p0 = world::cometPositionAt(*at0, at0->travelled);
        CHECK(glm::length(p0 - d0 * 1000.0f) < 1.0f);

        const auto at10 = resolveComet(e, 9.999);
        REQUIRE(at10.has_value());
        const glm::vec3 p1 = world::cometPositionAt(*at10, at10->travelled);
        CHECK(glm::length(p1 - d1 * 1000.0f) < 2.0f);
    }

    SECTION("on an ordinary crossing the elevation stays at its authored ends, to a fraction of a degree") {
        // The real regression guard, and the shape every authored crossing has. The chord
        // implementation launched at 30, aimed at 5, and passed through 49 on the way -- out of
        // frame in the shot it was written for.
        //
        // The half-degree of slack is not a weakened assertion, it is the geometry: a great circle's
        // apex sits slightly above its higher endpoint when the bearings are far enough apart, and
        // this crossing measures 30.22 against an authored 30. Half a degree still catches the
        // nineteen the chord produced, by a factor of forty.
        for (int i = 0; i <= 40; ++i) {
            const double t = 10.0 * i / 40.0 * 0.999;
            const auto r = resolveComet(e, t);
            REQUIRE(r.has_value());
            const glm::vec3 p = world::cometPositionAt(*r, r->travelled);
            const float elevation = glm::degrees(std::asin(p.y / glm::length(p)));
            CHECK(elevation <= 30.5f);
            CHECK(elevation >= 4.5f);
        }
    }

    SECTION("a near-antipodal crossing rises towards the zenith, which is what a great circle does") {
        // Not a defect, and worth an assertion so nobody later "fixes" it. Two bearings half the sky
        // apart in the same vertical plane have a great circle through the zenith, and the shortest
        // arc between them goes over the top. An author who wants a low crossing between opposite
        // horizons is asking for two comets, or for a shorter one.
        world::AtmosphericEffect over = plainComet("over");
        over.comet.path.startAzimuth = 90.0f;
        over.comet.path.startElevation = 30.0f;
        over.comet.path.endAzimuth = 270.0f;
        over.comet.path.endElevation = 10.0f;
        const auto mid = resolveComet(over, 5.0);
        REQUIRE(mid.has_value());
        const glm::vec3 p = world::cometPositionAt(*mid, mid->travelled);
        CHECK(glm::degrees(std::asin(p.y / glm::length(p))) > 60.0f);
        // ...and it is still at the authored distance while doing it, which is the property that
        // actually matters.
        CHECK_THAT(glm::length(p - mid->anchor), WithinRel(1000.0f, 1e-3f));
    }
}

TEST_CASE("a comet's trajectory is a pure function of its parameters and the transport second",
          "[world][atmospherics][determinism]") {
    const world::AtmosphericEffect e = plainComet();

    // The same second, reached by two different routes. Nothing here integrates, so nothing here can
    // depend on how the clock got to 4.25 -- which is exactly what ADR-091 requires.
    const auto a = resolveComet(e, 4.25);
    const auto b = resolveComet(e, 4.25);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(world::cometPositionAt(*a, a->travelled) == world::cometPositionAt(*b, b->travelled));
    CHECK_THAT(a->travelled, WithinAbs(b->travelled, 0.0f));

    SECTION("and the packed record is byte-identical for the same second") {
        const world::CometGpu ga = world::packComet(*a);
        const world::CometGpu gb = world::packComet(*b);
        CHECK(std::memcmp(&ga, &gb, sizeof(world::CometGpu)) == 0);
    }

    SECTION("a later second is further along, monotonically") {
        float previous = -1.0f;
        for (int i = 0; i <= 50; ++i) {
            const auto r = resolveComet(e, 10.0 * i / 50.0 * 0.999);
            REQUIRE(r.has_value());
            CHECK(r->travelled > previous);
            previous = r->travelled;
        }
    }

    SECTION("acceleration reparameterises without moving either end") {
        world::AtmosphericEffect fast = e;
        fast.comet.path.acceleration = 3.0f;
        const auto slowEnd = resolveComet(e, 9.999);
        const auto fastEnd = resolveComet(fast, 9.999);
        REQUIRE(slowEnd.has_value());
        REQUIRE(fastEnd.has_value());
        // Same destination...
        CHECK_THAT(fastEnd->travelled, WithinRel(slowEnd->travelled, 1e-3f));
        // ...but behind it at the halfway point, because it started slower.
        const auto slowMid = resolveComet(e, 5.0);
        const auto fastMid = resolveComet(fast, 5.0);
        REQUIRE(slowMid.has_value());
        REQUIRE(fastMid.has_value());
        CHECK(fastMid->travelled < slowMid->travelled);
    }
}

TEST_CASE("activation and timing gate an atmospheric effect", "[world][atmospherics][timing]") {
    world::AtmosphericEffect e = plainComet();
    e.timing.windowStart = 4.0;
    e.timing.windowSeconds = 6.0;
    e.timing.fadeIn = 1.0;
    e.timing.fadeOut = 1.0;

    CHECK_FALSE(resolveComet(e, 3.5).has_value());  // before the window
    CHECK_FALSE(resolveComet(e, 10.5).has_value()); // after it

    const auto early = resolveComet(e, 4.5);   // mid fade-in
    const auto middle = resolveComet(e, 7.0);  // fully up
    const auto late = resolveComet(e, 9.5);    // mid fade-out
    REQUIRE(early.has_value());
    REQUIRE(middle.has_value());
    REQUIRE(late.has_value());
    CHECK(early->envelope > 0.0f);
    CHECK(early->envelope < middle->envelope);
    CHECK(late->envelope < middle->envelope);
    CHECK_THAT(middle->envelope, WithinAbs(1.0f, 1e-4f));

    SECTION("a disabled effect resolves to nothing at all") {
        e.enabled = false;
        CHECK_FALSE(resolveComet(e, 7.0).has_value());
    }

    SECTION("a cut-gated effect never fires without a cut") {
        // The honest answer for a camera nobody is directing, and the same one ADR-207 gives.
        e.activation = world::Activation::CameraTravel;
        CHECK_FALSE(resolveComet(e, 7.0).has_value());
    }

    SECTION("a repeat interval restarts the crossing") {
        e.timing.windowStart = 0.0; // the case above moved it to 4.0
        e.timing.windowSeconds = 60.0;
        e.timing.repeatSeconds = 5.0;
        e.timing.fadeIn = 0.0;
        e.timing.fadeOut = 0.0;
        const auto first = resolveComet(e, 2.0);
        const auto second = resolveComet(e, 7.0); // one interval later, same phase
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK_THAT(second->travelled, WithinRel(first->travelled, 1e-4f));
    }
}

TEST_CASE("no more than the GPU limit of atmospheric effects is ever written",
          "[world][atmospherics][gpu]") {
    std::vector<world::AtmosphericEffect> many;
    for (std::size_t i = 0; i < world::kMaxGpuComets + 4; ++i) {
        many.push_back(plainComet("comet" + std::to_string(i)));
    }
    for (std::size_t i = 0; i < world::kMaxGpuAuroras + 3; ++i) {
        world::AtmosphericEffect a = world::glowmereAurora("aurora" + std::to_string(i));
        a.timing.fadeIn = 0.0;
        many.push_back(a);
    }
    REQUIRE(world::validateAtmosphericEffects(many).has_value());

    world::AtmosphericFrame frame;
    world::buildAtmosphericFrame(many, contextAt(2.0), frame);
    CHECK(frame.cometCount == world::kMaxGpuComets);
    CHECK(frame.auroraCount == world::kMaxGpuAuroras);
    CHECK(frame.any());
}

TEST_CASE("packing puts every authored number in the lane the shader reads",
          "[world][atmospherics][gpu]") {
    world::AtmosphericEffect e = plainComet();
    e.comet.appearance.coreColor = glm::vec3(0.25f, 0.5f, 0.75f);
    e.comet.appearance.coreIntensity = 8.0f;
    e.comet.appearance.headSize = 33.0f;
    e.comet.appearance.tailLength = 1234.0f;
    e.comet.appearance.tailWidth = 44.0f;
    e.comet.sparkle.enabled = false;
    e.comet.rainbow.enabled = false;

    const auto r = resolveComet(e, 0.0);
    REQUIRE(r.has_value());
    const world::CometGpu g = world::packComet(*r);

    CHECK_THAT(g.dir0Tail.w, WithinAbs(1234.0f, 1e-3f));
    CHECK_THAT(g.core.w, WithinAbs(33.0f, 1e-3f));
    CHECK_THAT(g.shape.x, WithinAbs(44.0f, 1e-3f));
    CHECK_THAT(g.arc.x, WithinAbs(1000.0f, 1e-3f));
    CHECK_THAT(g.arc.y,
               WithinRel(std::acos(glm::dot(skyDirection(30.0f, 30.0f), skyDirection(120.0f, 5.0f))), 1e-4f));
    // The envelope is folded into the radiance rather than carried as its own lane.
    CHECK_THAT(g.core.r, WithinAbs(0.25f * 8.0f, 1e-4f));
    // Off means zero, so the shader's guard is a compare against a number and not a flag.
    CHECK_THAT(g.sparkle.x, WithinAbs(0.0f, 0.0f));
    CHECK_THAT(g.rainbow.w, WithinAbs(0.0f, 0.0f));

    SECTION("a fade scales every radiance and nothing else") {
        world::AtmosphericEffect fading = e;
        fading.timing.fadeIn = 4.0;
        const auto half = resolveComet(fading, 2.0); // halfway up a smoothstep: 0.5
        REQUIRE(half.has_value());
        const world::CometGpu gh = world::packComet(*half);
        CHECK_THAT(gh.core.r, WithinAbs(g.core.r * half->envelope, 1e-4f));
        // ...but the geometry is untouched by the fade.
        CHECK_THAT(gh.core.w, WithinAbs(33.0f, 1e-3f));
    }
}

TEST_CASE("an aurora's spectrum reaches the packed record, and silence is neutral",
          "[world][atmospherics][gpu][audio]") {
    const world::AtmosphericEffect e = world::glowmereAurora("sky");
    std::array<world::ResolvedAtmospheric, world::kMaxGpuComets> comets{};
    std::array<world::ResolvedAtmospheric, world::kMaxGpuAuroras> auroras{};
    const std::array<world::AtmosphericEffect, 1> set{e};

    std::array<float, world::kAuroraBands> bands{};
    for (std::size_t i = 0; i < bands.size(); ++i) {
        bands[i] = static_cast<float>(i) / static_cast<float>(bands.size() - 1);
    }
    world::AtmosphericContext ctx = contextAt(10.0);
    ctx.spectrum = bands;
    REQUIRE(world::resolveAtmosphericEffects(set, ctx, comets, auroras).auroras == 1);
    const world::AuroraGpu g = world::packAurora(auroras[0], bands);
    CHECK_THAT(g.band0.x, WithinAbs(bands[0], 1e-5f));
    CHECK_THAT(g.band3.w, WithinAbs(bands[15], 1e-5f));
    CHECK_THAT(g.band1.y, WithinAbs(bands[5], 1e-5f));

    SECTION("no music fills the bins with the neutral value, not with zero") {
        // A zero-filled spectrum would collapse the curtain; 0.5 is the value the shader's height
        // mapping treats as an ordinary aurora, which is what silence should look like.
        const world::AuroraGpu silent = world::packAurora(auroras[0], {});
        CHECK_THAT(silent.band0.x, WithinAbs(0.5f, 1e-5f));
        CHECK_THAT(silent.band3.w, WithinAbs(0.5f, 1e-5f));
    }
}

TEST_CASE("ground illumination sums the live effects and respects its three words",
          "[world][atmospherics][ground]") {
    world::AtmosphericEffect aurora = world::glowmereAurora("sky");
    aurora.timing.fadeIn = 0.0;
    aurora.ground.mode = world::GroundGlow::Strong;
    aurora.ground.color = glm::vec3(0.0f, 1.0f, 0.0f);
    aurora.ground.intensity = 1.0f;

    std::vector<world::AtmosphericEffect> set{aurora};
    world::AtmosphericFrame frame;
    world::buildAtmosphericFrame(set, contextAt(5.0), frame);
    const float strong = frame.ground.ambient.g;
    CHECK(strong > 0.0f);

    SECTION("Subtle is well under half of Strong") {
        // Not an arbitrary assertion: section 6 warns the illumination must not flatten the scene,
        // and a "subtle" that is really two thirds is a setting nobody ever turns off instead.
        set[0].ground.mode = world::GroundGlow::Subtle;
        world::buildAtmosphericFrame(set, contextAt(5.0), frame);
        CHECK(frame.ground.ambient.g < strong * 0.5f);
        CHECK(frame.ground.ambient.g > 0.0f);
    }

    SECTION("Off is exactly zero, not merely small") {
        set[0].ground.mode = world::GroundGlow::Off;
        world::buildAtmosphericFrame(set, contextAt(5.0), frame);
        CHECK_THAT(frame.ground.ambient.g, WithinAbs(0.0f, 0.0f));
    }

    SECTION("a dormant effect contributes nothing") {
        set[0].enabled = false;
        world::buildAtmosphericFrame(set, contextAt(5.0), frame);
        CHECK_THAT(frame.ground.ambient.g, WithinAbs(0.0f, 0.0f));
        CHECK_FALSE(frame.any());
    }

    SECTION("a comet contributes a moving ground track") {
        world::AtmosphericEffect comet = plainComet("low");
        comet.ground.mode = world::GroundGlow::Strong;
        comet.ground.color = glm::vec3(1.0f, 0.0f, 0.0f);
        comet.ground.radius = 400.0f;
        std::vector<world::AtmosphericEffect> two{comet};
        world::buildAtmosphericFrame(two, contextAt(2.0), frame);
        const glm::vec4 early = frame.ground.point;
        CHECK_THAT(early.w, WithinAbs(400.0f, 1e-3f));
        CHECK(frame.ground.pointColor.r > 0.0f);
        // It is the point *under* the comet, so it is on the ground plane.
        CHECK_THAT(early.y, WithinAbs(0.0f, 1e-5f));
        world::buildAtmosphericFrame(two, contextAt(6.0), frame);
        CHECK(glm::length(glm::vec3(frame.ground.point) - glm::vec3(early)) > 1.0f);
    }
}

TEST_CASE("a preset configures parameters and nothing else", "[world][atmospherics][presets]") {
    world::AtmosphericEffect e = world::bioluminescentComet("Named");
    e.activation = world::Activation::HeroFocus;
    e.timing.windowStart = 12.5;
    e.ground.mode = world::GroundGlow::Strong;

    REQUIRE(world::applyCometStyle(e, "Rainbow Cosmic"));
    CHECK(e.style == "Rainbow Cosmic");
    CHECK(e.comet.rainbow.enabled);
    // Name, activation, timing and ground illumination are the author's, not the preset's.
    CHECK(e.name == "Named");
    CHECK(e.activation == world::Activation::HeroFocus);
    CHECK(e.timing.windowStart == 12.5);
    CHECK(e.ground.mode == world::GroundGlow::Strong);

    SECTION("switching away from a rainbow preset turns the rainbow off again") {
        // A preset that only sets what it wants leaves the previous one's rainbow on, and the user
        // reads that as the preset being broken rather than as two presets overlapping.
        REQUIRE(world::applyCometStyle(e, "Emerald Teal"));
        CHECK_FALSE(e.comet.rainbow.enabled);
    }

    SECTION("every shipped preset name applies and validates") {
        for (const std::string_view style : world::cometStyleNames()) {
            world::AtmosphericEffect c = world::bioluminescentComet("c");
            CHECK(world::applyCometStyle(c, style));
            CHECK(c.validate().has_value());
            CHECK(c.kind == world::AtmosphereKind::Comet);
        }
        for (const std::string_view style : world::auroraStyleNames()) {
            world::AtmosphericEffect a = world::glowmereAurora("a");
            CHECK(world::applyAuroraStyle(a, style));
            CHECK(a.validate().has_value());
            CHECK(a.kind == world::AtmosphereKind::Aurora);
        }
    }

    SECTION("the first two comet presets are visually distinct, as section 10 requires") {
        world::AtmosphericEffect cyan = world::bioluminescentComet("a");
        world::AtmosphericEffect rainbow = world::bioluminescentComet("b");
        REQUIRE(world::applyCometStyle(cyan, "Bioluminescent Cyan"));
        REQUIRE(world::applyCometStyle(rainbow, "Rainbow Cosmic"));
        CHECK(cyan.comet.rainbow.enabled != rainbow.comet.rainbow.enabled);
        CHECK(glm::length(cyan.comet.appearance.tailColor - rainbow.comet.appearance.tailColor) > 0.2f);
    }

    SECTION("a name that is not a preset is refused rather than half-applied") {
        world::AtmosphericEffect before = e;
        CHECK_FALSE(world::applyCometStyle(e, "Not A Preset"));
        CHECK(e.style == before.style);
    }
}

TEST_CASE("every meaningful atmospheric parameter is declared and modulatable",
          "[world][atmospherics][params]") {
    params::ParameterSet params;
    std::vector<world::AtmosphericEffect> effects{world::bioluminescentComet("Comet"),
                                                  world::glowmereAurora("Sky")};
    world::AtmosphericParameters registered = world::registerAtmosphericParameters(params, effects);
    REQUIRE(registered.effects.size() == 2);

    const auto require = [&](const char* path) {
        params::IParameter* p = params.find(path);
        INFO(path);
        REQUIRE(p != nullptr);
        CHECK(p->flags().modulatable);
        CHECK(p->flags().serialized);
        return p;
    };
    // A sample across every group the brief names, rather than all ninety: what this is guarding is
    // that the table reaches each family, and a row that is present works in all three directions
    // by construction.
    for (const char* path : {"atmos/Comet/enabled", "atmos/Comet/coreColor", "atmos/Comet/coreIntensity",
                             "atmos/Comet/tailLength", "atmos/Comet/tailWidth", "atmos/Comet/sparkleDensity",
                             "atmos/Comet/rainbowScale", "atmos/Comet/startAzimuth", "atmos/Comet/distance",
                             "atmos/Comet/speed", "atmos/Comet/acceleration", "atmos/Comet/curvature",
                             "atmos/Comet/groundIntensity", "atmos/Comet/fadeIn", "atmos/Comet/windowStart"}) {
        require(path);
    }
    for (const char* path : {"atmos/Sky/lowColor", "atmos/Sky/topColor", "atmos/Sky/intensity",
                             "atmos/Sky/curtainHeight", "atmos/Sky/curtains", "atmos/Sky/waveAmplitude",
                             "atmos/Sky/complexity", "atmos/Sky/audioBass", "atmos/Sky/audioHigh",
                             "atmos/Sky/spectrumShape", "atmos/Sky/filaments", "atmos/Sky/horizonGlow"}) {
        require(path);
    }

    SECTION("only the kind's own parameters are registered") {
        // Registering both payloads would put ninety parameters in the table for every one an artist
        // can reach, half of them doing nothing -- which is worse than their being absent.
        CHECK(params.find("atmos/Comet/curtainHeight") == nullptr);
        CHECK(params.find("atmos/Sky/tailLength") == nullptr);
    }

    SECTION("a hard range is wider than the slider, so a route has somewhere to go") {
        params::IParameter* core = params.find("atmos/Comet/coreIntensity");
        REQUIRE(core != nullptr);
        CHECK(core->hardMax(0) > core->softMax(0));
    }

    SECTION("finals reach the live set and bases reach the authored one") {
        params::IParameter* core = params.find("atmos/Comet/coreIntensity");
        REQUIRE(core != nullptr);
        core->setBaseComponent(0, 11.0f);
        core->setFinalComponent(0, 30.0f); // as a route would, this frame

        world::applyAtmosphericParameters(registered, effects);
        CHECK_THAT(effects[0].comet.appearance.coreIntensity, WithinAbs(30.0f, 1e-4f));

        // ...and a save takes the base, because the finals carry this frame's beat on them.
        world::captureAtmosphericParameters(registered, effects);
        CHECK_THAT(effects[0].comet.appearance.coreIntensity, WithinAbs(11.0f, 1e-4f));
    }

    SECTION("unregistering is exact") {
        world::unregisterAtmosphericParameters(params, registered);
        CHECK(params.find("atmos/Comet/coreIntensity") == nullptr);
        CHECK(params.find("atmos/Sky/intensity") == nullptr);
        CHECK(registered.effects.empty());
    }
}

TEST_CASE("the default routes name real parameters and leave silence alone",
          "[world][atmospherics][params][audio]") {
    params::ParameterSet params;
    std::vector<world::AtmosphericEffect> effects{world::glowmereAurora("Sky"),
                                                  world::bioluminescentComet("Comet")};
    world::registerAtmosphericParameters(params, effects);

    for (const auto kind : {world::AtmosphereKind::Aurora, world::AtmosphereKind::Comet}) {
        const char* name = kind == world::AtmosphereKind::Aurora ? "Sky" : "Comet";
        const auto routes = world::defaultAtmosphericRoutes(name, kind);
        CHECK_FALSE(routes.empty());
        for (const params::ModRoute& r : routes) {
            INFO(r.target);
            CHECK(params.find(r.target) != nullptr);
            // `Add` so an unplayed project looks like what somebody authored rather than like a
            // collapsed one, and shaped with an attack and a decay rather than used as a gate.
            CHECK(r.op == params::ModOp::Add);
            CHECK(r.chain.decayMs > 0.0f);
        }
    }
}

TEST_CASE("atmospheric effects round-trip through a scene file",
          "[world][atmospherics][json][composition]") {
    // A sibling of `"worldEffects"`, and the properties that matter are the ones a hand-edited file
    // depends on: a scene that never declared one is unchanged, a scene that did gets it back, and a
    // malformed one is refused with the index in the message.
    const std::string base = R"({"format": "avgen-scene", "version": 1, "name": "fx", "nodes": []})";
    assets::AssetRegistry registry;

    SECTION("a scene with none has none, and writes none back") {
        auto comp = scene::Composition::fromJson(nlohmann::json::parse(base), registry);
        REQUIRE(comp);
        CHECK((*comp)->atmosphericEffects().empty());
        CHECK_FALSE((*comp)->toJson().contains("atmosphericEffects"));
    }

    SECTION("a declared effect survives the trip") {
        nlohmann::json doc = nlohmann::json::parse(base);
        doc["atmosphericEffects"] = nlohmann::json::array(
            {world::bioluminescentComet("Comet").toJson(), world::glowmereAurora("Sky").toJson()});
        auto comp = scene::Composition::fromJson(doc, registry);
        REQUIRE(comp);
        REQUIRE((*comp)->atmosphericEffects().size() == 2);
        CHECK((*comp)->atmosphericEffects()[0].name == "Comet");
        CHECK((*comp)->atmosphericEffects()[1].kind == world::AtmosphereKind::Aurora);
        const nlohmann::json written = (*comp)->toJson();
        REQUIRE(written.contains("atmosphericEffects"));
        CHECK(written["atmosphericEffects"] == doc["atmosphericEffects"]);
    }

    SECTION("a malformed effect is refused, and the message says which one") {
        nlohmann::json doc = nlohmann::json::parse(base);
        doc["atmosphericEffects"] = nlohmann::json::array(
            {world::glowmereAurora("Sky").toJson(),
             nlohmann::json{{"name", "bad"}, {"kind", "comet"},
                            {"comet", {{"path", {{"distance", -5.0}}}}}}});
        auto comp = scene::Composition::fromJson(doc, registry);
        REQUIRE_FALSE(comp);
        CHECK(comp.error().message.find("atmosphericEffects[1]") != std::string::npos);
    }

    SECTION("two effects of one name are refused, because a name is half a parameter path") {
        nlohmann::json doc = nlohmann::json::parse(base);
        doc["atmosphericEffects"] = nlohmann::json::array(
            {world::bioluminescentComet("Same").toJson(), world::glowmereAurora("Same").toJson()});
        CHECK_FALSE(scene::Composition::fromJson(doc, registry));
    }
}

TEST_CASE("the shipped Glowmere Atmospherics scene declares what the demonstration needs",
          "[world][atmospherics][glowmere]") {
    // The counterpart of test_world_effects.cpp's Glowmere assertion. What it guards is that the
    // example somebody opens actually contains the thing it is an example of -- the failure mode
    // being a regenerated scene that quietly lost its effects.
    const std::filesystem::path scene =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-atmospherics.scene.json";
    if (!std::filesystem::exists(scene)) {
        SKIP("glowmere-atmospherics.scene.json is not in this checkout");
    }
    std::ifstream in(scene);
    REQUIRE(in);
    nlohmann::json doc;
    in >> doc;
    REQUIRE(doc.contains("atmosphericEffects"));

    std::size_t comets = 0;
    std::size_t auroras = 0;
    bool rainbow = false;
    bool sparkle = false;
    bool ground = false;
    for (const nlohmann::json& j : doc.at("atmosphericEffects")) {
        const auto e = world::AtmosphericEffect::fromJson(j);
        INFO(j.value("name", "?"));
        REQUIRE(e.has_value());
        if (e->kind == world::AtmosphereKind::Comet) {
            ++comets;
            rainbow = rainbow || e->comet.rainbow.enabled;
            sparkle = sparkle || e->comet.sparkle.enabled;
        } else {
            ++auroras;
        }
        ground = ground || e->ground.mode != world::GroundGlow::Off;
    }
    // Section 14's demonstration list, as an assertion rather than as a claim in a report.
    CHECK(comets >= 2);          // a hero comet and a colour variation
    CHECK(auroras >= 1);         // a horizon-originating aurora
    CHECK(rainbow);              // rainbow comet mode
    CHECK(sparkle);              // a sparkling comet tail
    CHECK(ground);               // optional ground illumination, demonstrated
    CHECK(comets <= world::kMaxGpuComets);
}

// ---- the dispatch is exhaustive, and the vortex is not the `else` ------------------------------
//
// `resolveAtmosphericEffects` dispatched with `if comet / else if aurora / else vortex`, so the
// vortex was not a *test* -- it was the fall-through. A kind the chain does not claim therefore did
// not resolve as nothing; it resolved as a vortex, and was then dropped as "the second vortex in
// the scene", which is a silent no-op wearing a limit's clothes.
//
// This is ADR-182's rule applied to a dispatch: a probe over the three kinds that exist proves
// nothing about the fourth, because all three are named in the chain. The only probe that can fail
// is one carrying a kind the chain does *not* name. `AtmosphereKind` has a fixed underlying type
// (`std::uint8_t`), so a value no enumerator names is a well-defined value of the type rather than
// undefined behaviour, and it is exactly the shape a forgotten `case` produces at runtime.
//
// `conformance::checkAtmospheric`'s check 4 cannot stand in for this one: it decides which counter
// is "mine" with a ternary chain whose own `else` is `counts.vortices`, so a new kind falling into
// the resolve's vortex arm is compared against the vortex counter and agrees with itself.
TEST_CASE("a kind the resolve dispatch does not name resolves as nothing, not as a vortex",
          "[world][atmospherics][dispatch]") {
    auto resolveOne = [](world::AtmosphereKind kind) {
        world::AtmosphericEffect e = world::cosmicVortex("probe");
        e.kind = kind;
        e.enabled = true;
        e.activation = world::Activation::Always;
        e.timing = world::Timing{};
        e.timing.fadeIn = 0.0;
        e.timing.fadeOut = 0.0;
        const std::array<world::AtmosphericEffect, 1> set{e};
        std::array<world::ResolvedAtmospheric, world::kMaxGpuComets> comets{};
        std::array<world::ResolvedAtmospheric, world::kMaxGpuAuroras> auroras{};
        return world::resolveAtmosphericEffects(set, contextAt(0.5), comets, auroras);
    };

    SECTION("the control: a real vortex still resolves as one vortex") {
        const auto counts = resolveOne(world::AtmosphereKind::Vortex);
        CHECK(counts.vortices == 1);
        CHECK(counts.comets == 0);
        CHECK(counts.auroras == 0);
        CHECK(counts.dropped == 0);
    }

    SECTION("a kind no arm claims is not silently attributed to another kind") {
        const auto counts = resolveOne(static_cast<world::AtmosphereKind>(0xF0));
        INFO("an unnamed kind resolved as " << counts.comets << " comet(s), " << counts.auroras
                                            << " aurora(s), " << counts.vortices << " vortex/vortices");
        CHECK(counts.vortices == 0);
        CHECK(counts.comets == 0);
        CHECK(counts.auroras == 0);
        // Not zero: it is reported, because an effect the engine cannot draw is a thing the UI
        // should be able to say out loud rather than a thing that quietly does not happen.
        CHECK(counts.dropped == 1);
    }
}
