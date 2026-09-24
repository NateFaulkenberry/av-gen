// The resolver: times and names, against a synthetic song and against the benchmark (ADR-755,
// director-system-progress.md Slice 1.2).
//
// The LLM never does musical-time arithmetic and never guesses a subject (spec §1.4, §13, §14). So
// every case here is either an exact answer or a refusal that names the candidates.

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "directing/plan.hpp"
#include "directing/resolver.hpp"
#include "directing/time_ref.hpp"
#include "seq/sequence.hpp"
#include "song/section_timeline.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

using namespace avgen;
using namespace avgen::directing;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

namespace {

TimeRef parsed(std::string_view text) {
    std::vector<Issue> issues;
    auto t = parseTime(text, issues);
    INFO(text);
    for (const Issue& i : issues) {
        INFO(i.toJson().dump());
    }
    REQUIRE(t.has_value());
    CHECK(issues.empty());
    return *t;
}

IssueCode refusal(std::string_view text) {
    std::vector<Issue> issues;
    const auto t = parseTime(text, issues);
    INFO(text);
    CHECK_FALSE(t.has_value());
    REQUIRE_FALSE(issues.empty());
    return issues.front().code;
}

// intro 0-20 | chorus 20-40 (two timeline entries) | verse 40-60 | chorus 60-80 | outro 80-90,
// at 120 BPM with a grid that stops at 40 s.
MusicalContext syntheticSong() {
    seq::Sequence piece;
    const auto add = [&](const char* type, double a, double b) {
        song::Section s;
        s.type = type;
        s.startSeconds = a;
        s.endSeconds = b;
        piece.sectionTimeline.sections.push_back(s);
    };
    add("intro", 0, 20);
    add("chorus", 20, 30);
    add("chorus", 30, 40);
    add("verse", 40, 60);
    add("chorus", 60, 80);
    add("outro", 80, 90);
    std::vector<double> beats;
    for (double t = 0.0; t < 40.0; t += 0.5) {
        beats.push_back(t);
    }
    return musicalContextFrom(piece, beats, 120.0, 90.0);
}

TimeResolution resolve(std::string_view text, const MusicalContext& ctx) { return resolveTime(parsed(text), ctx); }

std::unique_ptr<app::Engine> benchmark() {
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    auto loaded = engine->loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json");
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    return engine;
}

nlohmann::json testsupport_readPlan() {
    return testsupport::readJson(fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/rook_umbra.plan.json");
}

} // namespace

TEST_CASE("the time parser reads what people write, and refuses the rest", "[directing][time]") {
    CHECK(parsed("1:30").seconds == 90.0);
    CHECK(parsed("01:30.5").seconds == 90.5);
    CHECK(parsed("1:02:03").seconds == 3723.0);
    CHECK(parsed("90s").seconds == 90.0);
    CHECK(parsed("90 seconds").seconds == 90.0);
    CHECK(parsed("92.55").seconds == 92.55);

    const TimeRef bar = parsed("bar 64 beat 3");
    CHECK(bar.kind == TimeRef::Kind::Bar);
    CHECK(bar.bar == 64);
    CHECK(bar.beat == 3);
    CHECK(parsed("Bar 9").beat == 1);

    const TimeRef second = parsed("the second chorus");
    CHECK(second.kind == TimeRef::Kind::Section);
    CHECK(second.section == "chorus");
    CHECK(second.occurrence == 2);
    CHECK(parsed("chorus 2") .occurrence == 2);
    CHECK(parsed("Chorus 2").section == "chorus");
    CHECK(parsed("the last verse").occurrence == -1);
    CHECK(parsed("final chorus").occurrence == -1);
    CHECK(parsed("3rd verse").occurrence == 3);
    CHECK(parsed("start of the bridge").anchor == TimeRef::Anchor::Start);
    const TimeRef end = parsed("end of chorus 2");
    CHECK(end.anchor == TimeRef::Anchor::End);
    CHECK(end.occurrence == 2);
    CHECK(parsed("chorus 2 end").anchor == TimeRef::Anchor::End);
    CHECK(parsed("middle eight").section == "middle_eight");
    CHECK(parsed("post-chorus").section == "post_chorus");
    CHECK(normaliseSectionType("preChorus") == "pre_chorus"); // the analyser's spelling meets a person's
    const TimeRef offset = parsed("chorus 2 + 1.5s");
    CHECK(offset.offsetSeconds == 1.5);
    CHECK(parsed("1:30 - 0.25").offsetSeconds == -0.25);
    CHECK(parsed("1:30").text == "1:30");

    CHECK(refusal("1:75") == IssueCode::MalformedTime);
    CHECK(refusal("beat 3") == IssueCode::MalformedTime);
    CHECK(refusal("") == IssueCode::MalformedTime);
    CHECK(refusal("chorus 2x") == IssueCode::MalformedTime);
    CHECK(refusal("bar 0") == IssueCode::MalformedTime);
}

TEST_CASE("a time resolves against the song: runs, not entries; grids, then tempo; never a guess",
          "[directing][time]") {
    const MusicalContext ctx = syntheticSong();
    REQUIRE(ctx.sections.size() == 5); // the two chorus entries are one passage
    CHECK(ctx.sectionSource == "sectionTimeline");

    CHECK(resolve("1:15", ctx).seconds == 75.0);
    CHECK(resolve("chorus 1", ctx).seconds == 20.0);
    CHECK(resolve("end of chorus 1", ctx).seconds == 40.0); // the run's end, not its first entry's
    CHECK(resolve("second chorus", ctx).seconds == 60.0);   // the second time it comes round
    CHECK(resolve("last chorus", ctx).seconds == 60.0);
    CHECK(resolve("the verse", ctx).seconds == 40.0);       // one verse: not ambiguous
    CHECK(resolve("chorus 2 + 1.5s", ctx).seconds == 61.5);
    CHECK_FALSE(resolve("second chorus", ctx).explanation.empty());

    SECTION("bars: the grid, then the tempo with a warning, and beats are bounded by the bar") {
        const TimeResolution onGrid = resolve("bar 3 beat 2", ctx); // beat index 9 -> 4.5 s
        CHECK(onGrid.seconds == 4.5);
        CHECK(onGrid.issues.empty());
        const TimeResolution offGrid = resolve("bar 30", ctx); // index 116 -> past the 40 s grid
        REQUIRE(offGrid.seconds);
        CHECK_THAT(*offGrid.seconds, WithinAbs(58.0, 1e-9));
        REQUIRE(offGrid.issues.size() == 1);
        CHECK(offGrid.issues[0].severity == Severity::Warning);
        const TimeResolution tooMany = resolve("bar 2 beat 5", ctx);
        CHECK_FALSE(tooMany.seconds);
        CHECK(tooMany.issues[0].code == IssueCode::MalformedTime);

        MusicalContext silent = ctx;
        silent.beatTimes.clear();
        silent.tempoBpm = 0.0;
        CHECK(resolve("bar 2", silent).issues[0].code == IssueCode::UnresolvableTime);
    }
    SECTION("refusals name what there is") {
        const TimeResolution ambiguous = resolve("the chorus", ctx);
        CHECK_FALSE(ambiguous.seconds);
        REQUIRE(ambiguous.issues.size() == 1);
        CHECK(ambiguous.issues[0].code == IssueCode::AmbiguousTime);
        CHECK(ambiguous.issues[0].details["candidates"].size() == 2);
        CHECK(ambiguous.issues[0].suggestions == std::vector<std::string>{"chorus 1", "chorus 2"});

        const TimeResolution typo = resolve("chorous 2", ctx);
        REQUIRE(typo.issues.size() == 1);
        CHECK(typo.issues[0].code == IssueCode::UnresolvableTime);
        CHECK(typo.issues[0].suggestions == std::vector<std::string>{"chorus"});

        CHECK(resolve("third chorus", ctx).issues[0].code == IssueCode::UnresolvableTime);
        CHECK(resolve("2:00", ctx).issues[0].code == IssueCode::TimeOutOfRange);
        CHECK(resolve("intro - 1s", ctx).issues[0].code == IssueCode::TimeOutOfRange);
    }
}

TEST_CASE("on the benchmark, 1:30 is 90 s and the second chorus is the chorus that comes back",
          "[directing][time][benchmark]") {
    auto engine = benchmark();
    const MusicalContext ctx = app::musicalContextFor(*engine);
    CHECK(ctx.sectionSource == "sectionTimeline");
    INFO("analysed beats: " << ctx.beatTimes.size() << ", tempo " << ctx.tempoBpm);

    CHECK(resolve("1:30", ctx).seconds == 90.0);
    // The film's first chorus is three timeline entries (89.1, 96.5, 103.8); the second is at 118.6.
    const TimeResolution second = resolve("the second chorus", ctx);
    REQUIRE(second.seconds);
    CHECK_THAT(*second.seconds, WithinAbs(118.6, 0.1));
    const TimeResolution first = resolve("end of the first chorus", ctx);
    REQUIRE(first.seconds);
    CHECK_THAT(*first.seconds, WithinAbs(111.3, 0.1));
    CHECK(resolve("the chorus", ctx).issues.at(0).code == IssueCode::AmbiguousTime);
    const TimeResolution bridge = resolve("the bridge", ctx);
    REQUIRE_FALSE(bridge.issues.empty());
    CHECK(bridge.issues[0].code == IssueCode::UnresolvableTime); // this film calls it "middle_eight"
    CHECK(resolve("the middle eight", ctx).seconds.has_value());
}

TEST_CASE("subjects: Rook is the entity, Umbra is ambiguous, the Umbra hero is the hero",
          "[directing][subjects][benchmark]") {
    auto engine = benchmark();
    const SubjectIndex index = app::subjectIndexFor(*engine);

    const SubjectResult rook = resolveSubject(index, "Rook");
    REQUIRE(rook.status == SubjectResult::Status::Resolved);
    CHECK(rook.identity.kind == SubjectKind::Entity);
    CHECK(rook.identity.id == "rook");
    CHECK(rook.identity.node == "rook");

    const SubjectResult typo = resolveSubject(index, "Rookk");
    REQUIRE(typo.status == SubjectResult::Status::Unknown);
    REQUIRE(typo.issue);
    CHECK(typo.issue->code == IssueCode::UnknownSubject);
    CHECK(std::find(typo.issue->suggestions.begin(), typo.issue->suggestions.end(), "rook") !=
          typo.issue->suggestions.end());

    // Spec §14's example, as the scene actually is: a hero and four more `umbra-*` nodes.
    const SubjectResult umbra = resolveSubject(index, "Umbra");
    REQUIRE(umbra.status == SubjectResult::Status::Ambiguous);
    REQUIRE(umbra.issue);
    CHECK(umbra.issue->code == IssueCode::AmbiguousReference);
    INFO(umbra.issue->toJson().dump(2));
    CHECK(umbra.candidates.size() >= 2);
    CHECK(std::any_of(umbra.candidates.begin(), umbra.candidates.end(), [](const SubjectIdentity& c) {
        return c.kind == SubjectKind::Hero && c.id == "umbra-cap";
    }));
    // ...and the node the hero names is the hero, not a second candidate.
    CHECK(std::none_of(umbra.candidates.begin(), umbra.candidates.end(), [](const SubjectIdentity& c) {
        return c.kind == SubjectKind::Node && c.id == "umbra-cap";
    }));

    const SubjectResult hero = resolveSubject(index, "the Umbra hero mushroom");
    REQUIRE(hero.status == SubjectResult::Status::Resolved);
    CHECK(hero.identity.kind == SubjectKind::Hero);
    CHECK(hero.identity.id == "umbra-cap");
    CHECK(resolveSubject(index, "Umbra", SubjectKind::Hero).identity.id == "umbra-cap");
    CHECK(resolveSubject(index, "umbra-stem").identity.id == "umbra-stem"); // exact wins

    const SubjectResult tide = resolveSubject(index, "Tide");
    CHECK(tide.status == SubjectResult::Status::Resolved);
    CHECK(tide.identity.kind == SubjectKind::Entity);

    // A camera by the name a person sees, whatever this film calls its cameras.
    const auto cameras = std::find_if(index.identities().begin(), index.identities().end(),
                                      [](const SubjectIdentity& c) { return c.kind == SubjectKind::Camera && c.id != "main"; });
    if (cameras != index.identities().end()) {
        const SubjectResult cam = resolveSubject(index, "the " + cameras->name + " camera");
        CHECK(cam.status == SubjectResult::Status::Resolved);
        CHECK(cam.identity.id == cameras->id);
    }

    CHECK(resolveSubject(index, "scene/fogDensity").identity.kind == SubjectKind::Parameter);
    CHECK(resolveSubject(index, "scene/fogDensty").status == SubjectResult::Status::Unknown);
    CHECK(resolveSubject(index, "the world").identity.kind == SubjectKind::World);
    // Effects (ADR-702) answer only when an effect is asked for.
    const SubjectResult pulse = resolveSubject(index, "Umbra's pulse", SubjectKind::Effect);
    REQUIRE(pulse.status == SubjectResult::Status::Resolved);
    CHECK(pulse.identity.id == "umbra-cap-hero-pulse");

    // Asked for a kind it is not: said, with what it is.
    const SubjectResult wrongKind = resolveSubject(index, "Tide", SubjectKind::Camera);
    REQUIRE(wrongKind.status == SubjectResult::Status::Unknown);
    CHECK_FALSE(wrongKind.candidates.empty());
}

TEST_CASE("a plan's subjects and times resolve in place, and a chosen id is checked, not trusted",
          "[directing][subjects][benchmark]") {
    auto engine = benchmark();
    const SubjectIndex index = app::subjectIndexFor(*engine);
    const MusicalContext ctx = app::musicalContextFor(*engine);

    PlanParse parse = parsePlan(testsupport_readPlan());
    REQUIRE(parse.plan);
    Plan plan = *parse.plan;
    const std::vector<Issue> issues = resolvePlanSubjects(plan, index);
    for (const Issue& i : issues) {
        INFO(i.toJson().dump());
    }
    CHECK(issues.empty());
    CHECK(plan.subject("rook")->kind == SubjectKind::Entity);
    CHECK(plan.subject("umbra")->id == "umbra-cap");

    const PlanTimes times = resolvePlanTimes(plan, ctx);
    for (const Issue& i : times.issues) {
        INFO(i.toJson().dump());
    }
    CHECK(times.issues.empty());
    CHECK(times.at("/shots/0/start") == 90.0);
    CHECK(times.at("/shots/0/camera/2/at") == 92.2);
    CHECK(times.at("/retimes/0/until") == 93.0);

    // A model that picked a candidate that does not exist is caught.
    Plan chosen = *parse.plan;
    chosen.subject("umbra")->kind = SubjectKind::Hero;
    chosen.subject("umbra")->id = "umbra-stalk";
    const std::vector<Issue> bad = resolvePlanSubjects(chosen, index);
    REQUIRE(bad.size() == 1);
    CHECK(bad[0].code == IssueCode::UnknownSubject);
    CHECK(bad[0].subject == "umbra");
}
