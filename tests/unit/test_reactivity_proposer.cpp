// ADR-927: the default reactivity proposer, on the glade (tests/support/reactivity_fixture.hpp).
//
// A golden plan pins the whole proposal (tests/data/directing/reactivity/glade-proposal.json; set
// AVGEN_REGEN_REACTIVITY_GOLDEN=1 to rewrite it after an intended change, and say why in the commit).
// The music is written by hand -- 120 BPM, measured kicks, claps and hats, the glade's six sections --
// so the golden depends on the scene and the proposer and not on the analyser. Beside the golden, the
// properties the brief asks for are checked one by one, so a regenerated golden cannot quietly drop
// one: three levels; each hero its own source, timing and amplitude; depth that follows the section;
// the fungi on the kick at +0.25-0.35 and the lamps on the claps; a hue keyed by section and never
// driven by the audio; nothing on a global light, a shared material, a character or a faint layer;
// and the validator finds no "everything on one source" or "one phase" in it.

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/reactivity.hpp"
#include "directing/reactivity_proposer.hpp"
#include "support/gltf_fixture.hpp"
#include "support/reactivity_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <tuple>

using namespace avgen;
using namespace avgen::directing;
using Catch::Matchers::WithinAbs;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// The glade's music, as a measured context would carry it.
MusicalContext gladeMusic(bool claps = true, bool measured = true, bool sections = true) {
    MusicalContext m;
    m.tempoBpm = 120.0;
    m.phraseBars = 8;
    for (int k = 0; k < 130; ++k) {
        m.beatTimes.push_back(0.5 + 0.5 * k);
    }
    m.firstBeatSeconds = 0.5;
    m.durationSeconds = 66.0;
    if (!sections) {
        return m;
    }
    m.sectionSource = "sectionTimeline";
    const song::SectionTimeline timeline = testsupport::gladeSections();
    const std::map<std::string, float> midLevel{{"intro", 0.52f}, {"groove", 0.60f}, {"breakdown", 0.45f},
                                                {"build", 0.64f}, {"drop", 0.70f}, {"outro", 0.50f}};
    std::map<std::string, int> occurrence;
    for (const song::Section& s : timeline.sections) {
        SectionRun run;
        run.type = s.type;
        run.startSeconds = s.startSeconds;
        run.endSeconds = s.endSeconds;
        run.energy = s.energy;
        run.occurrence = ++occurrence[s.type];
        if (measured) {
            run.audio.frames = 1000;
            run.audio.energy = 0.3f + 0.3f * s.energy;
            run.audio.kickRate = s.type == "breakdown" ? 0.0f : 2.0f;
            run.audio.snareRate = claps ? (s.type == "breakdown" ? 0.0f : 1.0f) : 0.0f;
            run.audio.hatRate = 2.0f;
            run.audio.onsetRate = run.audio.kickRate + run.audio.snareRate + run.audio.hatRate;
            run.audio.bandCount = 5;
            run.audio.bandLevels = {0.6f + 0.2f * s.energy, 0.55f, midLevel.at(s.type), 0.5f, 0.45f};
        }
        m.sections.push_back(std::move(run));
    }
    return m;
}

struct Glade {
    app::Engine engine{app::EngineMode::Offline};
    explicit Glade(const testsupport::GladeOptions& options = {}) {
        const auto glb = testsupport::writeTriangleGlb("reactivity_proposer");
        REQUIRE(testsupport::loadGlade(engine, glb, glb, options));
    }
    [[nodiscard]] SceneFacts facts() { return app::sceneFactsFor(engine); }
};

std::vector<const PlanRoute*> routesTo(const Plan& p, std::string_view target) {
    std::vector<const PlanRoute*> out;
    for (const PlanRoute& r : p.routes) {
        if (r.route.target == target) {
            out.push_back(&r);
        }
    }
    return out;
}

const PlanRoute& only(const Plan& p, std::string_view target) {
    const auto routes = routesTo(p, target);
    INFO(target);
    REQUIRE(routes.size() == 1);
    return *routes.front();
}

std::vector<const Issue*> issues(const Validation& v, IssueCode code) {
    std::vector<const Issue*> out;
    for (const Issue& i : v.issues) {
        if (i.code == code) {
            out.push_back(&i);
        }
    }
    return out;
}

bool noteSays(const ReactivityProposal& p, std::string_view fragment) {
    return std::any_of(p.notes.begin(), p.notes.end(), [&](const std::string& n) { return n.find(fragment) != std::string::npos; });
}

} // namespace

TEST_CASE("The default proposal on the glade matches its golden plan, and is the same every time",
          "[directing][reactivity][proposer][golden][adr927]") {
    Glade glade;
    const SceneFacts facts = glade.facts();
    const ReactivityProposal a = proposeReactivity(facts.capabilities.reactive(), gladeMusic());
    const ReactivityProposal b = proposeReactivity(glade.facts().capabilities.reactive(), gladeMusic());
    CHECK(a.plan.toJson().dump() == b.plan.toJson().dump()); // deterministic, byte for byte
    CHECK(a.notes == b.notes);

    const json doc{{"plan", a.plan.toJson()}, {"notes", a.notes}, {"summary", a.summaryJson()}};
    const fs::path golden = fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/reactivity/glade-proposal.json";
    if (const char* regen = std::getenv("AVGEN_REGEN_REACTIVITY_GOLDEN"); regen != nullptr && regen[0] == '1') {
        fs::create_directories(golden.parent_path());
        std::ofstream(golden) << doc.dump(2) << '\n';
    }
    std::ifstream in(golden);
    REQUIRE(in.good());
    const json recorded = json::parse(in);
    INFO("differs at: " << json::diff(recorded, doc).dump().substr(0, 1500));
    CHECK(recorded == doc);
}

TEST_CASE("The proposal answers the music at three levels, each hero on its own layer, depth following the section",
          "[directing][reactivity][proposer][adr927]") {
    Glade glade;
    const SceneFacts facts = glade.facts();
    const ReactivityProposal p = proposeReactivity(facts.capabilities.reactive(), gladeMusic());
    const Plan& plan = p.plan;
    INFO(p.summaryJson().dump(1));

    // ---- three levels, several groups ----
    std::map<ReactiveLevel, int> levels;
    for (const PlanRoute& r : plan.routes) {
        ++levels[r.level];
        CHECK_FALSE(r.reason.empty());
    }
    CHECK(levels[ReactiveLevel::Micro] >= 3);
    CHECK(levels[ReactiveLevel::Meso] >= 6);
    CHECK(levels[ReactiveLevel::Macro] >= 5);

    // ---- meso: the heroes, most important first, take the kick, the clap, the bar, the breath ----
    CHECK(only(plan, "nodes/elder-gills/emissiveBoost").route.source == "audio.onsetLow");
    CHECK(only(plan, "nodes/lantern-gills/emissiveBoost").route.source == "audio.onsetMid");
    CHECK(only(plan, "nodes/spire-gills/emissiveBoost").route.source == "music.downbeat");
    CHECK(only(plan, "nodes/moss-gills/emissiveBoost").route.source == "lfo.two-bar-breath");
    // Each hero's own triple of source, timing and amplitude: no two heroes share all three.
    std::set<std::tuple<std::string, float, float, float, float>> triples;
    for (const char* hero : {"elder", "lantern", "spire", "moss"}) {
        const params::ModRoute& r = only(plan, std::string("nodes/") + hero + "-gills/emissiveBoost").route;
        CHECK(triples.insert({r.source, r.chain.delayMs, r.chain.decayMs, r.amount, r.chain.remapOutMax}).second);
    }
    // A hero's program-lit parts answer 35 ms apart; its plain stem is left alone, and says so.
    CHECK(only(plan, "nodes/elder-cap/emissiveBoost").route.source == "audio.onsetLow");
    CHECK(only(plan, "nodes/elder-cap/emissiveBoost").route.chain.delayMs -
              only(plan, "nodes/elder-gills/emissiveBoost").route.chain.delayMs ==
          35.0f);
    CHECK(routesTo(plan, "nodes/elder-stem/emissiveBoost").empty());
    CHECK(noteSays(p, "nodes/elder-stem/emissiveBoost is left alone"));
    // The elder's spores and practical light echo it, later.
    const PlanRoute& spores = only(plan, "particles/elder-spores/emissive");
    CHECK(spores.route.source == "audio.onsetLow");
    CHECK(spores.route.chain.delayMs == 90.0f);
    const PlanRoute& practical = only(plan, "lightrig/Glade/elder-practical/intensity");
    CHECK(practical.route.source == "audio.onsetLow");
    CHECK(practical.route.chain.delayMs == 50.0f);
    // The character answers nothing: its glow is its simulation's.
    CHECK(routesTo(plan, "nodes/walker/emissiveBoost").empty());

    // ---- the glowing layers: the fungi echo the kick at +0.25-0.35, the lamps flare on the claps ----
    const PlanRoute& fungi = only(plan, "nodes/meadow/scatter/fungi/emissionGain");
    CHECK(fungi.route.source == "audio.onsetLow");
    CHECK(fungi.route.op == params::ModOp::Add);
    CHECK(fungi.route.amount >= 0.25f);
    CHECK(fungi.route.amount <= 0.35f);
    CHECK(fungi.route.chain.delayMs == 90.0f);
    CHECK(fungi.route.chain.attackMs <= 10.0f);
    CHECK(fungi.route.chain.decayMs >= 250.0f);
    CHECK(fungi.route.chain.decayMs <= 400.0f);
    const PlanRoute& lamps = only(plan, "nodes/meadow/scatter/lamps/emissionGain");
    CHECK(lamps.route.source == "audio.onsetMid");
    CHECK(lamps.route.chain.attackMs <= 10.0f);
    CHECK(only(plan, "nodes/meadow/scatter/shelf/emissionGain").route.source == "audio.onsetHigh");
    CHECK(only(plan, "nodes/meadow/scatter/shelf/emissionGain").level == ReactiveLevel::Micro);
    // The faint layer is left alone; the dark one was never offered.
    CHECK(routesTo(plan, "nodes/meadow/scatter/grass/emissionGain").empty());
    CHECK(noteSays(p, "faintly glowing layers are left alone"));
    CHECK(routesTo(plan, "nodes/meadow/scatter/stones/emissionGain").empty());

    // ---- depth follows the section: 35% in the quietest (breakdown 0.20), full in the loudest (1.00) ----
    CHECK(p.depthSource == "section.energy");
    const float quiet = p.depthMin + (p.depthMax - p.depthMin) * 0.20f;
    const float loud = p.depthMin + (p.depthMax - p.depthMin) * 1.00f;
    CHECK_THAT(quiet, WithinAbs(0.35, 0.002));
    CHECK_THAT(loud, WithinAbs(1.0, 0.002));
    for (const PlanRoute& r : plan.routes) {
        const bool beat = r.route.source.starts_with("audio.onset") || r.route.source.starts_with("music.") ||
                          r.route.source == "section.change";
        if (beat) {
            INFO(r.key);
            CHECK(r.route.depthSource == "section.energy");
        }
        // Never a depth that flickers or fires for one frame.
        CHECK(r.route.depthSource != "audio.rms");
        CHECK_FALSE(r.route.depthSource.starts_with("music."));
    }

    // ---- macro: the section on the world, the wave through the fungi, the lead on the aurora ----
    CHECK(only(plan, "scene/ecologyLight").route.source == "section.energy");
    CHECK(only(plan, "scene/volumeDensity").route.source == "section.energy");
    CHECK(only(plan, "scene/windSpeed").route.source == "section.energy");
    CHECK(only(plan, "scene/wind/gustAmount").route.source == "lfo.two-bar-breath");
    CHECK(only(plan, "nodes/meadow/water/glow").route.source == "section.energy");
    CHECK(only(plan, "fx/aurora/intensity").route.source == "audio.midLevel");
    CHECK(only(plan, "nodes/meadow/scatter/fungi/emissiveFieldAmount").route.source == "section.energy");
    CHECK(only(plan, "procedural/moss-gills/emissiveFieldAmount").route.source == "section.energy");

    // ---- the colour: keyed by section through a timeline source, never driven by the audio ----
    const auto hueSource = std::find_if(plan.sources.begin(), plan.sources.end(),
                                        [](const PlanSource& s) { return s.kind == "timeline"; });
    REQUIRE(hueSource != plan.sources.end());
    const PlanRoute& fungiHue = only(plan, "nodes/meadow/scatter/fungi/hueOffset");
    CHECK(fungiHue.route.source == hueSource->signal());
    CHECK(fungiHue.level == ReactiveLevel::Macro);
    for (const PlanRoute& r : plan.routes) {
        if (r.route.target.ends_with("/hueOffset")) {
            CHECK(r.route.source.starts_with("timeline."));
        }
    }
    // The keys: cooler (+0.06) in the breakdown, warmer (-0.08) in the drop.
    const json& keys = hueSource->settings.at("keys");
    const song::SectionTimeline sections = testsupport::gladeSections();
    const auto valueAt = [&](double t) {
        double v = keys.front().at("value").get<double>();
        for (const json& k : keys) {
            if (k.at("time").get<double>() <= t) {
                v = k.at("value").get<double>();
            }
        }
        return v;
    };
    CHECK_THAT(valueAt(sections.sections[2].endSeconds - 0.01), WithinAbs(0.06, 1e-4)); // breakdown
    CHECK_THAT(valueAt(sections.sections[4].endSeconds - 0.01), WithinAbs(-0.08, 1e-4)); // drop
    CHECK(only(plan, "nodes/meadow/scatter/lamps/hueOffset").route.chain.delayMs > fungiHue.route.chain.delayMs);

    // ---- what it leaves alone ----
    CHECK(routesTo(plan, "lightrig/Glade/moon/intensity").empty());
    CHECK(routesTo(plan, "lightrig/Glade/keyIntensity").empty());
    CHECK(routesTo(plan, "material/tissue/emissionIntensity").empty());
    CHECK(noteSays(p, "the style shift the owner ruled out"));
    CHECK(noteSays(p, "in lockstep"));

    // ---- the validator agrees: every route live, nothing on one source, nothing in one phase ----
    Plan copy = plan;
    const Validation v = validatePlan(copy, facts);
    for (const Issue& i : v.issues) {
        INFO(i.toJson().dump());
        CHECK(i.severity != Severity::Error);
    }
    CHECK(issues(v, IssueCode::OneSource).empty());
    CHECK(issues(v, IssueCode::OnePhase).empty());
    CHECK(issues(v, IssueCode::DeadTarget).empty());
    CHECK(issues(v, IssueCode::PhaseRateTrap).empty());
}

TEST_CASE("The proposal follows what the music offers: no claps, no audio, no sections",
          "[directing][reactivity][proposer][adr927]") {
    Glade glade;
    const ReactiveCatalog catalog = glade.facts().capabilities.reactive();

    SECTION("no claps: nothing reads the snare band, and the lamps take the bar") {
        const ReactivityProposal p = proposeReactivity(catalog, gladeMusic(false));
        for (const PlanRoute& r : p.plan.routes) {
            CHECK(r.route.source != "audio.onsetMid");
        }
        CHECK(only(p.plan, "nodes/meadow/scatter/lamps/emissionGain").route.source == "music.downbeat");
        CHECK(only(p.plan, "nodes/lantern-gills/emissiveBoost").route.source == "music.downbeat");
    }
    SECTION("no measured audio: only the grid and the sections are followed") {
        const ReactivityProposal p = proposeReactivity(catalog, gladeMusic(true, false));
        for (const PlanRoute& r : p.plan.routes) {
            INFO(r.key);
            CHECK_FALSE(r.route.source.starts_with("audio."));
        }
        CHECK(p.depthSource == "section.energy");
        CHECK(only(p.plan, "nodes/elder-gills/emissiveBoost").route.source == "music.downbeat");
    }
    SECTION("no sections: no depth from them and no colour keyed by them") {
        const ReactivityProposal p = proposeReactivity(catalog, gladeMusic(true, true, false));
        CHECK(p.plan.sources.size() <= 1); // the breath at most: no hue timeline
        for (const PlanRoute& r : p.plan.routes) {
            CHECK(r.route.depthSource != "section.energy");
            CHECK_FALSE(r.route.target.ends_with("/hueOffset"));
        }
    }
}

TEST_CASE("A wave field on the transport clock is named, not routed: it would leave once at 0 s",
          "[directing][reactivity][proposer][adr927]") {
    testsupport::GladeOptions options;
    options.triggeredWave = false;
    Glade glade(options);
    const ReactivityProposal p = proposeReactivity(glade.facts().capabilities.reactive(), gladeMusic());
    CHECK(routesTo(p.plan, "nodes/meadow/scatter/fungi/emissiveFieldAmount").empty());
    CHECK(noteSays(p, "runs on the transport clock"));

    testsupport::GladeOptions none;
    none.wave = false;
    Glade bare(none);
    const ReactivityProposal q = proposeReactivity(bare.facts().capabilities.reactive(), gladeMusic());
    CHECK(noteSays(q, "no glowing layer names a wave field"));
}

// ADR-916: the water's tears are proposed at the paths the water stream registers them under. Off (their
// amount at 0, where the tear code is compiled out) nothing routes to any of them -- the control; on, the
// amount follows the section, slowly, and the shear and coverage are left alone (they move the ripples
// inside a seam as they change).
TEST_CASE("The proposal lifts the water's tears with the section once they are on, and not while they are off",
          "[directing][reactivity][proposer][adr927][tears]") {
    Glade glade;
    const std::string base = "nodes/meadow/water/tears/";
    const ReactivityProposal off = proposeReactivity(glade.facts().capabilities.reactive(), gladeMusic());
    for (const char* leaf : {"amount", "shear", "coverage"}) {
        INFO(leaf);
        CHECK(routesTo(off.plan, base + leaf).empty());
    }
    auto* amount = glade.engine.params().findAs<float>(base + "amount");
    REQUIRE(amount != nullptr);
    amount->setBase(0.4f);
    const ReactivityProposal on = proposeReactivity(glade.facts().capabilities.reactive(), gladeMusic());
    const PlanRoute& lift = only(on.plan, base + "amount");
    CHECK(lift.route.source == "section.energy");
    CHECK(lift.route.op == params::ModOp::Multiply);
    CHECK(lift.route.chain.attackMs >= 1000.0f); // slowly
    CHECK(routesTo(on.plan, base + "shear").empty());
    CHECK(routesTo(on.plan, base + "coverage").empty());
}
