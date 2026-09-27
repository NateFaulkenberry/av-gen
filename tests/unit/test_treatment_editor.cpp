// ADR-937: a section's treatment is edited where it is chosen, and the edit survives a project save.
//
// Glowmere Valley 3's own treatments ("GV3: the riser's roll", ...) appeared by name in the Sequence
// panel's "shot" picker, and their dials could be changed only in the project file (gv3-cut). The panel
// now draws the chosen treatment's dials under the picker from `ui::treatmentDials()` and edits through
// `ui::editTreatment` / `ui::copyTreatmentForSection`. Without a window, this checks what those decide:
//
//   * reach -- every dial a director reads is on the list, under words for what it does to the cut
//     (the list is held against the treatment's own file format, so a dial added to one and not the
//     other fails here);
//   * the edit -- reaches every section using the treatment and the director's reading of it, and
//     nothing else; a built-in, an unknown id and an out-of-range dial are refused with nothing changed;
//   * "Edit a copy" -- a built-in becomes the project's own for one section;
//   * the save -- an edit made the panel's way, on the engine's sequence, comes back from the project
//     file exactly; the control is the same save without the edit.

#include "analysis/structure.hpp"
#include "app/engine.hpp"
#include "seq/sequence.hpp"
#include "song/from_analysis.hpp"
#include "song/section_cue.hpp"
#include "song/section_timeline.hpp"
#include "song/shot_intent.hpp"
#include "song/shot_language.hpp"
#include "support/temp_dir.hpp"
#include "ui/treatment_editor_logic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

// One of GV3's own, as songcut.TREATMENTS defines it: the riser's roll, cutting faster to its end.
song::ShotIntent roll() {
    song::ShotIntent t;
    t.id = "gv3_riser_roll";
    t.name = "GV3: the riser's roll";
    t.description = "The fastest a rising arc cuts.";
    t.focus = song::SubjectFocus::Environment;
    t.focusStrength = 0.25f;
    t.framing = song::FramingRange{song::Framing::Medium, song::Framing::VeryWide};
    t.movement = 0.5f;
    t.energy = 0.75f;
    t.variation = 0.5f;
    t.cutFrequency = 0.5f;
    t.visualDensity = 0.5f;
    t.cameras = song::CameraCount{1, 3};
    t.arc = song::Arc::Rising;
    return t;
}

analysis::SongStructure structure() {
    analysis::SongStructure s;
    s.durationSeconds = 80.0;
    for (const auto& [from, to, f] : std::vector<std::tuple<double, double, analysis::SectionFunction>>{
             {0.0, 20.0, analysis::SectionFunction::Intro},
             {20.0, 40.0, analysis::SectionFunction::Verse},
             {40.0, 60.0, analysis::SectionFunction::Verse},
             {60.0, 80.0, analysis::SectionFunction::Chorus}}) {
        analysis::SongSection section;
        section.startSeconds = from;
        section.endSeconds = to;
        section.function = f;
        section.energy = 0.5f;
        s.sections.push_back(section);
    }
    return s;
}

// A piece whose first two sections take GV3's roll and whose last two keep their types' built-ins.
seq::Sequence piece() {
    seq::Sequence s;
    s.name = "treatments";
    s.structure = structure();
    REQUIRE(s.shotLanguage.defineIntent(roll()).has_value());
    s.sectionTimeline = song::timelineFromStructure(s.structure, s.shotLanguage);
    REQUIRE(s.sectionTimeline.sections.size() == 4);
    REQUIRE(song::setSectionShotIntent(s.sectionTimeline, 0, "gv3_riser_roll", s.shotLanguage));
    REQUIRE(song::setSectionShotIntent(s.sectionTimeline, 1, "gv3_riser_roll", s.shotLanguage));
    return s;
}

} // namespace

TEST_CASE("UI reach: every dial of a treatment is on the section inspector, named for what the cut does",
          "[song][ui][treatment][adr937]") {
    // The treatment's own file format is the list of what a director reads (id, name and description
    // apart, which the inspector edits as text): each of its keys must be a dial, and nothing else.
    const nlohmann::json written = song::shotIntentToJson(roll());
    std::set<std::string> fileKeys;
    for (const auto& [key, value] : written.items()) {
        if (key != "id" && key != "name" && key != "description") {
            fileKeys.insert(key);
        }
    }
    std::set<std::string> dialKeys;
    std::set<std::string> labels;
    for (const ui::TreatmentDial& d : ui::treatmentDials()) {
        INFO(d.key);
        dialKeys.insert(d.key);
        CHECK(std::string(d.label).size() >= 5);
        CHECK(std::string(d.tip).size() > 20);
        CHECK(labels.insert(d.label).second); // no two dials read the same
        CHECK((d.kind == ui::TreatmentDialKind::Amount) == (d.amount != nullptr));
    }
    CHECK(dialKeys == fileKeys);
    CHECK(ui::treatmentDials().size() == 10);

    // Each 0..1 dial writes the field its key names -- a table that pointed "cut rate" at `movement`
    // would draw the right words over the wrong number.
    for (const ui::TreatmentDial& d : ui::treatmentDials()) {
        if (d.amount == nullptr) {
            continue;
        }
        INFO(d.key);
        song::ShotIntent t = roll();
        t.*d.amount = 0.8125f;
        CHECK(song::shotIntentToJson(t).at(d.key).get<float>() == 0.8125f);
    }
    // Every choice of the enumerated dials has words of its own.
    std::set<std::string> seen;
    for (const song::SubjectFocus f : song::allSubjectFocuses()) {
        CHECK(seen.insert(ui::focusLabel(f)).second);
    }
    for (const song::Framing f : song::allFramings()) {
        CHECK(seen.insert(ui::framingLabel(f)).second);
    }
    for (const song::Arc a : song::allArcs()) {
        CHECK(seen.insert(ui::arcLabel(a)).second);
    }
}

TEST_CASE("an edit to one of the project's treatments reaches every section using it, and nothing else",
          "[song][ui][treatment][adr937]") {
    seq::Sequence s = piece();
    song::ShotLanguage& language = s.shotLanguage;
    const song::SectionTimeline& timeline = s.sectionTimeline;
    CHECK(ui::treatmentEditable(language, "gv3_riser_roll"));
    CHECK(ui::treatmentUsers(timeline, language, "gv3_riser_roll") == 2);
    const song::ShotIntentId chorusId = language.intentIdFor(timeline.sections[3]);
    CHECK_FALSE(ui::treatmentEditable(language, chorusId)); // a built-in
    const std::vector<song::SectionCue> before = song::cueSheet(timeline, language);

    song::ShotIntent edited = *language.intent("gv3_riser_roll");
    edited.cutFrequency = 0.875f;
    edited.arc = song::Arc::Burst;
    edited.framing.widest = song::Framing::Wide;
    edited.name = "GV3: the roll, tighter";
    REQUIRE(ui::editTreatment(language, edited).has_value());

    // Both sections that use it follow; the director's reading of them -- the cue sheet Song Mode cuts
    // from -- follows with them.
    for (std::size_t i : {0u, 1u}) {
        INFO("section " << i);
        const song::ShotIntent& now = language.intentFor(timeline.sections[i]);
        CHECK(now.cutFrequency == 0.875f);
        CHECK(now.arc == song::Arc::Burst);
        CHECK(now.framing.widest == song::Framing::Wide);
        CHECK(now.name == "GV3: the roll, tighter");
    }
    const std::vector<song::SectionCue> after = song::cueSheet(timeline, language);
    REQUIRE(after.size() == before.size());
    CHECK(after[0].intentAt(after[0].startSeconds).cutFrequency !=
          before[0].intentAt(before[0].startSeconds).cutFrequency);
    // The sections that did not use it are untouched.
    CHECK(language.intentFor(timeline.sections[3]) == *song::builtInShotIntent(chorusId));
    CHECK(after[3].intent == before[3].intent);
}

TEST_CASE("a built-in, an unknown treatment and an out-of-range dial are refused, and nothing changes",
          "[song][ui][treatment][adr937]") {
    seq::Sequence s = piece();
    song::ShotLanguage& language = s.shotLanguage;
    const song::ShotLanguage untouched = language;

    const song::ShotIntentId chorusId = language.intentIdFor(s.sectionTimeline.sections[3]);
    song::ShotIntent builtIn = *song::builtInShotIntent(chorusId);
    builtIn.cutFrequency = 0.1f;
    const auto refusedBuiltIn = ui::editTreatment(language, builtIn);
    REQUIRE_FALSE(refusedBuiltIn.has_value());
    CHECK(refusedBuiltIn.error().message.find("copy") != std::string::npos);

    song::ShotIntent stranger = roll();
    stranger.id = "nobody_defined_this";
    CHECK_FALSE(ui::editTreatment(language, stranger).has_value());

    song::ShotIntent wild = *language.intent("gv3_riser_roll");
    wild.cutFrequency = 1.5f; // refused, not clamped (ADR-225)
    CHECK_FALSE(ui::editTreatment(language, wild).has_value());
    CHECK(language == untouched);
    CHECK(language.intent("gv3_riser_roll")->cutFrequency == 0.5f);
}

TEST_CASE("Edit a copy: a built-in becomes the project's own, for the one section",
          "[song][ui][treatment][adr937]") {
    seq::Sequence s = piece();
    song::ShotLanguage& language = s.shotLanguage;
    song::SectionTimeline& timeline = s.sectionTimeline;
    const song::ShotIntentId builtInId = language.intentIdFor(timeline.sections[2]);
    REQUIRE(song::builtInShotIntent(builtInId) != nullptr);
    const std::size_t sharing = ui::treatmentUsers(timeline, language, builtInId);

    const auto copied = ui::copyTreatmentForSection(language, timeline, 2);
    REQUIRE(copied.has_value());
    CHECK(*copied == builtInId + "_custom");
    CHECK(ui::treatmentEditable(language, *copied));
    CHECK(language.intentIdFor(timeline.sections[2]) == *copied);
    // By value: a reference into the language dies with the next definition (ShotLanguage::intentFor).
    const song::ShotIntent copy = *language.intent(*copied);
    const song::ShotIntent& original = *song::builtInShotIntent(builtInId);
    CHECK(copy.name == original.name + " (custom)");
    CHECK(copy.cutFrequency == original.cutFrequency);
    CHECK(copy.arc == original.arc);
    CHECK(copy.framing == original.framing);
    // Only that section took it; the built-in is where it was for the rest.
    CHECK(ui::treatmentUsers(timeline, language, builtInId) == sharing - 1);
    // A second copy of the same built-in gets its own id: section 1, back on its type's default (the
    // same verse built-in), copied after section 2's copy took "<id>_custom".
    REQUIRE(song::clearSectionShotIntent(timeline, 1));
    REQUIRE(language.intentIdFor(timeline.sections[1]) == builtInId);
    const auto again = ui::copyTreatmentForSection(language, timeline, 1);
    REQUIRE(again.has_value());
    CHECK(*again == builtInId + "_custom_2");
    CHECK(language.intentIdFor(timeline.sections[1]) == *again);
    CHECK(language.intentIdFor(timeline.sections[2]) == *copied);
    // ...and the copy edits like any of the project's own.
    song::ShotIntent edited = copy;
    edited.movement = 0.125f;
    REQUIRE(ui::editTreatment(language, edited).has_value());
    CHECK(language.intentFor(timeline.sections[2]).movement == 0.125f);
    CHECK_FALSE(ui::copyTreatmentForSection(language, timeline, 99).has_value());
}

TEST_CASE("a treatment edited the panel's way survives the project save", "[song][ui][treatment][project][adr937]") {
    const fs::path dir = testsupport::processTempDir() / "treatment_save";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path stage = dir / "stage.json";
    std::ofstream(stage) << R"({ "format": "avgen-scene", "version": 1, "name": "stage",
        "camera": { "mode": 1, "position": [0, 4, 12], "target": [0, 1, 0] },
        "nodes": [ { "name": "orb", "kind": "orb" } ] })";
    const fs::path edited = dir / "edited.json";
    const fs::path plain = dir / "plain.json";

    const auto save = [&](const fs::path& file, bool edit) {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadFile(stage).has_value());
        REQUIRE(engine.setSequence(piece()).has_value());
        engine.markProjectSaved();
        CHECK_FALSE(engine.projectDirty(true));
        if (edit) {
            // What the inspector does: the engine's own sequence, edited in place.
            seq::Sequence& live = engine.sequence();
            song::ShotIntent t = *live.shotLanguage.intent("gv3_riser_roll");
            t.cutFrequency = 0.9375f;
            t.movement = 0.0625f;
            t.cameras = song::CameraCount{2, 0};
            t.focus = song::SubjectFocus::Hero;
            t.description = "Edited in the Sequence panel.";
            REQUIRE(ui::editTreatment(live.shotLanguage, t).has_value());
            // The unsaved-changes check (ADR-440) serialises the project, so closing would ask.
            CHECK(engine.projectDirty(true));
        }
        REQUIRE(engine.saveProject(file).has_value());
    };
    save(edited, true);
    save(plain, false);

    app::Engine loaded(app::EngineMode::Offline);
    REQUIRE(loaded.loadProject(edited).has_value());
    const seq::Sequence& back = loaded.sequence();
    const song::ShotIntent* t = back.shotLanguage.intent("gv3_riser_roll");
    REQUIRE(t != nullptr);
    CHECK(t->cutFrequency == 0.9375f); // exactly: the file carries the edit, not a rounded neighbour
    CHECK(t->movement == 0.0625f);
    CHECK(t->cameras == song::CameraCount{2, 0});
    CHECK(t->focus == song::SubjectFocus::Hero);
    CHECK(t->description == "Edited in the Sequence panel.");
    CHECK(back.shotLanguage.intentFor(back.sectionTimeline.sections[0]).id == "gv3_riser_roll");
    CHECK(back.shotLanguage.intentFor(back.sectionTimeline.sections[1]).cutFrequency == 0.9375f);

    // The control: the same project saved without the edit comes back with the dials it had.
    app::Engine reloaded(app::EngineMode::Offline);
    REQUIRE(reloaded.loadProject(plain).has_value());
    const song::ShotIntent* p = reloaded.sequence().shotLanguage.intent("gv3_riser_roll");
    REQUIRE(p != nullptr);
    CHECK(p->cutFrequency == 0.5f);
    CHECK(p->focus == song::SubjectFocus::Environment);
    fs::remove_all(dir);
}
