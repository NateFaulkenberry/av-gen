#include "ui/edit_history.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"

#include <algorithm>

namespace avgen::ui {
namespace {

const std::string kNoLabel;

} // namespace

std::size_t EditCommand::touched() const {
    return params.size() + parents.size() + added.size() + removed.size();
}

std::vector<float> baseComponents(app::Engine& engine, const std::string& path) {
    const params::IParameter* p = engine.params().find(path);
    if (p == nullptr) {
        return {};
    }
    std::vector<float> out(p->componentCount());
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = p->baseComponent(i);
    }
    return out;
}

namespace {

// A node's transform lives in two places at once and both of them are read: the parameter, which is
// what the flattened scene and the project file use, and `CompositionNode::transform`, which is
// what a re-added node registers its parameters *from*. Writing only the parameter means a node
// that is deleted and undone comes back where it was before it was ever moved. So every transform
// write goes through here.
//
// This is the same trap that has cost this project time twice from the other end -- a project's
// `parameters` block overriding a hand-edited scene file. Two stores for one value is survivable
// only if every writer writes both.
void syncNodeTransform(scene::Composition& composition, const std::string& path) {
    // "nodes/<name>/<field>", and only the three transform fields.
    static constexpr std::string_view kPrefix = "nodes/";
    if (path.compare(0, kPrefix.size(), kPrefix) != 0) {
        return;
    }
    const std::size_t slash = path.find('/', kPrefix.size());
    if (slash == std::string::npos) {
        return;
    }
    const std::string name = path.substr(kPrefix.size(), slash - kPrefix.size());
    const std::string field = path.substr(slash + 1);
    scene::CompositionNode* node = composition.findNode(name);
    if (node == nullptr) {
        return;
    }
    if (field == "position" && node->positionParam != nullptr) {
        node->transform.position = node->positionParam->base();
    } else if (field == "scale" && node->scaleParam != nullptr) {
        node->transform.scale = node->scaleParam->base();
    } else if (field == "rotation" && node->rotationParam != nullptr) {
        node->transform.rotation = scene::quatFromEulerDegrees(node->rotationParam->base());
    } else if (field == "visible" && node->visibleParam != nullptr) {
        node->visible = node->visibleParam->value();
    }
}

} // namespace

bool setBaseComponents(app::Engine& engine, const std::string& path, const std::vector<float>& values) {
    params::IParameter* p = engine.params().find(path);
    if (p == nullptr || p->componentCount() != values.size()) {
        return false;
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        p->setBaseComponent(i, values[i]);
        // And the final, which is what everything that *reads* a parameter actually reads:
        // `Composition::nodeTransform` uses `value()`, not `base()`. Finals are recomputed from
        // base plus modulation at the top of every `Engine::update`, so this only brings the write
        // forward by one frame -- but that one frame is the difference between a gizmo whose
        // handle follows the object it is dragging and one that trails a frame behind it, and it
        // is the difference between an edit a test can observe and one it cannot.
        p->setFinalComponent(i, values[i]);
    }
    if (scene::Composition* composition = engine.composition()) {
        syncNodeTransform(*composition, path);
    }
    return true;
}

EditApply applyEdit(app::Engine& engine, EditCommand& command, bool forward) {
    EditApply out;
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        out.problems.push_back("there is no composition to edit");
        return out;
    }

    // Nodes are settled before the parents and the parameters, so that a parameter path or a parent
    // name the command carries has something to land on.
    auto restore = [&](std::vector<NodeRecord>& records) {
        for (NodeRecord& record : records) {
            if (!record.held) {
                continue; // already live: a double-apply, or a command pushed without an undo
            }
            const std::string wanted = record.held->name;
            // The node is handed over only on success. `addNode` takes by value, so moving out of
            // `held` first and then failing -- a glTF that will no longer load, a nested scene file
            // that has been deleted, a terrain that no longer validates -- destroys the node *and*
            // leaves `held` null, which this code reads as "it is in the scene". The one thing an
            // undo must never do is lose the thing it was asked to bring back.
            auto added = composition->addNode(scene::CompositionNode(std::move(*record.held)));
            if (!added) {
                out.problems.push_back("could not restore '" + wanted + "': " + added.error().message);
                continue;
            }
            record.held.reset();
            if ((*added)->name != wanted) {
                // Names are unique and the history is linear, so this should not happen. If it
                // does, the command's parameter paths now name a node that is not there, and
                // saying so beats writing them into the void.
                out.problems.push_back("'" + wanted + "' came back as '" + (*added)->name + "'");
                record.name = (*added)->name;
            }
            ++out.nodesAdded;
        }
    };
    auto take = [&](std::vector<NodeRecord>& records) {
        for (auto it = records.rbegin(); it != records.rend(); ++it) {
            if (it->held) {
                continue; // already out
            }
            it->held = composition->detachNode(it->name);
            if (!it->held) {
                out.problems.push_back("could not remove '" + it->name + "': it is not in the scene");
                continue;
            }
            ++out.nodesRemoved;
        }
    };

    // Removals first, in *both* directions, and the reason is name recycling. `Composition::uniqueName`
    // hands out the first free name, so a Replace stroke that erases `fern_2` and paints a fern in
    // its place gets `fern_2` back for the new one. Restore the old `fern_2` while the new one is
    // still in the scene and it is renamed on the way in -- and then every parameter path, selection
    // record and label in the command names a node that is not there. Freeing the names before
    // handing them back costs nothing and makes the collision impossible.
    if (forward) {
        take(command.removed);
        restore(command.added);
    } else {
        take(command.added);
        restore(command.removed);
    }

    for (const ParentChange& change : command.parents) {
        const std::string& want = forward ? change.after : change.before;
        if (auto ok = composition->setParent(change.node, want); !ok) {
            out.problems.push_back(ok.error().message);
        } else {
            ++out.parentsSet;
        }
    }

    for (const ParamChange& change : command.params) {
        const std::vector<float>& want = forward ? change.after : change.before;
        if (want.empty()) {
            continue;
        }
        if (!setBaseComponents(engine, change.path, want)) {
            out.problems.push_back("'" + change.path + "' is no longer a parameter of that shape");
            continue;
        }
        ++out.paramsWritten;
    }

    // Once, at the end. A node that came back brings its parameter paths with it, and the routes
    // and timeline tracks aimed at those paths were dropped when it left.
    engine.rebind();
    return out;
}

void EditHistory::push(EditCommand command) {
    if (command.empty()) {
        return;
    }
    redo_.clear();
    redoIds_.clear();
    undo_.push_back(std::move(command));
    undoIds_.push_back(nextId_++);
    ++revision_;
    trim();
}

std::uint64_t EditHistory::stateId() const {
    return undoIds_.empty() ? baseId_ : undoIds_.back();
}

void EditHistory::trim() {
    if (capacity_ == 0) {
        return;
    }
    while (undo_.size() > capacity_) {
        undo_.erase(undo_.begin());
        // The dropped command's state becomes the empty-stack state: with it gone, "undo everything"
        // lands on the document as it stood *after* that command, not on the one the session opened
        // with. Without this the save marker could match a state the history can no longer reach.
        if (!undoIds_.empty()) {
            baseId_ = undoIds_.front();
            undoIds_.erase(undoIds_.begin());
        }
    }
}

const std::string& EditHistory::undoLabel() const {
    return undo_.empty() ? kNoLabel : undo_.back().label;
}

const std::string& EditHistory::redoLabel() const {
    return redo_.empty() ? kNoLabel : redo_.back().label;
}

EditApply EditHistory::undo(app::Engine& engine, std::vector<std::string>* selection) {
    EditApply out;
    if (undo_.empty()) {
        out.problems.push_back("nothing to undo");
        return out;
    }
    EditCommand command = std::move(undo_.back());
    undo_.pop_back();
    out = applyEdit(engine, command, false);
    if (selection != nullptr) {
        *selection = command.selectionBefore;
    }
    redo_.push_back(std::move(command));
    if (!undoIds_.empty()) {
        redoIds_.push_back(undoIds_.back());
        undoIds_.pop_back();
    }
    ++revision_;
    return out;
}

EditApply EditHistory::redo(app::Engine& engine, std::vector<std::string>* selection) {
    EditApply out;
    if (redo_.empty()) {
        out.problems.push_back("nothing to redo");
        return out;
    }
    EditCommand command = std::move(redo_.back());
    redo_.pop_back();
    out = applyEdit(engine, command, true);
    if (selection != nullptr) {
        *selection = command.selectionAfter;
    }
    undo_.push_back(std::move(command));
    if (!redoIds_.empty()) {
        undoIds_.push_back(redoIds_.back());
        redoIds_.pop_back();
    }
    ++revision_;
    return out;
}

void EditHistory::clear() {
    undo_.clear();
    redo_.clear();
    undoIds_.clear();
    redoIds_.clear();
    // A fresh identity rather than back to zero: a cleared history describes a different document
    // -- a project was loaded, a scene swapped -- and a save marker taken before the clear must not
    // match the state after it.
    baseId_ = nextId_++;
    ++revision_;
    dragging_ = false;
    drag_ = EditCommand{};
}

std::vector<std::string> EditHistory::redoLabels() const {
    std::vector<std::string> out;
    out.reserve(redo_.size());
    // `redo_` has the next one to redo at the back, so walking backwards puts it first.
    for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) {
        out.push_back(it->label);
    }
    return out;
}

std::vector<std::string> EditHistory::labels() const {
    std::vector<std::string> out;
    out.reserve(undo_.size());
    for (const EditCommand& command : undo_) {
        out.push_back(command.label);
    }
    return out;
}

void EditHistory::beginDrag(app::Engine& engine, std::string label, const std::vector<std::string>& paths,
                            std::vector<std::string> selection) {
    if (dragging_) {
        // A drag that was never closed. Commit it rather than losing it: the alternative is an
        // edit that happened and cannot be taken back, which is the one outcome undo exists to
        // prevent.
        commitDrag(engine);
    }
    drag_ = EditCommand(std::move(label));
    drag_.selectionBefore = selection;
    drag_.selectionAfter = std::move(selection);
    for (const std::string& path : paths) {
        std::vector<float> before = baseComponents(engine, path);
        if (before.empty()) {
            continue;
        }
        drag_.params.push_back(ParamChange{path, std::move(before), {}});
    }
    dragging_ = true;
}

void EditHistory::commitDrag(app::Engine& engine) {
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    EditCommand command = std::move(drag_);
    drag_ = EditCommand{};
    std::vector<ParamChange> moved;
    for (ParamChange& change : command.params) {
        change.after = baseComponents(engine, change.path);
        if (change.after.size() != change.before.size()) {
            continue;
        }
        if (change.after != change.before) {
            moved.push_back(std::move(change));
        }
    }
    command.params = std::move(moved);
    // A drag that ended where it started is not an edit. Recording it would put a no-op on the
    // stack that an artist then has to press undo twice to get past.
    push(std::move(command));
}

void EditHistory::cancelDrag(app::Engine& engine) {
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    EditCommand command = std::move(drag_);
    drag_ = EditCommand{};
    for (const ParamChange& change : command.params) {
        static_cast<void>(setBaseComponents(engine, change.path, change.before));
    }
}

} // namespace avgen::ui
