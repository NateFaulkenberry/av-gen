// Feeds through the engine (ADR-1580/1581/1582): the beat clock and response.* from a stem, on the generated stem
// fixture (120 bpm: a kick every 0.5 s except 8-10 s, where the drum stem is silent and the bass plays a note at 8.0).

#include "app/engine.hpp"
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
#include <map>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

json stem(const testsupport::StemFixture& fx, const std::string& key, const std::string& file, double offset = 0.0,
          float gainDb = 0.0f, const std::string& role = "") {
    json e = {{"key", key}, {"label", key}, {"file", fx.file(file).string()}};
    if (offset != 0.0) e["offset"] = offset;
    if (gainDb != 0.0f) e["gainDb"] = gainDb;
    if (!role.empty()) e["role"] = role; // ADR-1630
    return e;
}

// The three stems with their roles (ADR-1630): the drum stem is a drums source, so it runs the classifier.
json standardStems(const testsupport::StemFixture& fx) {
    return json::array({stem(fx, "drums", "drums.wav", 0.0, 0.0f, "drums"), stem(fx, "bass", "bass.wav", 0.0, 0.0f, "bass"),
                        stem(fx, "synth", "synth.wav", 0.0, 0.0f, "synth")});
}

// The fixture's mix, a `sonic` block (so response.* runs), the stems and the feeds given.
fs::path writeProject(const testsupport::StemFixture& fx, const std::string& name, const json& stems, const json& feeds) {
    const fs::path dir = testsupport::processTempDir() / ("feeds-" + name);
    fs::create_directories(dir);
    json p = {{"format", "avgen-project"},
              {"version", 4},
              {"assets", {{"scene", {{"kind", "orb"}}}, {"audio", fx.file("mix.wav").string()}}},
              {"sonic", json::object()},
              {"parameters", json::object()},
              {"routes", json::array()}};
    if (!stems.is_null()) p["assets"]["stems"] = stems;
    if (!feeds.is_null()) p["feeds"] = feeds;
    std::ofstream(dir / "project.json") << p.dump(1);
    return dir / "project.json";
}

void frame(app::Engine& e, long long k, double fps) {
    e.update(FrameTime{static_cast<double>(k) / fps, k == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(k)});
}

// Every response.*, beat.* and music.* signal's value, per frame, over `seconds` at `fps`.
using Trace = std::map<std::string, std::vector<float>>;
Trace play(app::Engine& e, double seconds, double fps) {
    Trace out;
    std::vector<std::pair<std::string, signals::SignalId>> ids;
    for (long long k = 0; static_cast<double>(k) / fps <= seconds; ++k) {
        frame(e, k, fps);
        if (ids.empty()) {
            for (std::size_t i = 0; i < e.signals().size(); ++i) {
                const std::string& n = e.signals().info(static_cast<signals::SignalId>(i)).name;
                if (n.starts_with("response.") || n.starts_with("beat.") || n.starts_with("music.")) {
                    ids.emplace_back(n, static_cast<signals::SignalId>(i));
                }
            }
        }
        for (const auto& [n, id] : ids) {
            out[n].push_back(e.signals().value(id));
        }
    }
    return out;
}

// The times an envelope jumps (a hit), at `fps`.
std::vector<double> rises(const std::vector<float>& env, double fps) {
    std::vector<double> out;
    for (std::size_t i = 1; i < env.size(); ++i) {
        if (env[i] > env[i - 1] + 0.05f) {
            out.push_back(static_cast<double>(i) / fps);
        }
    }
    return out;
}

std::size_t within(const std::vector<double>& t, double from, double to) {
    return static_cast<std::size_t>(std::count_if(t.begin(), t.end(), [&](double x) { return x >= from && x < to; }));
}

} // namespace

TEST_CASE("feeds: with no feeds, response.*, beat.* and music.* do not depend on the stems", "[feeds][stems][engine]") {
    const auto& fx = testsupport::requireStemFixture();
    app::Engine with(app::EngineMode::Offline);
    REQUIRE(with.loadProject(writeProject(fx, "none-with", standardStems(fx), json())).has_value());
    app::Engine without(app::EngineMode::Offline);
    REQUIRE(without.loadProject(writeProject(fx, "none-without", json(), json())).has_value());
    CHECK(with.beatGrid().sourceKey.empty());
    CHECK(with.beatGrid().beats.beatTimes == without.track()->beats().beatTimes);
    const Trace a = play(with, 19.9, 30.0);
    const Trace b = play(without, 19.9, 30.0);
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() > 30);
    for (const auto& [name, values] : a) {
        INFO(name);
        REQUIRE(b.contains(name));
        CHECK(values == b.at(name));
    }
}

TEST_CASE("feeds: hits from the drum stem are the drums' hits; levels from it follow its silence",
          "[feeds][stems][engine]") {
    const auto& fx = testsupport::requireStemFixture();
    app::Engine mix(app::EngineMode::Offline);
    REQUIRE(mix.loadProject(writeProject(fx, "hits-mix", standardStems(fx), json())).has_value());
    app::Engine fed(app::EngineMode::Offline);
    REQUIRE(fed.loadProject(writeProject(fx, "hits-fed", standardStems(fx),
                                         {{"hits", "stem.drums"}, {"levels", "stem.drums"}}))
                .has_value());
    CHECK(fed.projectWarnings().empty());
    // ADR-1630: a drums stem runs the hits tier by its role, fed or not; the bass stem, named by no feed, does not.
    REQUIRE(fed.stems()[0].track != nullptr);
    CHECK(fed.stems()[0].track->hits());
    CHECK(mix.stems()[0].track->hits());
    CHECK_FALSE(mix.stems()[1].track->hits());
    constexpr double fps = 60.0;
    const Trace m = play(mix, 19.9, fps);
    const Trace f = play(fed, 19.9, fps);
    // 8-10 s: the drum stem is silent and the bass plays a note at 8.0. The mix hears a low attack there; the drums
    // feed none.
    const auto mixLow = rises(m.at("response.lowEnv"), fps);
    const auto fedLow = rises(f.at("response.lowEnv"), fps);
    INFO("mix low in 7.9-9.9: " << within(mixLow, 7.9, 9.9) << ", fed low " << within(fedLow, 7.9, 9.9));
    CHECK(within(mixLow, 7.9, 9.9) >= 1);
    CHECK(within(fedLow, 8.05, 9.95) == 0);
    // Every fed low attack is one of the drum stem's kicks.
    const auto kicks = fx.times("drums", "kicks");
    CHECK(fedLow.size() >= kicks.size() * 3 / 4);
    for (double t : fedLow) {
        INFO("fed low attack at " << t);
        CHECK(std::any_of(kicks.begin(), kicks.end(), [&](double k) { return t >= k - 0.04 && t <= k + 0.08; }));
    }
    // No response signal names an instrument, fed or not.
    for (const char* retired : {"response.kickEnv", "response.snareEnv", "response.hatEnv"}) {
        CHECK_FALSE(m.contains(retired));
        CHECK_FALSE(f.contains(retired));
    }
    // The levels: from the drum stem they fall away in its silence (its own reference holds); the mix's do not.
    const auto most = [&](const Trace& tr, const char* name, double from, double to) {
        const auto& v = tr.at(name);
        return *std::max_element(v.begin() + static_cast<std::ptrdiff_t>(from * fps),
                                 v.begin() + static_cast<std::ptrdiff_t>(to * fps));
    };
    INFO("level 8.3-9.9: mix " << most(m, "response.level", 8.3, 9.9) << ", fed " << most(f, "response.level", 8.3, 9.9));
    CHECK(most(f, "response.level", 8.3, 9.9) < 0.05f);
    CHECK(most(m, "response.level", 8.3, 9.9) > 0.3f);
    // ...and come back with the drums.
    CHECK(most(f, "response.level", 12.0, 13.0) > 0.5f);
    // Melodic and pitch stay on the mix.
    CHECK(m.at("response.pitch") == f.at("response.pitch"));
}

TEST_CASE("feeds: a stem outside its span feeds nothing; a key that names nothing warns and reads the mix",
          "[feeds][stems][engine]") {
    const auto& fx = testsupport::requireStemFixture();
    json stems = standardStems(fx);
    stems.push_back(stem(fx, "late", "drums.wav", 12.0, 0.0f, "drums"));
    app::Engine late(app::EngineMode::Offline);
    REQUIRE(late.loadProject(writeProject(fx, "late", stems, {{"low", "stem.late"}})).has_value());
    const auto lows = rises(play(late, 15.0, 60.0).at("response.lowEnv"), 60.0);
    CHECK(within(lows, 0.0, 11.95) == 0);
    CHECK(within(lows, 12.4, 15.0) >= 4);

    app::Engine typo(app::EngineMode::Offline);
    REQUIRE(typo.loadProject(writeProject(fx, "typo", standardStems(fx), {{"hits", "stem.drumz"}, {"beat", "stem.drumz"}}))
                .has_value());
    const auto& w = typo.projectWarnings();
    CHECK(std::any_of(w.begin(), w.end(), [](const std::string& s) { return s.find("stem.drumz") != std::string::npos; }));
    CHECK(typo.beatGrid().sourceKey.empty());
    app::Engine plain(app::EngineMode::Offline);
    REQUIRE(plain.loadProject(writeProject(fx, "typo-plain", standardStems(fx), json())).has_value());
    CHECK(play(typo, 6.0, 30.0) == play(plain, 6.0, 30.0));

    // A malformed block is a load warning, and the mix feeds everything.
    app::Engine bad(app::EngineMode::Offline);
    REQUIRE(bad.loadProject(writeProject(fx, "bad", standardStems(fx), {{"beat", "drums"}})).has_value());
    CHECK(bad.feeds().empty());
    CHECK_FALSE(bad.projectWarnings().empty());
}

TEST_CASE("feeds: the beat clock follows the drum stem's grid, offset included", "[feeds][stems][engine][beat]") {
    const auto& fx = testsupport::requireStemFixture();
    app::Engine e(app::EngineMode::Offline);
    REQUIRE(e.loadProject(writeProject(fx, "beat", standardStems(fx), {{"beat", "stem.drums"}})).has_value());
    const analysis::TrackedGrid& g = e.beatGrid();
    INFO("mix tempo " << e.track()->beats().tempoBpm << ", drums " << g.beats.tempoBpm);
    CHECK(g.sourceKey == "drums");
    // The fixture's backbeat (a snare on 2 and 4) reads as 60 bpm to the mix's tracker too: the same tracker, the
    // same answer, on a clean drum stem.
    CHECK(g.beats.tempoBpm == Catch::Approx(e.track()->beats().tempoBpm).margin(1.0f));
    REQUIRE(g.beats.beatTimes.size() >= 15);
    const auto kicks = fx.times("drums", "kicks");
    std::size_t onKick = 0;
    for (double b : g.beats.beatTimes) {
        onKick += std::any_of(kicks.begin(), kicks.end(), [&](double k) { return std::abs(b - k) <= 0.03; }) ? 1 : 0;
    }
    CHECK(onKick >= g.beats.beatTimes.size() * 3 / 4);
    CHECK(e.tempo().bpm == Catch::Approx(static_cast<double>(g.beats.tempoBpm)));
    // beat.bpm and the pulse follow it.
    const Trace t = play(e, 19.9, 60.0);
    CHECK(t.at("beat.bpm").back() == Catch::Approx(g.beats.tempoBpm));
    // The same stem 0.1 s later on the timeline: the grid moves with it.
    json shifted = json::array({stem(fx, "drums", "drums.wav", 0.1)});
    app::Engine s(app::EngineMode::Offline);
    REQUIRE(s.loadProject(writeProject(fx, "beat-shift", shifted, {{"beat", "stem.drums"}})).has_value());
    REQUIRE(s.beatGrid().beats.beatTimes.size() == g.beats.beatTimes.size());
    for (std::size_t i = 0; i < g.beats.beatTimes.size(); ++i) {
        CHECK(s.beatGrid().beats.beatTimes[i] == Catch::Approx(g.beats.beatTimes[i] + 0.1).margin(0.012));
    }
}

TEST_CASE("feeds: a seek lands response.*, beat.* and music.* where a play does", "[feeds][stems][engine][seek]") {
    const auto& fx = testsupport::requireStemFixture();
    const auto project = writeProject(fx, "seek", standardStems(fx),
                                      {{"beat", "stem.drums"}, {"hits", "stem.drums"}, {"levels", "stem.bass"}});
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
        for (std::size_t i = 0; i < played.signals().size(); ++i) {
            const std::string& n = played.signals().info(static_cast<signals::SignalId>(i)).name;
            if (n.starts_with("response.") || n.starts_with("beat.") || n.starts_with("music.")) {
                INFO(n);
                const auto other = scrubbed.signals().find(n);
                REQUIRE(other.has_value());
                CHECK(scrubbed.signals().value(*other) ==
                      Catch::Approx(played.signals().value(static_cast<signals::SignalId>(i))).margin(1e-6));
            }
        }
    }
}

TEST_CASE("feeds: the block and the stem trim survive a save; a project without them writes neither",
          "[feeds][stems][engine][project]") {
    const auto& fx = testsupport::requireStemFixture();
    json stems = json::array({stem(fx, "drums", "drums.wav", 0.0, -6.5f), stem(fx, "bass", "bass.wav")});
    const json feeds = {{"beat", "stem.drums"}, {"hits", "stem.drums"}, {"low", "stem.bass"}, {"onset", "audio"}};
    app::Engine e(app::EngineMode::Offline);
    REQUIRE(e.loadProject(writeProject(fx, "save", stems, feeds)).has_value());
    const fs::path saved = testsupport::processTempDir() / "feeds-save" / "saved.json";
    REQUIRE(e.saveProject(saved).has_value());
    json doc;
    std::ifstream(saved) >> doc;
    CHECK(doc["feeds"] == feeds);
    CHECK(doc["assets"]["stems"][0]["gainDb"] == -6.5);
    CHECK_FALSE(doc["assets"]["stems"][1].contains("gainDb"));
    app::Engine reloaded(app::EngineMode::Offline);
    REQUIRE(reloaded.loadProject(saved).has_value());
    CHECK(reloaded.feeds() == e.feeds());
    CHECK(reloaded.stems()[0].source.gainDb == -6.5f);
    // Clearing the feeds in the session takes the tier off the stem again and the block out of the file.
    reloaded.setFeeds({});
    CHECK_FALSE(reloaded.stems()[0].track->hits());
    CHECK(reloaded.beatGrid().sourceKey.empty());
    REQUIRE(reloaded.saveProject(saved).has_value());
    json cleared;
    std::ifstream(saved) >> cleared;
    CHECK_FALSE(cleared.contains("feeds"));
}


TEST_CASE("feeds: routes on the response hits are live on the mix and from a drum stem",
          "[feeds][stems][engine][liveness]") {
    const auto& fx = testsupport::requireStemFixture();
    const auto audit = [&](const char* name, const json& feeds) {
        app::Engine e(app::EngineMode::Offline);
        REQUIRE(e.loadProject(writeProject(fx, name, standardStems(fx), feeds)).has_value());
        for (const char* source : {"response.low", "response.lowEnv", "response.onset"}) {
            params::ModRoute r;
            r.source = source;
            r.target = "camera/height";
            r.amount = 1.0f;
            e.modulator().addRoute(r);
        }
        e.rebind();
        return e.auditRoutes();
    };
    for (const json& feeds : {json(), json{{"hits", "stem.drums"}}, json{{"low", "stem.drumz"}}}) {
        const scene::RouteAudit a = audit("liveness-hits", feeds);
        REQUIRE(a.routes.size() == 3);
        for (const scene::AuditEntry& entry : a.routes) {
            INFO(feeds.dump() << " " << entry.label);
            CHECK(entry.verdict != params::liveness::Verdict::Dead);
        }
    }
}
