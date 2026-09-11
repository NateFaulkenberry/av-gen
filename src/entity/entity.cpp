#include "entity/entity.hpp"

#include "core/hash.hpp"
#include "core/log.hpp"
#include "params/serialization.hpp"
#include "scene/procedural_detail.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <span>

namespace avgen::entity {
namespace {

constexpr float kDegrees = 180.0f / 3.14159265358979323846f;

std::uint32_t nameSeed(std::string_view name, std::uint32_t sceneSeed) {
    // FNV-1a over the name, mixed with the scene seed. Deterministic across runs and platforms,
    // and different for two entities that differ only in their name -- which is what stops a row
    // of identical props from moving in lockstep.
    std::uint32_t h = 2166136261u ^ sceneSeed;
    for (const char c : name) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619u;
    }
    return h == 0u ? 1u : h;
}

bool isAllDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

glm::vec3 readVec3(const nlohmann::json& j, const char* key, glm::vec3 fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3) {
        return fallback;
    }
    glm::vec3 out = fallback;
    for (int i = 0; i < 3; ++i) {
        if (j[key][static_cast<std::size_t>(i)].is_number()) {
            out[i] = j[key][static_cast<std::size_t>(i)].get<float>();
        }
    }
    return out;
}

glm::quat quatFromEulerDegrees(glm::vec3 degrees) {
    return glm::quat(glm::radians(degrees));
}

// Inverse of glm::quat(vec3), which builds Rz * Ry * Rx. glm::eulerAngles recovers the middle
// angle with asin and loses precision near +-90 degrees; atan2 does not. The same recovery the
// composition uses, so a transform written here reads back identically there.
glm::vec3 eulerDegrees(const glm::quat& q) {
    const glm::mat3 m = glm::mat3_cast(q);
    const float cy = std::sqrt(m[0][0] * m[0][0] + m[0][1] * m[0][1]);
    const float y = std::atan2(-m[0][2], cy);
    float x = 0.0f;
    float z = 0.0f;
    if (cy > 1e-6f) {
        x = std::atan2(m[1][2], m[2][2]);
        z = std::atan2(m[0][1], m[0][0]);
    } else {
        x = std::atan2(-m[2][1], m[1][1]);
    }
    return glm::degrees(glm::vec3(x, y, z));
}

} // namespace

// ---- Entity ----------------------------------------------------------------------------------

Entity::Entity(EntityDesc desc, std::uint32_t sceneSeed)
    : desc_(std::move(desc)),
      seed_(desc_.seed != 0 ? desc_.seed : nameSeed(desc_.name, sceneSeed)),
      rng_(seed_) {
    // The runtime copies of everything an action may change. Keeping them beside the description
    // rather than in it is what lets a reset put the entity back exactly as the scene file wrote
    // it: `equip` adds to `attachments_`, and a seek throws that away without touching the author's
    // own attachments.
    propertyValues_.reserve(desc_.properties.size());
    for (const PropertyDesc& property : desc_.properties) {
        propertyValues_.emplace_back(property.name, std::clamp(property.value, property.min, property.max));
    }
    attachments_ = desc_.attachments;
    schedule_.setDesc(desc_.schedule);
    if (!desc_.actions.empty()) {
        actions_.push(desc_.actions, Authority::Routine);
    }
}

const std::string& Entity::clipFor(std::string_view activity) const {
    static const std::string kNone;
    const auto find = [&](std::string_view name) -> const std::string* {
        const auto it = std::find_if(desc_.clips.begin(), desc_.clips.end(),
                                     [&](const std::pair<std::string, std::string>& c) {
                                         return c.first == name;
                                     });
        return it == desc_.clips.end() ? nullptr : &it->second;
    };
    if (const std::string* exact = find(activity)) {
        return *exact;
    }
    // No clip for the activity an action named. Falling back to `idle` rather than to nothing is
    // deliberate: a character told to sit whose asset has no sit clip should stand there, not
    // freeze in its bind pose.
    if (const std::string* idle = find("idle")) {
        return *idle;
    }
    return kNone;
}

bool Entity::hasProperty(std::string_view property) const {
    return std::any_of(propertyValues_.begin(), propertyValues_.end(),
                       [&](const std::pair<std::string, float>& p) { return p.first == property; });
}

float Entity::property(std::string_view property) const {
    const auto it = std::find_if(propertyValues_.begin(), propertyValues_.end(),
                                 [&](const std::pair<std::string, float>& p) { return p.first == property; });
    return it == propertyValues_.end() ? 0.0f : it->second;
}

bool Entity::setProperty(std::string_view property, float value) {
    for (std::size_t i = 0; i < propertyValues_.size(); ++i) {
        if (propertyValues_[i].first != property) {
            continue;
        }
        const PropertyDesc& declared = desc_.properties[i];
        propertyValues_[i].second = std::clamp(value, declared.min, declared.max);
        if (i < propertyParams_.size() && propertyParams_[i] != nullptr) {
            // The base, not the final: the modulation pass resets finals to bases every frame, so a
            // state written to a final would be forgotten by the next one. Writing the base is also
            // what makes an equipped prop survive a save.
            propertyParams_[i]->setBase(propertyValues_[i].second);
        }
        return true;
    }
    return false;
}

const InteractionDesc* Entity::interaction(std::string_view verb) const {
    const auto it = std::find_if(desc_.interactions.begin(), desc_.interactions.end(),
                                 [&](const InteractionDesc& i) { return i.name == verb; });
    return it == desc_.interactions.end() ? nullptr : &*it;
}

std::string_view Entity::occupant(std::string_view verb) const {
    const auto it = std::find_if(claims_.begin(), claims_.end(),
                                 [&](const std::pair<std::string, std::string>& c) { return c.first == verb; });
    return it == claims_.end() ? std::string_view{} : std::string_view(it->second);
}

bool Entity::claim(std::string_view verb, std::string_view who) {
    for (auto& claim : claims_) {
        if (claim.first == verb) {
            return claim.second == who;
        }
    }
    claims_.emplace_back(std::string(verb), std::string(who));
    return true;
}

void Entity::release(std::string_view verb, std::string_view who) {
    claims_.erase(std::remove_if(claims_.begin(), claims_.end(),
                                 [&](const std::pair<std::string, std::string>& c) {
                                     return c.first == verb && c.second == who;
                                 }),
                  claims_.end());
}

bool Entity::attach(const std::string& node, const std::string& socket) {
    if (std::find_if(desc_.sockets.begin(), desc_.sockets.end(),
                     [&](const SocketDesc& s) { return s.name == socket; }) == desc_.sockets.end()) {
        // An attachment to a socket that does not exist is the silent-nothing failure this layer
        // exists to prevent. Refuse it and let the action report it by name.
        return false;
    }
    for (AttachmentDesc& attachment : attachments_) {
        if (attachment.node == node) {
            attachment.socket = socket;
            return true;
        }
    }
    attachments_.push_back(AttachmentDesc{node, socket});
    return true;
}

bool Entity::detach(const std::string& node) {
    const auto it = std::find_if(attachments_.begin(), attachments_.end(),
                                 [&](const AttachmentDesc& a) { return a.node == node; });
    if (it == attachments_.end()) {
        return false;
    }
    attachments_.erase(it);
    return true;
}

const std::string& Entity::clipFor(Activity activity) const {
    static const std::string kNone;
    const auto find = [&](std::string_view name) -> const std::string* {
        const auto it = std::find_if(desc_.clips.begin(), desc_.clips.end(),
                                     [&](const std::pair<std::string, std::string>& c) {
                                         return c.first == name;
                                     });
        return it == desc_.clips.end() ? nullptr : &it->second;
    };
    if (const std::string* exact = find(activityName(activity))) {
        return *exact;
    }
    if (const std::string* idle = find("idle")) {
        return *idle;
    }
    return kNone;
}

bool Entity::socketTransform(std::string_view socket, scene::Transform& out) const {
    const auto it = std::find_if(desc_.sockets.begin(), desc_.sockets.end(),
                                 [&](const SocketDesc& s) { return s.name == socket; });
    if (it == desc_.sockets.end()) {
        return false;
    }
    scene::Transform base;
    base.position = state_.position() + motion_.position;
    base.rotation = quatFromEulerDegrees(glm::vec3(motion_.rotation.x, motion_.rotation.y + state_.yaw * kDegrees,
                                                   motion_.rotation.z));
    // A joint, when there is a skeleton to ask. Until the animation layer installs one, a socket
    // rides the entity's own frame: an approximate place is the right failure for a prop that has
    // to be somewhere, and it means a scene can be authored before the skeleton exists.
    if (skeleton_ != nullptr && !it->joint.empty()) {
        scene::Transform joint;
        if (skeleton_->jointWorldTransform(it->joint, joint)) {
            base = scene::detail::composeTransforms(base, joint);
        }
    }
    out = scene::detail::composeTransforms(base, it->offset);
    return true;
}

// ---- EntityWorld -----------------------------------------------------------------------------

void EntityWorld::setEntities(std::vector<EntityDesc> descs, std::uint32_t sceneSeed) {
    entities_.clear();
    problems_.clear();
    sceneSeed_ = sceneSeed;
    for (EntityDesc& desc : descs) {
        auto entity = std::make_unique<Entity>(std::move(desc), sceneSeed);
        for (const BehaviorDesc& behavior : entity->desc_.behaviors) {
            auto made = makeBehavior(behavior.kind, &behavior.settings);
            if (!made) {
                std::string known;
                for (const std::string_view kind : behaviorKinds()) {
                    if (!known.empty()) {
                        known += ", ";
                    }
                    known += kind;
                }
                problems_.push_back(fmt::format("entity '{}': unknown behaviour '{}' (known: {})",
                                                entity->name(), behavior.kind, known));
                continue;
            }
            made->reset(entity->rng_);
            entity->behaviors_.push_back(std::move(made));
        }
        entity->actions_.setSink(&pendingEvents_, entity->name());
        entities_.push_back(std::move(entity));
    }
    for (const std::string& problem : problems_) {
        log::warn("{}", problem);
    }
}

void EntityWorld::clear() {
    entities_.clear();
    bindings_.clear();
    landmarks_.clear();
    problems_.clear();
    registered_.clear();
    actionEvents_.clear();
    pendingEvents_.clear();
    counts_ = {};
}

Entity* EntityWorld::find(std::string_view name) {
    const auto it = std::find_if(entities_.begin(), entities_.end(),
                                 [&](const std::unique_ptr<Entity>& e) { return e->name() == name; });
    return it == entities_.end() ? nullptr : it->get();
}

const Entity* EntityWorld::find(std::string_view name) const {
    const auto it = std::find_if(entities_.begin(), entities_.end(),
                                 [&](const std::unique_ptr<Entity>& e) { return e->name() == name; });
    return it == entities_.end() ? nullptr : it->get();
}

void EntityWorld::setBindings(std::vector<NodeBinding> bindings) {
    bindings_ = std::move(bindings);
    for (auto& entity : entities_) {
        if (const NodeBinding* b = binding(entity->desc_.driven())) {
            entity->state_.anchor = b->anchor;
        }
    }
}

const NodeBinding* EntityWorld::binding(const std::string& node) const {
    const auto it = std::find_if(bindings_.begin(), bindings_.end(),
                                 [&](const NodeBinding& b) { return b.node == node; });
    return it == bindings_.end() ? nullptr : &*it;
}

bool EntityWorld::pointOfInterest(std::string_view name, glm::vec3& out) const {
    if (const Entity* e = find(name)) {
        out = e->state_.position() + e->motion_.position;
        return true;
    }
    const auto it = std::find_if(landmarks_.begin(), landmarks_.end(),
                                 [&](const std::pair<std::string, glm::vec3>& l) { return l.first == name; });
    if (it != landmarks_.end()) {
        out = it->second;
        return true;
    }
    return false;
}

void EntityWorld::registerParameters(params::ParameterSet& params, const std::string& prefix) {
    prefix_ = prefix;
    registered_.clear();
    for (auto& entity : entities_) {
        for (std::size_t i = 0; i < entity->behaviors_.size(); ++i) {
            const BehaviorDesc& desc = entity->desc_.behaviors[i];
            const std::string name = desc.name.empty() ? desc.kind : desc.name;
            const std::string base = prefix + entity->name() + "/" + name + "/";
            entity->behaviors_[i]->registerParameters(params, base);
            entity->behaviors_[i]->collectParameterPaths(registered_);
        }
        // Declared state, as ordinary parameters. This is the whole of what makes "the headphones
        // are on" readable by anything else: a reaction targets "state/headphones", a track
        // keyframes it, a preset saves it, and none of them has to know an action wrote it.
        entity->propertyParams_.assign(entity->desc_.properties.size(), nullptr);
        for (std::size_t i = 0; i < entity->desc_.properties.size(); ++i) {
            const PropertyDesc& property = entity->desc_.properties[i];
            const std::string path = prefix + entity->name() + "/state/" + property.name;
            auto& param = params.add(params::ParamDesc<float>{.path = path,
                                                              .defaultValue = property.value,
                                                              .hardMin = property.min,
                                                              .hardMax = property.max});
            param.setBase(entity->propertyValues_[i].second);
            entity->propertyParams_[i] = &param;
            registered_.push_back(path);
        }
    }
    parametersLive_ = true;
}

void EntityWorld::unregisterParameters(params::ParameterSet& params) {
    for (const std::string& path : registered_) {
        params.remove(path);
    }
    registered_.clear();
    parametersLive_ = false;
    for (auto& entity : entities_) {
        entity->positionParam_ = nullptr;
        entity->rotationParam_ = nullptr;
        entity->scaleParam_ = nullptr;
        // Stale pointers into a set that has just been emptied. The state itself lives on in
        // propertyValues_, which is why it survives a scene swap and a rebind.
        std::fill(entity->propertyParams_.begin(), entity->propertyParams_.end(), nullptr);
    }
}

std::string EntityWorld::resolveTarget(const Entity& entity, const std::string& target,
                                       const std::string& prefix, std::vector<std::string>* tried) const {
    if (target.empty()) {
        return {};
    }
    // An explicit escape hatch for the case the addressing rules do not cover: '@' means "this is
    // already a parameter path, leave it alone". Without one, the only way out of a naming scheme
    // is to widen the scheme until it means nothing.
    if (target.front() == '@') {
        return target.substr(1);
    }
    std::vector<std::string> candidates;
    candidates.push_back(prefix + entity.name() + "/" + target);
    if (const NodeBinding* b = binding(entity.desc().driven()); b != nullptr && b->exists) {
        if (!b->transformPrefix.empty()) {
            candidates.push_back(b->transformPrefix + target);
        }
        if (!b->geometryPrefix.empty()) {
            // "parts/<material>/..." is the readable spelling of "parts/<index>/...". An asset's
            // material names are what an artist sees in the file they exported; the index is an
            // internal ordering by surface area that nobody can predict and that changes the day
            // someone edits the model.
            constexpr std::string_view kParts = "parts/";
            if (target.rfind(kParts, 0) == 0) {
                const std::size_t slash = target.find('/', kParts.size());
                if (slash != std::string::npos) {
                    const std::string part = target.substr(kParts.size(), slash - kParts.size());
                    if (!isAllDigits(part)) {
                        for (std::size_t i = 0; i < b->partNames.size(); ++i) {
                            if (b->partNames[i] == part) {
                                candidates.push_back(b->geometryPrefix + "parts/" + std::to_string(i) +
                                                     target.substr(slash));
                                break;
                            }
                        }
                    }
                }
            }
            candidates.push_back(b->geometryPrefix + target);
        }
    }
    for (const std::string& candidate : candidates) {
        if (params_ != nullptr && params_->find(candidate) != nullptr) {
            return candidate;
        }
    }
    if (tried != nullptr) {
        *tried = std::move(candidates);
    }
    return {};
}

std::vector<params::ModRoute> EntityWorld::compileReactions(const params::ParameterSet& params,
                                                            std::vector<std::string>& problems) const {
    params_ = &params;
    std::vector<params::ModRoute> routes;
    for (const auto& entity : entities_) {
        for (const ReactionDesc& reaction : entity->desc().reactions) {
            std::vector<std::string> tried;
            const std::string target = resolveTarget(*entity, reaction.target, prefix_, &tried);
            if (target.empty()) {
                std::string names;
                for (const std::string& candidate : tried) {
                    if (!names.empty()) {
                        names += ", ";
                    }
                    names += candidate;
                }
                std::string parts;
                if (const NodeBinding* b = binding(entity->desc().driven())) {
                    for (std::size_t i = 0; i < b->partNames.size(); ++i) {
                        if (b->partNames[i].empty()) {
                            continue;
                        }
                        if (!parts.empty()) {
                            parts += ", ";
                        }
                        parts += b->partNames[i];
                    }
                }
                std::string message =
                    fmt::format("entity '{}': reaction '{}' <- '{}' resolves to no parameter (tried: {})",
                                entity->name(), reaction.target, reaction.signal,
                                names.empty() ? "nothing" : names);
                if (!parts.empty()) {
                    message += fmt::format("; this node's material parts are: {}", parts);
                }
                problems.push_back(std::move(message));
                continue;
            }
            params::ModRoute route;
            route.source = reaction.signal;
            route.target = target;
            route.component = reaction.component;
            route.amount = reaction.depth;
            route.op = reaction.op;
            route.polarity = reaction.polarity;
            route.chain = reaction.chain;
            route.enabled = reaction.enabled;
            routes.push_back(std::move(route));
        }
    }
    return routes;
}

void EntityWorld::bind(params::ParameterSet& params, const std::string& prefix) {
    prefix_ = prefix;
    params_ = &params;
    for (auto& entity : entities_) {
        const NodeBinding* b = binding(entity->desc_.driven());
        if (b == nullptr || !b->exists) {
            // An entity that drives a node the scene does not have is the exact failure this
            // project keeps shipping in silence. Say it once, here, with the name.
            const std::string message = fmt::format("entity '{}' drives node '{}', which this scene has no node for",
                                                    entity->name(), entity->desc_.driven());
            if (std::find(problems_.begin(), problems_.end(), message) == problems_.end()) {
                problems_.push_back(message);
                log::warn("{}", message);
            }
            entity->positionParam_ = nullptr;
            entity->rotationParam_ = nullptr;
            entity->scaleParam_ = nullptr;
            continue;
        }
        entity->positionParam_ = params.findAs<glm::vec3>(b->transformPrefix + "position");
        entity->rotationParam_ = params.findAs<glm::vec3>(b->transformPrefix + "rotation");
        entity->scaleParam_ = params.findAs<glm::vec3>(b->transformPrefix + "scale");
        entity->state_.anchor = b->anchor;
        entity->actions_.setSink(&pendingEvents_, entity->name());
        entity->propertyParams_.assign(entity->desc_.properties.size(), nullptr);
        for (std::size_t i = 0; i < entity->desc_.properties.size(); ++i) {
            const std::string path = prefix + entity->name() + "/state/" + entity->desc_.properties[i].name;
            entity->propertyParams_[i] = params.findAs<float>(path);
            if (entity->propertyParams_[i] != nullptr) {
                entity->propertyParams_[i]->setBase(entity->propertyValues_[i].second);
            }
        }
    }
    navPath_.setNavigator(&nav_);
}

void EntityWorld::reset() {
    actionEvents_.clear();
    pendingEvents_.clear();
    for (auto& entity : entities_) {
        entity->rng_ = Rng(entity->seed_);
        entity->state_ = EntityState{};
        entity->motion_ = MotionOffset{};
        entity->locomotion_ = LocomotionState{};
        entity->coarseAccum_ = 0.0;
        if (const NodeBinding* b = binding(entity->desc_.driven())) {
            entity->state_.anchor = b->anchor;
        }
        for (auto& behavior : entity->behaviors_) {
            behavior->reset(entity->rng_);
        }
        // Intent goes back to what the scene file said, not to nothing: a seek must reproduce the
        // entity the author described, including the routine it was given and the props it was
        // placed holding. Everything an action added since -- an equipped prop, a set property, a
        // claimed chair -- is exactly what a seek should discard.
        entity->actions_.reset();
        entity->schedule_.reset();
        entity->gait_.reset();
        entity->attachments_ = entity->desc_.attachments;
        entity->claims_.clear();
        for (std::size_t i = 0; i < entity->desc_.properties.size() && i < entity->propertyValues_.size(); ++i) {
            const PropertyDesc& property = entity->desc_.properties[i];
            entity->propertyValues_[i].second = std::clamp(property.value, property.min, property.max);
            if (i < entity->propertyParams_.size() && entity->propertyParams_[i] != nullptr) {
                entity->propertyParams_[i]->setBase(entity->propertyValues_[i].second);
            }
        }
        if (!entity->desc_.actions.empty()) {
            entity->actions_.push(entity->desc_.actions, Authority::Routine);
        }
    }
}

float EntityWorld::property(std::string_view entity, std::string_view property, float fallback) const {
    const Entity* found = find(entity);
    return found != nullptr && found->hasProperty(property) ? found->property(property) : fallback;
}

bool EntityWorld::startRoutine(std::string_view entity, double now) {
    Entity* found = find(entity);
    if (found == nullptr || found->schedule().desc().entries.empty()) {
        return false;
    }
    found->schedule().start(now);
    found->actions().hold(Authority::Routine, false);
    return true;
}

bool EntityWorld::pauseRoutine(std::string_view entity, double now) {
    Entity* found = find(entity);
    if (found == nullptr || !found->schedule().running()) {
        return false;
    }
    found->schedule().pause(now, found->actions());
    return true;
}

bool EntityWorld::resumeRoutine(std::string_view entity, double now) {
    Entity* found = find(entity);
    if (found == nullptr || !found->schedule().paused()) {
        return false;
    }
    found->schedule().resume(now, found->actions());
    return true;
}

bool EntityWorld::direct(std::string_view entity, std::vector<ActionDesc> actions, double now) {
    Entity* found = find(entity);
    if (found == nullptr) {
        return false;
    }
    // The Director tier, so whatever the entity was doing keeps its place and its elapsed seconds
    // and carries on when this drains. That is ADR-091's "resumes rather than resets", and it is a
    // property of *which tier* the override goes on rather than of anything the caller must do.
    found->actions().override(std::move(actions), Authority::Director, now);
    return true;
}

void EntityWorld::update(const EntityUpdate& ctx, params::ParameterSet& params) {
    if (!parametersLive_) {
        return;
    }
    params_ = &params;
    counts_ = {};
    navPath_.setNavigator(&nav_);
    // Cleared, not freed: the capacity is kept, so a frame in which twelve actions complete costs
    // no allocation after the first one that did (§47). Anything raised since the last update --
    // a director's cancellation -- is folded in rather than lost.
    actionEvents_.clear();
    if (!pendingEvents_.empty()) {
        actionEvents_.swap(pendingEvents_);
        pendingEvents_.clear();
    }
    for (auto& entityPtr : entities_) {
        Entity& entity = *entityPtr;
        const glm::vec3 here = entity.state_.position();
        const float distance = glm::length(here - ctx.viewPosition);

        // An entity under orders is never culled. A behaviour is ambient and losing it off camera
        // costs nothing; an *action* is something a director said, and a character that stopped
        // walking to the nightstand because the camera looked away would be a bug nobody could
        // reproduce. The coarse stage below still applies, so the cost stays bounded -- what is
        // refused here is only the "do not update at all" band.
        const bool underOrders = entity.actions_.pending() > 0 || entity.schedule_.running();
        if (!underOrders && entity.desc_.cullDistance > 0.0f && distance > entity.desc_.cullDistance) {
            // Far enough away that nothing it could do would be visible. Not merely a cheaper
            // update: no update, and no parameter write either, so the node stays exactly where
            // the scene put it.
            ++counts_.skipped;
            entity.active_ = false;
            continue;
        }

        double dt = ctx.dt;
        if (entity.desc_.fullDetailDistance > 0.0f && distance > entity.desc_.fullDetailDistance) {
            entity.coarseAccum_ += ctx.dt;
            if (entity.coarseAccum_ < static_cast<double>(entity.desc_.coarseInterval)) {
                ++counts_.skipped;
                continue; // its parameters keep last frame's values; nothing on screen moves
            }
            dt = entity.coarseAccum_;
            entity.coarseAccum_ = 0.0;
            entity.state_.detail = static_cast<float>(ctx.dt / std::max(dt, 1e-6));
            ++counts_.coarse;
        } else {
            entity.coarseAccum_ = 0.0;
            entity.state_.detail = 1.0f;
            ++counts_.full;
        }
        entity.active_ = true;

        entity.motion_ = MotionOffset{};
        entity.state_.hasLookTarget = false;
        entity.state_.reaction = 0.0f;
        entity.state_.activity = Activity::Idle;
        entity.state_.driven = false;

        // ---- intent, before behaviour (ADR-091) ----
        // The hierarchy is read top-down, so the action tier gets its say first and the behaviours
        // below it see `driven` and yield. Yielding is not stopping: a wanderer keeps its
        // destination and its pause timer, which is what makes the fall-back a resume.
        entity.schedule_.update(ctx.time, entity.actions_);
        ActionOutput intent;
        if (entity.actions_.pending() > 0) {
            ActionContext ac;
            ac.time = ctx.time;
            ac.dt = dt;
            ac.rng = &entity.rng_;
            ac.self = &entity;
            ac.world = this;
            ac.path = &pathProvider();
            ac.gait = &entity.desc_.gait;
            ac.events = &actionEvents_;
            intent = entity.actions_.update(ac, entity.state_);
        }

        BehaviorContext bc;
        bc.time = ctx.time;
        bc.dt = dt;
        bc.bus = ctx.bus;
        bc.nav = &nav_;
        bc.world = this;
        bc.rng = &entity.rng_;
        for (auto& behavior : entity.behaviors_) {
            behavior->update(bc, entity.state_, entity.motion_);
        }

        // Fold the behaviours' offsets onto the node's parameter *finals*. Bases are left alone, so
        // saving the project writes back what the author placed rather than wherever the entity
        // happened to be when they hit save.
        const glm::vec3 offset = entity.motion_.position + entity.state_.travel;
        if (entity.positionParam_ != nullptr) {
            for (int i = 0; i < 3; ++i) {
                const auto c = static_cast<std::size_t>(i);
                entity.positionParam_->setFinalComponent(c, entity.positionParam_->finalComponent(c) + offset[i]);
            }
        }
        if (entity.rotationParam_ != nullptr) {
            const glm::vec3 rotation(entity.motion_.rotation.x,
                                     entity.motion_.rotation.y + entity.state_.yaw * kDegrees,
                                     entity.motion_.rotation.z);
            for (int i = 0; i < 3; ++i) {
                const auto c = static_cast<std::size_t>(i);
                entity.rotationParam_->setFinalComponent(c, entity.rotationParam_->finalComponent(c) + rotation[i]);
            }
        }
        if (entity.scaleParam_ != nullptr) {
            for (int i = 0; i < 3; ++i) {
                const auto c = static_cast<std::size_t>(i);
                entity.scaleParam_->setFinalComponent(c, entity.scaleParam_->finalComponent(c) * entity.motion_.scale[i]);
            }
        }

        // A character doing nothing else, with a reaction still ringing, is reacting. Resolved
        // here rather than in the behaviour that raised it, because whether there was anything
        // else to do is only known once every behaviour has had its turn.
        if (entity.state_.activity == Activity::Idle && entity.state_.reaction > 0.4f) {
            entity.state_.activity = Activity::React;
        }
        // ---- the gait (§7) ----
        // The behaviour said how fast it is going; the gait says what that looks like. Separated
        // because the two answers have different requirements: the speed must be exactly what the
        // navigation produced, and the *clip* must not change four times a second because the
        // speed is sitting on a threshold. Hysteresis lives here and nowhere else, so every
        // behaviour and every action gets it without asking.
        const Activity gait =
            entity.gait_.select(entity.desc_.gait, entity.state_.activity, entity.state_.speed,
                                entity.state_.turnRate, dt);
        entity.locomotion_.activity = gait;
        entity.locomotion_.playbackRate = Gait::playbackRate(entity.desc_.gait, gait, entity.state_.speed);
        entity.locomotion_.blend = entity.desc_.gait.blend;
        // What an action asked to be played, if it asked for anything. Assigned rather than
        // rebuilt so a steady state reuses the string's capacity.
        if (entity.locomotion_.action != intent.activity) {
            entity.locomotion_.action.assign(intent.activity);
        }
        entity.locomotion_.time = ctx.time;
        entity.locomotion_.position = entity.state_.position() + entity.motion_.position;
        entity.locomotion_.yaw = entity.state_.yaw;
        entity.locomotion_.speed = entity.state_.speed;
        entity.locomotion_.turnRate = entity.state_.turnRate;
        entity.locomotion_.reaction = entity.state_.reaction;
        entity.locomotion_.lookTarget = entity.state_.lookTarget;
        entity.locomotion_.hasLookTarget = entity.state_.hasLookTarget;
        if (entity.pose_ != nullptr) {
            entity.pose_->setLocomotion(entity.locomotion_);
        }

        applyAttachments(entity, params);
    }
    if (actionListener_) {
        for (const ActionEvent& event : actionEvents_) {
            actionListener_(event);
        }
    }
}

void EntityWorld::applyAttachments(const Entity& entity, params::ParameterSet& params) const {
    for (const AttachmentDesc& attachment : entity.attachments()) {
        scene::Transform t;
        if (!entity.socketTransform(attachment.socket, t)) {
            continue;
        }
        const NodeBinding* b = binding(attachment.node);
        if (b == nullptr || !b->exists) {
            continue;
        }
        if (auto* position = params.findAs<glm::vec3>(b->transformPrefix + "position")) {
            for (std::size_t c = 0; c < 3; ++c) {
                position->setFinalComponent(c, t.position[static_cast<int>(c)]);
            }
        }
        if (auto* rotation = params.findAs<glm::vec3>(b->transformPrefix + "rotation")) {
            const glm::vec3 euler = eulerDegrees(t.rotation);
            for (std::size_t c = 0; c < 3; ++c) {
                rotation->setFinalComponent(c, euler[static_cast<int>(c)]);
            }
        }
    }
}

// ---- serialisation ---------------------------------------------------------------------------

Result<EntityDesc> entityFromJson(const nlohmann::json& j, const std::filesystem::path& baseDir) {
    if (!j.is_object()) {
        return fail("entity must be an object");
    }
    if (!j.contains("name") || !j["name"].is_string() || j["name"].get<std::string>().empty()) {
        return fail("entity: 'name' is required and must be a non-empty string");
    }
    EntityDesc desc;
    desc.name = j["name"].get<std::string>();
    // A profile first, so the entity's own blocks append to it rather than replacing it: a scene
    // takes "a hovering craft that answers the music" and then adds the one reaction that is about
    // this craft in this shot.
    if (j.contains("profile") && j["profile"].is_string()) {
        desc.profile = j["profile"].get<std::string>();
        const std::filesystem::path path =
            baseDir.empty() ? std::filesystem::path(desc.profile) : baseDir / desc.profile;
        auto loaded = loadProfile(path);
        if (!loaded) {
            return fail("entity '{}': {}", desc.name, loaded.error().message);
        }
        desc.behaviors = std::move(loaded->behaviors);
        desc.reactions = std::move(loaded->reactions);
        desc.clips = std::move(loaded->clips);
        desc.sockets = std::move(loaded->sockets);
        desc.profileBehaviors = desc.behaviors.size();
        desc.profileReactions = desc.reactions.size();
        desc.profileClips = desc.clips.size();
        desc.profileSockets = desc.sockets.size();
    }
    if (j.contains("node") && j["node"].is_string()) {
        desc.node = j["node"].get<std::string>();
    }
    if (j.contains("seed") && j["seed"].is_number_unsigned()) {
        desc.seed = j["seed"].get<std::uint32_t>();
    }
    if (j.contains("fullDetailDistance") && j["fullDetailDistance"].is_number()) {
        desc.fullDetailDistance = j["fullDetailDistance"].get<float>();
    }
    if (j.contains("coarseInterval") && j["coarseInterval"].is_number()) {
        desc.coarseInterval = std::max(0.0f, j["coarseInterval"].get<float>());
    }
    if (j.contains("cullDistance") && j["cullDistance"].is_number()) {
        desc.cullDistance = j["cullDistance"].get<float>();
    }
    if (j.contains("behaviors") && j["behaviors"].is_array()) {
        for (const auto& item : j["behaviors"]) {
            if (!item.is_object() || !item.contains("kind") || !item["kind"].is_string()) {
                return fail("entity '{}': every behaviour needs a string 'kind'", desc.name);
            }
            BehaviorDesc behavior;
            behavior.kind = item["kind"].get<std::string>();
            behavior.name = item.contains("name") && item["name"].is_string()
                                ? item["name"].get<std::string>()
                                : behavior.kind;
            behavior.settings = item;
            desc.behaviors.push_back(std::move(behavior));
        }
    }
    if (j.contains("reactions") && j["reactions"].is_array()) {
        for (const auto& item : j["reactions"]) {
            if (!item.is_object()) {
                return fail("entity '{}': every reaction must be an object", desc.name);
            }
            ReactionDesc reaction;
            if (!item.contains("signal") || !item["signal"].is_string()) {
                return fail("entity '{}': a reaction needs a string 'signal'", desc.name);
            }
            if (!item.contains("target") || !item["target"].is_string()) {
                return fail("entity '{}': a reaction needs a string 'target'", desc.name);
            }
            reaction.signal = item["signal"].get<std::string>();
            reaction.target = item["target"].get<std::string>();
            if (item.contains("component") && item["component"].is_number_integer()) {
                reaction.component = item["component"].get<int>();
            }
            if (item.contains("depth") && item["depth"].is_number()) {
                reaction.depth = item["depth"].get<float>();
            }
            if (item.contains("enabled") && item["enabled"].is_boolean()) {
                reaction.enabled = item["enabled"].get<bool>();
            }
            if (item.contains("op") && item["op"].is_string()) {
                const auto op = params::modOpFromName(item["op"].get<std::string>());
                if (!op) {
                    return fail("entity '{}': unknown reaction op '{}'", desc.name,
                                item["op"].get<std::string>());
                }
                reaction.op = *op;
            }
            if (item.contains("polarity") && item["polarity"].is_string()) {
                reaction.polarity = item["polarity"].get<std::string>() == "bipolar"
                                        ? params::Polarity::Bipolar
                                        : params::Polarity::Unipolar;
            }
            if (item.contains("chain")) {
                auto chain = params::chainFromJson(item["chain"]);
                if (!chain) {
                    return fail("entity '{}': reaction chain: {}", desc.name, chain.error().message);
                }
                reaction.chain = *chain;
            }
            desc.reactions.push_back(std::move(reaction));
        }
    }
    if (j.contains("clips") && j["clips"].is_object()) {
        for (const auto& [activity, state] : j["clips"].items()) {
            if (!state.is_string()) {
                return fail("entity '{}': clip for '{}' must be a string", desc.name, activity);
            }
            desc.clips.emplace_back(activity, state.get<std::string>());
        }
    }
    if (j.contains("sockets") && j["sockets"].is_array()) {
        for (const auto& item : j["sockets"]) {
            if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) {
                return fail("entity '{}': every socket needs a string 'name'", desc.name);
            }
            SocketDesc socket;
            socket.name = item["name"].get<std::string>();
            if (item.contains("joint") && item["joint"].is_string()) {
                socket.joint = item["joint"].get<std::string>();
            }
            socket.offset.position = readVec3(item, "position", glm::vec3(0.0f));
            socket.offset.rotation = quatFromEulerDegrees(readVec3(item, "rotation", glm::vec3(0.0f)));
            socket.offset.scale = readVec3(item, "scale", glm::vec3(1.0f));
            desc.sockets.push_back(std::move(socket));
        }
    }
    if (j.contains("attachments") && j["attachments"].is_array()) {
        for (const auto& item : j["attachments"]) {
            if (!item.is_object() || !item.contains("node") || !item["node"].is_string() ||
                !item.contains("socket") || !item["socket"].is_string()) {
                return fail("entity '{}': every attachment needs a 'node' and a 'socket'", desc.name);
            }
            desc.attachments.push_back(
                AttachmentDesc{item["node"].get<std::string>(), item["socket"].get<std::string>()});
        }
    }
    // ---- intent (ADR-096) ----
    if (j.contains("properties")) {
        const nlohmann::json& properties = j["properties"];
        if (properties.is_object()) {
            // The readable spelling: { "headphones": 0, "awake": 1 }. A 0..1 flag is what almost
            // every state property is, so it is the shape that needs no ceremony.
            for (const auto& [name, value] : properties.items()) {
                if (!value.is_number()) {
                    return fail("entity '{}': property '{}' must be a number", desc.name, name);
                }
                desc.properties.push_back(PropertyDesc{name, value.get<float>(), 0.0f, 1.0f});
            }
        } else if (properties.is_array()) {
            for (const auto& item : properties) {
                if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) {
                    return fail("entity '{}': every property needs a string 'name'", desc.name);
                }
                PropertyDesc property;
                property.name = item["name"].get<std::string>();
                property.value = item.contains("value") && item["value"].is_number()
                                     ? item["value"].get<float>() : 0.0f;
                property.min = item.contains("min") && item["min"].is_number()
                                   ? item["min"].get<float>() : 0.0f;
                property.max = item.contains("max") && item["max"].is_number()
                                   ? item["max"].get<float>() : 1.0f;
                desc.properties.push_back(std::move(property));
            }
        } else {
            return fail("entity '{}': 'properties' must be an object or an array", desc.name);
        }
    }
    if (j.contains("interactions")) {
        if (!j["interactions"].is_array()) {
            return fail("entity '{}': 'interactions' must be an array", desc.name);
        }
        for (const auto& item : j["interactions"]) {
            auto interaction = interactionFromJson(item);
            if (!interaction) {
                return fail("entity '{}': {}", desc.name, interaction.error().message);
            }
            desc.interactions.push_back(std::move(*interaction));
        }
    }
    if (j.contains("actions")) {
        auto actions = actionsFromJson(j["actions"]);
        if (!actions) {
            return fail("entity '{}': {}", desc.name, actions.error().message);
        }
        desc.actions = std::move(*actions);
    }
    if (j.contains("schedule")) {
        auto schedule = scheduleFromJson(j["schedule"]);
        if (!schedule) {
            return fail("entity '{}': {}", desc.name, schedule.error().message);
        }
        desc.schedule = std::move(*schedule);
    }
    if (j.contains("gait")) {
        auto gait = gaitFromJson(j["gait"]);
        if (!gait) {
            return fail("entity '{}': {}", desc.name, gait.error().message);
        }
        desc.gait = *gait;
    }
    return desc;
}

Result<EntityDesc> profileFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("an entity profile must be an object");
    }
    if (j.contains("format") && j["format"].is_string() &&
        j["format"].get<std::string>() != "avgen-entity-profile") {
        return fail("not an entity profile: format '{}'", j["format"].get<std::string>());
    }
    nlohmann::json copy = j;
    copy["name"] = "profile";
    copy.erase("profile"); // profiles do not chain: one level of indirection is a library, two is a maze
    return entityFromJson(copy);
}

Result<EntityDesc> loadProfile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return fail("cannot open entity profile '{}'", path.string());
    }
    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        return fail("entity profile '{}': {}", path.string(), e.what());
    }
    auto desc = profileFromJson(j);
    if (!desc) {
        return fail("entity profile '{}': {}", path.string(), desc.error().message);
    }
    return desc;
}

Result<std::vector<EntityDesc>> entitiesFromJson(const nlohmann::json& j,
                                                 const std::filesystem::path& baseDir) {
    if (!j.is_array()) {
        return fail("'entities' must be an array");
    }
    std::vector<EntityDesc> out;
    out.reserve(j.size());
    for (const auto& item : j) {
        auto entity = entityFromJson(item, baseDir);
        if (!entity) {
            return fail("{}", entity.error().message);
        }
        const std::string& name = entity->name;
        if (std::any_of(out.begin(), out.end(), [&](const EntityDesc& d) { return d.name == name; })) {
            return fail("entity '{}' is declared twice", name);
        }
        out.push_back(std::move(*entity));
    }
    return out;
}

nlohmann::json entityToJson(const EntityDesc& entity) {
    nlohmann::json j = nlohmann::json::object();
    j["name"] = entity.name;
    if (!entity.profile.empty()) {
        j["profile"] = entity.profile;
    }
    if (!entity.node.empty()) {
        j["node"] = entity.node;
    }
    if (entity.seed != 0) {
        j["seed"] = entity.seed;
    }
    if (entity.fullDetailDistance > 0.0f) {
        j["fullDetailDistance"] = entity.fullDetailDistance;
        j["coarseInterval"] = entity.coarseInterval;
    }
    if (entity.cullDistance > 0.0f) {
        j["cullDistance"] = entity.cullDistance;
    }
    if (entity.behaviors.size() > entity.profileBehaviors) {
        nlohmann::json behaviors = nlohmann::json::array();
        for (const BehaviorDesc& behavior :
             std::span(entity.behaviors).subspan(entity.profileBehaviors)) {
            // The settings object is the authored form and already carries `kind` and `name`.
            behaviors.push_back(behavior.settings.is_object() ? behavior.settings
                                                              : nlohmann::json{{"kind", behavior.kind}});
        }
        j["behaviors"] = std::move(behaviors);
    }
    if (entity.reactions.size() > entity.profileReactions) {
        nlohmann::json reactions = nlohmann::json::array();
        for (const ReactionDesc& reaction :
             std::span(entity.reactions).subspan(entity.profileReactions)) {
            nlohmann::json r = nlohmann::json::object();
            r["signal"] = reaction.signal;
            r["target"] = reaction.target;
            r["depth"] = reaction.depth;
            r["op"] = params::modOpName(reaction.op);
            r["polarity"] = reaction.polarity == params::Polarity::Bipolar ? "bipolar" : "unipolar";
            if (reaction.component >= 0) {
                r["component"] = reaction.component;
            }
            if (!reaction.enabled) {
                r["enabled"] = false;
            }
            r["chain"] = params::chainToJson(reaction.chain);
            reactions.push_back(std::move(r));
        }
        j["reactions"] = std::move(reactions);
    }
    if (entity.clips.size() > entity.profileClips) {
        nlohmann::json clips = nlohmann::json::object();
        for (const auto& [activity, state] : std::span(entity.clips).subspan(entity.profileClips)) {
            clips[activity] = state;
        }
        j["clips"] = std::move(clips);
    }
    if (entity.sockets.size() > entity.profileSockets) {
        nlohmann::json sockets = nlohmann::json::array();
        for (const SocketDesc& socket : std::span(entity.sockets).subspan(entity.profileSockets)) {
            nlohmann::json s = nlohmann::json::object();
            s["name"] = socket.name;
            if (!socket.joint.empty()) {
                s["joint"] = socket.joint;
            }
            const glm::vec3 euler = eulerDegrees(socket.offset.rotation);
            s["position"] = {socket.offset.position.x, socket.offset.position.y, socket.offset.position.z};
            s["rotation"] = {euler.x, euler.y, euler.z};
            s["scale"] = {socket.offset.scale.x, socket.offset.scale.y, socket.offset.scale.z};
            sockets.push_back(std::move(s));
        }
        j["sockets"] = std::move(sockets);
    }
    if (!entity.attachments.empty()) {
        nlohmann::json attachments = nlohmann::json::array();
        for (const AttachmentDesc& attachment : entity.attachments) {
            attachments.push_back(nlohmann::json{{"node", attachment.node}, {"socket", attachment.socket}});
        }
        j["attachments"] = std::move(attachments);
    }
    if (!entity.properties.empty()) {
        const bool simple = std::all_of(entity.properties.begin(), entity.properties.end(),
                                        [](const PropertyDesc& p) { return p.min == 0.0f && p.max == 1.0f; });
        if (simple) {
            nlohmann::json properties = nlohmann::json::object();
            for (const PropertyDesc& property : entity.properties) {
                properties[property.name] = property.value;
            }
            j["properties"] = std::move(properties);
        } else {
            nlohmann::json properties = nlohmann::json::array();
            for (const PropertyDesc& property : entity.properties) {
                properties.push_back(nlohmann::json{{"name", property.name},
                                                    {"value", property.value},
                                                    {"min", property.min},
                                                    {"max", property.max}});
            }
            j["properties"] = std::move(properties);
        }
    }
    if (!entity.interactions.empty()) {
        nlohmann::json interactions = nlohmann::json::array();
        for (const InteractionDesc& interaction : entity.interactions) {
            interactions.push_back(interactionToJson(interaction));
        }
        j["interactions"] = std::move(interactions);
    }
    if (!entity.actions.empty()) {
        j["actions"] = actionsToJson(entity.actions);
    }
    if (!entity.schedule.entries.empty()) {
        j["schedule"] = scheduleToJson(entity.schedule);
    }
    if (!(entity.gait == GaitSettings{})) {
        j["gait"] = gaitToJson(entity.gait);
    }
    return j;
}

nlohmann::json entitiesToJson(const std::vector<EntityDesc>& entities) {
    nlohmann::json j = nlohmann::json::array();
    for (const EntityDesc& entity : entities) {
        j.push_back(entityToJson(entity));
    }
    return j;
}

} // namespace avgen::entity
