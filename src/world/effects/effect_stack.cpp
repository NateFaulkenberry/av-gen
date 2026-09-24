#include "world/effects/effect_stack.hpp"

#include "world/effects/effect_registry.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <numeric>
#include <unordered_set>

namespace avgen::world {
namespace {

// Owners in first-appearance order, each with the list positions of its effects. The one place the
// storage invariant is computed from, so every mutator agrees about it.
struct Group {
    EffectOwner owner;
    std::vector<std::size_t> members;
};

std::vector<Group> groupsOf(std::span<const EffectInstance> effects) {
    std::vector<Group> groups;
    for (std::size_t i = 0; i < effects.size(); ++i) {
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&](const Group& g) { return g.owner == effects[i].owner; });
        if (it == groups.end()) {
            groups.push_back(Group{effects[i].owner, {}});
            it = std::prev(groups.end());
        }
        it->members.push_back(i);
    }
    return groups;
}

std::string slug(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool dash = false;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0) {
            out.push_back(static_cast<char>(std::tolower(u)));
            dash = false;
        } else if (c == '_' || c == '.') {
            out.push_back(c);
            dash = false;
        } else if (!out.empty() && !dash) {
            // Spaces, slashes and anything else become one dash. A slash in particular must not
            // survive: an id is half of a parameter path.
            out.push_back('-');
            dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out.empty() ? std::string("effect") : out;
}

bool idTaken(std::span<const EffectInstance> effects, std::string_view id) {
    return std::any_of(effects.begin(), effects.end(), [&](const EffectInstance& e) { return e.id == id; });
}

int nextOrder(std::span<const EffectInstance> effects, const EffectOwner& owner) {
    int n = 0;
    for (const EffectInstance& e : effects) {
        if (e.owner == owner) {
            n = std::max(n, e.order + 1);
        }
    }
    return n;
}

// A type's factory makes the instance for the owner it is most often attached to. When the owner is
// one the factory's source cannot stand on -- an `Owner` source on the World, which has no position
// -- the source becomes "whatever the cut is on", which is the only World-sized answer to "where am
// I". Endpoint-generic: it reads the schema's endpoint accessors, never a type.
void adaptToOwner(EffectInstance& e) {
    const EffectSchema* schema = effectSchema(e.kind);
    if (schema == nullptr || schema->getSource == nullptr || schema->setSource == nullptr) {
        return;
    }
    EffectEndpoint src = schema->getSource(e);
    if (src.kind == SourceKind::Owner && (e.owner.isWorld() || e.owner.kind == EffectTarget::Light)) {
        src.kind = SourceKind::FocusHero;
        schema->setSource(e, src);
    }
}

} // namespace

std::vector<std::size_t> effectsOf(std::span<const EffectInstance> effects, const EffectOwner& owner) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < effects.size(); ++i) {
        if (effects[i].owner == owner) {
            out.push_back(i);
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [&](std::size_t a, std::size_t b) { return effects[a].order < effects[b].order; });
    return out;
}

std::size_t findEffect(std::span<const EffectInstance> effects, std::string_view id) {
    for (std::size_t i = 0; i < effects.size(); ++i) {
        if (effects[i].id == id) {
            return i;
        }
    }
    return effects.size();
}

std::string uniqueEffectId(std::span<const EffectInstance> effects, std::string_view base) {
    const std::string root = slug(base);
    if (!idTaken(effects, root)) {
        return root;
    }
    for (int n = 2;; ++n) {
        std::string candidate = std::format("{}-{}", root, n);
        if (!idTaken(effects, candidate)) {
            return candidate;
        }
    }
}

std::string newEffectId(std::span<const EffectInstance> effects, const EffectOwner& owner, EffectKind kind) {
    const EffectSchema* schema = effectSchema(kind);
    const std::string type = schema != nullptr ? schema->displayName : effectKindName(kind);
    if (owner.isWorld() || owner.name.empty()) {
        return uniqueEffectId(effects, owner.isWorld() ? type : std::string(effectTargetName(owner.kind)) + " " + type);
    }
    return uniqueEffectId(effects, owner.name + " " + type);
}

bool effectAllowedOn(EffectKind kind, EffectTarget target) {
    const EffectSchema* schema = effectSchema(kind);
    return schema != nullptr && (schema->targets & targetBit(target)) != 0;
}

std::vector<EffectKind> effectKindsFor(EffectTarget target) {
    std::vector<EffectKind> out;
    for (const EffectSchema* schema : effectSchemas()) {
        if ((schema->targets & targetBit(target)) != 0 && schema->factory != nullptr) {
            out.push_back(schema->kind);
        }
    }
    return out;
}

Result<std::string> addEffect(std::vector<EffectInstance>& effects, const EffectOwner& owner, EffectKind kind) {
    const EffectSchema* schema = effectSchema(kind);
    if (schema == nullptr || schema->factory == nullptr) {
        return fail("effect type {} is not registered", static_cast<int>(kind));
    }
    EffectInstance e = schema->factory(schema->displayName);
    e.owner = owner;
    return insertEffect(effects, std::move(e));
}

Result<std::string> insertEffect(std::vector<EffectInstance>& effects, EffectInstance effect) {
    if (auto ok = effect.owner.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    if (!effectAllowedOn(effect.kind, effect.owner.kind)) {
        const EffectSchema* schema = effectSchema(effect.kind);
        return fail("{} cannot be attached to {} '{}': the type does not support that target",
                    schema != nullptr ? schema->displayName : "this effect type",
                    effectTargetName(effect.owner.kind), effect.owner.label());
    }
    adaptToOwner(effect);
    if (effect.id.empty() || idTaken(effects, effect.id) || effect.id.find('/') != std::string::npos) {
        effect.id = newEffectId(effects, effect.owner, effect.kind);
    }
    if (effect.name.empty()) {
        if (const EffectSchema* schema = effectSchema(effect.kind)) {
            effect.name = schema->displayName;
        }
    }
    effect.order = nextOrder(effects, effect.owner);
    if (auto ok = effect.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    std::string id = effect.id;
    effects.push_back(std::move(effect));
    normaliseEffectOrder(effects);
    return id;
}

bool removeEffect(std::vector<EffectInstance>& effects, std::string_view id) {
    const std::size_t at = findEffect(effects, id);
    if (at == effects.size()) {
        return false;
    }
    effects.erase(effects.begin() + static_cast<std::ptrdiff_t>(at));
    normaliseEffectOrder(effects);
    return true;
}

bool moveEffectTo(std::vector<EffectInstance>& effects, std::string_view id, int order) {
    const std::size_t at = findEffect(effects, id);
    if (at == effects.size()) {
        return false;
    }
    normaliseEffectOrder(effects);
    const std::size_t self = findEffect(effects, id);
    std::vector<std::size_t> stack = effectsOf(effects, effects[self].owner);
    const int from = effects[self].order;
    const int to = std::clamp(order, 0, static_cast<int>(stack.size()) - 1);
    if (from == to) {
        return false;
    }
    // Remove and reinsert in the owner's own order, then renumber. Nothing outside the stack moves.
    const std::size_t moving = stack[static_cast<std::size_t>(from)];
    stack.erase(stack.begin() + from);
    stack.insert(stack.begin() + to, moving);
    for (std::size_t i = 0; i < stack.size(); ++i) {
        effects[stack[i]].order = static_cast<int>(i);
    }
    normaliseEffectOrder(effects);
    return true;
}

bool moveEffect(std::vector<EffectInstance>& effects, std::string_view id, int delta) {
    const std::size_t at = findEffect(effects, id);
    if (at == effects.size() || delta == 0) {
        return false;
    }
    return moveEffectTo(effects, id, effects[at].order + delta);
}

Result<std::string> duplicateEffect(std::vector<EffectInstance>& effects, std::string_view id) {
    const std::size_t at = findEffect(effects, id);
    if (at == effects.size()) {
        return fail("no effect has the id '{}'", id);
    }
    EffectInstance copy = effects[at];
    copy.name += " copy";
    copy.id = uniqueEffectId(effects, copy.id + " copy");
    const int below = effects[at].order + 1;
    auto added = insertEffect(effects, std::move(copy));
    if (!added) {
        return added;
    }
    moveEffectTo(effects, *added, below);
    return added;
}

std::size_t removeEffectsOf(std::vector<EffectInstance>& effects, const EffectOwner& owner) {
    const std::size_t before = effects.size();
    std::erase_if(effects, [&](const EffectInstance& e) { return e.owner == owner; });
    return before - effects.size();
}

std::size_t renameEffectOwner(std::vector<EffectInstance>& effects, const EffectOwner& from,
                              const EffectOwner& to) {
    std::size_t moved = 0;
    for (EffectInstance& e : effects) {
        if (e.owner == from) {
            e.owner = to;
            ++moved;
        }
    }
    normaliseEffectOrder(effects);
    return moved;
}

void normaliseEffectOrder(std::vector<EffectInstance>& effects) {
    const std::vector<Group> groups = groupsOf(effects);
    std::vector<EffectInstance> sorted;
    sorted.reserve(effects.size());
    for (const Group& g : groups) {
        std::vector<std::size_t> members = g.members;
        std::stable_sort(members.begin(), members.end(),
                         [&](std::size_t a, std::size_t b) { return effects[a].order < effects[b].order; });
        int n = 0;
        for (const std::size_t i : members) {
            sorted.push_back(std::move(effects[i]));
            sorted.back().order = n++;
        }
    }
    effects = std::move(sorted);
}

Result<void> validateEffects(std::span<const EffectInstance> effects) {
    std::unordered_set<std::string> ids;
    for (const EffectInstance& e : effects) {
        if (auto ok = e.validate(); !ok) {
            return ok;
        }
        if (!ids.insert(e.id).second) {
            return fail("two effects have the id '{}'; an id is half of a parameter path", e.id);
        }
        const EffectSchema* schema = effectSchema(e.kind);
        if (schema == nullptr) {
            return fail("effect '{}': its type is not registered", e.id);
        }
        if ((schema->targets & targetBit(e.owner.kind)) == 0) {
            return fail("effect '{}': a {} cannot be attached to {} '{}'", e.id, schema->displayName,
                        effectTargetName(e.owner.kind), e.owner.label());
        }
    }
    // Orders contiguous per owner. Checked rather than silently renumbered: a file that says two
    // effects are both first in one stack is a file whose author meant something this cannot guess.
    for (const Group& g : groupsOf(effects)) {
        std::vector<int> orders;
        orders.reserve(g.members.size());
        for (const std::size_t i : g.members) {
            orders.push_back(effects[i].order);
        }
        std::sort(orders.begin(), orders.end());
        for (std::size_t i = 0; i < orders.size(); ++i) {
            if (orders[i] != static_cast<int>(i)) {
                return fail("the effect stack of {} has orders that are not 0..{} (found {} at position {})",
                            g.owner.label(), orders.size() - 1, orders[i], i);
            }
        }
    }
    return {};
}

void effectEvaluationOrder(std::span<const EffectInstance> effects, std::vector<std::uint32_t>& out) {
    out.resize(effects.size());
    std::iota(out.begin(), out.end(), 0u);
    const auto key = [&](std::uint32_t i) {
        const EffectSchema* schema = effectSchema(effects[i].kind);
        const int stage = schema != nullptr ? static_cast<int>(schema->stage) : 0;
        const int priority = schema != nullptr ? schema->priority : 0;
        return std::pair<int, int>(stage, priority);
    };
    std::stable_sort(out.begin(), out.end(), [&](std::uint32_t a, std::uint32_t b) { return key(a) < key(b); });
}

} // namespace avgen::world
