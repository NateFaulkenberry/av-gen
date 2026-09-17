// Undo for sequencer edits (the sequencer's half of ADR-092).
//
// The panel could delete a shot and had no way to put it back -- every context menu offered Delete,
// nothing offered a way out of it, and the comment above `trimShotEnd` said so in as many words.
// What these check is not that a shot disappears, which was never in doubt, but that the *history*
// carries the sequencer: one command, on the same stack as a world edit, reversible in place.
//
// The delete these exercise is the panel's, reduced to the two lines that touch the data, because
// the panel itself needs a window and the thing under test does not.

#include "app/edit_system.hpp"
#include "app/engine.hpp"
#include "ui/edit_history.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace avgen;

namespace {

seq::Sequence threeShots() {
    seq::Sequence piece;
    piece.name = "piece";
    for (int i = 0; i < 3; ++i) {
        seq::Shot shot;
        shot.name = fmt::format("shot {}", i + 1);
        shot.startSeconds = static_cast<double>(i) * 4.0;
        shot.durationSeconds = 4.0;
        piece.shots.push_back(std::move(shot));
    }
    return piece;
}

// The panel's delete, bracketed the way the panel brackets it.
ui::EditCommand deleteShot(app::Engine& engine, std::size_t index, std::string label) {
    auto change = std::make_unique<ui::TimelineChange>();
    change->before = engine.sequence();
    engine.sequence().shots.erase(engine.sequence().shots.begin() +
                                  static_cast<std::ptrdiff_t>(index));
    change->after = engine.sequence();
    ui::EditCommand command(std::move(label));
    command.timeline = std::move(change);
    return command;
}

} // namespace

TEST_CASE("Deleting a shot can be undone", "[seq][undo]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setSequence(threeShots()));
    REQUIRE(engine.sequence().shots.size() == 3);

    ui::EditHistory history;
    history.push(deleteShot(engine, 1, "Delete shot"));
    REQUIRE(engine.sequence().shots.size() == 2);
    REQUIRE(history.canUndo());
    CHECK(history.undoLabel() == "Delete shot");

    const ui::EditApply undone = history.undo(engine);
    CHECK(undone.ok());
    CHECK(undone.timelinesInstalled == 1);

    // The count alone would pass on a sequence that put *any* shot back. The names are what say it
    // put back the one that left, in the place it left from.
    REQUIRE(engine.sequence().shots.size() == 3);
    CHECK(engine.sequence().shots[0].name == "shot 1");
    CHECK(engine.sequence().shots[1].name == "shot 2");
    CHECK(engine.sequence().shots[2].name == "shot 3");
    CHECK(engine.sequence().shots[1].startSeconds == 4.0);

    REQUIRE(history.canRedo());
    CHECK(history.redo(engine).ok());
    REQUIRE(engine.sequence().shots.size() == 2);
    CHECK(engine.sequence().shots[1].name == "shot 3");
}

TEST_CASE("A sequencer command is on the same stack as everything else", "[seq][undo]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setSequence(threeShots()));

    ui::EditHistory history;
    const std::uint64_t empty = history.stateId();
    history.push(deleteShot(engine, 2, "Delete shot"));
    CHECK(history.undoSize() == 1);
    CHECK(history.stateId() != empty);
    CHECK(history.labels() == std::vector<std::string>{"Delete shot"});

    // And it is not empty, which is the guard that stops `push` dropping it on the floor: an
    // EditCommand whose only content is a timeline used to satisfy `empty()` and be discarded.
    ui::EditCommand probe = deleteShot(engine, 0, "Delete shot");
    CHECK_FALSE(probe.empty());
    CHECK(probe.touched() == 1);
}

TEST_CASE("An edit that did not touch the audio does not reinstall it", "[seq][undo]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setSequence(threeShots()));

    // `clipsTouched` stays false, so both sides of the command carry empty clip vectors. If undo
    // installed them anyway it would wipe whatever audio the piece had -- which is the failure this
    // flag exists to prevent, and it is invisible in a test whose fixture has no audio. So the
    // check is on the command's own shape rather than on an outcome that cannot differ here.
    ui::EditCommand command = deleteShot(engine, 0, "Delete shot");
    REQUIRE(command.timeline != nullptr);
    CHECK_FALSE(command.timeline->clipsTouched);
    CHECK(command.timeline->clipsBefore.empty());

    ui::EditHistory history;
    history.push(std::move(command));
    CHECK(history.undo(engine).ok());
    CHECK(engine.sequence().shots.size() == 3);
}
