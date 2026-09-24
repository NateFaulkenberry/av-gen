// Shockwave, Ripple and Velocity Distortion (Effect Library Wave 2): TRIGGER's first DF users.
//
// The producers are asked what a frame block can answer -- a front expands and weakens with age, is
// released where its owner WAS, several overlap, a ripple train starts at its trigger, a wake lies
// along the recorded path and a slow owner leaves none -- and then the engine is asked the question
// the whole design exists for: a Shockwave on the beats of an analysed track, on a moving owner, is
// the same frame block PLAYED to a second as SCRUBBED to it. Likewise a Proximity-triggered one,
// whose event comes out of the checkpointed history.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "support/synth.hpp"
#include "support/temp_dir.hpp"
#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/effect_trigger.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

void setRow(world::EffectInstance& e, const char* key, const char* leaf, float v) {
    e.values.setFloat(std::string(key) + "/" + leaf, v);
}

world::Trigger repeat(double period, double phase = 0.0) {
    world::Trigger t;
    t.source = world::TriggerSource::Repeat;
    t.period = period;
    t.phase = phase;
    return t;
}

world::EffectInstance shock(world::Trigger trig, world::EffectOwner owner = {}) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::Shockwave, "shock");
    e.id = "shock";
    e.owner = std::move(owner);
    e.activation = world::Activation::Trigger;
    e.timing.trigger = std::move(trig);
    return e;
}

struct Built {
    world::DistortionFrame frame;
    world::EffectStatus status = world::EffectStatus::Disabled;
};

Built build(const world::EffectInstance& e, double seconds, const world::EffectSceneQuery* scene = nullptr,
            const world::TriggerClock* clock = nullptr) {
    static const world::TriggerClock kNone;
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.scene = scene;
    ctx.triggers = clock != nullptr ? clock : &kNone;
    ctx.cameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
    const std::vector<world::EffectInstance> list{e};
    std::vector<world::EffectStatus> status(1);
    std::vector<std::string> reasons(1);
    Built out;
    world::buildDistortionFrame(list, ctx, out.frame, {}, status, reasons);
    out.status = status[0];
    return out;
}

float frontRadius(const world::DistortionProxy& p) { return p.shape.x * glm::length(glm::vec3(p.axis0)); }

// An owner flying along +x at `speed`, with a recorded path: what HIST would answer.
class FlyingOwner final : public world::EffectSceneQuery {
public:
    double now = 0.0;
    float speed = 10.0f;
    [[nodiscard]] glm::vec3 at(double t) const { return {speed * static_cast<float>(t), 3.0f, 0.0f}; }
    [[nodiscard]] bool nodePosition(std::string_view, glm::vec3& out) const override {
        out = at(now);
        return true;
    }
    [[nodiscard]] bool nodeView(std::string_view, world::NodeView& out) const override {
        out = world::NodeView{};
        out.world = glm::mat4(1.0f);
        out.world[3] = glm::vec4(at(now), 1.0f);
        out.boundsMin = at(now) - glm::vec3(1.0f);
        out.boundsMax = at(now) + glm::vec3(1.0f);
        out.hasBounds = true;
        out.entityCount = 1;
        return true;
    }
    [[nodiscard]] bool nodeVelocity(std::string_view, glm::vec3& out) const override {
        out = glm::vec3(speed, 0.0f, 0.0f);
        return true;
    }
    [[nodiscard]] bool nodeDrawnPosition(std::string_view, double t, glm::vec3& out) const override {
        if (t < 0.0 || t > now) {
            return false;
        }
        out = at(t);
        return true;
    }
};

} // namespace

TEST_CASE("Shockwave: a front expands, thins out and is gone after its duration", "[shockwave]") {
    world::EffectInstance e = shock(repeat(10.0, 1.0)); // one front, released at 1.0 s
    setRow(e, "shockwave", "duration", 1.0f);
    setRow(e, "shockwave", "maxRadius", 20.0f);
    CHECK(build(e, 0.9).status == world::EffectStatus::Dormant);
    float lastRadius = 0.0f;
    float lastAmp = 1e9f;
    for (const double t : {1.1, 1.3, 1.5, 1.8}) {
        INFO("t = " << t);
        const Built b = build(e, t);
        REQUIRE(b.frame.count == 1);
        CHECK(b.status == world::EffectStatus::Drawn);
        const world::DistortionProxy& p = b.frame.proxies[0];
        CHECK(static_cast<int>(p.axis1.w + 0.5f) == static_cast<int>(world::DistortionField::Shock));
        const float r = frontRadius(p);
        CHECK(r > lastRadius);
        CHECK(r <= 20.0f);
        if (t > 1.2) {
            CHECK(p.terms.w < lastAmp); // weaker as it spreads (and, late, as it fades)
        }
        lastRadius = r;
        lastAmp = p.terms.w;
    }
    CHECK(build(e, 2.01).frame.count == 0); // past the duration
}

TEST_CASE("Shockwave: fronts from overlapping triggers are all drawn, up to maxConcurrent", "[shockwave]") {
    world::EffectInstance e = shock(repeat(0.4));
    setRow(e, "shockwave", "duration", 1.4f);
    setRow(e, "shockwave", "maxConcurrent", 4.0f);
    // At 1.3 s the fronts of 1.2, 0.8 and 0.4 are live (0.0 is 1.3 old; still inside 1.4 too).
    const Built b = build(e, 1.3);
    CHECK(b.frame.count == 4);
    // Newest first: the newest front is the smallest, and each older one is larger.
    for (std::size_t i = 1; i < b.frame.count; ++i) {
        CHECK(frontRadius(b.frame.proxies[i]) > frontRadius(b.frame.proxies[i - 1]));
    }
    setRow(e, "shockwave", "maxConcurrent", 2.0f);
    CHECK(build(e, 1.3).frame.count == 2);
}

TEST_CASE("Shockwave: a front is released where its owner WAS, and stays there", "[shockwave]") {
    FlyingOwner owner;
    world::EffectInstance e = shock(repeat(10.0, 1.0), world::EffectOwner::entity("craft"));
    for (const double t : {1.2, 1.6}) {
        owner.now = t;
        const Built b = build(e, t, &owner);
        REQUIRE(b.frame.count == 1);
        const glm::vec3 c(b.frame.proxies[0].centre);
        CHECK(c.x == Approx(10.0f)); // at 1.0 s the craft was at x = 10, whatever t is now
        CHECK(glm::length(owner.at(t) - c) > 1.0f);
    }
}

TEST_CASE("Ripple: a train of rings in a disc, its front travelling at its speed", "[shockwave][ripple]") {
    world::EffectInstance e = world::makeEffect(world::EffectKind::Ripple, "ripple");
    e.id = "ripple";
    e.timing.trigger = repeat(10.0, 2.0);
    setRow(e, "ripple", "radius", 8.0f);
    setRow(e, "ripple", "speed", 4.0f);
    CHECK(build(e, 1.9).frame.count == 0);
    const Built b = build(e, 3.0);
    REQUIRE(b.frame.count == 1);
    const world::DistortionProxy& p = b.frame.proxies[0];
    CHECK(static_cast<int>(p.axis0.w + 0.5f) == static_cast<int>(world::DistortionShape::Disc));
    CHECK(static_cast<int>(p.axis1.w + 0.5f) == static_cast<int>(world::DistortionField::Ripple));
    CHECK(p.motion.x == Approx(4.0f * 1.0f / 8.0f)); // the front: speed * age over the radius
    // The default plane faces the camera: the disc's normal is the view axis.
    CHECK(std::abs(glm::normalize(glm::vec3(p.axis2)).z) == Approx(1.0f));
    CHECK(p.terms.w > 0.0f);
}

TEST_CASE("Velocity Distortion: a wake along the recorded path, and none from a slow owner", "[shockwave][wake]") {
    FlyingOwner owner;
    owner.now = 3.0;
    world::EffectInstance e = world::makeEffect(world::EffectKind::VelocityDistortion, "wake");
    e.id = "wake";
    e.owner = world::EffectOwner::entity("craft");
    const Built b = build(e, 3.0, &owner);
    REQUIRE(b.frame.count >= 4);
    CHECK(b.status == world::EffectStatus::Drawn);
    for (std::size_t i = 0; i < b.frame.count; ++i) {
        const world::DistortionProxy& p = b.frame.proxies[i];
        INFO("segment " << i);
        CHECK(static_cast<int>(p.axis1.w + 0.5f) == static_cast<int>(world::DistortionField::Wake));
        // On the path (y = 3, z = 0), behind the owner (x < 30), along it (axis0 is x).
        CHECK(p.centre.y == Approx(3.0f));
        CHECK(p.centre.x < 30.0f - 1.0f);
        CHECK(std::abs(glm::normalize(glm::vec3(p.axis0)).x) == Approx(1.0f));
        if (i > 0) {
            CHECK(p.terms.w < b.frame.proxies[i - 1].terms.w); // older is calmer
        }
    }
    owner.speed = 1.0f; // under the 2 m/s floor
    CHECK(build(e, 3.0, &owner).frame.count == 0);
}

// ---- the engine: play = scrub --------------------------------------------------------------------

namespace {

void frameAt(app::Engine& engine, long long frame) {
    engine.update(FrameTime{static_cast<double>(frame) / 60.0, frame == 0 ? 0.0 : 1.0 / 60.0,
                            static_cast<std::uint64_t>(frame)});
}

std::filesystem::path clickTrack() {
    constexpr std::uint32_t kRate = 48000;
    const std::size_t total = 14 * kRate;
    auto clicks = testsupport::clickTrack(120.0f, kRate, total);
    auto tone = testsupport::sine(80.0f, kRate, total, 0.2f);
    for (std::size_t i = 0; i < total; ++i) {
        clicks[i] += tone[i];
    }
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(clicks, 2), 2, kRate);
    const auto path = testsupport::processTempDir() / "shockwave_clicks.wav";
    REQUIRE(file.writeWav(path).has_value());
    return path;
}

// A craft flown by a keyed track past a beacon (the History slice's fixture: motion the engine's seek
// replays exactly), carrying `effects`. No modulation routes: a smoothed route re-seeds on a seek and
// agrees only to float rounding (ADR-703), and this compares bytes.
void installCraft(app::Engine& engine, std::vector<world::EffectInstance> effects, bool audio) {
    if (audio) {
        static const std::filesystem::path wav = clickTrack();
        REQUIRE(engine.loadAudio(wav).has_value());
    }
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(R"({ "format": "avgen-scene", "version": 1,
        "name": "flown", "nodes": [ { "kind": "orb", "name": "craft", "position": [-30, 5, 0] },
                                    { "kind": "orb", "name": "beacon", "position": [0, 5, 0] } ] })"))
                .has_value());
    std::vector<world::EffectInstance> list;
    for (world::EffectInstance& e : effects) {
        e.id.clear();
        REQUIRE(world::insertEffect(list, std::move(e)).has_value());
    }
    REQUIRE(engine.setEffects(list).has_value());
    params::Track fly;
    fly.target = "nodes/craft/position";
    fly.keys.push_back(params::Key{.time = 0.0, .value = {-30.0f, 5.0f, 0.0f, 0.0f}});
    fly.keys.push_back(params::Key{.time = 8.0, .value = {30.0f, 6.0f, 4.0f, 0.0f}});
    engine.timeline().addTrack(fly);
    REQUIRE(engine.timeline().bind(engine.params()).has_value());
    // The engine's stock audio routes (the root's scale and spin on the mid band, ...) move every node
    // with the music, and a seek replays no routes (ADR-671): with a track loaded they would make the
    // craft's PATH differ between play and scrub before any effect is involved.
    engine.modulator().clearRoutes();
}

bool sameProxies(const world::DistortionFrame& a, const world::DistortionFrame& b) {
    return a.count == b.count &&
           std::memcmp(a.proxies.data(), b.proxies.data(), sizeof(world::DistortionProxy) * a.count) == 0;
}

} // namespace

TEST_CASE("Shockwave on the beats of an analysed track, on a moving owner, is the same played and scrubbed",
          "[shockwave][seek][determinism]") {
    world::EffectInstance e = world::makeEffect(world::EffectKind::Shockwave, "Shockwave");
    e.owner = world::EffectOwner::entity("craft");
    e.timing.trigger.everyN = 2; // one front a second at 120 bpm
    setRow(e, "shockwave", "duration", 1.6f);
    setRow(e, "shockwave", "maxConcurrent", 3.0f);
    constexpr long long kTarget = 331; // 5.52 s: two fronts in the air, mid-flight

    app::Engine played(app::EngineMode::Offline);
    installCraft(played, {e}, true);
    REQUIRE(played.track() != nullptr);
    REQUIRE(played.track()->beats().beatTimes.size() > 8);
    for (long long f = 0; f <= kTarget + 1; ++f) {
        frameAt(played, f);
    }
    app::Engine scrubbed(app::EngineMode::Offline);
    installCraft(scrubbed, {e}, true);
    frameAt(scrubbed, 0);
    scrubbed.seekSeconds(static_cast<double>(kTarget) / 60.0);
    frameAt(scrubbed, kTarget + 1);

    const world::DistortionFrame& a = played.scene().distortion;
    const world::DistortionFrame& b = scrubbed.scene().distortion;
    INFO("played " << a.count << " fronts, scrubbed " << b.count);
    REQUIRE(a.count >= 2); // the control: overlapping live fronts, not an empty equality
    CHECK(played.effectStatus(played.effects()[0].id) == world::EffectStatus::Drawn);
    // Released where the craft WAS: the fronts' centres differ from each other (it was moving).
    CHECK(glm::length(glm::vec3(a.proxies[0].centre) - glm::vec3(a.proxies[1].centre)) > 2.0f);
    for (std::size_t k = 0; k < std::min(a.count, b.count); ++k) {
        const float* x = reinterpret_cast<const float*>(&a.proxies[k]);
        const float* y = reinterpret_cast<const float*>(&b.proxies[k]);
        for (std::size_t f = 0; f < sizeof(world::DistortionProxy) / sizeof(float); ++f) {
            if (x[f] != y[f]) {
                UNSCOPED_INFO("proxy " << k << " lane " << f << ": " << x[f] << " vs " << y[f]);
            }
        }
    }
    CHECK(sameProxies(a, b));
}

TEST_CASE("Shockwave on a Proximity trigger is the same played and scrubbed (the event is in HIST)",
          "[trigger][shockwave][seek][determinism]") {
    world::EffectInstance e = world::makeEffect(world::EffectKind::Shockwave, "Shockwave");
    e.owner = world::EffectOwner::entity("craft");
    e.timing.trigger = world::Trigger{};
    e.timing.trigger.source = world::TriggerSource::Proximity;
    e.timing.trigger.entity = "beacon";
    e.timing.trigger.radius = 6.0f;
    setRow(e, "shockwave", "duration", 2.0f);
    // The craft passes x = 0 at 4 s at 7.5 m/s (with a little drift in y and z), so it comes within
    // 6 m of the beacon at about 3.2 s.
    constexpr long long kTarget = 222; // 3.7 s
    app::Engine played(app::EngineMode::Offline);
    installCraft(played, {e}, false);
    for (long long f = 0; f <= kTarget + 1; ++f) {
        frameAt(played, f);
    }
    app::Engine scrubbed(app::EngineMode::Offline);
    installCraft(scrubbed, {e}, false);
    frameAt(scrubbed, 0);
    scrubbed.seekSeconds(static_cast<double>(kTarget) / 60.0);
    frameAt(scrubbed, kTarget + 1);
    const world::DistortionFrame& a = played.scene().distortion;
    const world::DistortionFrame& b = scrubbed.scene().distortion;
    REQUIRE(a.count == 1);
    CHECK(sameProxies(a, b));
    // And before the craft came near, nothing.
    app::Engine early(app::EngineMode::Offline);
    installCraft(early, {e}, false);
    for (long long f = 0; f <= 150; ++f) {
        frameAt(early, f);
    }
    CHECK(early.scene().distortion.count == 0);
    CHECK(early.effectStatus(early.effects()[0].id) == world::EffectStatus::Dormant);
}
