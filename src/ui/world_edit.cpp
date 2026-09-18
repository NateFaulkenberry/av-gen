#include "core/interaction_latency.hpp"
#include "ui/world_edit.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>

namespace avgen::ui {
namespace {

const std::string kNothing;

// The world transform of a node's *parent chain*, i.e. the frame its local transform sits in.
// Identity for a root. Everything that has to keep an object still while changing what it hangs
// off goes through this.
glm::mat4 parentFrame(const scene::Composition& composition, const scene::CompositionNode& node) {
    if (node.parent.empty()) {
        return glm::mat4(1.0f);
    }
    const scene::CompositionNode* parent = composition.findNode(node.parent);
    if (parent == nullptr) {
        return glm::mat4(1.0f);
    }
    return composition.nodeWorldTransform(*parent).matrix();
}

std::string nodeParamPath(const std::string& node, const char* field) {
    return "nodes/" + node + "/" + field;
}

// Records one parameter's value before an edit, so the caller can pair it with the value after.
ParamChange openChange(app::Engine& engine, const std::string& path) {
    ParamChange change;
    change.path = path;
    change.before = baseComponents(engine, path);
    return change;
}

// Closes it, and drops it when nothing actually moved.
void closeChange(app::Engine& engine, ParamChange change, std::vector<ParamChange>& into) {
    change.after = baseComponents(engine, change.path);
    if (change.before.size() == change.after.size() && change.before != change.after) {
        into.push_back(std::move(change));
    }
}

} // namespace

// ---- Selection -----------------------------------------------------------------------------------

bool Selection::contains(const std::string& name) const {
    return std::find(nodes_.begin(), nodes_.end(), name) != nodes_.end();
}

const std::string& Selection::primary() const {
    return nodes_.empty() ? kNothing : nodes_.back();
}

void Selection::set(std::string name) {
    nodes_.clear();
    if (!name.empty()) {
        nodes_.push_back(std::move(name));
    }
}

void Selection::set(std::vector<std::string> names) {
    nodes_.clear();
    for (std::string& name : names) {
        add(std::move(name));
    }
}

void Selection::add(std::string name) {
    if (name.empty() || contains(name)) {
        return;
    }
    nodes_.push_back(std::move(name));
}

void Selection::remove(const std::string& name) {
    nodes_.erase(std::remove(nodes_.begin(), nodes_.end(), name), nodes_.end());
}

void Selection::toggle(std::string name) {
    if (contains(name)) {
        remove(name);
    } else {
        add(std::move(name));
    }
}

bool Selection::retainOnly(const scene::Composition& composition) {
    const std::size_t before = nodes_.size();
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                                [&](const std::string& n) { return composition.findNode(n) == nullptr; }),
                 nodes_.end());
    return nodes_.size() != before;
}

// ---- group navigation ------------------------------------------------------------------------------

std::string groupRootOf(const scene::Composition& composition, const std::string& name) {
    std::string best = name;
    const scene::CompositionNode* node = composition.findNode(name);
    // Bounded by the node count, because a scene file can be hand-edited into a cycle and an
    // editor that hangs on one is worse than an editor that picks the wrong parent.
    for (std::size_t guard = 0; node != nullptr && !node->parent.empty() && guard <= composition.nodeCount();
         ++guard) {
        const scene::CompositionNode* parent = composition.findNode(node->parent);
        if (parent == nullptr) {
            break;
        }
        if (parent->kind == scene::NodeKind::Group) {
            best = parent->name;
        }
        node = parent;
    }
    return best;
}

bool nodeLocked(const scene::Composition& composition, const std::string& name) {
    const scene::CompositionNode* node = composition.findNode(name);
    // Bounded for the same reason `groupRootOf` is: a hand-edited scene file can describe a cycle,
    // and an editor that hangs when the pointer passes over one is worse than one that answers
    // "not locked" about a scene that is already broken.
    for (std::size_t guard = 0; node != nullptr && guard <= composition.nodeCount(); ++guard) {
        if (node->locked) {
            return true;
        }
        if (node->parent.empty()) {
            break;
        }
        node = composition.findNode(node->parent);
    }
    return false;
}

std::vector<std::string> descendantsOf(const scene::Composition& composition, const std::string& name) {
    std::vector<std::string> out;
    std::vector<std::string> frontier{name};
    for (std::size_t guard = 0; !frontier.empty() && guard <= composition.nodeCount(); ++guard) {
        std::vector<std::string> next;
        for (const auto& node : composition.nodes()) {
            if (!node) {
                continue;
            }
            if (std::find(frontier.begin(), frontier.end(), node->parent) == frontier.end()) {
                continue;
            }
            if (std::find(out.begin(), out.end(), node->name) != out.end() || node->name == name) {
                continue;
            }
            out.push_back(node->name);
            next.push_back(node->name);
        }
        frontier = std::move(next);
    }
    return out;
}

std::vector<std::string> withDescendants(const scene::Composition& composition,
                                         std::span<const std::string> names) {
    std::vector<std::string> out;
    const auto push = [&](const std::string& n) {
        if (std::find(out.begin(), out.end(), n) == out.end()) {
            out.push_back(n);
        }
    };
    for (const std::string& name : names) {
        push(name);
        for (const std::string& child : descendantsOf(composition, name)) {
            push(child);
        }
    }
    return out;
}

std::vector<std::string> topmostOf(const scene::Composition& composition,
                                   std::span<const std::string> names) {
    std::vector<std::string> out;
    for (const std::string& name : names) {
        bool covered = false;
        const scene::CompositionNode* node = composition.findNode(name);
        for (std::size_t guard = 0; node != nullptr && !node->parent.empty() && guard <= composition.nodeCount();
             ++guard) {
            if (std::find(names.begin(), names.end(), node->parent) != names.end()) {
                covered = true;
                break;
            }
            node = composition.findNode(node->parent);
        }
        if (!covered && std::find(out.begin(), out.end(), name) == out.end()) {
            out.push_back(name);
        }
    }
    return out;
}

scene::WorldBounds selectionBounds(scene::Composition& composition, std::span<const std::string> names) {
    scene::WorldBounds out;
    for (const std::string& name : names) {
        out.include(composition.nodeBounds(name));
    }
    return out;
}

// ---- writing a transform ---------------------------------------------------------------------------

namespace {

bool writeVec3(app::Engine& engine, const std::string& node, const char* field, glm::vec3 value) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return false;
    }
    scene::CompositionNode* n = composition->findNode(node);
    if (n == nullptr) {
        return false;
    }
    const std::vector<float> components{value.x, value.y, value.z};
    if (!setBaseComponents(engine, nodeParamPath(node, field), components)) {
        return false;
    }
    return true;
}

} // namespace

bool setNodePosition(app::Engine& engine, const std::string& node, glm::vec3 value) {
    return writeVec3(engine, node, "position", value);
}

bool setNodeRotation(app::Engine& engine, const std::string& node, glm::vec3 degrees) {
    return writeVec3(engine, node, "rotation", degrees);
}

bool setNodeScale(app::Engine& engine, const std::string& node, glm::vec3 value) {
    return writeVec3(engine, node, "scale", value);
}

bool setNodeVisible(app::Engine& engine, const std::string& node, bool value) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr || composition->findNode(node) == nullptr) {
        return false;
    }
    // `setBaseComponents` writes the node's own `visible` flag too, the way it does a transform: the
    // flag is what a re-added node re-registers its parameter from, so writing only the parameter
    // would give a hidden node back visible after a delete and an undo.
    return setBaseComponents(engine, nodeParamPath(node, "visible"), {value ? 1.0f : 0.0f});
}

std::vector<std::string> transformParamPaths(std::span<const std::string> names) {
    std::vector<std::string> out;
    out.reserve(names.size() * 3);
    for (const std::string& name : names) {
        out.push_back(nodeParamPath(name, "position"));
        out.push_back(nodeParamPath(name, "rotation"));
        out.push_back(nodeParamPath(name, "scale"));
    }
    return out;
}

// ---- operations ------------------------------------------------------------------------------------

EditCommand placeNodes(app::Engine& engine, std::vector<scene::CompositionNode> nodes, std::string label,
                       std::vector<std::string>* created) {
    EditCommand command(std::move(label));
    if (nodes.empty()) {
        return command;
    }
    if (engine.composition() == nullptr) {
        engine.newComposition();
    }
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return command;
    }
    for (scene::CompositionNode& node : nodes) {
        auto added = composition->addNode(std::move(node));
        if (!added) {
            log::warn("place: {}", added.error().message);
            continue;
        }
        command.added.emplace_back((*added)->name);
        if (created != nullptr) {
            created->push_back((*added)->name);
        }
    }
    // Once. Engine::addNode rebinds per call, which for a brush stroke is one full pass over every
    // route and every timeline track per plant.
    engine.rebind();
    return command;
}

EditCommand deleteNodes(app::Engine& engine, std::span<const std::string> names) {
    EditCommand command;
    scene::Composition* composition = engine.composition();
    if (composition == nullptr || names.empty()) {
        return command;
    }
    // Parent-first. `applyEdit` restores this list forward (parents before children, so no node is
    // ever added naming a parent that is not there yet) and detaches it backwards (children before
    // parents, so removing a parent never re-parents a child that is about to go anyway).
    const std::vector<std::string> doomed = withDescendants(*composition, topmostOf(*composition, names));
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it) {
        if (auto held = composition->detachNode(*it)) {
            command.removed.emplace_back(*it, std::move(held));
        }
    }
    std::reverse(command.removed.begin(), command.removed.end());
    if (command.removed.empty()) {
        return command;
    }
    engine.rebind();
    command.label = command.removed.size() == 1 ? "Delete " + command.removed.front().name
                                                : "Delete " + std::to_string(command.removed.size()) + " objects";
    return command;
}

EditCommand duplicateNodes(app::Engine& engine, std::span<const std::string> names, glm::vec3 offset,
                           std::vector<std::string>* created) {
    EditCommand command;
    scene::Composition* composition = engine.composition();
    if (composition == nullptr || names.empty()) {
        return command;
    }
    const std::vector<std::string> sources = withDescendants(*composition, topmostOf(*composition, names));
    // Old name -> new name, so a duplicated group's children hang off the *copy* of the group
    // rather than off the original. Without this, duplicating a group produces a second set of
    // objects parented to the first group, and moving either moves both.
    std::vector<std::pair<std::string, std::string>> renames;
    std::vector<scene::CompositionNode> copies;
    for (const std::string& name : sources) {
        const scene::CompositionNode* source = composition->findNode(name);
        if (source == nullptr) {
            continue;
        }
        scene::CompositionNode copy = scene::cloneNodeSpec(*source);
        // Only the copies whose parent is *not* also being copied move. A child's local transform
        // is relative to its parent, and the parent's copy has already taken the offset, so
        // offsetting the child too would move it twice as far. The test for that is exactly "is my
        // parent in this batch" and nothing else -- an earlier version also treated anything the
        // caller named explicitly as a root, which double-offset every child of a group that the
        // caller had happened to select alongside the group itself.
        const bool root = std::find(sources.begin(), sources.end(), source->parent) == sources.end();
        if (root) {
            // `offset` is world space and `transform.position` is local to the copy's parent. For a
            // node under a rotated or scaled parent those are different directions, so the offset is
            // taken through the parent's frame -- the same conversion `moveNodes` does, for exactly
            // the same reason.
            const glm::mat4 frame = parentFrame(*composition, *source);
            const glm::vec3 local = glm::vec3(glm::inverse(frame) * glm::vec4(offset, 0.0f));
            copy.transform.position += local;
        }
        copies.push_back(std::move(copy));
    }
    // Added one at a time so each new name is known before the next node names it as a parent.
    if (engine.composition() == nullptr) {
        engine.newComposition();
    }
    for (scene::CompositionNode& copy : copies) {
        const std::string original = copy.name;
        for (const auto& [from, to] : renames) {
            if (copy.parent == from) {
                copy.parent = to;
                break;
            }
        }
        auto added = composition->addNode(std::move(copy));
        if (!added) {
            log::warn("duplicate: {}", added.error().message);
            continue;
        }
        renames.emplace_back(original, (*added)->name);
        command.added.emplace_back((*added)->name);
        if (created != nullptr && std::find(names.begin(), names.end(), original) != names.end()) {
            created->push_back((*added)->name);
        }
    }
    engine.rebind();
    if (command.added.empty()) {
        return command;
    }
    command.label = command.added.size() == 1 ? "Duplicate " + command.added.front().name
                                              : "Duplicate " + std::to_string(command.added.size()) + " objects";
    return command;
}

EditCommand pasteNodes(app::Engine& engine, const std::vector<scene::CompositionNode>& nodes,
                       glm::vec3 offset, std::vector<std::string>* created) {
    EditCommand command;
    if (nodes.empty()) {
        return command;
    }
    if (engine.composition() == nullptr) {
        engine.newComposition();
    }
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return command;
    }
    // Which names are in this batch, so a child can tell whether its parent came with it.
    std::vector<std::string> batch;
    batch.reserve(nodes.size());
    for (const scene::CompositionNode& node : nodes) {
        batch.push_back(node.name);
    }
    std::vector<std::pair<std::string, std::string>> renames;
    for (const scene::CompositionNode& node : nodes) {
        scene::CompositionNode copy = scene::cloneNodeSpec(node);
        const std::string original = copy.name;
        const bool insideBatch = std::find(batch.begin(), batch.end(), copy.parent) != batch.end();
        if (!insideBatch) {
            // Its parent did not come with it. Dropping to a root rather than keeping the name: the
            // scene being pasted into may not have that node, and a parent naming something absent
            // is a node that never appears.
            copy.parent.clear();
            copy.transform.position += offset;
        }
        for (const auto& [from, to] : renames) {
            if (copy.parent == from) {
                copy.parent = to;
                break;
            }
        }
        auto added = composition->addNode(std::move(copy));
        if (!added) {
            log::warn("paste: {}", added.error().message);
            continue;
        }
        renames.emplace_back(original, (*added)->name);
        command.added.emplace_back((*added)->name);
        if (created != nullptr) {
            created->push_back((*added)->name);
        }
    }
    engine.rebind();
    if (command.added.empty()) {
        return command;
    }
    command.label = command.added.size() == 1
                        ? "Paste " + command.added.front().name
                        : "Paste " + std::to_string(command.added.size()) + " objects";
    return command;
}

EditCommand groupNodes(app::Engine& engine, std::span<const std::string> names, std::string groupName,
                       std::string* created) {
    EditCommand command;
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return command;
    }
    const std::vector<std::string> members = topmostOf(*composition, names);
    if (members.size() < 1) {
        return command;
    }
    const scene::WorldBounds bounds = selectionBounds(*composition, members);
    // The pivot goes at the middle of the footprint and the *bottom* of the box: an artist rotating
    // a group of trees expects them to turn about the ground between them, not about a point
    // hovering at half their height.
    const glm::vec3 pivot = bounds.valid
                                ? glm::vec3(bounds.centre().x, bounds.min.y, bounds.centre().z)
                                : glm::vec3(0.0f);

    scene::CompositionNode group;
    group.name = groupName.empty() ? "group" : std::move(groupName);
    group.kind = scene::NodeKind::Group;
    group.transform.position = pivot;
    auto added = composition->addNode(std::move(group));
    if (!added) {
        log::warn("group: {}", added.error().message);
        return command;
    }
    const std::string madeName = (*added)->name;
    command.added.emplace_back(madeName);
    if (created != nullptr) {
        *created = madeName;
    }

    for (const std::string& member : members) {
        scene::CompositionNode* node = composition->findNode(member);
        if (node == nullptr || node->name == madeName) {
            continue;
        }
        const glm::mat4 world = composition->nodeWorldTransform(*node).matrix();
        command.parents.push_back(ParentChange{member, node->parent, madeName});
        if (auto ok = composition->setParent(member, madeName); !ok) {
            log::warn("group: {}", ok.error().message);
            command.parents.pop_back();
            continue;
        }
        // The group is a pure translation, so the new local transform is the old world transform
        // with the pivot taken off. Computed through matrices anyway, because a member that was
        // already inside a rotated group is not a translation away from where it was.
        ParamChange position = openChange(engine, nodeParamPath(member, "position"));
        ParamChange rotation = openChange(engine, nodeParamPath(member, "rotation"));
        ParamChange scale = openChange(engine, nodeParamPath(member, "scale"));
        const scene::Transform local =
            scene::Transform::fromMatrix(glm::inverse(parentFrame(*composition, *node)) * world);
        setNodePosition(engine, member, local.position);
        setNodeRotation(engine, member, scene::eulerDegrees(local.rotation));
        setNodeScale(engine, member, local.scale);
        closeChange(engine, std::move(position), command.params);
        closeChange(engine, std::move(rotation), command.params);
        closeChange(engine, std::move(scale), command.params);
    }
    engine.rebind();
    command.label = "Group " + std::to_string(members.size()) +
                    (members.size() == 1 ? " object" : " objects");
    return command;
}

EditCommand ungroupNode(app::Engine& engine, const std::string& groupName,
                        std::vector<std::string>* members) {
    EditCommand command;
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return command;
    }
    scene::CompositionNode* group = composition->findNode(groupName);
    if (group == nullptr || group->kind != scene::NodeKind::Group) {
        return command;
    }
    const std::string grandParent = group->parent;
    std::vector<std::string> children;
    for (const auto& node : composition->nodes()) {
        if (node && node->parent == groupName) {
            children.push_back(node->name);
        }
    }
    for (const std::string& child : children) {
        scene::CompositionNode* node = composition->findNode(child);
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world = composition->nodeWorldTransform(*node).matrix();
        command.parents.push_back(ParentChange{child, groupName, grandParent});
        if (auto ok = composition->setParent(child, grandParent); !ok) {
            log::warn("ungroup: {}", ok.error().message);
            command.parents.pop_back();
            continue;
        }
        ParamChange position = openChange(engine, nodeParamPath(child, "position"));
        ParamChange rotation = openChange(engine, nodeParamPath(child, "rotation"));
        ParamChange scale = openChange(engine, nodeParamPath(child, "scale"));
        const scene::Transform local =
            scene::Transform::fromMatrix(glm::inverse(parentFrame(*composition, *node)) * world);
        setNodePosition(engine, child, local.position);
        setNodeRotation(engine, child, scene::eulerDegrees(local.rotation));
        setNodeScale(engine, child, local.scale);
        closeChange(engine, std::move(position), command.params);
        closeChange(engine, std::move(rotation), command.params);
        closeChange(engine, std::move(scale), command.params);
        if (members != nullptr) {
            members->push_back(child);
        }
    }
    if (auto held = composition->detachNode(groupName)) {
        command.removed.emplace_back(groupName, std::move(held));
    }
    engine.rebind();
    command.label = "Ungroup " + groupName;
    return command;
}

// ---- heroes ------------------------------------------------------------------------------------

bool nodeIsHero(const scene::Composition& composition, const std::string& name) {
    const auto& heroes = composition.heroes();
    return std::any_of(heroes.begin(), heroes.end(),
                       [&](const world::HeroPoint& hero) { return heroNamesNode(hero, name); });
}

world::HeroPoint heroFromNode(scene::Composition& composition, const std::string& name) {
    world::HeroPoint hero;
    // The hero is the object: same name, and that name is the only tie between them (ADR-107).
    // `assetId` is left empty on purpose -- it means "look this up in the asset library", and a node
    // in a scene is not a library entry.
    hero.name = name;
    const scene::CompositionNode* node = composition.findNode(name);
    if (node == nullptr) {
        return hero;   // validate() will reject it; the caller checks the node first
    }
    hero.yaw = scene::eulerDegrees(composition.nodeWorldTransform(*node).rotation).y;

    const scene::WorldBounds bounds = composition.nodeBounds(name);
    if (bounds.valid) {
        hero.position = bounds.centre();
        const glm::vec3 size = bounds.size();
        // Half the horizontal extent, and the full height: what `radius` and `height` are defined
        // as, and what everything that clears space around a hero or stands a camera off from one
        // reads instead of holding the asset library to ask how big it is.
        hero.radius = std::max(0.05f, std::max(size.x, size.z) * 0.5f);
        hero.height = std::max(0.05f, size.y);
    } else {
        // A group with nothing under it, or an asset that did not load. The node's own position is
        // still the truth about where it is; its size is not knowable, so the defaults stand.
        hero.position = composition.nodeWorldTransform(*node).position;
    }
    hero.preferredCameraDistance = std::max(6.0f, hero.radius * 3.0f + hero.height * 1.5f);
    hero.activationRadius = hero.preferredCameraDistance * 3.0f;
    return hero;
}

EditCommand setNodesHero(app::Engine& engine, std::span<const std::string> names, bool hero) {
    EditCommand command;
    scene::Composition* composition = engine.composition();
    if (composition == nullptr || names.empty()) {
        return command;
    }
    for (const std::string& name : names) {
        // Declaring one measures the object, so there has to be an object. *Un*declaring one does
        // not: a hero may name an assembly rather than a node -- Glowmere's elder is three nodes and
        // no single one of them -- and a hero with no row in the Objects list was a hero nothing in
        // the application could take back.
        if (hero && composition->findNode(name) == nullptr) {
            continue;
        }
        HeroChange change;
        change.node = name;
        for (const world::HeroPoint& declared : composition->heroes()) {
            if (heroNamesNode(declared, name)) {
                change.before.push_back(declared);
            }
        }
        if (hero) {
            if (!change.before.empty()) {
                continue;   // already one; designating it again would only re-measure it
            }
            change.after.push_back(heroFromNode(*composition, name));
        } else if (change.before.empty()) {
            continue;       // not one to begin with
        }
        command.heroes.push_back(std::move(change));
    }
    if (command.heroes.empty()) {
        return command;
    }
    command.label = fmt::format("{} {}", hero ? "Make hero" : "Unmake hero",
                                command.heroes.size() == 1
                                    ? command.heroes.front().node
                                    : std::to_string(command.heroes.size()) + " objects");
    // T2 for the interaction log, and only here -- after the early returns. A star of something
    // already starred returns above with an empty command and nothing happens; opening a record
    // before that point would report a latency for an edit that was refused, which is the shape of
    // dishonesty this instrument exists to avoid.
    core::interactions().beginWithoutInput(core::Interaction::HeroStar);
    core::interactions().markCommand();
    core::applyInjectedDelay(core::Interaction::HeroStar); // ADR-182's control; zero unless asked
    // Performed by the same code that replays it, rather than here and then again differently.
    // `applyEdit` assembles the whole list and validates it in one go, which is the only way
    // `Composition::setHeroes` can be called -- it rejects a set rather than a member.
    EditApply applied = applyEdit(engine, command, true);
    // T3: `setHeroes` has taken the new list. The flatten it asked for is paid by the next
    // `Engine::update`, which is where T4 lands -- so a star's model change and its evaluated
    // consequence are a frame apart by construction, and the log shows that rather than hiding it.
    core::interactions().markModel();
    if (!applied.ok()) {
        for (const std::string& problem : applied.problems) {
            log::warn("hero: {}", problem);
        }
        command = EditCommand{};   // nothing changed, so there is nothing to put on the history
        core::interactions().abandon(); // a refused edit is not a fast one
    }
    return command;
}

EditCommand moveNodes(app::Engine& engine, std::span<const std::string> names, glm::vec3 delta) {
    EditCommand command;
    scene::Composition* composition = engine.composition();
    if (composition == nullptr || names.empty()) {
        return command;
    }
    const std::vector<std::string> moving = topmostOf(*composition, names);
    for (const std::string& name : moving) {
        scene::CompositionNode* node = composition->findNode(name);
        if (node == nullptr) {
            continue;
        }
        ParamChange change = openChange(engine, nodeParamPath(name, "position"));
        // The delta is in world space. A node under a rotated or scaled parent has to translate by
        // the delta expressed in its parent's frame, or dragging a grouped object east moves it
        // somewhere else entirely.
        const glm::mat4 frame = parentFrame(*composition, *node);
        const glm::vec3 world = glm::vec3(composition->nodeWorldTransform(*node).position) + delta;
        const glm::vec4 local = glm::inverse(frame) * glm::vec4(world, 1.0f);
        setNodePosition(engine, name, glm::vec3(local));
        closeChange(engine, std::move(change), command.params);
    }
    if (command.params.empty()) {
        return command;
    }
    command.label = moving.size() == 1 ? "Move " + moving.front()
                                       : "Move " + std::to_string(moving.size()) + " objects";
    return command;
}

} // namespace avgen::ui
