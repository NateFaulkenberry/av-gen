// ADR-216's seam, both halves (seq/section_actions.hpp).
//
// The half that matters most to test is the one that did not exist: until this change, a generated
// section event reached `Engine::firedEvents()` and stopped. Nothing in the application read it.
// So the interesting assertions here are not "the mapping is correct" but "the mapping exists and
// something applies it" -- a path that is wired to nothing passes every unit test you can write
// about its pieces.

#include "entity/action.hpp"
#include "seq/section_actions.hpp"
#include "seq/section_performance.hpp"
#include "seq/sequence.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace avgen;

TEST_CASE("a verb becomes the action the action system already understands", "[seq][section]") {
    SECTION("move and face track an entity rather than a node") {
        // The distinction is the point: directing a character at a *character* has to follow where
        // that character is now, not where its node was authored.
        auto moved = seq::actionFromEvent("rook", "move", "visitor");
        REQUIRE(moved.has_value());
        CHECK(moved->entity == "rook");
        CHECK(moved->action.kind == entity::ActionKind::Move);
        CHECK(moved->action.target.kind == entity::TargetKind::EntityRef);
        CHECK(moved->action.target.name == "visitor");
    }

    SECTION("pose names an activity, and an activity is not a target") {
        auto posed = seq::actionFromEvent("ember", "pose", "wave");
        REQUIRE(posed.has_value());
        CHECK(posed->action.kind == entity::ActionKind::Pose);
        CHECK(posed->action.activity == "wave");
        CHECK(posed->action.target.kind == entity::TargetKind::None);
    }

    SECTION("interact splits the prop from the verb it published") {
        auto acted = seq::actionFromEvent("sage", "interact", "lantern.light");
        REQUIRE(acted.has_value());
        CHECK(acted->action.target.kind == entity::TargetKind::Interaction);
        CHECK(acted->action.target.name == "lantern");
        CHECK(acted->action.target.member == "light");
    }

    SECTION("an unknown verb is an error carrying the list, not a silent no-op") {
        auto bad = seq::actionFromEvent("rook", "dance", "");
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().message.find("dance") != std::string::npos);
        CHECK(bad.error().message.find("pose") != std::string::npos);   // the list is in the message
    }

    SECTION("a subject is required, because an action needs somebody to do it") {
        CHECK_FALSE(seq::actionFromEvent("", "wait", "").has_value());
    }
}

TEST_CASE("an authored section table round-trips and refuses what it cannot mean", "[seq][section]") {
    seq::SectionPerformanceSet set;
    set.entries.push_back({signals::MusicalSection::Drop,
                           {.subject = "visitor", .verb = "pose", .argument = "hover", .priority = 3}});
    set.entries.push_back(
        {signals::MusicalSection::Outro, {.subject = "rook", .verb = "move", .argument = "cairn"}});
    REQUIRE(validate(set).has_value());

    // ADR-225: a setting the application does not keep is not a setting.
    const auto restored = seq::sectionPerformanceSetFromJson(seq::toJson(set));
    REQUIRE(restored.has_value());
    CHECK(*restored == set);

    SECTION("a verb that is not an action kind is refused at load") {
        nlohmann::json doc = seq::toJson(set);
        doc[0]["verb"] = "dance";
        const auto bad = seq::sectionPerformanceSetFromJson(doc);
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().message.find("dance") != std::string::npos);
    }

    SECTION("a section kind that does not exist is refused at load") {
        nlohmann::json doc = seq::toJson(set);
        doc[0]["section"] = "ocean";
        REQUIRE_FALSE(seq::sectionPerformanceSetFromJson(doc).has_value());
    }

    SECTION("a row with no subject is refused: it would generate an event nothing could apply") {
        seq::SectionPerformanceSet orphan;
        orphan.entries.push_back({signals::MusicalSection::Chorus, {.subject = "", .verb = "wait"}});
        REQUIRE_FALSE(validate(orphan).has_value());
    }
}

TEST_CASE("the authored table is what fills the hole, and an empty one still declines",
          "[seq][section]") {
    // `section_performance.hpp` is explicit that declining is the honest answer until somebody
    // authors something, and that a table full of guesses would be a second director. Both halves
    // of that are asserted here, because it is a design rule and rules rot silently.
    const seq::SectionPerformanceTable empty = seq::tableFrom(seq::SectionPerformanceSet{});
    const seq::SectionPerformanceTable builtIn = seq::defaultSectionPerformanceTable();
    for (const auto kind : {signals::MusicalSection::Intro, signals::MusicalSection::Drop,
                            signals::MusicalSection::Chorus, signals::MusicalSection::Outro}) {
        CHECK_FALSE(empty(kind).has_value());
        CHECK_FALSE(builtIn(kind).has_value());
    }

    seq::SectionPerformanceSet set;
    set.entries.push_back({signals::MusicalSection::Drop, {.subject = "visitor", .verb = "pose"}});
    const seq::SectionPerformanceTable authored = seq::tableFrom(set);
    REQUIRE(authored(signals::MusicalSection::Drop).has_value());
    CHECK(authored(signals::MusicalSection::Drop)->subject == "visitor");
    // Only what it names. A table that answered for everything would be inventing behaviour.
    CHECK_FALSE(authored(signals::MusicalSection::Chorus).has_value());
}

// ---- the half that did not exist -------------------------------------------------------------
//
// Everything above tests pieces. This tests the WIRE: that a fired section event reaches the action
// system at all.
//
// Before this change the answer was no, and no unit test could have told you. `firedEvents()` was
// populated correctly, the mapping would have been correct had anything called it, and every piece
// passed its own tests -- while a section that was supposed to make a character move made nothing
// move, because the only production reader of `firedEvents()` was nobody. That is the failure this
// case exists to catch, and it is why it asserts on the ENTITY's queue rather than on the event.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "seq/sequence.hpp"

#include <algorithm>
#include <filesystem>

namespace {
std::filesystem::path glowmereProject() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.json";
}
} // namespace

TEST_CASE("a fired section event reaches the action system", "[seq][section][integration]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(glowmereProject())) {
        SKIP("Glowmere Valley 2 is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(glowmereProject());
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    entity::Entity* subject = comp->entityWorld().find("rook");
    REQUIRE(subject != nullptr);

    // A scheduled EntityAction at a known second: the exact shape `generatePerformanceEvents` emits,
    // built here directly so this case tests the wire rather than the generator (which has its own).
    seq::Sequence sequence = engine.sequence();
    seq::SequenceEvent event;
    event.id = "test.section.pose";
    event.when.kind = seq::TriggerKind::Time;
    event.when.timeSeconds = 1.0;
    event.what.kind = seq::EventActionKind::EntityAction;
    event.what.target = "rook";
    event.what.value = "pose";
    event.what.argument = "wave";
    sequence.events.push_back(std::move(event));
    REQUIRE(engine.setSequence(std::move(sequence)).has_value());

    const std::size_t before = subject->actions().pending(entity::Authority::Director);

    FixedStepClock clock(30.0, 0.0);
    bool fired = false;
    std::size_t peak = before;
    for (int i = 0; i < 90; ++i) {   // three seconds, so 1.0 s is crossed
        FrameTime time = clock.tick();
        time.frameIndex = static_cast<std::uint64_t>(i);
        engine.update(time);
        fired = fired || !engine.firedEvents().empty();
        peak = std::max(peak, subject->actions().pending(entity::Authority::Director));
    }

    // The event fired -- without this the assertion below would be vacuous in the most ordinary way,
    // by measuring a wire nothing was ever sent down.
    REQUIRE(fired);
    // ...and something arrived at the other end. This is the assertion that was false before the
    // applier existed: the event fired then too, and the queue never moved.
    CHECK(peak > before);
#endif
}

TEST_CASE("an authored performer table survives a sequence round-trip", "[seq][section]") {
    // ADR-225. The table is what makes the "Generate performer actions" checkbox live, so a
    // table the application does not keep is a checkbox that greys itself out after a save.
    seq::Sequence piece;
    piece.name = "sequence";
    piece.sectionPerformance.entries.push_back(
        {signals::MusicalSection::Drop, {.subject = "visitor", .verb = "pose", .argument = "hover"}});
    piece.sectionPerformance.entries.push_back(
        {signals::MusicalSection::Outro, {.subject = "rook", .verb = "move", .argument = "cairn"}});

    const nlohmann::json doc = piece.toJson();
    const auto restored = seq::Sequence::fromJson(doc);
    REQUIRE(restored.has_value());
    CHECK(restored->sectionPerformance == piece.sectionPerformance);

    SECTION("a project with no table writes no key, rather than an empty array") {
        seq::Sequence bare;
        bare.name = "sequence";
        CHECK_FALSE(bare.toJson().contains("sectionPerformance"));
    }
}

TEST_CASE("the multicam demo's director table is actually read", "[seq][section][integration]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path project =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" /
        "glowmere-valley-2-multicam.json";
    if (!std::filesystem::exists(project)) {
        SKIP("the Glowmere multi-camera demo is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(project);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());

    // "It loaded without an error" is not evidence the table was read: an unknown key is ignored by
    // design, so a table under the wrong name, at the wrong nesting, or spelled differently loads
    // perfectly and does nothing. This asserts the rows arrived.
    const seq::SectionPerformanceSet& table = engine.sequence().sectionPerformance;
    INFO("rows: " << table.entries.size());
    REQUIRE(table.entries.size() == 7);

    // Every row has to name a real entity and a verb the action system understands, or the table is
    // a list of events nothing can apply -- which looks exactly like the feature being broken.
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    for (const seq::SectionPerformanceEntry& entry : table.entries) {
        INFO("row for " << signals::musicalSectionName(entry.kind) << ": " << entry.direction.subject
                        << " " << entry.direction.verb << " " << entry.direction.argument);
        CHECK(comp->entityWorld().find(entry.direction.subject) != nullptr);
        const auto action = seq::actionFromEvent(entry.direction.subject, entry.direction.verb,
                                                 entry.direction.argument);
        CHECK(action.has_value());
        // A `move` names another character, so its argument has to be one too -- a typo here is a
        // walk toward nothing, which the action system reports and nobody reads.
        if (entry.direction.verb == "move") {
            CHECK(comp->entityWorld().find(entry.direction.argument) != nullptr);
        }
    }
#endif
}
