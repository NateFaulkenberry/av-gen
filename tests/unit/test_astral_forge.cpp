// THE ASTRAL FORGE in production (ADR-1221, ADR-1222): the `"astral"` block, its parameters (reached by routes and
// MIDI like any other), the two conductors and the quality tier. The GPU half is
// tests/rendering/test_astral_forge_gpu.cpp.

#include "app/interactive_resolution.hpp"
#include "assets/asset_registry.hpp"
#include "control/control_map.hpp"
#include "control/midi.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/quality_policy.hpp"
#include "rendering/render_quality.hpp"
#include "scene/astral_forge.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"
#include "spatial/audio_history.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>

using namespace avgen;
using Catch::Approx;

namespace {

nlohmann::json sceneWithAstral(const nlohmann::json& astral) {
    return nlohmann::json{{"format", "avgen-scene"}, {"version", 1}, {"name", "astral"},
                          {"camera", {{"mode", 1}, {"position", {0, 0, 25}}, {"target", {0, 0, 0}}, {"fov", 30.0}}},
                          {"astral", astral}};
}

// A synthetic song: 120 bpm for `seconds`, a kick on every beat (strong on each 16th), one verse.
astral::SongAnalysis syntheticSong(double seconds) {
    astral::SongAnalysis s;
    s.duration = seconds;
    s.tempoBpm = 120.0f;
    s.hopRate = 93.75f;
    s.hops = static_cast<int>(seconds * s.hopRate);
    s.env0.assign(static_cast<std::size_t>(s.hops), glm::vec4(0.5f));
    s.env1.assign(static_cast<std::size_t>(s.hops), glm::vec4(0.5f));
    s.centroid.assign(static_cast<std::size_t>(s.hops), 0.5f);
    for (double t = 0.0; t < seconds; t += 0.5) {
        s.beats.push_back(t);
        s.kickT.push_back(static_cast<float>(t));
        s.kickS.push_back(std::fmod(t, 8.0) < 1e-6 ? 1.0f : 0.3f);
    }
    s.sections.push_back(astral::Section{0.0, seconds, 0, 0.8f, 0.8f, "verse"});
    return s;
}

struct Comp {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet params;
    params::Modulator modulator;
    explicit Comp(const nlohmann::json& scene) {
        auto c = scene::Composition::fromJson(scene, registry);
        REQUIRE(c.has_value());
        comp = std::move(*c);
        comp->attach(params, modulator);
    }
    void frame(double t) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = t;
        comp->update(ft);
    }
};

} // namespace

TEST_CASE("the astral block parses, round-trips and refuses what it does not know", "[astral][adr1221]") {
    auto a = scene::astralFromJson(nlohmann::json::parse(R"({"particles": 1048576, "grid": 160, "conductor": "song",
        "camera": false, "controls": {"zoom": 1.2, "god": 4, "palette": 0.5}})"));
    REQUIRE(a.has_value());
    CHECK(a->enabled);
    CHECK(a->particles == 1048576u);
    CHECK(a->gridRes == 160);
    CHECK(a->conductor == scene::AstralConductorMode::Song);
    CHECK_FALSE(a->driveCamera);
    CHECK(a->controls.zoom == Approx(1.2f));
    CHECK(a->controls.god == 4);
    auto back = scene::astralFromJson(scene::astralToJson(*a));
    REQUIRE(back.has_value());
    CHECK(back->particles == a->particles);
    CHECK(back->controls.palette == Approx(0.5f));
    CHECK(back->conductor == scene::AstralConductorMode::Song);

    CHECK_FALSE(scene::astralFromJson(nlohmann::json::parse(R"({"partciles": 1})")).has_value());
    CHECK_FALSE(scene::astralFromJson(nlohmann::json::parse(R"({"controls": {"zoon": 1}})")).has_value());
    CHECK_FALSE(scene::astralFromJson(nlohmann::json::parse(R"({"grid": 100})")).has_value());   // not a multiple of 8
    CHECK_FALSE(scene::astralFromJson(nlohmann::json::parse(R"({"conductor": "dj"})")).has_value());
    CHECK_FALSE(scene::astralFromJson(nlohmann::json::parse(R"({"controls": {"god": 9}})")).has_value());
}

TEST_CASE("the astral parameters are real parameters: their finals reach the frame's block", "[astral][adr1221]") {
    Comp c(sceneWithAstral({{"controls", {{"zoom", 1.25}}}}));
    for (const char* leaf : {"summon", "hold", "collapse", "god", "intensity", "palette", "light", "atmosphere", "cameraStyle",
                             "godRays", "legibility", "zoom", "exposure", "camera", "density"}) {
        INFO(leaf);
        CHECK(c.params.find(std::string("astral/") + leaf) != nullptr);
    }
    c.frame(1.0);
    const scene::AstralForge& a = c.comp->scene().astral;
    REQUIRE(a.enabled);
    CHECK(a.live.zoom == Approx(1.25f)); // the authored rest value is the parameter's default
    c.params.findAs<float>("astral/summon")->setBase(1.0f);
    c.params.findAs<int>("astral/god")->setBase(astral::kMachine);
    c.frame(1.0);
    CHECK(c.comp->scene().astral.live.summon == Approx(1.0f));
    CHECK(c.comp->scene().astral.state.C >= 0.999f);               // summoned: fully formed
    CHECK(c.comp->scene().astral.state.archA == Approx(static_cast<float>(astral::kMachine)));
    // the scene file keeps the block
    const nlohmann::json saved = c.comp->toJson();
    REQUIRE(saved.contains("astral"));
    CHECK(saved["astral"]["controls"]["zoom"].get<float>() == Approx(1.25f));
}

TEST_CASE("a route drives an astral parameter, and the conductor places the camera unless told not to",
          "[astral][adr1221]") {
    Comp c(sceneWithAstral(nlohmann::json::object()));
    signals::SignalBus bus;
    const auto id = bus.declare("test.energy", 0.0f, 1.0f, false);
    params::ModRoute r;
    r.source = "test.energy";
    r.target = "astral/hold";
    r.op = params::ModOp::Replace;
    c.modulator.addRoute(r);
    (void)c.modulator.bind(bus, c.params); // the composition's default routes name signals this bus lacks: those stay unbound
    bus.set(id, 1.0f);
    c.params.resetFinals();
    c.modulator.applyRoutes(bus, c.params, 1.0 / 60.0);
    FrameTime ft{};
    ft.renderTime = 2.0;
    c.comp->update(ft);
    CHECK(c.comp->scene().astral.live.hold == Approx(1.0f));
    const glm::vec3 eye = c.comp->scene().astral.state.eye;
    CHECK(glm::length(c.comp->scene().camera.position - eye) < 1e-4f); // the conductor's camera
    c.params.findAs<float>("astral/camera")->setBase(0.0f);
    c.frame(2.0);
    CHECK(glm::length(c.comp->scene().camera.position - glm::vec3(0, 0, 25)) < 1e-3f); // the scene's own
}

TEST_CASE("the song conductor is a pure function of the song second", "[astral][adr1221]") {
    const astral::SongAnalysis song = syntheticSong(40.0);
    const astral::Score score = astral::buildScore(song);
    REQUIRE(score.phrases.size() == 5); // 80 beats: five 16-beat phrases
    astral::Controls k;
    for (double t : {0.5, 7.9, 8.03, 21.7, 33.3}) {
        const astral::State a = astral::conductSong(t, song, score, k);
        const astral::State b = astral::conductSong(t, song, score, k);
        CHECK(a.C == b.C);
        CHECK(a.eye == b.eye);
        CHECK(a.shot == b.shot);
    }
    // a strong kick opens the phrase at 8 s: the god collapses there
    CHECK(astral::conductSong(8.2, song, score, k).C < 0.3f);
    // the controls: a chosen god, a summoned and held face, a closer camera, the opening fade
    k.god = astral::kChoir;
    k.hold = 1.0f;
    k.zoom = 2.0f;
    const astral::State s = astral::conductSong(8.2, song, score, k);
    CHECK(s.archA == Approx(static_cast<float>(astral::kChoir)));
    CHECK(s.C >= 0.97f);
    astral::Controls k1;
    k1.god = astral::kChoir;
    k1.hold = 1.0f;
    const astral::State far = astral::conductSong(8.2, song, score, k1);
    CHECK(glm::length(s.eye - s.target) == Approx(0.5f * glm::length(far.eye - far.target)).epsilon(0.05));
    CHECK(astral::conductSong(0.5, song, score, astral::Controls{}).exposure < 0.2f); // fading up from black
}

TEST_CASE("the live conductor builds phrases from the beats it hears and collapses on a performer's trigger",
          "[astral][adr1221][live]") {
    spatial::AudioHistory audio = spatial::AudioHistory::livePlaceholder(93.75);
    std::vector<float> row(spatial::kAudioBins, 0.4f);
    scene::AstralLiveConductor live;
    astral::Controls k;
    double t = 0.0;
    int beats = 0;
    for (; t < 20.0; t += 1.0 / 60.0) {
        audio.appendLive(t, row);
        if (t >= beats * 0.5) {
            audio.addLiveOnset(spatial::OnsetSource::Beat, t, 1.0f);
            audio.addLiveOnset(spatial::OnsetSource::Low, t, beats % 16 == 0 ? 1.0f : 0.3f);
            ++beats;
        }
        (void)live.update(audio, audio.now(t), k, false);
    }
    // 40 beats: two closed phrases of 16 and an open third
    CHECK(live.score().phrases.size() == 3);
    CHECK(live.score().beatSeconds == Approx(0.5).margin(0.02));
    CHECK(live.score().phrases[1].kickOpens); // its downbeat was a strong kick
    // a performer's collapse opens a phrase at once, as a kick would
    const astral::State s = live.update(audio, audio.now(t), k, true);
    CHECK(live.score().phrases.size() == 4);
    CHECK(live.score().phrases.back().kickOpens);
    CHECK(s.strobe > 0.0f);
}

TEST_CASE("MIDI reaches the astral controls through the live project's control map", "[astral][adr1222][midi]") {
    std::ifstream in(std::string(AVGEN_SOURCE_DIR) + "/examples/astral-forge/astral-forge-live.json");
    REQUIRE(in.good());
    const nlohmann::json project = nlohmann::json::parse(in);
    auto map = control::ControlMap::fromJson(project.at("control"));
    REQUIRE(map.has_value());
    Comp c(sceneWithAstral(nlohmann::json::object()));
    int bound = 0;
    for (const control::MidiBinding& b : map->midi) {
        if (b.target.parameter.empty()) continue;
        INFO("binding to " << b.target.parameter);
        CHECK(c.params.find(b.target.parameter) != nullptr); // every bound path exists
        ++bound;
    }
    CHECK(bound >= 10);
    // CC 1 (summon) at full: the engine maps 0..1 onto the binding's range and sets the base
    for (control::MidiBinding& b : map->midi) {
        if (b.kind != control::MidiBindKind::ControlChange || b.number != 1) continue;
        control::MidiMessage m;
        m.kind = control::MidiKind::ControlChange;
        m.data1 = 1;
        m.data2 = 127;
        const auto hit = control::matchMidi(b, m);
        REQUIRE(hit.has_value());
        c.params.find(b.target.parameter)->setBaseComponent(0, b.target.min + hit->value * (b.target.max - b.target.min));
    }
    c.frame(3.0);
    CHECK(c.comp->scene().astral.live.summon == Approx(1.0f));
}

TEST_CASE("the astral tier: offline is exact and complete, the live ladder trades particles", "[astral][adr1222][quality]") {
    using rendering::QualitySettings;
    using rendering::QualityTier;
    CHECK(QualitySettings::forTier(QualityTier::Offline).astralTier == 0u);
    CHECK(QualitySettings::forTier(QualityTier::Realtime).astralTier == 1u);
    const auto& ladder = app::liveQualityLadder(app::LiveQualityStrategy::Balanced);
    const QualitySettings base = QualitySettings::forTier(QualityTier::Realtime);
    std::uint32_t last = 0;
    for (const app::LiveQualityRung& r : ladder) {
        const QualitySettings q = app::applyLiveRung(base, base, r, 0.25f);
        CHECK(q.astralTier >= last); // never more particles at a lower level
        last = q.astralTier;
    }
    CHECK(last == 4u);
    CHECK(app::applyLiveRung(base, base, ladder[0], 0.25f).astralTier == 1u); // Ultra is the live reference
    app::LiveQualityRung c{};
    REQUIRE(app::applyLeverToCeiling(c, "astralhalf"));
    CHECK(c.astralTier == 3u);
    std::string error;
    const auto back = app::ceilingFromJsonText(app::ceilingJsonText(c), error);
    REQUIRE(back.has_value());
    CHECK(back->astralTier == 3u);
}
