// Slice 4: event-driven proposals (ADR-767). A plan places items ON what happens in the film --
// {"event": "abduction/beam", "occurrence": 2} -- by an observation of a watched play that the plan
// carries, so its times are the same whenever it is compiled. Never guessed: unwatched is an error.

#include "app/directing_context.hpp"
#include "app/directing_record.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "directing/time_ref.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

using namespace avgen;
using namespace avgen::directing;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

MusicalContext watched() {
    MusicalContext ctx;
    ctx.durationSeconds = 200.0;
    ctx.observedUntil = 60.0;
    ctx.observed = {{"abduction/beam", "visitor", 7.85}, {"goal.arrived", "rook", 14.4},
                    {"abduction/beam", "visitor", 24.9}, {"abduction/beam", "saucer-2", 30.0}};
    return ctx;
}

TimeRef eventRef(json j) {
    std::vector<Issue> issues;
    auto t = TimeRef::fromJson(j, "/t", issues);
    REQUIRE(t.has_value());
    return *t;
}

bool hasCode(const TimeResolution& r, IssueCode code, Severity severity) {
    return std::any_of(r.issues.begin(), r.issues.end(),
                       [&](const Issue& i) { return i.code == code && i.severity == severity; });
}

} // namespace

TEST_CASE("an event time is placed by the watched film, never guessed", "[directing][events]") {
    const MusicalContext ctx = watched();
    SECTION("unwatched: an error that says how to find out") {
        MusicalContext none = ctx;
        none.observedUntil = -1.0;
        const TimeResolution r = resolveTime(eventRef({{"event", "abduction/beam"}, {"occurrence", 1}}), none, "/t");
        CHECK_FALSE(r.seconds.has_value());
        CHECK(hasCode(r, IssueCode::UnresolvableTime, Severity::Error));
    }
    SECTION("the nth time, by anyone or by one subject, with an offset") {
        TimeResolution r = resolveTime(eventRef({{"event", "abduction/beam"}, {"occurrence", 2}}), ctx, "/t");
        REQUIRE(r.seconds);
        CHECK(*r.seconds == Catch::Approx(24.9));
        r = resolveTime(eventRef({{"event", "abduction/beam"}, {"subject", "saucer-2"}, {"occurrence", 1},
                                  {"offsetSeconds", -1.0}}),
                        ctx, "/t");
        REQUIRE(r.seconds);
        CHECK(*r.seconds == Catch::Approx(29.0));
        r = resolveTime(eventRef({{"event", "abduction/beam"}, {"occurrence", -1}}), ctx, "/t");
        CHECK(*r.seconds == Catch::Approx(30.0));
        // Timed with audio off: said, and never silently.
        CHECK(hasCode(r, IssueCode::NonDeterministic, Severity::Warning));
    }
    SECTION("ambiguous, too few, and never happened") {
        CHECK(hasCode(resolveTime(eventRef({{"event", "abduction/beam"}}), ctx, "/t"), IssueCode::AmbiguousTime,
                      Severity::Error));
        CHECK(hasCode(resolveTime(eventRef({{"event", "abduction/beam"}, {"occurrence", 9}}), ctx, "/t"),
                      IssueCode::UnresolvableTime, Severity::Error));
        const TimeResolution missing = resolveTime(eventRef({{"event", "abduction/bem"}, {"occurrence", 1}}), ctx, "/t");
        CHECK(hasCode(missing, IssueCode::UnresolvableTime, Severity::Error));
        REQUIRE_FALSE(missing.issues.empty());
        CHECK(std::find(missing.issues[0].suggestions.begin(), missing.issues[0].suggestions.end(), "abduction/beam") !=
              missing.issues[0].suggestions.end());
    }
    SECTION("an event time and a plan's observation survive the document") {
        const TimeRef t = eventRef({{"event", "goal.arrived"}, {"subject", "rook"}, {"occurrence", 1}});
        std::vector<Issue> issues;
        CHECK(TimeRef::fromJson(t.toJson(), "/t", issues) == t);
        json doc = json::parse(R"({"schemaVersion": 1, "id": "p", "title": "P", "tier": "baked",
            "markers": [{"key": "m", "name": "arrival", "at": {"event": "goal.arrived", "subject": "rook", "occurrence": 1}}],
            "observation": {"events": [{"name": "goal.arrived", "subject": "rook", "seconds": 14.4}], "until": 60}})");
        PlanParse parsed = parsePlan(doc);
        REQUIRE(parsed.plan);
        REQUIRE(parsed.plan->observation);
        CHECK(parsed.plan->observation->first.size() == 1);
        const PlanParse again = parsePlan(parsed.plan->toJson());
        REQUIRE(again.plan);
        CHECK(*again.plan == *parsed.plan);
    }
}

TEST_CASE("the watched film is the played film: an event-driven marker lands on the event", "[directing][events][benchmark]") {
    app::Engine live(app::EngineMode::Offline);
    REQUIRE(live.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    const auto first = app::watchWorldEvents(live, 30.0);
    REQUIRE(first.has_value());
    const auto second = app::watchWorldEvents(live, 30.0);
    REQUIRE(second.has_value());
    CHECK(first->events == second->events); // the same film, the same events
    std::vector<ObservedEvent> beams;
    for (const ObservedEvent& e : first->events) {
        if (e.name == "abduction/beam") {
            beams.push_back(e);
        }
    }
    REQUIRE(beams.size() >= 2);

    json doc = json::parse(R"({"schemaVersion": 1, "id": "beams", "title": "The second beam", "tier": "baked",
        "subjects": [{"alias": "visitor", "text": "the visitor", "hint": "hero"}],
        "markers": [{"key": "beam2", "name": "second beam", "at": {"event": "abduction/beam", "subject": "visitor", "occurrence": 2}}],
        "shots": [{"key": "shot", "name": "the-beam", "start": {"event": "abduction/beam", "occurrence": 2, "offsetSeconds": -1},
                   "durationSeconds": 4, "subject": "visitor", "camera": [{"move": "push_in"}]}]})");
    doc["observation"] = app::observationJson(*first);
    PlanParse parsed = parsePlan(doc);
    REQUIRE(parsed.plan);
    const Compilation c = compilePlan(*parsed.plan, app::sceneFactsFor(live));
    INFO(c.diffText());
    CHECK_FALSE(c.validation.hasErrors());
    const auto marker = std::find_if(c.staged.sequence.markers.begin(), c.staged.sequence.markers.end(),
                                     [](const seq::Marker& m) { return m.name == "second beam"; });
    REQUIRE(marker != c.staged.sequence.markers.end());
    CHECK(marker->timeSeconds == beams[1].seconds);
    const seq::Shot* shot = c.staged.sequence.shotNamed("the-beam");
    REQUIRE(shot != nullptr);
    CHECK(shot->startSeconds == Catch::Approx(beams[1].seconds - 1.0));

    // Played, the project raises that event at that second (audio off, as watched).
    app::Engine played(app::EngineMode::Offline);
    REQUIRE(played.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    scene::DetailLimits limits = played.detailLimits();
    limits.entityDistanceCull = false;
    played.setDetailLimits(limits);
    REQUIRE(played.setAudioClips({}).has_value());
    double seen = -1.0;
    int count = 0;
    std::uint64_t lastSequence = 0;
    for (std::uint64_t f = 0; f <= static_cast<std::uint64_t>(std::ceil(beams[1].seconds * 60.0)) + 1 && seen < 0.0; ++f) {
        played.update(FrameTime{static_cast<double>(f) / 60.0, f == 0 ? 0.0 : 1.0 / 60.0, f});
        const entity::EntityWorld& world = played.composition()->entityWorld();
        for (const entity::WorldEvent& e : world.worldEvents()) {
            if (e.sequence <= lastSequence && (lastSequence != 0 || e.sequence == 0)) {
                continue;
            }
            lastSequence = e.sequence;
            if (world.eventName(e.type) == "abduction/beam" && ++count == 2) {
                seen = e.time;
            }
        }
    }
    INFO("watched " << beams[1].seconds << " s, played " << seen << " s");
    CHECK(seen == beams[1].seconds);
}
