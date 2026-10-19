// stems-multichannel M3: analysis stems through the engine and the project file (ADR-1520). The generated fixture
// (tools/make_stem_fixture.py) is the mix plus its stems; the engine must PLAY the mix and only ANALYSE the stems.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "support/stem_fixture.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

json stemEntry(const testsupport::StemFixture& fx, const std::string& key, const std::string& label,
               const std::string& file, double offset = 0.0) {
    json e = {{"key", key}, {"label", label}, {"file", fx.file(file).string()}};
    if (offset != 0.0) {
        e["offset"] = offset;
    }
    return e;
}

// A project: the fixture's mix, the stems given, and two routes on stem signals -- one through a stateful chain (an
// envelope with a 300 ms decay), so a seek that did not replay it would land somewhere a play never was.
//
// With `probe` (an event signal) the routes are instead one plain Replace route from it onto orb/impulse: the bus
// clears its events at the end of `update`, so a test sees an event the way a scene does -- through a route.
fs::path writeProject(const testsupport::StemFixture& fx, const json& stems, const std::string& name = "stems",
                      const std::string& probe = "") {
    const fs::path dir = testsupport::processTempDir() / name;
    fs::create_directories(dir);
    json p = {{"format", "avgen-project"},
              {"version", 4},
              {"assets", {{"scene", {{"kind", "orb"}}}, {"audio", fx.file("mix.wav").string()}}},
              {"parameters", json::object()},
              {"routes", json::array({
                   {{"source", "stem.drums.onset"}, {"target", "orb/impulse"}, {"op", "add"}, {"amount", 1.0},
                    {"chain", {{"attackMs", 5}, {"decayMs", 300}}}},
                   {{"source", "stem.bass.level"}, {"target", "orb/emissive"}, {"op", "add"}, {"amount", 2.0}},
               })}};
    if (!stems.is_null()) {
        p["assets"]["stems"] = stems;
    }
    if (!probe.empty()) {
        p["routes"] = json::array({{{"source", probe}, {"target", "orb/impulse"}, {"op", "replace"}, {"amount", 1.0}}});
    }
    std::ofstream(dir / "project.json") << p.dump(1);
    return dir / "project.json";
}

json standardStems(const testsupport::StemFixture& fx) {
    return json::array({stemEntry(fx, "drums", "Drums", "drums.wav"), stemEntry(fx, "bass", "Bass", "bass.wav"),
                        stemEntry(fx, "synth", "Synth", "synth.wav"), stemEntry(fx, "pad", "Pad", "pad.wav")});
}

void frame(app::Engine& e, long long k, double fps) {
    e.update(FrameTime{static_cast<double>(k) / fps, k == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(k)});
}

signals::SignalId id(app::Engine& e, const std::string& name) {
    const auto found = e.signals().find(name);
    REQUIRE(found.has_value());
    return *found;
}

bool probeFired(app::Engine& e) {
    return e.params().find("orb/impulse")->finalComponent(0) > 0.0f;
}

// Plays `seconds` at `fps` and returns the render times at which the project's probe event fired.
std::vector<double> eventTimes(app::Engine& e, double seconds, double fps) {
    std::vector<double> out;
    for (long long k = 0; static_cast<double>(k) / fps <= seconds; ++k) {
        frame(e, k, fps);
        if (probeFired(e)) {
            out.push_back(static_cast<double>(k) / fps);
        }
    }
    return out;
}

} // namespace

TEST_CASE("stems: the engine plays the mix, untouched, and analyses the stems beside it", "[stems][engine]") {
    const auto& fx = testsupport::requireStemFixture();
    app::Engine with(app::EngineMode::Offline);
    REQUIRE(with.loadProject(writeProject(fx, standardStems(fx), "with")).has_value());
    app::Engine without(app::EngineMode::Offline);
    REQUIRE(without.loadProject(writeProject(fx, json(), "without")).has_value());
    REQUIRE(with.audioFile() != nullptr);
    REQUIRE(without.audioFile() != nullptr);
    // The installed playback buffer is the mix, sample for sample, with or without stems: no stem was summed in.
    const auto a = with.audioFile()->interleaved();
    const auto b = without.audioFile()->interleaved();
    REQUIRE(a.size() == b.size());
    CHECK(std::equal(a.begin(), a.end(), b.begin()));
    auto mix = audio::AudioFile::load(fx.file("mix.wav"));
    REQUIRE(mix.has_value());
    CHECK(std::equal(a.begin(), a.end(), mix->interleaved().begin()));
    CHECK(with.audioClips().size() == 1); // the arrangement is the mix alone
    REQUIRE(with.stems().size() == 4);
    for (const auto& s : with.stems()) {
        INFO(s.source.key);
        CHECK(s.track != nullptr);
        CHECK(s.report.ok());
    }
    CHECK(with.projectWarnings().empty());
}

TEST_CASE("stems: each stem's events reach its own signals at the fixture's times, at any frame rate",
          "[stems][engine]") {
    const auto& fx = testsupport::requireStemFixture();
    const auto kicks = fx.times("drums", "kicks");
    std::size_t counts[3] = {};
    const double rates[3] = {24.0, 30.0, 60.0};
    for (int r = 0; r < 3; ++r) {
        app::Engine e(app::EngineMode::Offline);
        REQUIRE(e.loadProject(writeProject(fx, standardStems(fx), "rates", "stem.drums.onset")).has_value());
        const double fps = rates[r];
        const auto drums = eventTimes(e, 19.9, fps);
        counts[r] = drums.size();
        // Every kick (after the first hop) fires on the render frame that contains it, or the one after.
        for (double k : kicks) {
            if (k < 0.1) {
                continue;
            }
            INFO(fps << " fps, kick at " << k);
            CHECK(std::any_of(drums.begin(), drums.end(),
                              [&](double t) { return t >= k - 0.035 && t <= k + 0.035 + 1.0 / fps; }));
        }
    }
    // The events are OR-ed per frame, never dropped: the count is the stem's, not the frame rate's -- except that two
    // onsets inside one frame are one event. Hats and kicks are 250 ms apart, so even 24 fps keeps them apart.
    CHECK(counts[0] == counts[1]);
    CHECK(counts[1] == counts[2]);

    // The bass stem's onsets are bass notes, not the drums the mix also holds.
    app::Engine e(app::EngineMode::Offline);
    REQUIRE(e.loadProject(writeProject(fx, standardStems(fx), "bass", "stem.bass.onset")).has_value());
    const auto bass = eventTimes(e, 19.9, 60.0);
    const auto starts = fx.times("bass", "notes");
    for (double t : bass) {
        INFO("bass onset at " << t);
        CHECK(std::any_of(starts.begin(), starts.end(), [&](double s) { return t >= s - 0.04 && t <= s + 0.06; }));
    }
    CHECK(bass.size() >= starts.size() - 1);
}

TEST_CASE("stems: outside a stem's span, and for a missing or silent stem, the signals read 0 with no event",
          "[stems][engine]") {
    const auto& fx = testsupport::requireStemFixture();
    json stems = json::array({stemEntry(fx, "short", "Short", "drums_short.wav"),
                              stemEntry(fx, "late", "Late", "drums.wav", 12.0),
                              stemEntry(fx, "quiet", "Quiet", "silent.wav"),
                              stemEntry(fx, "gone", "Gone", "no-such-stem.wav")});
    app::Engine e(app::EngineMode::Offline);
    REQUIRE(e.loadProject(writeProject(fx, stems, "edges", "stem.short.onset")).has_value());
    CHECK(e.hasAudio()); // the mix plays whatever the stems did
    const auto& warnings = e.projectWarnings();
    CHECK(std::any_of(warnings.begin(), warnings.end(),
                      [](const std::string& w) { return w.find("no-such-stem.wav") != std::string::npos; }));
    CHECK(std::any_of(warnings.begin(), warnings.end(),
                      [](const std::string& w) { return w.find("silent.wav") != std::string::npos; }));
    for (long long k = 0; k <= 60 * 15; ++k) {
        frame(e, k, 60.0);
        const double t = static_cast<double>(k) / 60.0;
        if (t > 1.05) {
            REQUIRE(e.signals().value(id(e, "stem.short.active")) == 0.0f);
            REQUIRE(e.signals().value(id(e, "stem.short.rms")) == 0.0f);
            REQUIRE_FALSE(probeFired(e)); // the short stem's onset, through its route
        }
        if (t < 11.99) {
            REQUIRE(e.signals().value(id(e, "stem.late.active")) == 0.0f);
            REQUIRE(e.signals().value(id(e, "stem.late.onsetStrength")) == 0.0f);
        }
        for (const char* key : {"quiet", "gone"}) {
            REQUIRE(e.signals().value(id(e, std::string("stem.") + key + ".onsetStrength")) == 0.0f);
            REQUIRE(e.signals().value(id(e, std::string("stem.") + key + ".level")) == 0.0f);
        }
        REQUIRE(e.signals().value(id(e, "stem.gone.active")) == 0.0f);
    }
    CHECK(e.signals().value(id(e, "stem.late.active")) == 1.0f); // at 15 s the late stem has begun
}

TEST_CASE("stems: a seek lands every stem signal and a stem route's envelope where a play does", "[stems][engine][seek]") {
    const auto& fx = testsupport::requireStemFixture();
    const auto project = writeProject(fx, standardStems(fx), "seek");
    for (const long long target : {183LL, 600LL, 1001LL}) {
        app::Engine played(app::EngineMode::Offline);
        REQUIRE(played.loadProject(project).has_value());
        REQUIRE(played.seekReplaysSignals());
        for (long long k = 0; k <= target + 1; ++k) {
            frame(played, k, 60.0);
        }
        app::Engine scrubbed(app::EngineMode::Offline);
        REQUIRE(scrubbed.loadProject(project).has_value());
        scrubbed.seekSeconds(static_cast<double>(target) / 60.0);
        frame(scrubbed, target + 1, 60.0);
        INFO("target frame " << target);
        for (const char* key : {"drums", "bass", "synth", "pad"}) {
            for (const auto& name : signals::StemSignals::names(key)) {
                INFO(name);
                CHECK(scrubbed.signals().value(id(scrubbed, name)) == played.signals().value(id(played, name)));
            }
        }
        CHECK(scrubbed.params().find("orb/impulse")->finalComponent(0) ==
              Catch::Approx(played.params().find("orb/impulse")->finalComponent(0)).margin(1e-6));
        CHECK(scrubbed.params().find("orb/emissive")->finalComponent(0) ==
              Catch::Approx(played.params().find("orb/emissive")->finalComponent(0)).margin(1e-6));
    }
}

TEST_CASE("stems: keys survive a save and a relabel; duplicate labels get distinct keys", "[stems][engine][project]") {
    const auto& fx = testsupport::requireStemFixture();
    json stems = json::array({stemEntry(fx, "drums", "Drums", "drums.wav"),
                              stemEntry(fx, "bass", "Bass", "bass_late.wav", -0.25)});
    app::Engine e(app::EngineMode::Offline);
    REQUIRE(e.loadProject(writeProject(fx, stems, "save")).has_value());
    // Relabel the drums: the key, the ids and the route stay.
    const auto before = id(e, "stem.drums.onset");
    auto list = std::vector<audio::StemSource>{};
    for (const auto& s : e.stems()) {
        list.push_back(s.source);
    }
    list[0].label = "Drum Bus";
    // Two more with the same label, and one with a key that is not valid.
    list.push_back({.key = "", .label = "Drum Bus", .file = fx.file("drums.wav")});
    list.push_back({.key = "Bad.Key", .label = "Drum Bus", .file = fx.file("drums.wav")});
    e.setStems(list);
    REQUIRE(e.stems().size() == 4);
    CHECK(e.stems()[0].source.key == "drums");
    CHECK(e.stems()[2].source.key == "drum_bus");
    CHECK(e.stems()[3].source.key == "drum_bus_2");
    CHECK(id(e, "stem.drums.onset") == before);
    CHECK(e.signals().info(before).label.starts_with("Drum Bus:"));
    const auto& w = e.projectWarnings();
    CHECK(std::any_of(w.begin(), w.end(), [](const std::string& m) { return m.find("Bad.Key") != std::string::npos; }));

    const fs::path saved = testsupport::processTempDir() / "save" / "saved.json";
    REQUIRE(e.saveProject(saved).has_value());
    app::Engine reloaded(app::EngineMode::Offline);
    REQUIRE(reloaded.loadProject(saved).has_value());
    REQUIRE(reloaded.stems().size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(reloaded.stems()[i].source.key == e.stems()[i].source.key);
        CHECK(reloaded.stems()[i].source.label == e.stems()[i].source.label);
        CHECK(reloaded.stems()[i].source.offsetSeconds == e.stems()[i].source.offsetSeconds);
        CHECK(fs::equivalent(reloaded.stems()[i].source.file, e.stems()[i].source.file));
    }
    // The route on stem.drums.onset is bound and moves its target on a kick.
    float most = 0.0f;
    for (long long k = 0; k <= 120; ++k) {
        frame(reloaded, k, 60.0);
        most = std::max(most, reloaded.params().find("orb/impulse")->finalComponent(0));
    }
    CHECK(most > 0.0f);
    // A project without stems clears them.
    REQUIRE(reloaded.loadProject(writeProject(fx, json(), "cleared")).has_value());
    CHECK(reloaded.stems().empty());
}
