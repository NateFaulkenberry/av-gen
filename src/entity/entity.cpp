#include "entity/entity.hpp"

#include "entity/nav_grid.hpp"

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

const char* interestKindName(InterestKind kind) {
    switch (kind) {
    case InterestKind::Landmark: return "landmark";
    case InterestKind::Character: return "character";
    case InterestKind::Glow: return "glow";
    case InterestKind::Water: return "water";
    case InterestKind::Vista: return "vista";
    }
    return "unknown";
}

void EntityWorld::setExtraInterestPoints(std::vector<InterestPoint> extras) {
    extraInterests_ = std::move(extras);
    refreshInterestPoints();
}

void EntityWorld::refreshInterestPoints() {
    interests_.clear();
    // Order matters only for reproducibility, not for preference -- a behaviour weights by kind,
    // not by position in the list -- but it has to be *an* order, and this one is the order the
    // host declared things in, followed by what the terrain offered.
    for (const auto& [name, position] : landmarks_) {
        InterestPoint point;
        point.position = position;
        point.name = name;
        // A landmark that is also an entity moves, and a behaviour that cached its position would
        // walk to where the craft used to be. Naming the kind here lets the behaviour re-resolve.
        point.kind = find(name) != nullptr ? InterestKind::Character : InterestKind::Landmark;
        point.weight = 1.0f;
        interests_.push_back(std::move(point));
    }
    for (const InterestPoint& extra : extraInterests_) {
        interests_.push_back(extra);
    }
    if (const NavGrid* grid = nav_.grid()) {
        for (const glm::vec3& p : grid->shorePoints()) {
            interests_.push_back(InterestPoint{p, {}, InterestKind::Water, 1.0f});
        }
        for (const glm::vec3& p : grid->vistaPoints()) {
            interests_.push_back(InterestPoint{p, {}, InterestKind::Vista, 1.0f});
        }
    }
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
    }
}

void EntityWorld::reset() {
    for (auto& entity : entities_) {
        entity->rng_ = Rng(entity->seed_);
        entity->state_ = EntityState{};
        entity->motion_ = MotionOffset{};
        entity->coarseAccum_ = 0.0;
        entity->everUpdated_ = false;
        if (const NodeBinding* b = binding(entity->desc_.driven())) {
            entity->state_.anchor = b->anchor;
        }
        for (auto& behavior : entity->behaviors_) {
            behavior->reset(entity->rng_);
        }
    }
}

void EntityWorld::seek(double time, params::ParameterSet* params, const signals::SignalBus* bus,
                       glm::vec3 viewPosition, double step, double maxSeconds) {
    if (params != nullptr) {
        params->resetFinals();
    }
    reset();
    const double target = std::max(time, 0.0);
    const double dt = std::max(step, 1e-3);
    const double span = std::min(target, std::max(maxSeconds, 0.0));
    const auto steps = static_cast<std::uint64_t>(span / dt);
    // A fixed step, not the frame's. That is what makes the answer a function of `time` alone: a
    // seek that integrated whatever dt the last frame happened to take would land somewhere that
    // depended on the machine it ran on.
    for (std::uint64_t i = 0; i < steps; ++i) {
        const double now = target - span + static_cast<double>(i) * dt;
        for (auto& entityPtr : entities_) {
            Entity& entity = *entityPtr;
            const float distance = glm::length(entity.state_.position() - viewPosition);
            if (entity.desc_.cullDistance > 0.0f && distance > entity.desc_.cullDistance &&
                entity.everUpdated_) {
                continue;
            }
            entity.motion_ = MotionOffset{};
            entity.state_.hasLookTarget = false;
            entity.state_.reaction = 0.0f;
            entity.state_.activity = Activity::Idle;
            entity.state_.detail = 1.0f;
            entity.everUpdated_ = true;

            BehaviorContext bc;
            bc.time = now;
            bc.dt = dt;
            bc.bus = bus;
            bc.nav = &nav_;
            bc.world = this;
            bc.rng = &entity.rng_;
            for (auto& behavior : entity.behaviors_) {
                behavior->update(bc, entity.state_, entity.motion_);
            }
        }
    }
    // Publish the state the next frame will build on, without touching the parameter set.
    for (auto& entityPtr : entities_) {
        Entity& entity = *entityPtr;
        entity.locomotion_.activity = entity.state_.activity;
        entity.locomotion_.time = target;
        entity.locomotion_.position = entity.state_.position() + entity.motion_.position;
        entity.locomotion_.yaw = entity.state_.yaw;
        entity.locomotion_.speed = entity.state_.speed;
        entity.locomotion_.turnRate = entity.state_.turnRate;
        entity.locomotion_.reaction = entity.state_.reaction;
        entity.locomotion_.lookTarget = entity.state_.lookTarget;
        entity.locomotion_.hasLookTarget = entity.state_.hasLookTarget;
    }
}

glm::vec2 EntityWorld::crowdSeparation(std::size_t self, glm::vec2 p, float radius) const {
    if (crowd_.empty() || radius <= 0.0f) {
        return glm::vec2(0.0f);
    }
    glm::vec2 push(0.0f);
    // A disc query into the grid, not a pass over every body. `query` allocates into a caller's
    // vector, so this keeps its own and pays one allocation the first time rather than one per call.
    static thread_local std::vector<std::uint32_t> hits;
    crowd_.query(p, radius, hits);
    for (const std::uint32_t i : hits) {
        if (i < crowdOwner_.size() && crowdOwner_[i] == self) {
            continue; // a body does not push itself
        }
        const spatial::NavigationObstacle& other = crowd_.obstacles()[i];
        const float reach = other.radius + radius;
        glm::vec2 d = p - other.center;
        const float distSq = glm::dot(d, d);
        if (distSq >= reach * reach) {
            continue;
        }
        if (distSq < 1e-6f) {
            // Exactly coincident. Nothing in the geometry says which way to go, so take a direction
            // from the pair's own indices: arbitrary, and the same arbitrary answer every frame,
            // which is what stops two bodies jittering against each other forever.
            const float angle = static_cast<float>((self + i) % 97u) * 0.06479f;
            push += glm::vec2(std::cos(angle), std::sin(angle)) * reach;
            continue;
        }
        const float dist = std::sqrt(distSq);
        // Softer than the static push: bodies yield to each other rather than bouncing, and a
        // separation that resolved fully in one frame reads as two people repelling like magnets.
        push += (d / dist) * (reach - dist) * 0.5f;
    }
    return push;
}

void EntityWorld::update(const EntityUpdate& ctx, params::ParameterSet& params) {
    if (!parametersLive_) {
        return;
    }
    params_ = &params;
    counts_ = {};
    // The crowd, as it was at the end of the last update. Built before anything moves so every
    // character separates against the same snapshot: building it as they go would make the answer
    // depend on the order they happen to be stored in.
    crowd_.clear();
    crowdOwner_.clear();
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        const Entity& entity = *entities_[i];
        if (!entity.active_ || entity.state_.radius <= 0.0f) {
            continue;
        }
        const glm::vec3 at = entity.state_.position();
        spatial::NavigationObstacle body;
        body.center = glm::vec2(at.x, at.z);
        body.radius = entity.state_.radius;
        body.base = at.y;
        body.height = std::max(entity.state_.radius * 2.0f, 1.0f);
        body.type = spatial::ObstacleType::Creature;
        crowd_.add(body);
        crowdOwner_.push_back(i);
    }
    crowd_.build();

    for (std::size_t entityIndex = 0; entityIndex < entities_.size(); ++entityIndex) {
        auto& entityPtr = entities_[entityIndex];
        Entity& entity = *entityPtr;
        const glm::vec3 here = entity.state_.position();
        const float distance = glm::length(here - ctx.viewPosition);
        // The one update level of detail may never skip. Everything below reasons about an entity
        // that has state worth keeping; on the first frame there is none, and skipping leaves the
        // pose sink reading a default-constructed LocomotionState at the origin.
        const bool first = !entity.everUpdated_;

        if (!first && entity.desc_.cullDistance > 0.0f && distance > entity.desc_.cullDistance) {
            // Far enough away that nothing it could do would be visible. Not merely a cheaper
            // update: no update, and no parameter write either, so the node stays exactly where
            // the scene put it.
            ++counts_.skipped;
            entity.active_ = false;
            continue;
        }

        double dt = ctx.dt;
        if (!first && entity.desc_.fullDetailDistance > 0.0f && distance > entity.desc_.fullDetailDistance) {
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
        entity.everUpdated_ = true;

        entity.motion_ = MotionOffset{};
        entity.state_.hasLookTarget = false;
        entity.state_.reaction = 0.0f;
        entity.state_.activity = Activity::Idle;

        BehaviorContext bc;
        bc.time = ctx.time;
        bc.dt = dt;
        bc.bus = ctx.bus;
        bc.nav = &nav_;
        bc.world = this;
        bc.self = entityIndex;
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
