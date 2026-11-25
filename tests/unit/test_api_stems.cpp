// ADR-2158 (Engine API stage S5): audio stems through the session. `audio.import_stem` copies a stem into
// <project>/assets/audio/stems/ under ADR-2156's file identity (sha256 de-duplication, a manifest v2 entry, the copy
// renamed into place), installs it with `Engine::setStems`, and returns the stem.<key>.* signals read back from the
// bus. A route on one binds and the route audit calls it live; project.save writes assets.stems[i] with a file
// reference and project.open restores it.
//
// Everything drives a real `app::Engine` through a real `api::Session`, as the PoC client will.

#include "ai/tool_api.hpp"
#include "api/path_policy.hpp"
#include "api/session.hpp"
#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "audio/source_role.hpp"
#include "scene/route_liveness.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <unistd.h>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("avgen_stems_" + name + "_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return fs::weakly_canonical(dir);
}

std::string code(const api::CallResult& r) {
    if (r.error) {
        UNSCOPED_INFO(r.error->message << " / " << r.error->recovery);
    }
    return r.error ? ai::toolErrorCodeName(r.error->code) : "none";
}

void checkOutput(const api::Session& s, const std::string& tool, const api::CallResult& r) {
    const ai::Tool* t = s.tools().find(tool);
    REQUIRE(t != nullptr);
    REQUIRE_FALSE(t->definition.outputSchema.is_null());
    const auto problem = ai::validateAgainstSchema(t->definition.outputSchema, r.result, tool);
    INFO((problem ? problem->message : std::string{}) << "\n" << r.result.dump(2));
    CHECK_FALSE(problem.has_value());
}

// A generated two-tone stem: a 60 Hz "kick" gated on every beat at 120 BPM and a quiet 2 kHz tone throughout,
// mono float WAV at 48 kHz.
void writeTwoToneStem(const fs::path& file, double seconds = 4.0) {
    constexpr std::uint32_t rate = 48000;
    const auto frames = static_cast<std::size_t>(seconds * rate);
    std::vector<float> samples(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double sinceBeat = std::fmod(t, 0.5);
        const double kick = sinceBeat < 0.15 ? std::exp(-sinceBeat * 30.0) : 0.0;
        samples[i] = static_cast<float>(0.8 * kick * std::sin(2.0 * std::numbers::pi * 60.0 * t) +
                                        0.05 * std::sin(2.0 * std::numbers::pi * 2000.0 * t));
    }
    REQUIRE(audio::AudioFile::fromInterleaved(std::move(samples), 1, rate).writeWav(file).has_value());
}

std::vector<std::string> names(const json& signalList) {
    std::vector<std::string> out;
    for (const json& s : signalList) {
        out.push_back(s.at("name").get<std::string>());
    }
    return out;
}

struct Rig {
    fs::path root;
    fs::path read;
    fs::path write;
    app::Engine engine{app::EngineMode::Offline};
    api::LocalHost host{engine};
    std::unique_ptr<api::Session> s;
    explicit Rig(const std::string& name) : root(freshDir(name)), read(root / "exports"), write(root / "work") {
        fs::create_directories(read);
        fs::create_directories(write);
        api::SessionOptions o;
        o.client = api::ClientInfo{api::ClientKind::Test, "test", "1"};
        o.permissions.readRoots = {read};
        o.permissions.writeRoots = {write};
        auto made = api::Session::create(std::move(o), host);
        REQUIRE(made.has_value());
        s = std::move(*made);
        s->pump();
    }
    ~Rig() {
        s.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    fs::path create(const std::string& path) {
        const auto r = s->call("project.create", json{{"path", path}, {"scene", "empty"}});
        INFO(code(r));
        REQUIRE(r.success);
        return fs::path(r.result.at("folder").get<std::string>());
    }
};

bool routeLive(const app::Engine& engine, const std::string& source) {
    const scene::RouteAudit audit = engine.auditRoutes();
    for (const scene::AuditEntry& e : audit.routes) {
        if (e.label.find(source) != std::string::npos) {
            INFO(e.label << ": " << scene::auditReason(e));
            CHECK(e.verdict == params::liveness::Verdict::Live);
            return e.verdict == params::liveness::Verdict::Live;
        }
    }
    FAIL("no audited route from " << source);
    return false;
}

} // namespace

TEST_CASE("audio.import_stem: the stem's signals are on the bus, a route on one is live, and a save round-trips it",
          "[api][stems]") {
    Rig rig("import");
    const fs::path project = rig.create("stems/stems.json");
    writeTwoToneStem(rig.read / "drums.wav");

    const auto imported = rig.s->call("audio.import_stem",
                                      json{{"file", (rig.read / "drums.wav").string()}, {"key", "drums"},
                                           {"role", "drums"}, {"label", "Drum bus"}});
    INFO(code(imported));
    REQUIRE(imported.success);
    checkOutput(*rig.s, "audio.import_stem", imported);
    const json& out = imported.result;
    CHECK(out.at("id") == "asset://project/audio/stems/drums.wav");
    CHECK(fs::exists(project / "assets" / "audio" / "stems" / "drums.wav"));
    CHECK(out.at("duplicate") == false);
    CHECK(out.at("role") == "drums");
    CHECK(out.at("analysis").at("analysed") == true);
    CHECK(out.at("analysis").at("sampleRate") == 48000);
    CHECK(out.at("analysis").at("channels") == 1);
    CHECK(std::abs(out.at("analysis").at("durationSeconds").get<double>() - 4.0) < 1e-3);
    // The signals the call returns are the bus's, which is what signal.list reads.
    const auto listed = rig.s->call("signal.list", json{{"query", "stem.drums."}, {"limit", 200}});
    REQUIRE(listed.success);
    const std::vector<std::string> fromCall = names(out.at("signals"));
    const std::vector<std::string> fromBus = names(listed.result.at("signals"));
    CHECK(fromCall == fromBus);
    for (const char* wanted : {"stem.drums.level", "stem.drums.rms", "stem.drums.onset", "stem.drums.bass",
                               "stem.drums.active"}) {
        INFO(wanted);
        CHECK(std::find(fromCall.begin(), fromCall.end(), wanted) != fromCall.end());
    }
    {
        const auto roles = out.at("roleSignals").get<std::vector<std::string>>();
        CHECK(std::find(roles.begin(), roles.end(), "role.drums.level") != roles.end());
    }
    // The manifest names the copy with what the analysis found.
    {
        std::ifstream in(project / "assets" / "manifest.json");
        const json manifest = json::parse(in);
        bool found = false;
        for (const json& e : manifest.at("assets")) {
            if (e.at("path") == "assets/audio/stems/drums.wav") {
                found = true;
                CHECK(e.at("id") == "asset://project/audio/stems/drums.wav");
                CHECK(e.at("sha256") == out.at("sha256"));
                CHECK(e.at("import").at("sampleRate") == 48000);
            }
        }
        CHECK(found);
    }

    // A route from it binds, and the audit calls it live.
    const auto route = rig.s->call("modulation.create",
                                   json{{"source", "stem.drums.level"}, {"target", "camera/height"}, {"amount", 0.5}});
    INFO(code(route));
    REQUIRE(route.success);
    CHECK(route.result.at("bound") == true);
    CHECK(routeLive(rig.engine, "stem.drums.level"));

    const auto stems = rig.s->call("audio.list_stems", json::object());
    REQUIRE(stems.success);
    checkOutput(*rig.s, "audio.list_stems", stems);
    REQUIRE(stems.result.at("stems").size() == 1);
    CHECK(stems.result.at("stems")[0].at("key") == "drums");
    CHECK(stems.result.at("stems")[0].at("signalCount") == out.at("signals").size());
    CHECK(stems.result.at("roles") == json{"drums", "bass", "synth", "vocal", "other"});

    // The save writes the file reference relative to the project; reopening restores the stem and its signals.
    REQUIRE(rig.s->call("project.save", json::object()).success);
    {
        std::ifstream in(project / "stems.json");
        const json doc = json::parse(in);
        const json& stem = doc.at("assets").at("stems").at(0);
        CHECK(stem.at("key") == "drums");
        CHECK(stem.at("role") == "drums");
        CHECK(stem.at("label") == "Drum bus");
        CHECK(stem.at("file").at("path") == "assets/audio/stems/drums.wav");
        CHECK(stem.at("file").at("size") == out.at("size"));
        CHECK(stem.at("file").at("sha256") == out.at("sha256"));
    }
    // A second engine and session over the same roots, as a later client would open it.
    struct Other {
        app::Engine engine{app::EngineMode::Offline};
        api::LocalHost host{engine};
        std::unique_ptr<api::Session> s;
    } other;
    {
        api::SessionOptions o;
        o.client = api::ClientInfo{api::ClientKind::Test, "test", "1"};
        o.permissions.readRoots = {rig.read};
        o.permissions.writeRoots = {rig.write};
        auto made = api::Session::create(std::move(o), other.host);
        REQUIRE(made.has_value());
        other.s = std::move(*made);
        other.s->pump();
    }
    const auto opened = other.s->call("project.open", json{{"path", (project / "stems.json").string()}});
    INFO(code(opened));
    REQUIRE(opened.success);
    REQUIRE(other.engine.stems().size() == 1);
    CHECK(other.engine.stems()[0].source.key == "drums");
    CHECK(other.engine.stems()[0].source.role == audio::SourceRole::Drums);
    CHECK(other.engine.stems()[0].track != nullptr);
    const auto relisted = other.s->call("signal.list", json{{"query", "stem.drums."}, {"limit", 200}});
    CHECK(names(relisted.result.at("signals")) == fromBus);
    CHECK(routeLive(other.engine, "stem.drums.level"));
}

TEST_CASE("audio.import_stem refuses a duplicate key, a bad key or role, and a path outside the roots",
          "[api][stems]") {
    Rig rig("refusals");
    const fs::path project = rig.create("refuse");
    writeTwoToneStem(rig.read / "drums.wav", 1.0);
    const std::string file = (rig.read / "drums.wav").string();
    REQUIRE(rig.s->call("audio.import_stem", json{{"file", file}, {"key", "drums"}}).success);

    SECTION("a key already in the project") {
        const auto again = rig.s->call("audio.import_stem", json{{"file", file}, {"key", "drums"}});
        CHECK(code(again) == "CONFLICT");
        CHECK(rig.engine.stems().size() == 1);
    }
    SECTION("the same bytes under another key reuse the copy") {
        const auto second = rig.s->call("audio.import_stem", json{{"file", file}, {"key", "kit"}});
        INFO(code(second));
        REQUIRE(second.success);
        CHECK(second.result.at("duplicate") == true);
        CHECK(second.result.at("id") == "asset://project/audio/stems/drums.wav");
        CHECK(rig.engine.stems().size() == 2);
    }
    SECTION("a key that is not an identifier") {
        for (const char* bad : {"Drums", "dr ums", "", "drums.level", "../x"}) {
            INFO(bad);
            CHECK(code(rig.s->call("audio.import_stem", json{{"file", file}, {"key", bad}})) == "INVALID_ARGUMENTS");
        }
    }
    SECTION("a role outside the table, with the valid list in the recovery") {
        const auto r = rig.s->call("audio.import_stem", json{{"file", file}, {"key", "k"}, {"role", "kick"}});
        CHECK(code(r) == "INVALID_ARGUMENTS");
        REQUIRE(r.error.has_value());
        for (const char* role : audio::kSourceRoleNames) {
            INFO(r.error->recovery);
            CHECK(r.error->recovery.find(role) != std::string::npos);
        }
    }
    SECTION("a file outside the session's roots") {
        const fs::path outside = rig.root / "elsewhere";
        fs::create_directories(outside);
        writeTwoToneStem(outside / "bass.wav", 1.0);
        const auto r = rig.s->call("audio.import_stem", json{{"file", (outside / "bass.wav").string()}, {"key", "bass"}});
        CHECK(code(r) == "PERMISSION_DENIED");
        CHECK_FALSE(fs::exists(project / "assets" / "audio" / "stems" / "bass.wav"));
    }
    SECTION("a file that does not decode leaves nothing behind") {
        std::ofstream(rig.read / "broken.wav") << "not a wav file";
        const auto r = rig.s->call("audio.import_stem", json{{"file", (rig.read / "broken.wav").string()}, {"key", "b"}});
        CHECK(code(r) == "UNSUPPORTED");
        CHECK_FALSE(fs::exists(project / "assets" / "audio" / "stems" / "broken.wav"));
        CHECK(rig.engine.stems().size() == 1);
    }
}

TEST_CASE("audio.import_stem in a rolled-back transaction: the stem stays, so its file stays", "[api][stems][compensation]") {
    Rig rig("rollback");
    const fs::path project = rig.create("rb");
    writeTwoToneStem(rig.read / "drums.wav", 1.0);
    const std::string file = (rig.read / "drums.wav").string();
    const fs::path copy = project / "assets" / "audio" / "stems" / "drums.wav";

    api::CallOptions in;
    in.transaction = rig.s->call("transaction.begin", json{{"label", "stems"}}).result.at("id").get<std::string>();
    // Irreversible: refused inside a transaction unless accepted.
    CHECK(code(rig.s->call("audio.import_stem", json{{"file", file}, {"key", "drums"}}, in)) == "FAILED_PRECONDITION");
    CHECK_FALSE(fs::exists(copy));
    in.irreversible = api::Irreversible::Accept;
    const auto imported = rig.s->call("audio.import_stem", json{{"file", file}, {"key", "drums"}}, in);
    INFO(code(imported));
    REQUIRE(imported.success);
    const auto back = rig.s->call("transaction.rollback", json{{"id", *in.transaction}});
    INFO(back.result.dump(2));
    REQUIRE(back.success);
    CHECK(back.result.at("notRestored").at("tools") == json{"audio.import_stem"});
    // S1's rule 2: the engine still names the file (no snapshot reaches the stem list), so it is kept, and said.
    CHECK(rig.engine.stems().size() == 1);
    CHECK(fs::exists(copy));
    bool reported = false;
    for (const json& k : back.result.at("files").at("kept")) {
        reported = reported || k.get<std::string>().find("drums.wav") != std::string::npos;
    }
    CHECK(reported);

    SECTION("once the stem is removed, nothing names the file") {
        const auto removed = rig.s->call("audio.remove_stem", json{{"key", "drums"}});
        REQUIRE(removed.success);
        checkOutput(*rig.s, "audio.remove_stem", removed);
        CHECK(removed.result.at("remaining") == 0);
        CHECK(rig.engine.stems().empty());
        CHECK(code(rig.s->call("audio.remove_stem", json{{"key", "drums"}})) == "NOT_FOUND");
        // The signals stay declared and read inactive.
        const auto active = rig.engine.signals().find("stem.drums.active");
        REQUIRE(active.has_value());
        CHECK(rig.engine.signals().value(*active) == 0.0f);
    }
}
