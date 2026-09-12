// The application's editing model (ADR-101): one history, one clipboard, one dispatch.
//
// These tests use stand-in contexts rather than the world editor, because what is being checked is
// the *dispatch* -- who gets asked, in what order, and what the menu is told -- and a real editor
// would answer those questions with its own behaviour mixed in.

#include "app/ai_edit_sink.hpp"
#include "app/edit_system.hpp"
#include "app/engine.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace avgen;

namespace {

// A context that records what it was asked and answers a fixed set of actions.
class FakeContext : public app::EditContext {
public:
    explicit FakeContext(std::string name, std::vector<app::EditAction> can = {})
        : name_(std::move(name)), can_(std::move(can)) {}

    [[nodiscard]] std::string_view editContextName() const override { return name_; }
    [[nodiscard]] bool canEdit(app::EditAction action) const override {
        return std::find(can_.begin(), can_.end(), action) != can_.end();
    }
    bool doEdit(app::EditAction action, app::Engine&, app::EditSystem&) override {
        if (decline) {
            declined.push_back(action);
            return false;
        }
        performed.push_back(action);
        return true;
    }
    void editSelectionRestored(const std::vector<std::string>& names) override {
        restored.push_back(names);
    }

    void allow(app::EditAction action) { can_.push_back(action); }

    bool decline = false;
    std::vector<app::EditAction> performed;
    std::vector<app::EditAction> declined;
    std::vector<std::vector<std::string>> restored;

private:
    std::string name_;
    std::vector<app::EditAction> can_;
};

} // namespace

TEST_CASE("An action goes to the editor that can do it", "[app][edits]") {
    app::Engine engine(app::EngineMode::Offline);
    app::EditSystem edits;

    FakeContext world("World", {app::EditAction::Copy, app::EditAction::Delete});
    FakeContext timeline("Timeline", {app::EditAction::Paste});
    edits.addContext(world);
    edits.addContext(timeline);

    SECTION("availability is asked of the contexts, not assumed") {
        CHECK(edits.canExecute(app::EditAction::Copy));
        CHECK(edits.canExecute(app::EditAction::Paste));
        CHECK(edits.canExecute(app::EditAction::Delete));
        // Nothing offers these, so the menu must grey them rather than advertise them.
        CHECK_FALSE(edits.canExecute(app::EditAction::Duplicate));
        CHECK_FALSE(edits.canExecute(app::EditAction::SelectAll));
    }

    SECTION("each action reaches the one editor that claimed it") {
        CHECK(edits.execute(app::EditAction::Copy, engine));
        CHECK(edits.execute(app::EditAction::Paste, engine));
        CHECK(world.performed == std::vector{app::EditAction::Copy});
        CHECK(timeline.performed == std::vector{app::EditAction::Paste});
    }

    SECTION("an action nobody claims does nothing and says so") {
        CHECK_FALSE(edits.execute(app::EditAction::Duplicate, engine));
        CHECK(world.performed.empty());
        CHECK(timeline.performed.empty());
    }

    SECTION("the focused editor is asked first when both could") {
        timeline.allow(app::EditAction::Copy);
        edits.setFocus(&timeline);
        CHECK(edits.execute(app::EditAction::Copy, engine));
        CHECK(timeline.performed == std::vector{app::EditAction::Copy});
        CHECK(world.performed.empty());
    }

    SECTION("a focused editor that cannot do it does not swallow the action") {
        // Focus is a preference, not a veto: the user clicked the timeline and pressed Delete, and
        // the thing they have selected is in the world.
        edits.setFocus(&timeline);
        CHECK(edits.execute(app::EditAction::Delete, engine));
        CHECK(world.performed == std::vector{app::EditAction::Delete});
    }

    SECTION("an editor that declines after all is not reported as having acted") {
        world.decline = true;
        CHECK_FALSE(edits.execute(app::EditAction::Delete, engine));
        CHECK(world.performed.empty());
        CHECK(world.declined == std::vector{app::EditAction::Delete});
    }
}

TEST_CASE("Undo and redo belong to the system, not to an editor", "[app][edits]") {
    app::Engine engine(app::EngineMode::Offline);
    app::EditSystem edits;
    // A context that claims undo cannot have it: undo acts on the history, and an editor answering
    // privately is exactly the isolated stack this system replaces.
    FakeContext greedy("Greedy", {app::EditAction::Undo, app::EditAction::Redo});
    edits.addContext(greedy);

    CHECK_FALSE(edits.canExecute(app::EditAction::Undo));
    CHECK_FALSE(edits.execute(app::EditAction::Undo, engine));
    CHECK(greedy.performed.empty());
}

TEST_CASE("The menu is told what will actually happen", "[app][edits]") {
    app::EditSystem edits;
    // Nothing to undo: the plain word, and the item greyed.
    CHECK(edits.menuLabel(app::EditAction::Undo) == "Undo");
    CHECK(edits.menuLabel(app::EditAction::Redo) == "Redo");
    CHECK_FALSE(edits.canExecute(app::EditAction::Undo));

    ui::EditCommand command("Move 3 objects");
    command.params.push_back(ui::ParamChange{"nodes/a/position", {0.0f}, {1.0f}});
    edits.history().push(std::move(command));

    // Now it names the command, so the menu says what Cmd+Z will take back.
    CHECK(edits.menuLabel(app::EditAction::Undo) == "Undo Move 3 objects");
    CHECK(edits.canExecute(app::EditAction::Undo));
    CHECK(edits.menuLabel(app::EditAction::Redo) == "Redo"); // nothing redoable yet

    // And every action has a name and a shortcut, so a new one cannot be added to the enum and
    // reach the menu as a blank.
    for (const app::EditAction action : app::editActions()) {
        INFO("action " << static_cast<int>(action));
        CHECK(std::string(app::editActionName(action)).size() > 0);
        CHECK(std::string(app::editActionShortcut(action)).size() > 0);
    }
}

TEST_CASE("History is global, so it survives moving between editors", "[app][edits]") {
    // The property that distinguishes this from a stack per editor. Three edits from two editors,
    // interleaved; undo takes them back newest first regardless of which editor made them, because
    // the user knows only that Cmd+Z takes back the last thing they did.
    app::Engine engine(app::EngineMode::Offline);
    app::EditSystem edits;

    const auto record = [&edits](const char* label) {
        ui::EditCommand command(label);
        command.params.push_back(ui::ParamChange{"nodes/a/position", {0.0f}, {1.0f}});
        edits.history().push(std::move(command));
    };
    record("Move Alien");    // world
    record("Add Marker");    // timeline
    record("Rotate Alien");  // world again

    CHECK(edits.menuLabel(app::EditAction::Undo) == "Undo Rotate Alien");
    CHECK(edits.execute(app::EditAction::Undo, engine));
    CHECK(edits.menuLabel(app::EditAction::Undo) == "Undo Add Marker");
    CHECK(edits.execute(app::EditAction::Undo, engine));
    CHECK(edits.menuLabel(app::EditAction::Undo) == "Undo Move Alien");
    CHECK(edits.execute(app::EditAction::Undo, engine));
    CHECK_FALSE(edits.canExecute(app::EditAction::Undo));

    // And forward again, in order.
    CHECK(edits.menuLabel(app::EditAction::Redo) == "Redo Move Alien");
    CHECK(edits.execute(app::EditAction::Redo, engine));
    CHECK(edits.menuLabel(app::EditAction::Redo) == "Redo Add Marker");
}

TEST_CASE("An undo hands the selection back to the editors", "[app][edits]") {
    // Undoing a delete that gave back five objects and left nothing selected is the case this
    // exists for: the objects are there and the user cannot tell which ones returned.
    app::Engine engine(app::EngineMode::Offline);
    app::EditSystem edits;
    FakeContext world("World");
    edits.addContext(world);

    ui::EditCommand command("Delete 2 objects");
    command.params.push_back(ui::ParamChange{"nodes/a/position", {0.0f}, {1.0f}});
    command.selectionBefore = {"rock", "fern"};
    edits.history().push(std::move(command));

    REQUIRE(edits.execute(app::EditAction::Undo, engine));
    REQUIRE(world.restored.size() == 1);
    CHECK(world.restored[0] == std::vector<std::string>{"rock", "fern"});
}

TEST_CASE("The project is dirty when it differs from what was saved", "[app][edits]") {
    app::Engine engine(app::EngineMode::Offline);
    app::EditSystem edits;
    const auto edit = [&edits](const char* label, float to) {
        ui::EditCommand command(label);
        command.params.push_back(ui::ParamChange{"nodes/a/position", {0.0f}, {to}});
        edits.history().push(std::move(command));
    };

    edits.markSaved();
    CHECK_FALSE(edits.dirty());

    edit("Create Alien", 1.0f);
    CHECK(edits.dirty());

    edits.markSaved();
    CHECK_FALSE(edits.dirty());

    SECTION("undoing back to the saved state is clean again") {
        edit("Move Alien", 2.0f);
        CHECK(edits.dirty());
        REQUIRE(edits.execute(app::EditAction::Undo, engine));
        CHECK_FALSE(edits.dirty());
    }

    SECTION("a different edit at the saved depth is dirty") {
        // The case depth cannot answer. Edit, undo, edit again: one command deep either way, and
        // the second one is not the document that was saved.
        edit("Move Alien", 2.0f);
        REQUIRE(edits.execute(app::EditAction::Undo, engine));
        REQUIRE_FALSE(edits.dirty());
        edit("Move Alien Again", 7.0f);
        CHECK(edits.dirty());
    }

    SECTION("a project boundary clears the history and starts clean") {
        edit("Move Alien", 2.0f);
        CHECK(edits.dirty());
        edits.clearHistory();
        CHECK_FALSE(edits.dirty());
        CHECK_FALSE(edits.canExecute(app::EditAction::Undo));
        CHECK_FALSE(edits.canExecute(app::EditAction::Redo));
    }
}

TEST_CASE("The clipboard carries what it holds, not just bytes", "[app][edits]") {
    app::EditSystem edits;
    CHECK(edits.clipboard().empty());
    CHECK_FALSE(edits.clipboard().holds("world/nodes"));

    app::ClipboardPayload payload;
    payload.type = "world/nodes";
    payload.source = "World";
    payload.count = 3;
    payload.data = std::make_shared<const std::vector<int>>(std::vector<int>{1, 2, 3});
    edits.clipboard().set(std::move(payload));

    CHECK_FALSE(edits.clipboard().empty());
    CHECK(edits.clipboard().holds("world/nodes"));
    // A context asks before offering Paste, so a timeline never offers to paste a tree.
    CHECK_FALSE(edits.clipboard().holds("timeline/items"));
    CHECK(edits.clipboard().payload().version == 1);
    CHECK(edits.clipboard().payload().count == 3);
    // Read back as what the type says, and null for anything else -- a caller cannot reinterpret a
    // timeline's payload as a tree by asking confidently.
    CHECK(edits.clipboard().payload().as<std::vector<int>>("world/nodes") != nullptr);
    CHECK(edits.clipboard().payload().as<std::vector<int>>("timeline/items") == nullptr);

    edits.clipboard().clear();
    CHECK(edits.clipboard().empty());
    CHECK_FALSE(edits.clipboard().holds("world/nodes"));
}

// ---- the AI transaction bridge (ADR-101) --------------------------------------------------------

namespace {

// Stands in for the snapshot sink that sits underneath. Records what it was told so the test can
// check that the promise it makes -- rollback -- is still being made.
class FakeFallback : public ai::TransactionSink {
public:
    void begin(const std::string& label) override { began.push_back(label); }
    void commit(const std::string& label) override { committed.push_back(label); }
    void abort() override { ++aborts; }
    [[nodiscard]] bool available() const override { return ok; }
    [[nodiscard]] std::string_view kind() const override { return "fake"; }

    bool ok = true;
    std::vector<std::string> began;
    std::vector<std::string> committed;
    int aborts = 0;
};

} // namespace

TEST_CASE("An assistant's task becomes one entry in the history", "[app][edits][ai]") {
    // Before this, a task that succeeded left nothing in the history: it was backed by a snapshot
    // that only a failure ever used. "The assistant moved my camera" could not be taken back the
    // way every other edit can.
    app::Engine engine(app::EngineMode::Offline);
    app::EditSystem edits;
    FakeFallback fallback;
    app::EditHistoryTransactionSink sink(engine, edits, fallback);
    // `applyEdit` refuses a session with no composition, so an undo here would report success and
    // do nothing -- which is how the missing warning above was found.
    engine.newComposition();

    // Something for the task to change. Registered before `begin`, so there is a "before" for it.
    params::IParameter& gain =
        engine.params().add(params::ParamDesc<float>{.path = "test/gain", .defaultValue = 1.0f,
                                                     .hardMin = 0.0f, .hardMax = 10.0f});

    SECTION("a task that changes parameters is one undoable command") {
        sink.begin("Make it night");
        // Several writes, the way a task makes them.
        gain.setBaseComponent(0, 2.0f);
        gain.setBaseComponent(0, 3.0f);
        gain.setBaseComponent(0, 4.0f);
        sink.commit("Make it night");

        // One entry, named for the task rather than for any of its writes.
        REQUIRE(edits.history().undoSize() == 1);
        CHECK(edits.history().undoLabel() == "Make it night");
        CHECK(edits.canExecute(app::EditAction::Undo));

        // And undoing it returns the value it had before the task, not the one before the last write.
        REQUIRE(edits.execute(app::EditAction::Undo, engine));
        CHECK(gain.baseComponent(0) == 1.0f);
        REQUIRE(edits.execute(app::EditAction::Redo, engine));
        CHECK(gain.baseComponent(0) == 4.0f);
    }

    SECTION("a task that changed nothing leaves no entry") {
        // An entry saying "Describe the scene" that undoes nothing is a history nobody can read.
        sink.begin("Describe the scene");
        sink.commit("Describe the scene");
        CHECK(edits.history().undoSize() == 0);
        CHECK_FALSE(edits.canExecute(app::EditAction::Undo));
    }

    SECTION("an aborted task leaves no entry") {
        // It did not happen. An entry for it would offer to undo an edit the user never saw.
        sink.begin("A task that fails");
        gain.setBaseComponent(0, 9.0f);
        sink.abort();
        CHECK(edits.history().undoSize() == 0);
        CHECK(fallback.aborts == 1); // and the snapshot underneath is what put the document back
    }

    SECTION("the snapshot underneath still gets its calls") {
        // This sink makes a task undoable; the snapshot makes it abortable. Different promises, and
        // dropping the second while adding the first would be a quiet loss of rollback.
        sink.begin("Something");
        gain.setBaseComponent(0, 5.0f);
        sink.commit("Something");
        CHECK(fallback.began == std::vector<std::string>{"Something"});
        CHECK(fallback.committed == std::vector<std::string>{"Something"});
    }

    SECTION("availability is the fallback's answer, since it is what can roll back") {
        CHECK(sink.available());
        fallback.ok = false;
        CHECK_FALSE(sink.available());
    }
}
