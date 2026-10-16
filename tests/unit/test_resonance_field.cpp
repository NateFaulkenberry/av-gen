// ADR-1400: the resonance field on the CPU -- mode shapes, excitation, persistence and damping, the
// limiter, determinism under seek and reset, the channels, readers, JSON, validation and parameters.
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
#include <functional>
#include <memory>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr double kRate = 100.0;

// The bin whose centre is nearest `hz` on the history's log axis.
int binOf(float hz) {
    const float pos = std::log2(hz / spatial::kAudioMinHz) / std::log2(spatial::kAudioMaxHz / spatial::kAudioMinHz);
    return std::clamp(static_cast<int>(std::lround(pos * (spatial::kAudioBins - 1))), 0, spatial::kAudioBins - 1);
}

// A whole-track history of `seconds`, row r's bins set by `level(t, bin)`.
std::shared_ptr<const spatial::AudioHistory> history(double seconds, const std::function<float(double, int)>& level,
                                                     std::vector<spatial::AudioOnset> lowOnsets = {}) {
    const int rows = static_cast<int>(seconds * kRate);
    std::vector<float> data(static_cast<std::size_t>(rows) * spatial::kAudioBins, 0.0f);
    for (int r = 0; r < rows; ++r) {
        for (int b = 0; b < spatial::kAudioBins; ++b) {
            data[static_cast<std::size_t>(r) * spatial::kAudioBins + static_cast<std::size_t>(b)] =
                level(static_cast<double>(r) / kRate, b);
        }
    }
    std::array<std::vector<spatial::AudioOnset>, spatial::kOnsetSources> onsets{};
    onsets[0] = std::move(lowOnsets);
    return std::make_shared<const spatial::AudioHistory>(
        spatial::AudioHistory::whole(kRate, 0.0, std::move(data), std::move(onsets)));
}

// A tone at `hz` (bins within one of it) from `on` to `off` seconds.
std::function<float(double, int)> tone(float hz, double on, double off) {
    const int centre = binOf(hz);
    return [=](double t, int b) { return (t >= on && t < off && std::abs(b - centre) <= 1) ? 1.0f : 0.0f; };
}

spatial::FieldSpec resonator(float modes = 8.0f) {
    spatial::FieldSpec f;
    f.name = "res";
    f.kind = spatial::FieldKind::Resonance;
    f.resonance.modes = modes;
    f.resonance.fLow = 40.0f;
    f.resonance.fHigh = 5000.0f;
    f.resonance.contrast = 0.0f;
    f.resonance.idle = 0.0f;
    f.resonance.limit = 100.0f; // effectively off unless a case wants it
    return f;
}

float envelopeOf(const spatial::ResonanceState& s, int mode) {
    return s.envelope[static_cast<std::size_t>(mode)];
}

} // namespace

TEST_CASE("Resonance mode shapes are finite, smooth at a point source and normalised", "[unit][resonance]") {
    spatial::ResonanceMode point0;
    point0.shape = glm::vec4(1.3f, 0.0f, 0.0f, 0.0f);
    spatial::ResonanceMode point3;
    point3.shape = glm::vec4(1.3f, 3.0f, 0.7f, 0.0f);
    spatial::ResonanceMode lattice;
    lattice.shape = glm::vec4(0.9f, 5.0f, 0.2f, 1.0f);
    // Finite everywhere, including the source itself.
    for (const auto* m : {&point0, &point3, &lattice}) {
        float e2 = -1.0f;
        const float at0 = spatial::resonanceModeShape(*m, glm::vec3(0.0f), &e2);
        CHECK(std::isfinite(at0));
        CHECK(e2 >= 0.0f);
    }
    // m = 0 is smooth through r = 0 (no cone tip): symmetric differences tend to zero slope.
    const float c = spatial::resonanceModeShape(point0, glm::vec3(0.0f));
    const float e = spatial::resonanceModeShape(point0, glm::vec3(1e-3f, 0.0f, 0.0f));
    CHECK(std::abs(e - c) < 1e-4f);
    CHECK(c == Approx(1.0f).margin(1e-5)); // the central antinode: sqrt(2) cos(-pi/4)
    // m > 0 vanishes at its source (angular nodes meet there).
    CHECK(std::abs(spatial::resonanceModeShape(point3, glm::vec3(0.0f))) < 1e-6f);
    // A lattice's mean square over a large patch is ~1 (its env2), as the sand normalisation assumes.
    double sum2 = 0.0;
    int n = 0;
    for (int i = 0; i < 200; ++i) {
        for (int j = 0; j < 200; ++j) {
            const float v = spatial::resonanceModeShape(lattice, glm::vec3(0.37f * i - 37.0f, 0.0f, 0.41f * j - 41.0f));
            sum2 += static_cast<double>(v) * v;
            ++n;
        }
    }
    CHECK(sum2 / n == Approx(1.0).margin(0.1));
    // A point mode's amplitude falls off with distance, slowly (cylindrically), and reach bounds it.
    point0.place.w = 0.0f;
    const float nearAmp = std::abs(spatial::resonanceModeShape(point0, glm::vec3(5.0f, 0.0f, 0.0f)));
    float farEnv = 0.0f;
    float nearEnv = 0.0f;
    (void)spatial::resonanceModeShape(point0, glm::vec3(5.0f, 0.0f, 0.0f), &nearEnv);
    (void)spatial::resonanceModeShape(point0, glm::vec3(80.0f, 0.0f, 0.0f), &farEnv);
    CHECK(farEnv < nearEnv);
    CHECK(farEnv > 0.05f * nearEnv);
    point0.place.w = 10.0f;
    float boundedEnv = 0.0f;
    (void)spatial::resonanceModeShape(point0, glm::vec3(80.0f, 0.0f, 0.0f), &boundedEnv);
    CHECK(boundedEnv < 0.05f * farEnv);
    CHECK(std::isfinite(nearAmp));
}

TEST_CASE("Modes sit on the dispersion axis: low modes hear low notes and draw long waves", "[unit][resonance]") {
    const auto f = resonator(8.0f);
    const auto s = spatial::evaluateResonance(f, nullptr, 0.0);
    REQUIRE(s.count == 8);
    CHECK(s.frequency[0] == Approx(40.0f));
    CHECK(s.frequency[7] == Approx(5000.0f));
    for (int i = 1; i < 8; ++i) {
        CHECK(s.frequency[static_cast<std::size_t>(i)] > s.frequency[static_cast<std::size_t>(i - 1)]);
        CHECK(s.modes[static_cast<std::size_t>(i)].shape.x > s.modes[static_cast<std::size_t>(i - 1)].shape.x); // k grows
    }
    CHECK(s.modes[0].shape.x == Approx(6.2831853f / f.resonance.lambdaMax));
    CHECK(s.modes[7].shape.x == Approx(6.2831853f / f.resonance.lambdaMin));
    // Low modes draw simple figures: the lowest mode's angular order is at most 1.
    CHECK(s.modes[0].shape.y <= 1.0f);
    // A fractional count fades the last mode in.
    const auto half = spatial::evaluateResonance(resonator(8.25f), nullptr, 0.0);
    REQUIRE(half.count == 9);
    CHECK(half.weight[8] == Approx(0.25f));
    CHECK(half.weight[7] == Approx(1.0f));
}

TEST_CASE("Frequency-selective excitation: a low tone rings the low modes, a high tone the high ones",
          "[unit][resonance]") {
    const auto f = resonator(8.0f);
    const auto low = history(10.0, tone(50.0f, 0.0, 10.0));
    const auto high = history(10.0, tone(3500.0f, 0.0, 10.0));
    const auto sl = spatial::evaluateResonance(f, low, 9.0);
    const auto sh = spatial::evaluateResonance(f, high, 9.0);
    // (a three-bin tone is a fraction of the resonance curve's weighted mean, which a broadband level fills)
    CHECK(envelopeOf(sl, 0) > 0.12f);
    CHECK(envelopeOf(sl, 0) > 10.0f * envelopeOf(sl, 7));
    CHECK(envelopeOf(sh, 7) > 0.12f);
    CHECK(envelopeOf(sh, 7) > 10.0f * envelopeOf(sh, 0));
    // A steady tone builds up to its level (the kernel has unit area): the resonant mode reads about the
    // filtered drive, never above the tone's own level.
    CHECK(envelopeOf(sl, 0) <= 1.0f);
}

TEST_CASE("Sustained excitation builds up, rings down, and low modes ring longest", "[unit][resonance]") {
    auto f = resonator(8.0f);
    f.resonance.persistence = 2.0f;
    f.resonance.buildup = 0.3f;
    // A broad tone (every bin) from 1 s to 4 s: every mode is driven alike.
    const auto h = history(12.0, [](double t, int) { return (t >= 1.0 && t < 4.0) ? 0.8f : 0.0f; });
    std::vector<float> lowRise;
    for (double t = 1.05; t < 4.0; t += 0.25) {
        lowRise.push_back(envelopeOf(spatial::evaluateResonance(f, h, t), 0));
    }
    for (std::size_t i = 1; i < lowRise.size(); ++i) {
        CHECK(lowRise[i] >= lowRise[i - 1]); // buildup is monotone under a steady drive
    }
    CHECK(lowRise.front() < 0.5f * lowRise.back());
    // After the tone stops, both ring down; the high mode has lost more of its level a second later.
    const auto atStop = spatial::evaluateResonance(f, h, 4.0);
    const auto later = spatial::evaluateResonance(f, h, 5.0);
    const float lowKept = envelopeOf(later, 0) / envelopeOf(atStop, 0);
    const float highKept = envelopeOf(later, 7) / envelopeOf(atStop, 7);
    CHECK(lowKept < 1.0f);
    CHECK(highKept < lowKept);
    // And long after, silence: the field is quiet again (idle is 0 here).
    const auto quiet = spatial::evaluateResonance(f, h, 11.9);
    for (int i = 0; i < 8; ++i) {
        CHECK(envelopeOf(quiet, i) < 0.02f);
    }
}

TEST_CASE("An onset strikes the modes its band reaches and they ring down", "[unit][resonance]") {
    auto f = resonator(8.0f);
    f.resonance.transient = 1.0f;
    const auto h = history(6.0, [](double, int) { return 0.0f; }, {{2.0, 1.0f}});
    CHECK(envelopeOf(spatial::evaluateResonance(f, h, 1.99), 0) == 0.0f);
    const auto struck = spatial::evaluateResonance(f, h, 2.1);
    CHECK(envelopeOf(struck, 0) > 0.5f);                          // the low source strikes the low modes ...
    CHECK(envelopeOf(struck, 7) < 0.01f * envelopeOf(struck, 0)); // ... and not the high ones
    CHECK(envelopeOf(spatial::evaluateResonance(f, h, 3.0), 0) < envelopeOf(struck, 0));
}

TEST_CASE("The resonance is a pure function of the clock: play, seek, reset and the cache agree", "[unit][resonance]") {
    auto f = resonator(10.0f);
    f.resonance.contrast = 0.4f;
    f.resonance.idle = 0.1f;
    const auto h = history(20.0, [](double t, int b) {
        return 0.5f + 0.5f * static_cast<float>(std::sin(t * 1.7 + b * 0.31) * std::cos(t * 0.23 * b));
    }, {{3.0, 0.8f}, {7.5, 1.0f}});
    spatial::ResonanceCache cache;
    // Play forwards through the cache...
    spatial::ResonanceState played;
    for (double t = 0.0; t <= 12.0 + 1e-9; t += 1.0 / 30.0) {
        played = spatial::evaluateResonance(f, h, t, &cache);
    }
    // A cache, not a recomputation: playing 12 s filtered each of its 1,201 rows once.
    CHECK(cache.rowsFiltered() <= 1202);
    const double at = played.clock;
    // ... a fresh evaluation (no cache), a seek backwards and forwards through the same cache, and a
    // brand-new cache all land on the same modes, exactly.
    const auto fresh = spatial::evaluateResonance(f, h, at);
    (void)spatial::evaluateResonance(f, h, 2.0, &cache);
    const auto sought = spatial::evaluateResonance(f, h, at, &cache);
    spatial::ResonanceCache reset;
    const auto afterReset = spatial::evaluateResonance(f, h, at, &reset);
    for (int i = 0; i < played.count; ++i) {
        const auto si = static_cast<std::size_t>(i);
        CHECK(played.modes[si].drive == fresh.modes[si].drive);
        CHECK(sought.modes[si].drive == fresh.modes[si].drive);
        CHECK(afterReset.modes[si].drive == fresh.modes[si].drive);
        CHECK(std::isfinite(fresh.modes[si].drive.x));
    }
}

TEST_CASE("Phase is continuous, and dense music rings more modes rather than louder ones", "[unit][resonance]") {
    auto f = resonator(12.0f);
    f.resonance.breath = 0.5f;
    f.resonance.limit = 1.0f;
    const auto dense = history(10.0, [](double, int) { return 1.0f; });
    // Continuity: a millisecond moves every mode's displacement by a small amount.
    const auto a = spatial::evaluateResonance(f, dense, 8.0);
    const auto b = spatial::evaluateResonance(f, dense, 8.001);
    for (int i = 0; i < a.count; ++i) {
        CHECK(std::abs(a.modes[static_cast<std::size_t>(i)].drive.x - b.modes[static_cast<std::size_t>(i)].drive.x) < 0.02f);
    }
    // The limiter holds the total near `limit` however many modes are driven.
    float total = 0.0f;
    for (int i = 0; i < a.count; ++i) {
        const float e = a.modes[static_cast<std::size_t>(i)].drive.y;
        total += e * e;
    }
    CHECK(std::sqrt(total) <= f.resonance.limit * 1.0001f);
    CHECK(a.limiterGain < 0.5f);
    // An evening's clock keeps the phase exact (double) and finite.
    const auto lateNight = spatial::evaluateResonance(f, nullptr, 4.0 * 3600.0 + 0.25);
    CHECK(std::isfinite(lateNight.modes[0].drive.x));
}

TEST_CASE("Silence keeps an idle figure; idle 0 and no audio is a still field", "[unit][resonance]") {
    auto f = resonator(6.0f);
    f.resonance.idle = 0.2f;
    const auto s = spatial::evaluateResonance(f, nullptr, 3.0);
    CHECK(envelopeOf(s, 0) > 0.05f);
    CHECK(envelopeOf(s, 5) == 0.0f); // only the lowest three breathe
    f.resonance.idle = 0.0f;
    const auto still = spatial::evaluateResonance(f, nullptr, 3.0);
    for (int i = 0; i < still.count; ++i) {
        CHECK(still.modes[static_cast<std::size_t>(i)].drive.x == 0.0f);
        CHECK(still.modes[static_cast<std::size_t>(i)].drive.y == 0.0f);
    }
}

TEST_CASE("Resonance channels: sand gathers on the nodes and the force points at them", "[unit][resonance]") {
    // One mode, a one-direction lattice: phi = sqrt(2) cos(k x), nodes at x = lambda / 4.
    spatial::FieldSpec f = resonator(1.0f);
    spatial::ResonanceState st;
    st.count = 1;
    st.lambdaMin = 8.0f;
    const float k = 6.2831853f / 8.0f;
    st.modes[0].shape = glm::vec4(k, 1.0f, 0.0f, 1.0f);
    st.modes[0].drive = glm::vec4(0.3f, 1.0f, 0.0f, 1.0f); // d, e, u, the memory (s)
    f.resonance.channel = spatial::ResonanceChannel::Sand;
    const float atNode = spatial::resonanceScalar(st, f, glm::vec3(2.0f, 0.0f, 0.0f));
    const float atAntinode = spatial::resonanceScalar(st, f, glm::vec3(0.0f, 0.0f, 0.0f));
    CHECK(atNode == Approx(1.0f).margin(1e-4)); // sqrt(Ebar) with Ebar = e^2 = 1
    CHECK(atAntinode < 0.01f);
    f.resonance.channel = spatial::ResonanceChannel::Amplitude;
    CHECK(spatial::resonanceScalar(st, f, glm::vec3(0.0f)) == Approx(1.41421356f).margin(1e-4));
    f.resonance.channel = spatial::ResonanceChannel::Displacement;
    CHECK(spatial::resonanceScalar(st, f, glm::vec3(0.0f)) == Approx(0.3f * 1.41421356f).margin(1e-4));
    // The force points from the antinodes toward the node at x = 2.
    f.resonance.channel = spatial::ResonanceChannel::Force;
    CHECK(spatial::resonanceVector(st, f, glm::vec3(1.0f, 0.0f, 0.0f)).x > 0.0f);
    CHECK(spatial::resonanceVector(st, f, glm::vec3(3.0f, 0.0f, 0.0f)).x < 0.0f);
    CHECK(std::abs(spatial::resonanceVector(st, f, glm::vec3(2.0f, 0.0f, 0.0f)).x) < 1e-2f);
    // The gradient of D: d/dx (0.3 sqrt2 cos kx) at x = 2 is -0.3 sqrt2 k.
    f.resonance.channel = spatial::ResonanceChannel::Gradient;
    CHECK(spatial::resonanceVector(st, f, glm::vec3(2.0f, 0.0f, 0.0f)).x == Approx(-0.3f * 1.41421356f * k).epsilon(0.02)); // central differences at lambdaMin / 24
}

TEST_CASE("A reader shares its resonator's modes and hears only its window", "[unit][resonance]") {
    spatial::FieldSet set;
    auto owner = resonator(8.0f);
    owner.resonance.idle = 0.5f;
    owner.resonance.channel = spatial::ResonanceChannel::Amplitude;
    set.fields.push_back(owner);
    spatial::FieldSpec reader;
    reader.name = "reader";
    reader.kind = spatial::FieldKind::Resonance;
    reader.reference = "res";
    reader.resonance.channel = spatial::ResonanceChannel::Amplitude;
    set.fields.push_back(reader);
    spatial::ResonanceCache cache;
    spatial::resolveResonance(set, cache, 5.0);
    REQUIRE(set.fields[0].resonanceState != nullptr);
    CHECK(set.fields[1].resonanceState == nullptr); // a reader holds no state of its own
    REQUIRE(spatial::resonanceStateOf(set.fields[1], &set) == set.fields[0].resonanceState.get());
    const glm::vec3 p(3.0f, 0.0f, 1.0f);
    const float ownerValue = spatial::sampleScalar(set.fields[0], p, 5.0, &set);
    CHECK(ownerValue > 0.0f);
    CHECK(spatial::sampleScalar(set.fields[1], p, 5.0, &set) == Approx(ownerValue));
    // A window above the idle modes hears nothing; a missing resonator is silence.
    set.fields[1].resonance.listenLow = 0.6f;
    set.fields[1].resonance.listenWidth = 0.01f;
    CHECK(spatial::sampleScalar(set.fields[1], p, 5.0, &set) == 0.0f);
    set.fields[1].reference = "nobody";
    CHECK(spatial::sampleScalar(set.fields[1], p, 5.0, &set) == 0.0f);
    // A vector channel is a vector field.
    set.fields[1].reference = "res";
    set.fields[1].resonance.listenLow = 0.0f;
    set.fields[1].resonance.channel = spatial::ResonanceChannel::Force;
    CHECK(set.fields[1].type() == spatial::FieldType::Vector);
    CHECK(std::isfinite(spatial::sampleVector(set.fields[1], p, 5.0, &set).x));
}

TEST_CASE("Resonance fields round-trip through JSON, validate and hash their spec", "[unit][resonance]") {
    auto f = resonator(9.5f);
    f.resonance.sources = {spatial::ResonanceSource{spatial::ResonanceSourceKind::Point, {0.0f, 6.0f, 0.0f}, 5, 0.0f, 0.6f, 0.0f, 1.0f},
                           spatial::ResonanceSource{spatial::ResonanceSourceKind::Lattice, {10.0f, 0.0f, 3.0f}, 6, 0.4f, 1.0f, 80.0f, 0.7f}};
    f.resonance.channel = spatial::ResonanceChannel::Sand;
    f.resonance.listenAlongAxis = true;
    const nlohmann::json j = f.toJson();
    auto back = spatial::FieldSpec::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->kind == spatial::FieldKind::Resonance);
    CHECK(back->resonance.sources == f.resonance.sources);
    CHECK(back->resonance.channel == spatial::ResonanceChannel::Sand);
    CHECK(back->structuralHash() == f.structuralHash());
    auto moved = f;
    moved.resonance.persistence = 3.0f;
    CHECK(moved.structuralHash() != f.structuralHash());
    // Another kind does not carry (or hash) a resonance block.
    spatial::FieldSpec radial;
    CHECK_FALSE(radial.toJson().contains("resonance"));
    // Validation.
    auto bad = f;
    bad.resonance.modes = 30.0f;
    CHECK_FALSE(bad.validate().has_value());
    bad = f;
    bad.resonance.fHigh = 10.0f;
    CHECK_FALSE(bad.validate().has_value());
    bad = f;
    bad.resonance.sources[1].order = 0; // a lattice needs a direction
    CHECK_FALSE(bad.validate().has_value());
    bad = f;
    bad.reference = f.name;
    CHECK_FALSE(bad.validate().has_value());
    nlohmann::json unknown = j;
    unknown["resonance"]["channel"] = "loudness";
    CHECK_FALSE(spatial::FieldSpec::fromJson(unknown).has_value());
}

TEST_CASE("Resonance parameters register for a resonator and a reader and apply as finals", "[unit][resonance]") {
    auto f = resonator(8.0f);
    f.resonance.sources = {spatial::ResonanceSource{}};
    params::ParameterSet ps;
    const auto p = scene::registerFieldParameters(ps, f, "field/res/");
    REQUIRE(ps.find("field/res/resonance/modes") != nullptr);
    REQUIRE(ps.find("field/res/resonance/persistence") != nullptr);
    REQUIRE(ps.find("field/res/resonance/source/0/position") != nullptr);
    REQUIRE(ps.find("field/res/resonance/sharpness") != nullptr);
    auto* modes = dynamic_cast<params::Parameter<float>*>(ps.find("field/res/resonance/modes"));
    REQUIRE(modes != nullptr);
    modes->setBase(14.5f);
    modes->resetFinal();
    spatial::FieldSpec live;
    scene::applyFieldParameters(p, f, live);
    CHECK(live.resonance.modes == Approx(14.5f));

    spatial::FieldSpec reader;
    reader.name = "reader";
    reader.kind = spatial::FieldKind::Resonance;
    reader.reference = "res";
    params::ParameterSet ps2;
    (void)scene::registerFieldParameters(ps2, reader, "field/reader/");
    CHECK(ps2.find("field/reader/resonance/modes") == nullptr); // a reader has no resonator to tune
    CHECK(ps2.find("field/reader/resonance/listenLow") != nullptr);
}

TEST_CASE("The sand's memory settles slower and remembers longer than the vibration", "[unit][resonance]") {
    auto f = resonator(8.0f);
    f.resonance.memory = 2.0f;
    // A broad tone from 1 s to 2 s.
    const auto h = history(14.0, [](double t, int) { return (t >= 1.0 && t < 2.0) ? 1.0f : 0.0f; });
    const auto early = spatial::evaluateResonance(f, h, 1.3);
    const auto late = spatial::evaluateResonance(f, h, 6.0);
    // Early in the tone the vibration leads; long after it the memory holds more of what it reached.
    CHECK(early.envelope[0] > early.memory[0]);
    CHECK(late.memory[0] > late.envelope[0]);
    CHECK(late.memory[0] > 0.0f);
    // memory 0 is the vibration's own envelope (bar the rise floor): the two agree.
    f.resonance.memory = 0.0f;
    const auto same = spatial::evaluateResonance(f, h, 2.5);
    CHECK(same.memory[0] == Approx(same.envelope[0]).epsilon(1e-4));
}
