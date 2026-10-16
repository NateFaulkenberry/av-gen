// ADR-1403: the resonance field's own motion -- kinematics (modes that wander and turn), onset ripples (travelling
// rings), and a source's amplitude after the limiter. All of it a pure function of the clock and the audio history.
#include "params/parameter_set.hpp"
#include "scene/field_params.hpp"
#include "spatial/audio_history.hpp"
#include "spatial/field.hpp"
#include "spatial/resonance.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr double kRate = 100.0;
constexpr double kTwoPi = 6.283185307179586;

// A quiet whole-track history of `seconds` (every bin at `level`) with the given onsets per band (low, mid, high).
std::shared_ptr<const spatial::AudioHistory> history(double seconds, float level,
                                                     std::array<std::vector<spatial::AudioOnset>, 3> bands = {}) {
    const int rows = static_cast<int>(seconds * kRate);
    std::vector<float> data(static_cast<std::size_t>(rows) * spatial::kAudioBins, level);
    std::array<std::vector<spatial::AudioOnset>, spatial::kOnsetSources> onsets{};
    for (int b = 0; b < 3; ++b) {
        onsets[static_cast<std::size_t>(b)] = std::move(bands[static_cast<std::size_t>(b)]);
    }
    return std::make_shared<const spatial::AudioHistory>(
        spatial::AudioHistory::whole(kRate, 0.0, std::move(data), std::move(onsets)));
}

spatial::FieldSpec moving() {
    spatial::FieldSpec f;
    f.name = "res";
    f.kind = spatial::FieldKind::Resonance;
    f.seed = 5;
    f.resonance.modes = 10.0f;
    f.resonance.contrast = 0.0f;
    f.resonance.idle = 0.3f;
    f.resonance.sources = {
        spatial::ResonanceSource{spatial::ResonanceSourceKind::Point, {0.0f, 2.0f, 0.0f}, 4, 0.0f, 0.5f, 0.0f, 1.0f},
        spatial::ResonanceSource{spatial::ResonanceSourceKind::Lattice, {3.0f, 0.0f, -2.0f}, 3, 0.5f, 1.0f, 0.0f, 1.0f}};
    for (auto& s : f.resonance.sources) {
        s.drift = 10.0f;
        s.driftPeriod = 40.0f;
        s.spin = 0.2f;
    }
    return f;
}

double wrap(double a) {
    a = std::fmod(a, kTwoPi);
    return a < 0.0 ? a + kTwoPi : a;
}

} // namespace

TEST_CASE("A moving source's modes wander within its drift and turn, as a pure function of the clock",
          "[unit][resonance][motion]") {
    const auto f = moving();
    auto still = f;
    for (auto& s : still.resonance.sources) {
        s.drift = 0.0f;
        s.spin = 0.0f;
    }
    const auto rest = spatial::evaluateResonance(still, nullptr, 30.0);
    const auto at0 = spatial::evaluateResonance(f, nullptr, 0.0);
    const auto at30 = spatial::evaluateResonance(f, nullptr, 30.0);
    REQUIRE(at30.modeCount == 10);
    int movedModes = 0;
    for (int i = 0; i < at30.modeCount; ++i) {
        const auto si = static_cast<std::size_t>(i);
        const auto& src = f.resonance.sources[static_cast<std::size_t>(at30.source[si])];
        // Still, a mode sits on its source; moving, it wanders, but never further than the drift (no audio).
        CHECK(glm::vec3(rest.modes[si].place) == src.position);
        const glm::vec3 off = glm::vec3(at30.modes[si].place) - src.position;
        CHECK(off.y == 0.0f);
        CHECK(std::abs(off.x) <= 10.0f + 1e-4f);
        CHECK(std::abs(off.z) <= 10.0f + 1e-4f);
        movedModes += glm::length(glm::vec3(at30.modes[si].place) - glm::vec3(at0.modes[si].place)) > 0.5f ? 1 : 0;
        // The shape's wavenumber and order are untouched: the figure moves, it is not re-drawn.
        CHECK(at30.modes[si].shape.x == rest.modes[si].shape.x);
        CHECK(at30.modes[si].shape.y == rest.modes[si].shape.y);
        // A lattice turns as one at `spin`: theta advances by spin x t.
        if (src.kind == spatial::ResonanceSourceKind::Lattice) {
            const double turned = wrap(static_cast<double>(at30.modes[si].shape.z) - static_cast<double>(rest.modes[si].shape.z));
            CHECK(turned == Approx(wrap(0.2 * 30.0)).margin(1e-4));
        }
    }
    CHECK(movedModes == at30.modeCount);
    // Point modes turn at their own rates, some against the others.
    int forwards = 0;
    int backwards = 0;
    for (int i = 0; i < at30.modeCount; ++i) {
        const auto si = static_cast<std::size_t>(i);
        if (f.resonance.sources[static_cast<std::size_t>(at30.source[si])].kind != spatial::ResonanceSourceKind::Point) {
            continue;
        }
        const auto later = spatial::evaluateResonance(f, nullptr, 30.1);
        const double d = static_cast<double>(later.modes[si].shape.z) - static_cast<double>(at30.modes[si].shape.z);
        const double step = std::remainder(d, kTwoPi);
        (step > 0.0 ? forwards : backwards) += 1;
        CHECK(std::abs(step) <= 1.3 * 0.2 * 0.1 + 1e-5);
    }
    CHECK(forwards + backwards > 0);
}

TEST_CASE("Motion is exact under seek, and continuous in time", "[unit][resonance][motion]") {
    auto f = moving();
    f.resonance.ripple = 1.0f;
    f.resonance.sources[0].driftAudio = 0.5f;
    const auto h = history(30.0, 0.3f, {{{{4.0, 1.0f}, {9.0, 0.8f}}, {{6.5, 1.0f}}, {{8.8, 0.6f}}}});
    spatial::ResonanceCache cache;
    spatial::ResonanceState played;
    for (double t = 0.0; t <= 9.5 + 1e-9; t += 1.0 / 30.0) {
        played = spatial::evaluateResonance(f, h, t, &cache);
    }
    const auto fresh = spatial::evaluateResonance(f, h, played.clock);
    (void)spatial::evaluateResonance(f, h, 1.0, &cache);
    const auto sought = spatial::evaluateResonance(f, h, played.clock, &cache);
    REQUIRE(fresh.count == played.count);
    REQUIRE(fresh.count > fresh.modeCount); // the ripples are there
    for (int i = 0; i < fresh.count; ++i) {
        const auto si = static_cast<std::size_t>(i);
        CHECK(played.modes[si].shape == fresh.modes[si].shape);
        CHECK(played.modes[si].place == fresh.modes[si].place);
        CHECK(played.modes[si].drive == fresh.modes[si].drive);
        CHECK(sought.modes[si].place == fresh.modes[si].place);
        CHECK(sought.modes[si].drive == fresh.modes[si].drive);
    }
    // A frame later, a mode has moved a little, not jumped (frame-rate independent: a function of the clock).
    const auto next = spatial::evaluateResonance(f, h, played.clock + 1.0 / 60.0);
    for (int i = 0; i < fresh.modeCount; ++i) {
        const auto si = static_cast<std::size_t>(i);
        CHECK(glm::length(glm::vec3(next.modes[si].place) - glm::vec3(fresh.modes[si].place)) < 0.1f);
    }
}

TEST_CASE("An onset launches a ring that travels at its speed and decays; it is gone after its life",
          "[unit][resonance][motion]") {
    auto f = moving();
    f.resonance.ripple = 1.5f;
    f.resonance.rippleSpeed = 40.0f;
    f.resonance.rippleLife = 1.5f;
    f.resonance.rippleWidth = 0.3f; // a packet narrower than its radius, so inside and outside are quiet
    const auto h = history(20.0, 0.0f, {{{{2.0, 1.0f}}, {}, {}}});
    const auto before = spatial::evaluateResonance(f, h, 1.9);
    CHECK(before.count == before.modeCount);
    const auto s1 = spatial::evaluateResonance(f, h, 3.0);
    REQUIRE(s1.count == s1.modeCount + 1);
    const auto& ring = s1.modes[static_cast<std::size_t>(s1.modeCount)];
    CHECK(ring.shape.w == 2.0f);                    // a ripple
    CHECK(ring.shape.z == Approx(40.0f));           // a kick's ring: speed x age (1 s)
    CHECK(ring.drive.x == Approx(1.5f * std::exp(-1.0f / 1.5f)).epsilon(1e-3)); // heard 1, gain 1, e^{-age/life}
    CHECK(ring.drive.z == Approx(0.1f));            // the low band's place on the dispersion
    CHECK(glm::length(glm::vec2(ring.place.x, ring.place.z)) <= f.resonance.rippleArea + 1e-3f);
    // The packet sits on its radius: |phi| at the ring is far larger than well inside or outside it.
    const glm::vec3 c(ring.place.x, 0.0f, ring.place.z);
    const float onRing = std::abs(spatial::resonanceModeShape(ring, c + glm::vec3(ring.shape.z, 0.0f, 0.0f)));
    const float inside = std::abs(spatial::resonanceModeShape(ring, c + glm::vec3(0.2f * ring.shape.z, 0.0f, 0.0f)));
    const float outside = std::abs(spatial::resonanceModeShape(ring, c + glm::vec3(3.0f * ring.shape.z, 0.0f, 0.0f)));
    CHECK(onRing > 0.5f);
    CHECK(inside < 0.05f * onRing);
    CHECK(outside < 0.05f * onRing);
    // Later it is further out and fainter; after four lives it is gone.
    const auto s2 = spatial::evaluateResonance(f, h, 4.0);
    const auto& ring2 = s2.modes[static_cast<std::size_t>(s2.modeCount)];
    CHECK(ring2.shape.z == Approx(80.0f));
    CHECK(ring2.drive.x < ring.drive.x);
    CHECK(spatial::evaluateResonance(f, h, 2.0 + 4.0 * 1.5 + 0.1).count == s1.modeCount);
    // Off: no rows.
    auto off = f;
    off.resonance.ripple = 0.0f;
    CHECK(spatial::evaluateResonance(off, h, 3.0).count == s1.modeCount);
}

TEST_CASE("A snare's ring is faster, shorter and more local than a kick's; only the newest rings survive",
          "[unit][resonance][motion]") {
    auto f = moving();
    f.resonance.ripple = 1.0f;
    f.resonance.rippleCount = 3.0f;
    const auto h = history(20.0, 0.0f, {{{{5.0, 1.0f}}, {{5.0, 1.0f}}, {}}});
    const auto s = spatial::evaluateResonance(f, h, 5.5);
    REQUIRE(s.count == s.modeCount + 2);
    const auto& kick = s.modes[static_cast<std::size_t>(s.modeCount)];      // same time: the low band first
    const auto& snare = s.modes[static_cast<std::size_t>(s.modeCount + 1)];
    CHECK(snare.shape.z > kick.shape.z);       // faster
    CHECK(snare.shape.x > kick.shape.x);       // a shorter wavelength
    CHECK(snare.place.w > 0.0f);               // local ...
    CHECK(kick.place.w == 0.0f);               // ... where the kick's ring is not
    CHECK(snare.drive.x < kick.drive.x);       // fainter, and it dies sooner:
    const auto later = spatial::evaluateResonance(f, h, 5.0 + 4.0 * 0.55 * f.resonance.rippleLife + 0.05);
    CHECK(later.count == later.modeCount + 1); // the kick's ring is still alive, the snare's gone
    // Many onsets: only the newest rippleCount.
    std::vector<spatial::AudioOnset> many;
    for (int k = 0; k < 10; ++k) {
        many.push_back({3.0 + 0.1 * k, 1.0f});
    }
    const auto busy = history(20.0, 0.0f, {{many, {}, {}}});
    const auto b = spatial::evaluateResonance(f, busy, 4.0);
    REQUIRE(b.count == b.modeCount + 3);
    for (int j = 0; j < 3; ++j) { // the newest three: 3.9, 3.8, 3.7 s
        CHECK(b.modes[static_cast<std::size_t>(b.modeCount + j)].shape.z ==
              Approx(f.resonance.rippleSpeed * (0.1f + 0.1f * static_cast<float>(j))).epsilon(1e-3));
    }
}

TEST_CASE("A source's amplitude scales its modes after the limiter; its gain does not, at saturation",
          "[unit][resonance][motion]") {
    spatial::FieldSpec f;
    f.name = "res";
    f.kind = spatial::FieldKind::Resonance;
    f.resonance.modes = 6.0f;
    f.resonance.contrast = 0.0f;
    f.resonance.limit = 0.05f; // saturated
    f.resonance.sources = {spatial::ResonanceSource{}};
    const auto h = history(10.0, 0.8f);
    const auto base = spatial::evaluateResonance(f, h, 6.0);
    auto halfAmp = f;
    halfAmp.resonance.sources[0].amplitude = 0.5f;
    auto halfGain = f;
    halfGain.resonance.sources[0].gain = 0.5f;
    const auto a = spatial::evaluateResonance(halfAmp, h, 6.0);
    const auto g = spatial::evaluateResonance(halfGain, h, 6.0);
    for (int i = 0; i < base.modeCount; ++i) {
        const auto si = static_cast<std::size_t>(i);
        CHECK(a.modes[si].drive.y == Approx(0.5f * base.modes[si].drive.y));
        CHECK(a.modes[si].drive.w == Approx(0.5f * base.modes[si].drive.w));
        CHECK(g.modes[si].drive.y == Approx(base.modes[si].drive.y).epsilon(0.02)); // the limiter undoes the gain
    }
}

TEST_CASE("Motion round-trips through JSON, hashes, validates and registers its parameters", "[unit][resonance][motion]") {
    auto f = moving();
    f.resonance.ripple = 0.8f;
    f.resonance.rippleSpeed = 12.0f;
    f.resonance.rippleCount = 4.0f;
    f.resonance.sources[1].amplitude = 0.3f;
    f.resonance.sources[0].driftAudio = 0.4f;
    const nlohmann::json j = f.toJson();
    CHECK(j["resonance"]["sources"][0]["drift"] == 10.0f);
    auto back = spatial::FieldSpec::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->resonance.sources == f.resonance.sources);
    CHECK(back->resonance.ripple == 0.8f);
    CHECK(back->resonance.rippleSpeed == 12.0f);
    CHECK(back->resonance.rippleCount == 4.0f);
    CHECK(back->structuralHash() == f.structuralHash());
    auto other = f;
    other.resonance.sources[0].spin = 0.3f;
    CHECK(other.structuralHash() != f.structuralHash());
    // A still source writes none of it (ADR-1400 files are unchanged).
    spatial::FieldSpec plain;
    plain.kind = spatial::FieldKind::Resonance;
    plain.resonance.sources = {spatial::ResonanceSource{}};
    const nlohmann::json pj = plain.toJson();
    CHECK_FALSE(pj["resonance"]["sources"][0].contains("drift"));
    CHECK_FALSE(pj["resonance"].contains("ripple"));
    // Validation bounds.
    auto bad = f;
    bad.resonance.sources[0].driftPeriod = 0.1f;
    CHECK_FALSE(bad.validate().has_value());
    bad = f;
    bad.resonance.sources[0].drift = -1.0f;
    CHECK_FALSE(bad.validate().has_value());
    bad = f;
    bad.resonance.rippleCount = 9.0f;
    CHECK_FALSE(bad.validate().has_value());
    bad = f;
    bad.resonance.rippleSpeed = 0.0f;
    CHECK_FALSE(bad.validate().has_value());
    CHECK(f.validate().has_value());
    // Parameters: the figure's motion and the ripples are performable.
    params::ParameterSet ps;
    const auto p = scene::registerFieldParameters(ps, f, "field/res/");
    for (const char* path : {"field/res/resonance/source/0/drift", "field/res/resonance/source/0/spin",
                             "field/res/resonance/source/1/amplitude", "field/res/resonance/ripple",
                             "field/res/resonance/rippleSpeed", "field/res/resonance/rippleLife"}) {
        INFO(path);
        CHECK(ps.find(path) != nullptr);
    }
    auto* drift = dynamic_cast<params::Parameter<float>*>(ps.find("field/res/resonance/source/0/drift"));
    REQUIRE(drift != nullptr);
    drift->setBase(22.0f);
    drift->resetFinal();
    spatial::FieldSpec live;
    scene::applyFieldParameters(p, f, live);
    CHECK(live.resonance.sources[0].drift == Approx(22.0f));
    // A relief reader's mix is performable too.
    spatial::FieldSpec relief;
    relief.name = "relief";
    relief.kind = spatial::FieldKind::Resonance;
    relief.reference = "res";
    relief.resonance.channel = spatial::ResonanceChannel::Relief;
    params::ParameterSet ps2;
    (void)scene::registerFieldParameters(ps2, relief, "field/relief/");
    CHECK(ps2.find("field/relief/resonance/reliefVibration") != nullptr);
}
