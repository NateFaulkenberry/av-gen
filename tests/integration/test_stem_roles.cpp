// Source roles (ADR-1630), the audio history from a source (ADR-1631) and the stem-set check (ADR-1632), through the
// engine on the generated stem fixture (tools/make_stem_fixture.py: 120 bpm, a kick every 0.5 s except 8-10 s; the
// mix is the stems' weighted sum; `bass_late.wav` is the bass 250 ms late).

#include "analysis/audio_history_builder.hpp"
#include "app/engine.hpp"
#include "audio/feeds.hpp"
#include "audio/stem_source.hpp"
#include "audio/synthetic_capture.hpp"
#include "core/time.hpp"
#include "signals/stem_signals.hpp"
#include "support/stem_fixture.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

json stem(const testsupport::StemFixture& fx, const std::string& key, const std::string& file, const std::string& role,
          double offset = 0.0) {
    json e = {{"key", key}, {"label", key}, {"file", fx.file(file).string()}};
    if (!role.empty()) e["role"] = role;
    if (offset != 0.0) e["offset"] = offset;
    return e;
}

json roleStems(const testsupport::StemFixture& fx, const std::string& drumsKey = "drums") {
    return json::array({stem(fx, drumsKey, "drums.wav", "drums"), stem(fx, "bass", "bass.wav", "bass"),
                        stem(fx, "synth", "synth.wav", "synth")});
}

// An envelope source on each trigger (attack 0, release 60 ms), so an event reads as a level after `update` clears the
// bus's events (probe B1: env.<name> publishes one frame after its trigger).
fs::path writeProject(const testsupport::StemFixture& fx, const std::string& name, const json& stems, const json& feeds,
                      const std::vector<std::pair<std::string, std::string>>& envelopes = {},
                      const json& extraSources = json::array()) {
    const fs::path dir = testsupport::processTempDir() / ("roles-" + name);
    fs::create_directories(dir);
    json params = json::object();
    json sources = extraSources;
    for (const auto& [env, trigger] : envelopes) {
        const std::string p = "sources/" + env + "/";
        params[p + "attackMs"] = 0.0;
        params[p + "decayMs"] = 0.0;
        params[p + "sustain"] = 1.0;
        params[p + "holdMs"] = 0.0;
        params[p + "releaseMs"] = 60.0;
        sources.push_back({{"kind", "envelope"}, {"name", env}, {"settings", {{"trigger", trigger}}}});
    }
    json p = {{"format", "avgen-project"},
              {"version", 4},
              {"assets", {{"scene", {{"kind", "orb"}}}, {"audio", fx.file("mix.wav").string()}}},
              {"sonic", json::object()},
              {"parameters", params},
              {"sources", sources},
              {"routes", json::array()}};
    if (!stems.is_null()) p["assets"]["stems"] = stems;
    if (!feeds.is_null()) p["feeds"] = feeds;
    std::ofstream(dir / "project.json") << p.dump(1);
    return dir / "project.json";
}

void frame(app::Engine& e, long long k, double fps) {
    e.update(FrameTime{static_cast<double>(k) / fps, k == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(k)});
}

float value(app::Engine& e, const std::string& name) {
    const auto id = e.signals().find(name);
    return id ? e.signals().value(*id) : -1.0f;
}

bool has(app::Engine& e, const std::string& name) {
    return e.signals().find(name).has_value();
}

bool warned(const app::Engine& e, const std::string& needle) {
    return std::any_of(e.projectWarnings().begin(), e.projectWarnings().end(),
                       [&](const std::string& w) { return w.find(needle) != std::string::npos; });
}

// How many times an envelope rises over `seconds` at `fps`.
std::size_t rises(app::Engine& e, const std::string& env, double seconds, double fps) {
    std::size_t n = 0;
    float previous = 0.0f;
    for (long long k = 0; static_cast<double>(k) / fps <= seconds; ++k) {
        frame(e, k, fps);
        const float v = value(e, env);
        n += v > 0.2f && previous <= 0.2f ? 1 : 0;
        previous = v;
    }
    return n;
}

} // namespace

// ---- ADR-1630: roles ------------------------------------------------------------------------------------------------

TEST_CASE("roles: only a drums source has class names; every role has its role.<role>.* vocabulary",
          "[stems][signals][adr1630]") {
    signals::SignalBus bus;
    const auto drums = signals::StemSignals::declare(bus, "kit", "Kit", audio::SourceRole::Drums);
    const auto bass = signals::StemSignals::declare(bus, "bass", "Bass", audio::SourceRole::Bass);
    CHECK(signals::StemSignals::names("kit", audio::SourceRole::Drums).size() == 19);
    CHECK(signals::StemSignals::names("bass", audio::SourceRole::Bass).size() == 16);
    for (const char* cls : {"kick", "snare", "hat"}) {
        CHECK(bus.find(std::string("stem.kit.") + cls).has_value());
        CHECK_FALSE(bus.find(std::string("stem.bass.") + cls).has_value());
    }
    CHECK(bass.kick == signals::kInvalidSignal);
    CHECK(bass.ids().size() == 16);
    const auto alias = signals::StemSignals::declareRole(bus, audio::SourceRole::Drums);
    const auto bassAlias = signals::StemSignals::declareRole(bus, audio::SourceRole::Bass);
    CHECK(bus.find("role.drums.kick").has_value());
    CHECK(bus.find("role.bass.level").has_value());
    CHECK_FALSE(bus.find("role.bass.kick").has_value());
    CHECK(signals::StemSignals::roleNames(audio::SourceRole::Synth).front() == "role.synth.active");

    analysis::StemFrame f;
    f.level = 0.7f;
    f.onset = true;
    f.onsetStrength = 2.0f;
    f.kick = true;
    f.kickStrength = 0.8f;
    drums.publish(bus, f);
    alias.copyFrom(bus, drums);
    CHECK(bus.value(alias.active) == 1.0f);
    CHECK(bus.value(alias.level) == 0.7f);
    CHECK(bus.event(alias.kick));
    CHECK(bus.value(alias.kick) == 0.8f);
    CHECK(bus.event(alias.onset));
    // A bass frame that the classifier called a kick publishes no class anywhere.
    bass.publish(bus, f);
    bassAlias.copyFrom(bus, bass);
    CHECK(bus.value(bassAlias.level) == 0.7f);
    CHECK(bus.event(bassAlias.onset));
    alias.publishInactive(bus);
    CHECK(bus.value(alias.active) == 0.0f);
    CHECK_FALSE(bus.event(alias.kick));
}

TEST_CASE("roles: a drums stem classifies with no flag; a bass stem never does; role.drums.* is the drum stem",
          "[stems][engine][adr1630]") {
    const auto& fx = testsupport::requireStemFixture();
    // The drum stem keyed `kit`: nothing reads the key `drums`.
    app::Engine e(app::EngineMode::Offline);
    REQUIRE(e.loadProject(writeProject(fx, "classes", roleStems(fx, "kit"), json(),
                                       {{"kit", "stem.kit.kick"}, {"alias", "role.drums.kick"}}))
                .has_value());
    CHECK(e.projectWarnings().empty());
    REQUIRE(e.stems().size() == 3);
    CHECK(e.stems()[0].source.role == audio::SourceRole::Drums);
    CHECK(e.stems()[0].track->hits()); // auto hits: no "hits" anywhere in the project
    CHECK_FALSE(e.stems()[1].track->hits());
    CHECK(has(e, "stem.kit.kick"));
    CHECK_FALSE(has(e, "stem.bass.kick"));
    CHECK_FALSE(has(e, "stem.synth.hat"));
    CHECK(has(e, "role.drums.kick"));
    CHECK(has(e, "role.bass.level"));
    CHECK_FALSE(has(e, "role.bass.kick"));
    CHECK_FALSE(has(e, "role.vocal.active")); // no source of the role: not declared
    // Frame for frame, role.drums.* is stem.kit.* and role.bass.* is stem.bass.*; the envelopes on the kit's kick
    // and on the alias's are identical, and fire on the fixture's kicks.
    constexpr double fps = 60.0;
    std::size_t kicks = 0;
    float previous = 0.0f;
    for (long long k = 0; static_cast<double>(k) / fps <= 19.9; ++k) {
        frame(e, k, fps);
        REQUIRE(value(e, "role.drums.level") == value(e, "stem.kit.level"));
        REQUIRE(value(e, "role.drums.onsetStrength") == value(e, "stem.kit.onsetStrength"));
        REQUIRE(value(e, "role.bass.level") == value(e, "stem.bass.level"));
        REQUIRE(value(e, "role.synth.centroid") == value(e, "stem.synth.centroid"));
        REQUIRE(value(e, "env.alias") == value(e, "env.kit"));
        const float v = value(e, "env.alias");
        kicks += v > 0.2f && previous <= 0.2f ? 1 : 0;
        previous = v;
    }
    const std::size_t truth = fx.times("drums", "kicks").size();
    INFO(kicks << " kicks through role.drums.kick against " << truth << " in the fixture");
    CHECK(kicks >= truth - 2);
    CHECK(kicks <= truth);
    // Past the stems' end the alias is inactive.
    frame(e, static_cast<long long>(25.0 * fps), fps);
    CHECK(value(e, "role.drums.active") == 0.0f);
}

TEST_CASE("roles: the role saves, \"hits\" is gone with a warning, and a stem with no role is other",
          "[stems][engine][project][adr1630]") {
    const auto& fx = testsupport::requireStemFixture();
    json stems = json::array({stem(fx, "drums", "drums.wav", ""), stem(fx, "bass", "bass.wav", "bass"),
                              stem(fx, "pad", "pad.wav", "piano")});
    stems[0]["hits"] = true;
    app::Engine e(app::EngineMode::Offline);
    REQUIRE(e.loadProject(writeProject(fx, "save", stems, json())).has_value());
    CHECK(warned(e, "\"hits\" is gone"));
    CHECK(warned(e, "is not drums, bass, synth, vocal or other"));
    // No inference: the key `drums` with `hits` and no role is other, and classifies nothing.
    CHECK(e.stems()[0].source.role == audio::SourceRole::Other);
    CHECK_FALSE(e.stems()[0].track->hits());
    CHECK_FALSE(has(e, "stem.drums.kick"));
    CHECK(e.stems()[2].source.role == audio::SourceRole::Other);
    const fs::path saved = testsupport::processTempDir() / "roles-save" / "saved.json";
    REQUIRE(e.saveProject(saved).has_value());
    json doc;
    std::ifstream(saved) >> doc;
    CHECK_FALSE(doc["assets"]["stems"][0].contains("role"));
    CHECK_FALSE(doc["assets"]["stems"][0].contains("hits"));
    CHECK(doc["assets"]["stems"][1]["role"] == "bass");
}

TEST_CASE("roles: a feed can name a role; a class from a source that is not a drums source is refused",
          "[feeds][stems][engine][adr1630]") {
    const auto& fx = testsupport::requireStemFixture();
    const std::vector<std::pair<std::string, std::string>> envs{{"kick", "response.kick"}, {"low", "response.low"}};
    app::Engine byRole(app::EngineMode::Offline);
    REQUIRE(byRole.loadProject(writeProject(fx, "feed-role", roleStems(fx, "kit"), {{"hits", "role.drums"}}, envs))
                .has_value());
    CHECK(byRole.projectWarnings().empty());
    CHECK(byRole.feeds().hits == "role.drums");
    CHECK(byRole.feedState("role.drums").resolved);
    app::Engine byKey(app::EngineMode::Offline);
    REQUIRE(byKey.loadProject(writeProject(fx, "feed-key", roleStems(fx, "kit"), {{"hits", "stem.kit"}}, envs))
                .has_value());
    const std::size_t roleKicks = rises(byRole, "env.kick", 19.9, 60.0);
    CHECK(roleKicks == rises(byKey, "env.kick", 19.9, 60.0));
    CHECK(roleKicks >= fx.times("drums", "kicks").size() - 2);
    // The bass stem asked for the kick: refused and said; the kick does not fire. Its low onsets are activity, and
    // feed.
    app::Engine bass(app::EngineMode::Offline);
    REQUIRE(bass.loadProject(writeProject(fx, "feed-bass", roleStems(fx), {{"kick", "role.bass"}, {"low", "stem.bass"}},
                                          envs))
                .has_value());
    CHECK(warned(bass, "a kick comes only from a drums source"));
    CHECK(bass.stems()[1].track->hits()); // fed: it runs the hits tier for the feed
    CHECK(rises(bass, "env.kick", 19.9, 60.0) == 0);
    CHECK(rises(bass, "env.low", 19.9, 60.0) >= 8);
    // The block round-trips with the role reference.
    CHECK(audio::Feeds::fromJson(byRole.feeds().toJson())->hits == "role.drums");
    CHECK_FALSE(audio::Feeds::fromJson(json{{"hits", "role.piano"}}).has_value());
}

TEST_CASE("roles: role.<role>.* is replayed by a seek exactly as a play publishes it", "[stems][engine][seek][adr1630]") {
    const auto& fx = testsupport::requireStemFixture();
    const fs::path project = writeProject(fx, "seek", roleStems(fx), json(), {{"alias", "role.drums.onset"}});
    constexpr double fps = 60.0;
    const long long target = static_cast<long long>(11.37 * fps);
    app::Engine played(app::EngineMode::Offline);
    REQUIRE(played.loadProject(project).has_value());
    for (long long k = 0; k <= target + 1; ++k) {
        frame(played, k, fps);
    }
    app::Engine scrubbed(app::EngineMode::Offline);
    REQUIRE(scrubbed.loadProject(project).has_value());
    scrubbed.seekSeconds(static_cast<double>(target) / fps);
    frame(scrubbed, target + 1, fps);
    for (const auto role : {audio::SourceRole::Drums, audio::SourceRole::Bass, audio::SourceRole::Synth}) {
        for (const std::string& name : signals::StemSignals::roleNames(role)) {
            INFO(name);
            CHECK(value(scrubbed, name) == value(played, name));
        }
    }
    CHECK(value(scrubbed, "env.alias") == Catch::Approx(value(played, "env.alias")).margin(1e-6));
}

// ---- ADR-1631: the history from a source ----------------------------------------------------------------------------

TEST_CASE("history: from a drums source its onset rows are its kicks, snares and hats; from another role its onsets",
          "[stems][analysis][fields][adr1631]") {
    const auto& fx = testsupport::requireStemFixture();
    app::Engine e(app::EngineMode::Offline);
    REQUIRE(e.loadProject(writeProject(fx, "history-unit", roleStems(fx), json())).has_value());
    const analysis::StemTrack& drums = *e.stems()[0].track;
    const analysis::StemTrack& bass = *e.stems()[1].track;
    const auto times = [](const analysis::StemTrack& t, bool analysis::StemFrame::*flag, double offset) {
        std::vector<double> out;
        for (const auto& f : t.frames()) {
            if (f.*flag) out.push_back(f.timeSeconds + offset);
        }
        return out;
    };
    const auto listed = [](const spatial::AudioHistory& h, spatial::OnsetSource s) {
        std::vector<double> out;
        for (const auto& o : h.onsets(s)) out.push_back(o.time);
        return out;
    };
    const auto mixHistory = analysis::buildAudioHistory(*e.track());
    const auto fromDrums = analysis::buildAudioHistory(*e.track(), {&drums, 0.75, true}, {&drums, 0.75, true});
    CHECK(listed(fromDrums, spatial::OnsetSource::Low) == times(drums, &analysis::StemFrame::kick, 0.75));
    CHECK(listed(fromDrums, spatial::OnsetSource::Mid) == times(drums, &analysis::StemFrame::snare, 0.75));
    CHECK(listed(fromDrums, spatial::OnsetSource::High) == times(drums, &analysis::StemFrame::hat, 0.75));
    CHECK(listed(fromDrums, spatial::OnsetSource::Beat) == listed(mixHistory, spatial::OnsetSource::Beat));
    CHECK_FALSE(times(drums, &analysis::StemFrame::kick, 0.0).empty());
    // A bass source: no identity, so its onsets are on all three rows.
    const auto fromBass = analysis::buildAudioHistory(*e.track(), {}, {&bass, 0.0, false});
    const auto bassOnsets = times(bass, &analysis::StemFrame::onset, 0.0);
    CHECK_FALSE(bassOnsets.empty());
    for (const auto s : {spatial::OnsetSource::Low, spatial::OnsetSource::Mid, spatial::OnsetSource::High}) {
        CHECK(listed(fromBass, s) == bassOnsets);
    }
    // The spectrogram: the source's rows at its offset, 0 before it and after it, held to the mix's end.
    const double end = e.track()->frames().back().timeSeconds;
    const auto spectrum = analysis::buildAudioHistory(*e.track(), {&bass, 2.0, false}, {});
    CHECK(spectrum.newestRow(1.9) < 0);
    CHECK(spectrum.newestRow(2.5) >= 0);
    float during = 0.0f;
    for (int b = 0; b < spatial::kAudioBins; ++b) {
        during = std::max(during, spatial::spectrumAt(spectrum, 3.0, 0.0f, static_cast<float>(b) / spatial::kAudioBins));
    }
    CHECK(during > 0.0f);
    CHECK(spectrum.newestRow(end) < spectrum.rowCount() - 1 + 1);
    for (int b = 0; b < spatial::kAudioBins; ++b) {
        CHECK(spatial::spectrumAt(spectrum, end, 0.0f, static_cast<float>(b) / spatial::kAudioBins) == 0.0f);
    }
    // The mix's history is unchanged by the new path.
    CHECK(listed(analysis::buildAudioHistory(*e.track(), {}, {}), spatial::OnsetSource::Low) ==
          listed(mixHistory, spatial::OnsetSource::Low));
}

TEST_CASE("history: feeds.history names a source by role; a missing one is said and the mix is heard",
          "[stems][engine][fields][adr1631]") {
    const auto& fx = testsupport::requireStemFixture();
    app::Engine mix(app::EngineMode::Offline);
    REQUIRE(mix.loadProject(writeProject(fx, "history-mix", roleStems(fx), json())).has_value());
    frame(mix, 0, 60.0);
    app::Engine fed(app::EngineMode::Offline);
    REQUIRE(fed.loadProject(writeProject(fx, "history-fed", roleStems(fx, "kit"),
                                         {{"history", "role.drums"}, {"historySpectrum", "role.bass"}}))
                .has_value());
    CHECK(fed.projectWarnings().empty());
    frame(fed, 0, 60.0);
    const auto h = fed.audioHistory();
    REQUIRE(h != nullptr);
    CHECK_FALSE(h->live()); // the whole-file history: a seek stays exact
    std::vector<double> kicks;
    for (const auto& f : fed.stems()[0].track->frames()) {
        if (f.kick) kicks.push_back(f.timeSeconds);
    }
    std::vector<double> low;
    for (const auto& o : h->onsets(spatial::OnsetSource::Low)) low.push_back(o.time);
    CHECK(low == kicks);
    CHECK(h->onsets(spatial::OnsetSource::Beat).size() == mix.audioHistory()->onsets(spatial::OnsetSource::Beat).size());
    CHECK(h->rowRate() == Catch::Approx(fed.stems()[1].track->sampleRate() / 512.0));
    // The bass's spectrogram is not the mix's: 9-10 s, where only the drums are silent, differs.
    bool differs = false;
    for (int b = 0; b < spatial::kAudioBins; ++b) {
        const float pos = static_cast<float>(b) / spatial::kAudioBins;
        differs = differs || spatial::spectrumAt(*h, 9.5, 0.0f, pos) != spatial::spectrumAt(*mix.audioHistory(), 9.5, 0.0f, pos);
    }
    CHECK(differs);
    // A history feed that names nothing: a warning, and the mix's history.
    app::Engine typo(app::EngineMode::Offline);
    REQUIRE(typo.loadProject(writeProject(fx, "history-typo", roleStems(fx), {{"history", "role.vocal"}})).has_value());
    CHECK(warned(typo, "feeds.history: there is no stem or input source 'role.vocal'"));
    frame(typo, 0, 60.0);
    CHECK(typo.audioHistory()->onsets(spatial::OnsetSource::Low).size() ==
          mix.audioHistory()->onsets(spatial::OnsetSource::Low).size());
}

// ---- ADR-1632: the stem-set check -----------------------------------------------------------------------------------

TEST_CASE("stem set: an aligned set sums to the mix; a late stem is found and its aligning offset named",
          "[stems][engine][adr1632]") {
    const auto& fx = testsupport::requireStemFixture();
    const double late = fx.meta["variants"]["bass_late.wav"]["lateBySeconds"].get<double>();
    const auto all = [&](const std::string& bassFile, double bassOffset) {
        return json::array({stem(fx, "drums", "drums.wav", "drums"), stem(fx, "bass", bassFile, "bass", bassOffset),
                            stem(fx, "synth", "synth.wav", "synth"), stem(fx, "pad", "pad.wav", "other")});
    };
    app::Engine aligned(app::EngineMode::Offline);
    REQUIRE(aligned.loadProject(writeProject(fx, "set-aligned", all("bass.wav", 0.0), json())).has_value());
    CHECK(aligned.projectWarnings().empty());
    const audio::StemSetCheck& ok = aligned.stemSetCheck();
    REQUIRE(ok.measured);
    INFO("residual " << ok.residualDb << " dB");
    CHECK(ok.residualDb < -40.0); // the mix is the stems' weighted sum
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(ok.lagSeconds[i] == 0.0);
        CHECK(ok.match[i] > 0.9f);
    }
    // The drums' fader, centre-panned: the mix's mono hears 0.45 x cos(pi/4) of the stem.
    CHECK(ok.gains[0] == Catch::Approx(0.45f * std::cos(3.14159265f / 4.0f)).margin(0.01));

    // The same bass 250 ms late in its file, at offset 0: found, and the offset that aligns it named.
    app::Engine wrong(app::EngineMode::Offline);
    REQUIRE(wrong.loadProject(writeProject(fx, "set-late", all("bass_late.wav", 0.0), json())).has_value());
    const audio::StemSetCheck& bad = wrong.stemSetCheck();
    INFO("lag " << bad.lagSeconds[1] << " s, residual " << bad.residualDb);
    // The file is late, so its audio belongs earlier: the fixture's own alignOffsetSeconds.
    const double align = fx.meta["variants"]["bass_late.wav"]["alignOffsetSeconds"].get<double>();
    CHECK(bad.lagSeconds[1] == Catch::Approx(align).margin(0.001));
    CHECK(warned(wrong, "lines up with the mix 250.0 ms earlier"));
    CHECK(warned(wrong, "the offset that aligns it is -0.2500 s"));
    for (const std::size_t other : {0u, 2u, 3u}) {
        CHECK(bad.lagSeconds[other] == 0.0);
    }
    CHECK(bad.residualDb > ok.residualDb + 10.0);
    // Its offset set as the warning says: aligned again, no warning.
    app::Engine fixed(app::EngineMode::Offline);
    REQUIRE(fixed.loadProject(writeProject(fx, "set-fixed", all("bass_late.wav", -late), json())).has_value());
    CHECK_FALSE(warned(fixed, "lines up with the mix")); // (its span, -0.25..19.75 s, is mixDiagnostics' warning)
    CHECK(fixed.stemSetCheck().lagSeconds[1] == 0.0);
    CHECK(fixed.stemSetCheck().residualDb < -40.0);
}

TEST_CASE("stem set: a silent stem, another rate and a mono stem are said, not misread as misalignment",
          "[stems][engine][adr1632]") {
    const auto& fx = testsupport::requireStemFixture();
    app::Engine e(app::EngineMode::Offline);
    REQUIRE(e.loadProject(writeProject(fx, "set-odd",
                                       json::array({stem(fx, "drums", "drums.wav", "drums"),
                                                    stem(fx, "quiet", "silent.wav", "other"),
                                                    stem(fx, "synth", "synth_44k1.wav", "synth")}),
                                       json()))
                .has_value());
    const audio::StemSetCheck& c = e.stemSetCheck();
    REQUIRE(c.measured);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(c.lagSeconds[i] == 0.0);
    }
    CHECK_FALSE(warned(e, "lines up with the mix"));
    CHECK_FALSE(warned(e, "does not match the mix"));
    CHECK(warned(e, "every sample is silent")); // checkStem's own warning, as before
    // The mono drum stem beside the stereo mix: info on its stem, not a warning.
    REQUIRE_FALSE(c.perStem[0].empty());
    CHECK(c.perStem[0][0].severity == audio::StemSeverity::Info);
    CHECK(c.perStem[0][0].message.find("analysed as mono (its width reads 0)") != std::string::npos);
    CHECK(c.gains[2] == 0.0f); // 44.1 kHz beside a 48 kHz mix: left out of the sum
}

// ---- live: the role alias and the history from an input source ------------------------------------------------------

namespace {

struct LiveRoles {
    bool drumsActive = false, bassActive = false;
    bool drumsKickDeclared = false, bassKickDeclared = false, clickKickDeclared = false;
    std::array<std::size_t, 4> history{};  // the live history's low / mid / high / beat onsets
    bool historyLive = false;
};

// The live engine on the synthetic 8-channel device (a click every 0.5 s per channel, channel c 31.25 ms x c late):
// channel 5 as `click` (bass), channel 7 as `kit` (drums), `feeds` set, for `seconds`.
LiveRoles runLive(const audio::Feeds& feeds, double seconds) {
    app::Engine engine(app::EngineMode::Live);
    engine.setLiveControl(false);
    engine.setInputSources({{.key = "click", .label = "Click", .channel = 4, .role = audio::SourceRole::Bass},
                            {.key = "kit", .label = "Kit", .channel = 6, .role = audio::SourceRole::Drums}});
    engine.setFeeds(feeds);
    audio::SyntheticCaptureConfig cfg;
    cfg.realtime = false;
    auto backend = std::make_unique<audio::SyntheticCapture>(cfg);
    audio::SyntheticCapture* device = backend.get();
    REQUIRE(engine.useAudioInputBackend(std::move(backend)).has_value());
    REQUIRE(engine.setLiveSonic(true).has_value());
    LiveRoles out;
    double t = 0.0;
    const int frames = static_cast<int>(seconds * 60.0);
    std::uint64_t pumped = 0;
    for (int i = 0; i < frames; ++i) {
        device->pump(6); // 768 frames, 16 ms
        pumped += 6 * cfg.blockFrames;
        const std::uint64_t hops = pumped >= 2048 ? (pumped - 2048) / 512 + 1 : 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline &&
               ((engine.channelAnalysis() != nullptr && engine.channelAnalysis()->hopsProduced() + 4 < 2 * hops) ||
                (engine.liveTimbre() != nullptr && engine.liveTimbre()->produced() + 2 < hops))) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        engine.update(FrameTime{t, 1.0 / 60.0, static_cast<std::uint64_t>(i)});
        t += 1.0 / 60.0;
    }
    out.drumsActive = value(engine, "role.drums.active") == 1.0f;
    out.bassActive = value(engine, "role.bass.active") == 1.0f;
    out.drumsKickDeclared = has(engine, "role.drums.kick");
    out.bassKickDeclared = has(engine, "role.bass.kick");
    out.clickKickDeclared = has(engine, "stem.click.kick");
    if (const auto h = engine.audioHistory()) {
        out.historyLive = h->live();
        for (std::size_t k = 0; k < 4; ++k) {
            out.history[k] = h->onsets(static_cast<spatial::OnsetSource>(k)).size();
        }
    }
    engine.stopAudioInput();
    return out;
}

} // namespace

TEST_CASE("roles live: role.<role>.* follows the input source of that role; the history hears the one feeds name",
          "[stems][multichannel][threading][engine][adr1630][adr1631]") {
    audio::Feeds fed;
    fed.history = "role.bass";
    const LiveRoles a = runLive(fed, 8.0);
    const LiveRoles b = runLive({}, 8.0);
    INFO("history from the bass source low/mid/high/beat " << a.history[0] << "/"
                        << a.history[1] << "/" << a.history[2] << "/" << a.history[3] << "; from the mix "
                        << b.history[0] << "/" << b.history[1] << "/" << b.history[2] << "/" << b.history[3]);
    CHECK(a.drumsActive);
    CHECK(a.bassActive);
    CHECK(a.drumsKickDeclared);
    CHECK_FALSE(a.bassKickDeclared);
    CHECK_FALSE(a.clickKickDeclared); // a bass input has no class names
    REQUIRE(a.historyLive);
    // A source with no identity: its onsets on all three rows -- one click every 0.5 s on the bass source, about 16 in
    // 8 s (the first while the detector warms up).
    CHECK(a.history[0] == a.history[1]);
    CHECK(a.history[1] == a.history[2]);
    CHECK(a.history[0] >= 11);
    CHECK(a.history[0] <= 17);
    // The beat row stays the mix's either way; the mix's own rows are not the source's.
    REQUIRE(b.historyLive);
    CHECK(a.history[0] != b.history[0]);
}
