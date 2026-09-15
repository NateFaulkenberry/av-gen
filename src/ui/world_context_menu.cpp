#include "ui/world_context_menu.hpp"

#include "app/edit_system.hpp"
#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "ui/shortcuts.hpp"
#include "ui/style.hpp"
#include "ui/world_edit.hpp"
#include "ui/world_editor.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <glm/glm.hpp>

namespace avgen::ui {
namespace {

// What the menu should call the thing it is about.
std::string subjectOf(const Selection& selection, const std::string& clicked) {
    if (!clicked.empty() && !selection.contains(clicked)) {
        return clicked;
    }
    if (selection.empty()) {
        return {};
    }
    if (selection.size() == 1) {
        return selection.primary();
    }
    return fmt::format("{} objects", selection.size());
}

} // namespace

bool worldObjectMenuBody(app::Engine& engine, WorldEditor& editor, const std::string& clicked,
                         const WorldMenuHost& host) {
    scene::Composition* composition = engine.composition();
    app::EditSystem* edits = editor.edits();
    if (composition == nullptr || edits == nullptr) {
        menuSubject("No editable scene");
        return false;
    }

    // Right-clicking something that is not in the selection makes it the selection first. Every
    // desktop editor does this, and the alternative -- a menu about the old selection appearing
    // over the object you just pointed at -- is the commonest way a context menu does the wrong
    // thing. Done before anything is drawn, so the rows below describe what will actually happen.
    if (!clicked.empty() && !editor.selection.contains(clicked)) {
        editor.selection.set(clicked);
    }

    const Selection& selection = editor.selection;
    const bool has = !selection.empty();
    const bool many = selection.size() > 1;
    bool acted = false;

    menuSubject(has ? subjectOf(selection, clicked) : std::string("Nothing selected"));

    // ---- the actions the application owns, through the one route it owns them by ---------------
    //
    // `menuLabel` is asked for the text rather than a literal being written here, so the row says
    // "Delete 3 objects" when that is what it will do -- the same string the Edit menu shows,
    // because it is the same call.
    const auto action = [&](app::EditAction which, const char* shortcut) {
        const bool can = edits->canExecute(which);
        if (menuAction(edits->menuLabel(which).c_str(), shortcut, can)) {
            static_cast<void>(edits->execute(which, engine));
            acted = true;
        }
    };

    if (has) {
        // Framing first: it is the one thing a person right-clicks an object in a list *to* do.
        if (host.frameSelection != nullptr &&
            menuAction("Frame in viewport", shortcut::kFrameSelection)) {
            *host.frameSelection = true;
            acted = true;
        }
        ImGui::Separator();
    }
    action(app::EditAction::Cut, shortcut::kCut);
    action(app::EditAction::Copy, shortcut::kCopy);
    action(app::EditAction::Paste, shortcut::kPaste);
    action(app::EditAction::Duplicate, shortcut::kDuplicate);
    ImGui::Separator();
    action(app::EditAction::Delete, shortcut::kDelete);

    if (has) {
        ImGui::Separator();
        // Grouping is the world editor's own and has no `EditAction`, on purpose: no other editor
        // in this application has the concept, and inventing a global one for a single client is
        // the pile of special cases `EditSystem` exists to avoid.
        if (menuAction("Group", shortcut::kGroup, many)) {
            editor.groupSelection(engine);
            acted = true;
        }
        const bool anyGroup = std::any_of(
            selection.nodes().begin(), selection.nodes().end(), [&](const std::string& name) {
                const scene::CompositionNode* n = composition->findNode(name);
                return n != nullptr && n->kind == scene::NodeKind::Group;
            });
        if (menuAction("Ungroup", shortcut::kUngroup, anyGroup)) {
            editor.ungroupSelection(engine);
            acted = true;
        }

        ImGui::Separator();
        // The three switches a row in the hierarchy already carries, so the menu and the row cannot
        // come to mean different things. The checkmark shows the *primary* object's state, which is
        // what a mixed selection can honestly say; choosing the row applies the opposite of that to
        // all of them, which is what every editor does with a mixed toggle.
        const scene::CompositionNode* primary = composition->findNode(selection.primary());
        const bool visible = primary == nullptr || primary->visible;
        const bool locked = primary != nullptr && primary->locked;
        const bool hero = primary != nullptr && nodeIsHero(*composition, primary->name);
        if (menuToggle("Visible", visible)) {
            editor.setNodesVisible(engine, selection.nodes(), !visible);
            acted = true;
        }
        if (menuToggle("Locked", locked)) {
            editor.setNodesLocked(engine, selection.nodes(), !locked);
            acted = true;
        }
        if (menuToggle("Hero", hero)) {
            editor.setNodesHero(engine, selection.nodes(), !hero);
            acted = true;
        }

        ImGui::Separator();
        // See the header for why position is not among these.
        if (menuAction("Reset rotation")) {
            editor.setSelectionRotation(engine, glm::vec3(0.0f));
            acted = true;
        }
        if (menuAction("Reset scale")) {
            editor.setSelectionScale(engine, glm::vec3(1.0f));
            acted = true;
        }
    } else {
        ImGui::Separator();
        action(app::EditAction::SelectAll, shortcut::kSelectAll);
    }

    ImGui::Separator();
    action(app::EditAction::Undo, shortcut::kUndo);
    action(app::EditAction::Redo, shortcut::kRedo);
    return acted;
}

} // namespace avgen::ui
