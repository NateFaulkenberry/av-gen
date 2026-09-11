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
      rng_(seed_) {}

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
        entities_.push_back(std::move(entity));
    }
    // The per-entity field state is sized per field, so a new entity set needs a new binding pass.
    fieldsBound_ = false;
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
    counts_ = {};
    fields_.clear();
    fieldRuntime_.clear();
    fieldReport_.clear();
    triggerEvents_.clear();
    gridPoints_.clear();
    gridEntity_.clear();
    grid_.clear();
    fieldCounts_ = FieldCounts{};
    fieldsBound_ = false;
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
    fieldParams_.assign(fields_.size(), FieldParams{});
    for (std::size_t f = 0; f < fields_.size(); ++f) {
        const FieldDesc& field = fields_[f];
        const std::string base = prefix + "fields/" + field.name + "/";
        const auto add = [&](const char* name, float value, float lo, float hi) {
            registered_.push_back(base + name);
            return &params.add(params::ParamDesc<float>{
                .path = base + name, .defaultValue = value, .hardMin = lo, .hardMax = hi});
        };
        FieldParams& p = fieldParams_[f];
        p.strength = add("strength", field.strength, 0.0f, 16.0f);
        p.scale = add("scale", 1.0f, 0.0f, 64.0f);
        p.inner = add("inner", field.volume.inner, 0.0f, 0.999f);
        p.floorGain = add("floorGain", field.floorGain, 0.0f, 16.0f);
        registered_.push_back(base + "center");
        p.center = &params.add(params::ParamDesc<glm::vec3>{.path = base + "center",
                                                            .defaultValue = field.volume.center,
                                                            .hardMin = glm::vec3(-1e5f),
                                                            .hardMax = glm::vec3(1e5f)});
    }
    for (auto& entity : entities_) {
        for (std::size_t i = 0; i < entity->behaviors_.size(); ++i) {
            const BehaviorDesc& desc = entity->desc_.behaviors[i];
            const std::string name = desc.name.empty() ? desc.kind : desc.name;
            const std::string base = prefix + entity->name() + "/" + name + "/";
            entity->behaviors_[i]->registerParameters(params, base);
            entity->behaviors_[i]->collectParameterPaths(registered_);
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
    fieldParams_.assign(fields_.size(), FieldParams{});
    for (auto& entity : entities_) {
        entity->positionParam_ = nullptr;
        entity->rotationParam_ = nullptr;
        entity->scaleParam_ = nullptr;
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
    for (std::size_t index = 0; index < entities_.size(); ++index) {
        const auto& entity = entities_[index];
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
            // ADR-097. Which entity owns this route is what lets a field scale *its* reactions
            // rather than every reaction in the scene, and it is recorded here because this is the
            // only place that knows. A reaction the author marked `"spatial": false` keeps
            // kNoOwner and is therefore never touched by a field -- the escape hatch a Multiply
            // route whose neutral output is 1 has to have.
            route.ownerEntity = reaction.spatial ? static_cast<std::uint32_t>(index)
                                                 : params::ModRoute::kNoOwner;
            routes.push_back(std::move(route));
        }
    }
    return routes;
}

// Whether this entity has anything that needs the node it names. Behaviours move a transform,
// sockets and attachments hang off one, and a relative reaction target is resolved against one; a
// reaction written as an absolute path needs none of it.
bool EntityWorld::needsNode(const Entity& entity) {
    const EntityDesc& desc = entity.desc();
    if (!desc.behaviors.empty() || !desc.sockets.empty() || !desc.attachments.empty()) {
        return true;
    }
    if (desc.reactions.empty()) {
        return true; // an entity that does nothing at all: the missing node is still the reason
    }
    return std::any_of(desc.reactions.begin(), desc.reactions.end(), [](const ReactionDesc& r) {
        return r.target.empty() || r.target.front() != '@';
    });
}

void EntityWorld::bind(params::ParameterSet& params, const std::string& prefix) {
    prefix_ = prefix;
    params_ = &params;
    for (auto& entity : entities_) {
        const NodeBinding* b = binding(entity->desc_.driven());
        if (b == nullptr || !b->exists) {
            // An entity that drives a node the scene does not have is the exact failure this
            // project keeps shipping in silence. Say it once, here, with the name -- *unless* this
            // entity has no use for a node at all. An entity whose reactions are every one of them
            // absolute (`@lightrig/key/fill/intensity`, `@post/bloom/amount`) and which has no
            // behaviours, sockets or attachments is how a light, the grade or a particle system
            // gains reactions (§39-§43), and warning about the node it does not drive would train
            // the reader to ignore the warning that matters.
            if (needsNode(*entity)) {
                const std::string message =
                    fmt::format("entity '{}' drives node '{}', which this scene has no node for",
                                entity->name(), entity->desc_.driven());
                if (std::find(problems_.begin(), problems_.end(), message) == problems_.end()) {
                    problems_.push_back(message);
                    log::warn("{}", message);
                }
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
    }
    // After the anchors, because a field's source may be an entity and an entity's position is its
    // anchor until something moves it.
    bindFields();
}

void EntityWorld::reset() {
    for (auto& entity : entities_) {
        entity->rng_ = Rng(entity->seed_);
        entity->state_ = EntityState{};
        entity->motion_ = MotionOffset{};
        entity->coarseAccum_ = 0.0;
        // Field state is live-tier state and it resets with the rest of it (ADR-091). An arc that
        // was half-way through when the playhead jumped is gone, and the entity is outside every
        // field until the next pass says otherwise -- which is the same honest answer a behaviour
        // gives, said out loud rather than approximated.
        entity->fieldAccum_ = 0.0;
        entity->arcPhase_ = Entity::ArcPhase::None;
        entity->arcField_ = Entity::kNoField;
        entity->arcLevel_ = 0.0f;
        entity->arcTimer_ = 0.0;
        entity->arcCooldownUntil_ = -1.0e30;
        entity->influence_ = entity->governed_ ? entity->fieldFloor_ : 1.0f;
        std::fill(entity->insideField_.begin(), entity->insideField_.end(), 0);
        std::fill(entity->insideNow_.begin(), entity->insideNow_.end(), 0);
        std::fill(entity->entryCount_.begin(), entity->entryCount_.end(), 0u);
        if (const NodeBinding* b = binding(entity->desc_.driven())) {
            entity->state_.anchor = b->anchor;
        }
        for (auto& behavior : entity->behaviors_) {
            behavior->reset(entity->rng_);
        }
    }
    triggerEvents_.clear();
    fieldCounts_ = FieldCounts{};
}

void EntityWorld::update(const EntityUpdate& ctx, params::ParameterSet& params) {
    if (!parametersLive_) {
        return;
    }
    params_ = &params;
    counts_ = {};
    for (auto& entityPtr : entities_) {
        Entity& entity = *entityPtr;
        const glm::vec3 here = entity.state_.position();
        const float distance = glm::length(here - ctx.viewPosition);

        if (entity.desc_.cullDistance > 0.0f && distance > entity.desc_.cullDistance) {
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

        const glm::vec3 travelBefore = entity.state_.travel;
        entity.motion_ = MotionOffset{};
        entity.state_.hasLookTarget = false;
        entity.state_.reaction = 0.0f;
        entity.state_.activity = Activity::Idle;

        // §21, the *entry* half of the arc, and it goes here rather than after the behaviours
        // because the behaviours already know what to do with it. `wander` reads React and yields
        // -- keeping its destination and its pause timer, and resuming from where it stopped --
        // which is the addendum's `Walking -> Dance -> Walking` contract, already written, already
        // tested, and belonging to the behaviour rather than to the thing that interrupted it. A
        // field that set the activity afterwards would be a second mechanism for the same idea.
        if (entity.arcLevel_ > 0.0f) {
            entity.state_.reaction = entity.arcLevel_;
            entity.state_.activity = entity.arcActivity_;
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

        // §21, the return half of the arc. The arc *overrides* an activity while it runs and then
        // stops overriding it; it never writes Idle, never resets a behaviour and never clears a
        // destination. So what comes back when it ends is whatever the behaviours are doing at
        // that moment -- `Walking -> Dance -> Walking`, structurally, rather than by remembering
        // to put something back (addendum §14).
        if (entity.arcLevel_ > 0.0f) {
            // And re-asserted, because a behaviour that ran after the entry write may have decided
            // something else -- `interest` raises Observe on a loud frame, and the arc outranks it
            // for as long as it lasts.
            entity.state_.reaction = std::max(entity.state_.reaction, entity.arcLevel_);
            entity.state_.activity = entity.arcActivity_;
            if (entity.arcHoldStill_) {
                // Restored, not zeroed at the source: the behaviour that was walking keeps its
                // destination and its progress toward it, and picks the walk up where it left it.
                entity.state_.travel = travelBefore;
                entity.state_.speed = 0.0f;
                entity.state_.turnRate = 0.0f;
            }
        }

        // A character doing nothing else, with a reaction still ringing, is reacting. Resolved
        // here rather than in the behaviour that raised it, because whether there was anything
        // else to do is only known once every behaviour has had its turn.
        if (entity.state_.activity == Activity::Idle && entity.state_.reaction > 0.4f) {
            entity.state_.activity = Activity::React;
        }
        entity.locomotion_.activity = entity.state_.activity;
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
}

void EntityWorld::applyAttachments(const Entity& entity, params::ParameterSet& params) const {
    for (const AttachmentDesc& attachment : entity.desc_.attachments) {
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


// ---- fields (ADR-097) --------------------------------------------------------------------------
//
// The field pass is a separate pass from the behaviour pass, and the separation is not cosmetic.
// A behaviour's output is an offset folded onto a node *after* modulation; a field's output is a
// gain on modulation itself, so it has to be settled *before* the routes run. Doing it inside
// update() would put every reaction in the scene one frame behind its field, which is invisible
// while something is moving slowly and is a different picture the moment anybody scrubs.

glm::vec3 Entity::fieldPosition() const {
    glm::vec3 p = state_.position() + motion_.position;
    if (positionParam_ != nullptr) {
        // Whatever the timeline (or a route that already ran) has moved the node by, relative to
        // where the scene put it. For a behaviour-driven entity this is zero at the moment the
        // field pass runs and the anchor plus travel is the whole answer. For a node a sequencer
        // actor drives it is the entire motion -- which is what makes a field over a baked actor a
        // pure function of time, and therefore scrub-safe and offline-exact (ADR-091).
        for (std::size_t c = 0; c < 3; ++c) {
            p[static_cast<int>(c)] += positionParam_->finalComponent(c) - positionParam_->baseComponent(c);
        }
    }
    return p;
}

void EntityWorld::setFields(std::vector<FieldDesc> fields) {
    fields_ = std::move(fields);
    fieldParams_.assign(fields_.size(), FieldParams{});
    fieldsBound_ = false;
}

// The authored field with its live knobs folded in: strength, scale, inner ratio, floor and centre
// as the timeline and the presets currently have them. Read from the parameter *finals*, which at
// the moment the field pass runs are exactly base + automation -- the frame's resetFinals() has
// wiped the previous frame's routes and this frame's have not run yet. That is what keeps a field
// over a baked source a pure function of time (ADR-091), and it is also why a modulation route
// pointed at a field knob does nothing; the host says so by name.
FieldDesc EntityWorld::resolvedField(std::size_t index) const {
    FieldDesc field = fields_[index];
    if (index >= fieldParams_.size()) {
        return field;
    }
    const FieldParams& p = fieldParams_[index];
    if (p.strength != nullptr) {
        field.strength = p.strength->finalComponent(0);
    }
    if (p.inner != nullptr) {
        field.volume.inner = std::clamp(p.inner->finalComponent(0), 0.0f, 0.999f);
    }
    if (p.floorGain != nullptr) {
        field.floorGain = p.floorGain->finalComponent(0);
    }
    if (p.center != nullptr) {
        for (std::size_t c = 0; c < 3; ++c) {
            field.volume.center[static_cast<int>(c)] = p.center->finalComponent(c);
        }
    }
    if (p.scale != nullptr) {
        // One knob for all three shapes, because "make the field bigger" is one intention and a
        // sphere's radius, a box's half-extents and a capsule's length are three spellings of it.
        const float scale = std::max(p.scale->finalComponent(0), 0.0f);
        field.volume.radius *= scale;
        field.volume.height *= scale;
        field.volume.halfExtents *= scale;
    }
    return field;
}

bool EntityWorld::fieldMatches(const FieldDesc& field, const Entity& entity) const {
    if (field.tags.empty()) {
        return true; // a field with no filter governs everything in the scene
    }
    for (const std::string& tag : field.tags) {
        if (std::find(entity.desc().tags.begin(), entity.desc().tags.end(), tag) !=
            entity.desc().tags.end()) {
            return true;
        }
        // The profile an entity was built from is a tag it did not have to be given. "Every NPC
        // built from the dancer profile" is then one word in a scene file rather than a list of
        // forty names that goes stale the day somebody adds the forty-first.
        if (!entity.desc().profile.empty() && entity.desc().profile == tag) {
            return true;
        }
    }
    return false;
}

bool EntityWorld::resolveFieldSource(const FieldDesc& field, glm::vec3& out,
                                     FieldAuthority& authority) const {
    if (field.source.empty()) {
        out = field.volume.center;
        authority = FieldAuthority::Static;
        return true;
    }
    // `volume.center` is an offset once a source is named: "twelve metres above the dancer".
    if (const Entity* e = find(field.source)) {
        out = e->fieldPosition() + field.volume.center;
        // ADR-091's rule, applied literally: an entity that integrates is the live tier.
        authority = e->behaviors().empty() ? FieldAuthority::Baked : FieldAuthority::Live;
        return true;
    }
    if (const NodeBinding* b = binding(field.source); b != nullptr && b->exists) {
        glm::vec3 p = b->anchor;
        if (params_ != nullptr) {
            if (const params::IParameter* pos = params_->find(b->transformPrefix + "position");
                pos != nullptr && pos->componentCount() >= 3) {
                for (std::size_t c = 0; c < 3; ++c) {
                    p[static_cast<int>(c)] += pos->finalComponent(c) - pos->baseComponent(c);
                }
            }
        }
        out = p + field.volume.center;
        authority = FieldAuthority::Baked;
        for (const auto& e : entities_) {
            if (e->desc().driven() == field.source && !e->behaviors().empty()) {
                authority = FieldAuthority::Live;
                break;
            }
        }
        return true;
    }
    glm::vec3 landmark(0.0f);
    if (pointOfInterest(field.source, landmark)) {
        out = landmark + field.volume.center;
        authority = FieldAuthority::Static; // a hero does not move
        return true;
    }
    out = field.volume.center;
    authority = FieldAuthority::Static;
    return false;
}

void EntityWorld::bindFields() {
    fieldRuntime_.assign(fields_.size(), FieldRuntime{});
    fieldReport_.clear();
    const std::size_t n = fields_.size();
    for (auto& entityPtr : entities_) {
        Entity& e = *entityPtr;
        e.insideField_.assign(n, 0);
        e.insideNow_.assign(n, 0);
        e.entryCount_.assign(n, 0);
        e.governedBy_.assign(n, 0);
        e.governed_ = false;
        e.fieldFloor_ = 0.0f;
        e.influence_ = 1.0f;
        e.arcPhase_ = Entity::ArcPhase::None;
        e.arcField_ = Entity::kNoField;
        e.arcLevel_ = 0.0f;
        e.arcTimer_ = 0.0;
        e.arcCooldownUntil_ = -1.0e30;
        e.fieldAccum_ = 0.0;
    }
    for (std::size_t f = 0; f < n; ++f) {
        const FieldDesc field = resolvedField(f);
        FieldRuntime& rt = fieldRuntime_[f];
        rt.sourceResolved = resolveFieldSource(field, rt.center, rt.authority);
        std::size_t governed = 0;
        for (auto& entityPtr : entities_) {
            Entity& e = *entityPtr;
            if (!field.enabled || !fieldMatches(field, e)) {
                continue;
            }
            e.governedBy_[f] = 1;
            ++governed;
            if (field.scaleReactions) {
                // Only a field that *scales* makes an entity's gain start from zero. A pure
                // trigger volume fires arcs and publishes signals without touching modulation, and
                // an entity governed by nothing else keeps the gain of exactly 1 it had before
                // fields existed -- which is what makes this change invisible to every scene that
                // does not use it.
                e.governed_ = true;
                e.fieldFloor_ = std::max(e.fieldFloor_, field.floorGain);
            }
        }
        rt.governed = governed;
        if (!rt.sourceResolved) {
            const std::string message =
                fmt::format("field '{}': source '{}' names no entity, node or hero in this scene; "
                            "the field stays at its authored centre",
                            field.name, field.source);
            if (std::find(problems_.begin(), problems_.end(), message) == problems_.end()) {
                problems_.push_back(message);
                log::warn("{}", message);
            }
        }
        // ADR-091 made visible. One line per field saying what it followed and which guarantee it
        // therefore has, logged at install and readable in the editor -- because the difference
        // between a scrub-exact field and one that is not is a thing an author has to be able to
        // read, not one they discover by rendering the same frame twice and diffing it.
        std::string line = fmt::format(
            "field '{}' ({} reach {:.1f}, falloff {}, strength {:.2f}) {} -> {}: {} entit{} governed",
            field.name, volumeShapeName(field.volume.shape), field.volume.reach(),
            falloffName(field.volume.falloff), field.strength,
            field.source.empty() ? std::string("fixed at its authored centre")
                                 : fmt::format("follows '{}'", field.source),
            fieldAuthorityName(rt.authority), governed, governed == 1 ? "y" : "ies");
        if (rt.authority == FieldAuthority::Live) {
            line += "; live source: this field's influence is NOT scrub-exact (ADR-091)";
            if (field.requireScrubExact) {
                const std::string message = fmt::format(
                    "field '{}' declares requireScrubExact but follows '{}', which is a live "
                    "entity: its influence depends on how the playhead got here (ADR-091)",
                    field.name, field.source);
                if (std::find(problems_.begin(), problems_.end(), message) == problems_.end()) {
                    problems_.push_back(message);
                    log::warn("{}", message);
                }
            }
        }
        fieldReport_.push_back(std::move(line));
    }
    // A governed entity starts at its floor rather than at 1. Otherwise an entity far enough away
    // to be on the coarse interval would read as fully inside every field until its first query,
    // which is a crowd that lights up before the camera gets to it.
    for (auto& entityPtr : entities_) {
        entityPtr->influence_ = entityPtr->governed_ ? entityPtr->fieldFloor_ : 1.0f;
    }
    fieldsBound_ = true;
}

void EntityWorld::beginArc(Entity& entity, std::uint32_t fieldIndex, double time) {
    const ReactionArc& arc = fields_[fieldIndex].arc;
    // A stream of this entity's own, for this field, for this entry. Never the behaviour stream:
    // that one advances once per frame per behaviour, so a draw taken from it would depend on the
    // frame rate and on what else the entity happens to be doing.
    Rng rng = arcStream(entity.seed(), fields_[fieldIndex].name, entity.entryCount_[fieldIndex]);
    const auto jitter = [&rng](float amount) { return amount * (rng.nextFloat() * 2.0f - 1.0f); };
    entity.arcDelay_ = std::max(0.0f, arc.delaySeconds + jitter(arc.delayJitter));
    entity.arcHold_ = std::max(0.0f, arc.holdSeconds + jitter(arc.holdJitter));
    entity.arcIntensity_ =
        std::clamp(arc.intensity * (1.0f + jitter(arc.intensityJitter)), 0.0f, 8.0f);
    entity.arcRelease_ = std::max(0.0f, arc.releaseSeconds);
    entity.arcHoldStill_ = arc.holdStill;
    entity.arcActivity_ = Activity::React;
    if (!activityFromName(arc.activity, entity.arcActivity_)) {
        const std::string message =
            fmt::format("field '{}': arc activity '{}' is not one of idle, walk, run, turn, "
                        "observe, react; using react",
                        fields_[fieldIndex].name, arc.activity);
        if (std::find(problems_.begin(), problems_.end(), message) == problems_.end()) {
            problems_.push_back(message);
            log::warn("{}", message);
        }
    }
    entity.arcPhase_ = entity.arcDelay_ > 0.0f ? Entity::ArcPhase::Delay : Entity::ArcPhase::Hold;
    entity.arcTimer_ = 0.0;
    entity.arcLevel_ = 0.0f;
    entity.arcField_ = fieldIndex;
    // The whole arc's length is known now, so the refractory gap is too. Set here rather than when
    // the arc ends, so a re-entry during the arc cannot start a second one either.
    entity.arcCooldownUntil_ = time + static_cast<double>(entity.arcDelay_ + entity.arcHold_ +
                                                          entity.arcRelease_ + arc.refractorySeconds);
    // Settle the level on the frame the edge was found rather than on the one after it. The arc
    // pass runs before the behaviour pass, so an arc that began here has to be visible to the
    // behaviours *this* frame -- otherwise crossing a line produces one frame of the old activity
    // and the crowd is a frame late, which is the same defect as evaluating the field after the
    // routes, one layer down.
    advanceArc(entity, 0.0);
}

void EntityWorld::advanceArc(Entity& entity, double dt) {
    if (entity.arcPhase_ == Entity::ArcPhase::None) {
        entity.arcLevel_ = 0.0f;
        return;
    }
    double remaining = std::max(dt, 0.0);
    while (entity.arcPhase_ != Entity::ArcPhase::None) {
        double length = 0.0;
        switch (entity.arcPhase_) {
        case Entity::ArcPhase::Delay: length = static_cast<double>(entity.arcDelay_); break;
        case Entity::ArcPhase::Hold: length = static_cast<double>(entity.arcHold_); break;
        case Entity::ArcPhase::Release: length = static_cast<double>(entity.arcRelease_); break;
        case Entity::ArcPhase::None: break;
        }
        const double budget = length - entity.arcTimer_;
        if (remaining < budget) {
            entity.arcTimer_ += remaining;
            break;
        }
        remaining -= std::max(budget, 0.0);
        entity.arcTimer_ = 0.0;
        switch (entity.arcPhase_) {
        case Entity::ArcPhase::Delay: entity.arcPhase_ = Entity::ArcPhase::Hold; break;
        case Entity::ArcPhase::Hold: entity.arcPhase_ = Entity::ArcPhase::Release; break;
        case Entity::ArcPhase::Release:
        case Entity::ArcPhase::None:
            entity.arcPhase_ = Entity::ArcPhase::None;
            entity.arcField_ = Entity::kNoField;
            break;
        }
        // A zero-length phase is stepped through in the same tick rather than costing a frame.
        if (remaining <= 0.0 && entity.arcPhase_ != Entity::ArcPhase::None) {
            double next = 0.0;
            switch (entity.arcPhase_) {
            case Entity::ArcPhase::Delay: next = static_cast<double>(entity.arcDelay_); break;
            case Entity::ArcPhase::Hold: next = static_cast<double>(entity.arcHold_); break;
            case Entity::ArcPhase::Release: next = static_cast<double>(entity.arcRelease_); break;
            case Entity::ArcPhase::None: break;
            }
            if (next > 0.0) {
                break;
            }
        }
    }
    switch (entity.arcPhase_) {
    case Entity::ArcPhase::None:
    case Entity::ArcPhase::Delay:
        entity.arcLevel_ = 0.0f;
        break;
    case Entity::ArcPhase::Hold:
        entity.arcLevel_ = entity.arcIntensity_;
        break;
    case Entity::ArcPhase::Release: {
        const float t = entity.arcRelease_ > 0.0f
                            ? static_cast<float>(entity.arcTimer_) / entity.arcRelease_
                            : 1.0f;
        entity.arcLevel_ = entity.arcIntensity_ * std::clamp(1.0f - t, 0.0f, 1.0f);
        break;
    }
    }
}

void EntityWorld::updateFields(const FieldUpdate& ctx, params::ParameterSet& params) {
    if (!parametersLive_) {
        // The same guard update() has, for the same reason: between unregisterParameters() and the
        // next registerParameters() the entities' cached parameter pointers are stale, and a stale
        // pointer that is merely usually fine is the bug that surfaces on a scene swap.
        return;
    }
    params_ = &params;
    triggerEvents_.clear();
    fieldCounts_ = FieldCounts{};
    if (!fieldsBound_) {
        bindFields();
    }
    if (fields_.empty() || entities_.empty()) {
        return; // no fields: every gain stays exactly 1 and this pass costs one branch
    }

    // 1. Where each field is this frame, and its signals. `declare` is idempotent and takes a name
    //    at runtime, so a field publishes itself on the ordinary bus without the bus, the analysis
    //    or the modulator learning that fields exist (§39-§43).
    float maxReach = 0.0f;
    resolved_.clear();
    resolved_.reserve(fields_.size());
    for (std::size_t f = 0; f < fields_.size(); ++f) {
        resolved_.push_back(resolvedField(f));
        const FieldDesc& field = resolved_.back();
        FieldRuntime& rt = fieldRuntime_[f];
        rt.inside = 0;
        rt.maxInfluence = 0.0f;
        FieldAuthority authority = rt.authority;
        rt.sourceResolved = resolveFieldSource(field, rt.center, authority);
        rt.authority = authority;
        if (ctx.bus != nullptr && rt.occupancy == signals::kInvalidSignal) {
            rt.occupancy = ctx.bus->declare("field." + field.name + ".occupancy", 0.0f, 1.0f);
            rt.enterSignal = ctx.bus->declare("field." + field.name + ".enter", 0.0f, 1.0f, true);
            rt.exitSignal = ctx.bus->declare("field." + field.name + ".exit", 0.0f, 1.0f, true);
        }
        if (field.enabled) {
            maxReach = std::max(maxReach, field.volume.reach());
        }
    }

    // 2. Advance every arc, whatever the level of detail. An arc that stopped being ticked because
    //    its character walked out of full detail would never end, and the character would come
    //    back into view still dancing to a field it left a minute ago.
    for (auto& entityPtr : entities_) {
        advanceArc(*entityPtr, ctx.dt);
    }

    // 3. The broad phase. Only entities some field's filter actually matches go into the grid, and
    //    the same three-band level of detail the behaviour pass uses applies here: past
    //    `cullDistance` an entity is not queried at all and keeps the influence it had, and past
    //    `fullDetailDistance` it is queried on the coarse interval instead of every frame.
    gridPoints_.clear();
    gridEntity_.clear();
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        Entity& e = *entities_[i];
        const bool anyFilter = std::any_of(e.governedBy_.begin(), e.governedBy_.end(),
                                           [](std::uint8_t g) { return g != 0; });
        if (!anyFilter) {
            continue;
        }
        ++fieldCounts_.governed;
        const glm::vec3 here = e.fieldPosition();
        const float distance = glm::length(here - ctx.viewPosition);
        if (e.desc().cullDistance > 0.0f && distance > e.desc().cullDistance) {
            continue;
        }
        if (e.desc().fullDetailDistance > 0.0f && distance > e.desc().fullDetailDistance) {
            e.fieldAccum_ += ctx.dt;
            if (e.fieldAccum_ < static_cast<double>(e.desc().coarseInterval)) {
                continue;
            }
            e.fieldAccum_ = 0.0;
        } else {
            e.fieldAccum_ = 0.0;
        }
        std::fill(e.insideNow_.begin(), e.insideNow_.end(), 0);
        e.influence_ = e.governed_ ? e.fieldFloor_ : 1.0f;
        gridPoints_.push_back(here);
        gridEntity_.push_back(static_cast<std::uint32_t>(i));
    }
    fieldCounts_.queried = gridPoints_.size();
    if (gridPoints_.empty()) {
        return;
    }
    // One cell per field reach: a field's bounds then touch at most three cells on an axis, so a
    // query visits a bounded number of cells however large the crowd or the world is.
    grid_.build(gridPoints_, std::max(maxReach, 1.0f));
    fieldCounts_.cells = grid_.cellCount();

    // 4. Field-major exact test over the neighbourhood the grid returned. This is the loop the
    //    brief forbids doing as entities x volumes: the grid is what makes it entities + the few
    //    that are actually near something.
    for (std::size_t f = 0; f < fields_.size(); ++f) {
        const FieldDesc& field = resolved_[f];
        if (!field.enabled) {
            continue;
        }
        FieldRuntime& rt = fieldRuntime_[f];
        TriggerVolume volume = field.volume;
        volume.center = rt.center;
        glm::vec3 lo(0.0f);
        glm::vec3 hi(0.0f);
        volume.bounds(lo, hi);
        grid_.forEachNear(lo, hi, [&](std::uint32_t point) {
            const std::uint32_t index = gridEntity_[point];
            Entity& e = *entities_[index];
            if (e.governedBy_[f] == 0) {
                return;
            }
            ++fieldCounts_.tested;
            const float raw = volume.influenceAt(gridPoints_[point]);
            if (raw <= 0.0f) {
                return;
            }
            e.insideNow_[f] = 1;
            ++rt.inside;
            ++fieldCounts_.inside;
            rt.maxInfluence = std::max(rt.maxInfluence, raw);
            if (field.scaleReactions) {
                // §19. Max rather than sum: influence is a 0..1 statement about how far inside
                // something is, and two overlapping speakers do not make a listener more than
                // fully inside. A field that wants to be louder says so with `strength`.
                e.influence_ = std::max(e.influence_, raw * field.strength);
            }
        });
    }

    // 5. Edges, and the arcs they start.
    for (std::size_t p = 0; p < gridEntity_.size(); ++p) {
        const std::uint32_t index = gridEntity_[p];
        Entity& e = *entities_[index];
        for (std::size_t f = 0; f < fields_.size(); ++f) {
            if (e.governedBy_[f] == 0) {
                continue;
            }
            const bool now = e.insideNow_[f] != 0;
            const bool was = e.insideField_[f] != 0;
            if (now == was) {
                continue;
            }
            e.insideField_[f] = now ? 1 : 0;
            triggerEvents_.push_back(TriggerEvent{static_cast<std::uint32_t>(f), index, now,
                                                  ctx.time, now ? e.influence_ : 0.0f});
            if (!now) {
                continue;
            }
            const std::uint32_t entries = e.entryCount_[f];
            e.entryCount_[f] = entries + 1u;
            if (fields_[f].arc.enabled && e.arcPhase_ == Entity::ArcPhase::None &&
                ctx.time >= e.arcCooldownUntil_) {
                beginArc(e, static_cast<std::uint32_t>(f), ctx.time);
            }
        }
    }

    // 6. Publish. A field is a signal source like any other from here on: a light, a material or a
    //    particle system reacts to `field.<name>.occupancy` through an ordinary reaction, and none
    //    of them has to learn what a volume is.
    if (ctx.bus != nullptr) {
        enteredThisFrame_.assign(fields_.size(), 0);
        exitedThisFrame_.assign(fields_.size(), 0);
        for (const TriggerEvent& event : triggerEvents_) {
            (event.enter ? enteredThisFrame_ : exitedThisFrame_)[event.field] = 1;
        }
        for (std::size_t f = 0; f < fields_.size(); ++f) {
            const FieldRuntime& rt = fieldRuntime_[f];
            if (rt.occupancy != signals::kInvalidSignal) {
                const float denominator = static_cast<float>(std::max<std::size_t>(rt.governed, 1));
                ctx.bus->set(rt.occupancy, static_cast<float>(rt.inside) / denominator);
            }
            if (rt.enterSignal != signals::kInvalidSignal) {
                ctx.bus->setEvent(rt.enterSignal, enteredThisFrame_[f] != 0);
            }
            if (rt.exitSignal != signals::kInvalidSignal) {
                ctx.bus->setEvent(rt.exitSignal, exitedThisFrame_[f] != 0);
            }
        }
    }
}

void EntityWorld::applySpatialGain(std::vector<params::ModRoute>& routes) const {
    for (params::ModRoute& route : routes) {
        if (!route.fromEntity || route.ownerEntity >= entities_.size()) {
            continue;
        }
        route.spatialGain = entities_[route.ownerEntity]->influence_;
    }
}

// ---- serialisation ---------------------------------------------------------------------------

Result<EntityDesc> entityFromJson(const nlohmann::json& j, const std::filesystem::path& baseDir,
                                  const ProfileLibrary* library) {
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
        EntityDesc loaded;
        // The library first (ADR-097): a scene that named one has already paid to read it, and ten
        // NPCs sharing "dancer" should not be ten file opens of the same twenty lines. A name the
        // library does not have is still a path, so every scene written before libraries existed
        // resolves exactly as it did.
        if (const EntityDesc* fromLibrary = library != nullptr ? library->find(desc.profile) : nullptr) {
            loaded = *fromLibrary;
        } else {
            const std::filesystem::path path =
                baseDir.empty() ? std::filesystem::path(desc.profile) : baseDir / desc.profile;
            auto read = loadProfile(path);
            if (!read) {
                if (library != nullptr && !library->empty()) {
                    std::string known;
                    for (const std::string& name : library->names()) {
                        if (!known.empty()) {
                            known += ", ";
                        }
                        known += name;
                    }
                    return fail("entity '{}': profile '{}' is not in the library '{}' (which has: "
                                "{}) and is not a file either: {}",
                                desc.name, desc.profile, library->source(), known,
                                read.error().message);
                }
                return fail("entity '{}': {}", desc.name, read.error().message);
            }
            loaded = std::move(*read);
        }
        desc.behaviors = std::move(loaded.behaviors);
        desc.reactions = std::move(loaded.reactions);
        desc.clips = std::move(loaded.clips);
        desc.sockets = std::move(loaded.sockets);
        desc.tags = std::move(loaded.tags);
        desc.profileBehaviors = desc.behaviors.size();
        desc.profileReactions = desc.reactions.size();
        desc.profileClips = desc.clips.size();
        desc.profileSockets = desc.sockets.size();
        desc.profileTags = desc.tags.size();
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
    if (j.contains("tags")) {
        if (!j["tags"].is_array()) {
            return fail("entity '{}': 'tags' must be an array of strings", desc.name);
        }
        for (const auto& tag : j["tags"]) {
            if (!tag.is_string()) {
                return fail("entity '{}': 'tags' must be an array of strings", desc.name);
            }
            const std::string value = tag.get<std::string>();
            if (std::find(desc.tags.begin(), desc.tags.end(), value) == desc.tags.end()) {
                desc.tags.push_back(value);
            }
        }
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
            if (item.contains("spatial") && item["spatial"].is_boolean()) {
                reaction.spatial = item["spatial"].get<bool>();
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

const EntityDesc* ProfileLibrary::find(std::string_view name) const {
    const auto it = std::find_if(profiles_.begin(), profiles_.end(),
                                 [&](const std::pair<std::string, EntityDesc>& p) { return p.first == name; });
    return it == profiles_.end() ? nullptr : &it->second;
}

void ProfileLibrary::add(std::string name, EntityDesc profile) {
    for (auto& existing : profiles_) {
        if (existing.first == name) {
            existing.second = std::move(profile);
            return;
        }
    }
    profiles_.emplace_back(std::move(name), std::move(profile));
}

std::vector<std::string> ProfileLibrary::names() const {
    std::vector<std::string> out;
    out.reserve(profiles_.size());
    for (const auto& p : profiles_) {
        out.push_back(p.first);
    }
    return out;
}

Result<ProfileLibrary> profileLibraryFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("an entity profile library must be an object");
    }
    if (j.contains("format") && j["format"].is_string() &&
        j["format"].get<std::string>() != "avgen-entity-profile-library") {
        return fail("not an entity profile library: format '{}'", j["format"].get<std::string>());
    }
    if (!j.contains("profiles") || !j["profiles"].is_object()) {
        return fail("an entity profile library needs a 'profiles' object of name -> profile");
    }
    ProfileLibrary library;
    for (const auto& [name, body] : j["profiles"].items()) {
        if (name.empty()) {
            return fail("entity profile library: a profile with an empty name");
        }
        auto profile = profileFromJson(body);
        if (!profile) {
            return fail("entity profile library: profile '{}': {}", name, profile.error().message);
        }
        library.add(name, std::move(*profile));
    }
    return library;
}

Result<ProfileLibrary> loadProfileLibrary(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return fail("cannot open entity profile library '{}'", path.string());
    }
    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        return fail("entity profile library '{}': {}", path.string(), e.what());
    }
    auto library = profileLibraryFromJson(j);
    if (!library) {
        return fail("entity profile library '{}': {}", path.string(), library.error().message);
    }
    library->setSource(path.string());
    return library;
}

Result<std::vector<EntityDesc>> entitiesFromJson(const nlohmann::json& j,
                                                 const std::filesystem::path& baseDir,
                                                 const ProfileLibrary* library) {
    if (!j.is_array()) {
        return fail("'entities' must be an array");
    }
    std::vector<EntityDesc> out;
    out.reserve(j.size());
    for (const auto& item : j) {
        auto entity = entityFromJson(item, baseDir, library);
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
            if (!reaction.spatial) {
                r["spatial"] = false;
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
    if (entity.tags.size() > entity.profileTags) {
        nlohmann::json tags = nlohmann::json::array();
        for (const std::string& tag : std::span(entity.tags).subspan(entity.profileTags)) {
            tags.push_back(tag);
        }
        j["tags"] = std::move(tags);
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
