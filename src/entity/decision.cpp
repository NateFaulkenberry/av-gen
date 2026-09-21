#include "entity/decision.hpp"

#include "entity/entity.hpp"
#include "entity/mind.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::entity {
namespace {

float readFloat(const nlohmann::json* j, const char* key, float fallback) {
    if (j == nullptr || !j->is_object() || !j->contains(key) || !(*j)[key].is_number()) {
        return fallback;
    }
    return (*j)[key].get<float>();
}

std::string readString(const nlohmann::json* j, const char* key, std::string fallback) {
    if (j == nullptr || !j->is_object() || !j->contains(key) || !(*j)[key].is_string()) {
        return fallback;
    }
    return (*j)[key].get<std::string>();
}

std::vector<std::string> readStrings(const nlohmann::json* j, const char* key) {
    std::vector<std::string> out;
    if (j == nullptr || !j->is_object() || !j->contains(key) || !(*j)[key].is_array()) {
        return out;
    }
    for (const auto& v : (*j)[key]) {
        if (v.is_string()) {
            out.push_back(v.get<std::string>());
        }
    }
    return out;
}

// The semantic tags a filter names, as one mask against this world's vocabulary (Phase D §25). A
// word the world has never heard contributes no bit, so a filter of only unknown words is a mask of
// 0 -- which callers treat as "matches nothing", not "no filter": an author who wrote a filter
// meant one.
std::uint64_t maskOf(const DecisionContext& ctx, const std::vector<std::string>& names) {
    if (ctx.world == nullptr) {
        return 0;
    }
    std::uint64_t mask = 0;
    for (const std::string& n : names) {
        mask |= ctx.world->semanticTags().bit(n);
    }
    return mask;
}

IntentType readIntent(const nlohmann::json* j, IntentType fallback) {
    if (j == nullptr || !j->is_object() || !j->contains("intent") || !(*j)["intent"].is_string()) {
        return fallback;
    }
    IntentType out = fallback;
    return intentTypeFromName((*j)["intent"].get<std::string>(), out) ? out : fallback;
}

// A point `distance` metres from `from`, directly away from `threat`, on the flat. When the two
// coincide the direction is the body's own facing reversed, so the answer is defined.
glm::vec3 awayFrom(const glm::vec3& from, const glm::vec3& threat, float distance, float yaw) {
    glm::vec2 d(from.x - threat.x, from.z - threat.z);
    const float len = glm::length(d);
    d = len > 1e-3f ? d / len : glm::vec2(-std::sin(yaw), -std::cos(yaw));
    return glm::vec3(from.x + d.x * distance, from.y, from.z + d.y * distance);
}

params::ParamDesc<float> floatDesc(std::string path, float def, float lo, float hi) {
    return params::ParamDesc<float>{std::move(path), def, lo, hi};
}

// The five `InterestKind`s as the names a scene file writes. One list, so a kind cannot be spelled
// one way in a weight table and another in a filter (ADR-225's rule applied to a name).
constexpr std::string_view kKindNames[5] = {"landmark", "character", "glow", "water", "vista"};

bool kindFromName(std::string_view name, InterestKind& out) {
    for (std::size_t i = 0; i < 5; ++i) {
        if (name == kKindNames[i]) {
            out = static_cast<InterestKind>(i);
            return true;
        }
    }
    return false;
}

// Reads a `weights` object -- `{"glow": 2.0, "character": 0.2}` -- onto a taste table, leaving
// anything it does not name alone.
void readWeights(const nlohmann::json* j, float (&weight)[5]) {
    if (j == nullptr || !j->is_object() || !j->contains("weights") || !(*j)["weights"].is_object()) {
        return;
    }
    for (const auto& [key, value] : (*j)["weights"].items()) {
        InterestKind kind{};
        if (kindFromName(key, kind) && value.is_number()) {
            weight[static_cast<std::size_t>(kind)] = value.get<float>();
        }
    }
}

void readTaste(const nlohmann::json* j, GoalTaste& taste) {
    readWeights(j, taste.weight);
    taste.minRange = readFloat(j, "minRange", taste.minRange);
    taste.maxRange = readFloat(j, "maxRange", taste.maxRange);
    taste.homeRadius = readFloat(j, "homeRadius", taste.homeRadius);
    taste.noveltyRadius = readFloat(j, "noveltyRadius", taste.noveltyRadius);
    taste.noveltyPenalty = readFloat(j, "noveltyPenalty", taste.noveltyPenalty);
}

// `hash(seed) + i * 0x9E3779B1`, the shape `app/cinematic.cpp:1555` uses and the one D2 demands: a
// draw from a seed and an index, never from a stream, so one extra decision does not re-cast every
// later one.
std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}


// Where a body should actually walk to, when the thing it is going to is a *thing*.
//
// A landmark's interest point is at the landmark, and a landmark is usually solid: the lab's cairns
// are heroes with a radius, and `obstaclesFromHeroes` turns every one of them into a cylinder the
// navigator refuses to stand in. `NavigatorPath::route` -- which is the provider `ActionQueue`'s
// `move` runs through -- returns `Unreachable` for a goal that is not navigable, so an option that
// named the landmark itself failed the instant it won. Measured on the guard fixture before this:
// the explorer scored six options, committed to one every tick, and travelled **0.00 m in 75 s**
// while its action queue reported `unreachable` and drained.
//
// `Explore` never hit this because it goes through `Navigator::requestPath`, which carries a
// `goalTolerance` and snaps; the action tier has no such thing. So the stand-off is the considerer's
// job and `approach` is the knob: walk to a point `approach` metres this side of it, which is also
// what "approach" has always meant to an author. 0 keeps the old behaviour of naming the point
// itself, which is right for a percept of a body -- a body is not a wall.
// Phase D §14: how far out an aware considerer's approach starts to slow. A body walking up to
// something it means to look at slows into it over a couple of strides rather than braking in the
// last one. One constant rather than a knob per considerer until an author needs it to differ.
constexpr float kArrival = 2.5f;

glm::vec3 standOff(glm::vec3 from, glm::vec3 to, float approach) {
    if (!(approach > 0.0f)) {
        return to;
    }
    const glm::vec2 delta(to.x - from.x, to.z - from.z);
    const float distance = glm::length(delta);
    if (distance <= approach) {
        return from; // already inside it: the option is "stay where you are and look"
    }
    const glm::vec2 at = glm::vec2(to.x, to.z) - delta / distance * approach;
    return glm::vec3(at.x, to.y, at.y);
}

} // namespace

// ---- the cadence -------------------------------------------------------------------------------

std::uint64_t decideTick(double time, float hertz, std::uint32_t seed) {
    if (!(hertz > 0.0f)) {
        // "Decide every step." The tick index is then the step index as closely as a double can
        // say it, which is what a test that wants no cadence at all asks for.
        return static_cast<std::uint64_t>(std::max(0.0, time) * 1e6);
    }
    // A phase in [0, 1) ticks, so a cast does not all decide on the same frame. It moves *when* a
    // body decides and never how often (ADR-290 §2).
    const double phase = static_cast<double>(hash32(seed ^ 0x5bf03635U)) / 4294967296.0;
    const double t = std::max(0.0, time) * static_cast<double>(hertz) + phase;
    return static_cast<std::uint64_t>(t);
}

// ---- the selector ------------------------------------------------------------------------------

void Selector::forget() {
    current_.clear();
    currentSubject_ = 0;
    chosen_ = kNone;
    started_ = false;
}

void Selector::reset() {
    hold_ = false;
    holdScore_ = 0.0f;
    options_.clear();
    current_.clear();
    currentSubject_ = 0;
    chosen_ = kNone;
    committedTick_ = 0;
    tick_ = 0;
    started_ = false;
    counts_ = Counts{};
}

bool Selector::select(const DecisionContext& ctx, std::span<const IConsiderer* const> considerers) {
    const std::uint64_t tick = decideTick(ctx.time, settings_.hertz, ctx.seed);
    if (started_ && tick == tick_) {
        return false; // between decisions: the list, the choice and the counts stand
    }
    tick_ = tick;
    started_ = true;
    ++counts_.ticks;

    options_.clear();
    for (const IConsiderer* considerer : considerers) {
        if (considerer != nullptr) {
            considerer->consider(ctx, options_);
        }
    }
    counts_.scored += options_.size();

    // The best applicable option. `<= 0` is "not applicable now" (character_ai.hpp §3), and a tie
    // is broken on the order the considerers appended -- which is the scene file's order, and so is
    // an author's statement rather than an artefact of iteration.
    std::size_t best = kNone;
    float bestScore = 0.0f;
    for (std::size_t i = 0; i < options_.size(); ++i) {
        const float score = options_[i].score;
        if (score <= 0.0f) {
            continue;
        }
        if (best == kNone || score > bestScore) {
            best = i;
            bestScore = score;
        }
    }
    if (best == kNone) {
        ++counts_.empty;
        chosen_ = kNone;
        current_.clear();
        currentSubject_ = 0;
        return false;
    }

    // The incumbent, found by name in *this* tick's list. Not by index: a considerer may append a
    // different number of options from one tick to the next -- `interest` does, every time the body
    // moves -- so an index kept across a tick names a different option.
    std::size_t incumbent = kNone;
    for (std::size_t i = 0; i < options_.size(); ++i) {
        if (!current_.empty() && options_[i].name == current_ &&
            options_[i].subject == currentSubject_ && options_[i].score > 0.0f) {
            incumbent = i;
            break;
        }
    }

    const bool commit = [&] {
        if (incumbent == kNone) {
            // Phase D §20: a plan in progress whose proposer went quiet keeps its slot unless beaten.
            if (hold_ && !current_.empty() && bestScore <= holdScore_ + settings_.margin) {
                ++counts_.holdRejections;
                return false;
            }
            return true; // nothing held, or what was held is no longer on offer
        }
        if (incumbent == best) {
            return false; // it is still winning; nothing to decide
        }
        const auto held = static_cast<double>(tick_ - committedTick_);
        if (held < static_cast<double>(settings_.dwellTicks)) {
            ++counts_.dwellRejections;
            return false;
        }
        if (bestScore <= options_[incumbent].score + settings_.margin) {
            ++counts_.marginRejections;
            return false;
        }
        return true;
    }();

    if (!commit && incumbent == kNone) {
        chosen_ = kNone; // held: the running option is not on this tick's list to point at
        return false;
    }
    if (!commit) {
        // The chosen option is the one that is *running*, not the one that scored highest. That
        // distinction is the whole value of the overlay: a dwell or a margin refusal is exactly the
        // case where "why is it still doing that" has an answer and nothing was drawing it.
        chosen_ = incumbent;
        return false;
    }

    const bool changed =
        current_ != options_[best].name || currentSubject_ != options_[best].subject;
    chosen_ = best;
    current_.assign(options_[best].name);
    currentSubject_ = options_[best].subject;
    if (changed) {
        committedTick_ = tick_;
        ++counts_.decisions;
    }
    return changed;
}

// ---- the goal model ----------------------------------------------------------------------------

// **Do not tidy this function.** Every expression in it is the expression `Explore::pickGoal`
// inlined before ADR-333, in the same order, and the extraction's control is a position trace taken
// from the build before the move and compared as raw float bits. A reciprocal multiply instead of
// the division, a `std::hypot` instead of `glm::length`, or hoisting `std::max(hi * 0.5f, 1.0f)`
// out of the loop are all algebraically identical and none of them is bit-identical.
float goalWeight(const GoalTaste& taste, glm::vec2 flat, glm::vec2 anchor, float lo, float hi,
                 float home, glm::vec3 position, InterestKind kind, float pointWeight,
                 std::span<const glm::vec3> visited) {
    const glm::vec2 at(position.x, position.z);
    const float distance = glm::length(at - flat);
    // The near bound is also what keeps a character from choosing *itself*: the host lists every
    // node an entity drives as a landmark, and this one is standing on its own. Nothing else is
    // needed for that, and a name comparison would only be a second rule that could disagree.
    if (distance < lo || distance > hi) {
        return -1.0f;
    }
    if (home > 0.0f && glm::length(at - anchor) > home) {
        return -1.0f;
    }
    float weight = taste.weight[static_cast<std::size_t>(kind)] * std::max(pointWeight, 0.0f);
    // Nearer is likelier, but only mildly: a falloff steep enough to matter is a character that
    // never crosses its own world.
    weight *= 1.0f / (1.0f + distance / std::max(hi * 0.5f, 1.0f));
    for (const glm::vec3& seen : visited) {
        if (glm::length(glm::vec2(seen.x - at.x, seen.z - at.y)) < taste.noveltyRadius) {
            weight *= taste.noveltyPenalty; // been there
            break;
        }
    }
    return weight;
}

namespace {

// The two bounds, resolved the way `pickGoal` resolved them: `hi` is at least one metre past `lo`,
// so a scene that authored them the wrong way round gets a band rather than an empty set.
void goalBounds(const GoalTaste& taste, float& lo, float& hi, float& home) {
    lo = taste.minRange;
    hi = std::max(taste.maxRange, lo + 1.0f);
    home = taste.homeRadius;
}

} // namespace

std::size_t scoreGoals(const DecisionContext& ctx, const GoalTaste& taste,
                       std::span<const InterestPoint> points, std::vector<GoalCandidate>& out) {
    if (ctx.state == nullptr) {
        return 0;
    }
    const glm::vec3 here = ctx.state->position();
    const glm::vec2 flat(here.x, here.z);
    const glm::vec2 anchor(ctx.state->anchor.x, ctx.state->anchor.z);
    float lo = 0.0f;
    float hi = 0.0f;
    float home = 0.0f;
    goalBounds(taste, lo, hi, home);

    const std::size_t before = out.size();
    for (const InterestPoint& point : points) {
        const float weight = goalWeight(taste, flat, anchor, lo, hi, home, point.position,
                                        point.kind, point.weight, ctx.visited);
        if (weight <= 0.0f) {
            continue;
        }
        out.push_back(GoalCandidate{point.position, point.name, point.kind, weight});
    }
    return out.size() - before;
}

std::size_t scoreGoals(const DecisionContext& ctx, const GoalTaste& taste,
                       std::span<const Percept> percepts, std::vector<GoalCandidate>& out) {
    if (ctx.state == nullptr) {
        return 0;
    }
    const glm::vec3 here = ctx.state->position();
    const glm::vec2 flat(here.x, here.z);
    const glm::vec2 anchor(ctx.state->anchor.x, ctx.state->anchor.z);
    float lo = 0.0f;
    float hi = 0.0f;
    float home = 0.0f;
    goalBounds(taste, lo, hi, home);

    const std::size_t before = out.size();
    for (const Percept& percept : percepts) {
        // A percept carries no `weight` of its own -- ADR-290 kept the sense stage's ranking in
        // `salience` and left the weighing to the decider, which is here. So the point's weight is
        // 1 and the taste table does the rest; the percept's own salience is a *separate* term
        // that `investigate` uses and the goal model does not, because salience is already taste
        // times nearness and multiplying it in again would square both.
        const float weight = goalWeight(taste, flat, anchor, lo, hi, home, percept.position,
                                        percept.kind, 1.0f, ctx.visited);
        if (weight <= 0.0f) {
            continue;
        }
        // The name has to come from somewhere the span can point at for as long as the option
        // lives, and a percept carries an index rather than a name (ADR-290 §1). The world's own
        // list is that storage.
        std::string_view name;
        if (ctx.world != nullptr && percept.kind != InterestKind::Character) {
            const std::span<const InterestPoint> points = ctx.world->interestPoints();
            if (percept.source < points.size()) {
                name = points[percept.source].name;
            }
        } else if (ctx.world != nullptr) {
            const auto& bodies = ctx.world->entities();
            if (percept.source < bodies.size() && bodies[percept.source] != nullptr) {
                name = bodies[percept.source]->name();
            }
        }
        out.push_back(GoalCandidate{percept.position, name, percept.kind, weight});
    }
    return out.size() - before;
}

// ---- StockConsiderer ---------------------------------------------------------------------------

void StockConsiderer::registerWeight(params::ParameterSet& params, const std::string& prefix,
                                     float def) {
    weightDefault_ = def;
    weightPath_ = prefix + "weight";
    weight_ = &params.add(floatDesc(weightPath_, def, 0.0f, 100.0f));
}

void StockConsiderer::collectWeightPath(std::vector<std::string>& out) const {
    if (!weightPath_.empty()) {
        out.push_back(weightPath_);
    }
}

// ---- idle --------------------------------------------------------------------------------------

IdleConsiderer::IdleConsiderer(const nlohmann::json* settings)
    : activity_(readString(settings, "activity", "")),
      duration_(static_cast<double>(readFloat(settings, "duration", 0.0f))) {
    weightDefault_ = readFloat(settings, "weight", 0.10f);
}

void IdleConsiderer::registerParameters(params::ParameterSet& params, const std::string& prefix) {
    registerWeight(params, prefix, weightDefault_);
}

void IdleConsiderer::collectParameterPaths(std::vector<std::string>& out) const {
    collectWeightPath(out);
}

void IdleConsiderer::consider(const DecisionContext& ctx, std::vector<Option>& out) const {
    (void)ctx;
    actions_.clear();
    if (!activity_.empty() || duration_ > 0.0) {
        ActionDesc pose;
        pose.kind = activity_.empty() ? ActionKind::Wait : ActionKind::Pose;
        pose.name = name_;
        pose.activity = activity_;
        pose.duration = duration_;
        actions_.push_back(std::move(pose));
    }
    // Empty actions are legal and mean "this option wins by doing nothing": the queue is left to
    // drain and the behaviours below carry on, which is what standing there is.
    out.push_back(Option{name_, weight(), std::span<const ActionDesc>(actions_),
                         Authority::Routine});
    out.back().intent = IntentType::Idle;
    out.back().addFactor("weight", weight());
}

// ---- holdPost ----------------------------------------------------------------------------------

HoldPostConsiderer::HoldPostConsiderer(const nlohmann::json* settings)
    : post_(readString(settings, "post", "")),
      activity_(readString(settings, "activity", "")),
      tolerance_(readFloat(settings, "tolerance", 1.5f)),
      pullDefault_(readFloat(settings, "pull", 0.08f)) {
    weightDefault_ = readFloat(settings, "weight", 1.0f);
    if (settings != nullptr && settings->is_object() && settings->contains("facing") &&
        (*settings)["facing"].is_number()) {
        faceYaw_ = (*settings)["facing"].get<float>();
        hasFacing_ = true;
    }
}

void HoldPostConsiderer::registerParameters(params::ParameterSet& params, const std::string& prefix) {
    registerWeight(params, prefix, weightDefault_);
    pullPath_ = prefix + "pull";
    pull_ = &params.add(floatDesc(pullPath_, pullDefault_, 0.0f, 10.0f));
}

void HoldPostConsiderer::collectParameterPaths(std::vector<std::string>& out) const {
    collectWeightPath(out);
    if (!pullPath_.empty()) {
        out.push_back(pullPath_);
    }
}

bool HoldPostConsiderer::postAt(const DecisionContext& ctx, glm::vec3& out) const {
    if (post_.empty()) {
        if (ctx.state == nullptr) {
            return false;
        }
        out = ctx.state->anchor; // R1: where the scene put it, which is what a post is
        return true;
    }
    if (ctx.world == nullptr) {
        return false;
    }
    // `pointOfInterest` reports simulation positions for bodies and authored positions for nodes
    // (R1). A post that names something the world does not have reports false rather than falling
    // back to the anchor: a guard silently standing somewhere else is the shape of defect this
    // repository has paid for.
    return ctx.world->pointOfInterest(post_, out);
}

void HoldPostConsiderer::consider(const DecisionContext& ctx, std::vector<Option>& out) const {
    glm::vec3 post{0.0f};
    if (!postAt(ctx, post) || ctx.state == nullptr) {
        return; // no post: this considerer has nothing to say, rather than a wrong thing
    }
    const glm::vec3 here = ctx.state->position();
    const float away = glm::length(glm::vec2(post.x - here.x, post.z - here.z));
    const float pull = pull_ != nullptr ? pull_->value() : pullDefault_;
    // Flat while it is standing the post, rising with the distance once it is not. One term, not
    // two options: "go back" and "stay" are the same intention at different distances, and an
    // author who wants a guard that will not be pulled away once it has left raises `pull`.
    const float score = weight() * (1.0f + pull * std::max(0.0f, away - tolerance_));

    actions_.clear();
    if (away > tolerance_) {
        ActionDesc walk;
        walk.kind = ActionKind::Move;
        walk.name = name_;
        walk.target.kind = TargetKind::Point;
        walk.target.point = post;
        walk.tolerance = tolerance_;
        actions_.push_back(std::move(walk));
    }
    if (hasFacing_) {
        // Face a direction rather than a thing: a sentry looks down the road, and the road is not
        // an entity. Expressed as a point one metre along the heading, because `Face` targets a
        // place and inventing a second target kind for an angle would be a second mechanism.
        ActionDesc face;
        face.kind = ActionKind::Face;
        face.name = name_;
        face.target.kind = TargetKind::Point;
        face.target.point = post + glm::vec3(std::sin(faceYaw_), 0.0f, std::cos(faceYaw_));
        actions_.push_back(std::move(face));
    }
    if (!activity_.empty()) {
        ActionDesc pose;
        pose.kind = ActionKind::Pose;
        pose.name = name_;
        pose.activity = activity_; // an activity, never a clip (R4)
        pose.duration = 0.0;
        actions_.push_back(std::move(pose));
    }
    out.push_back(Option{name_, score, std::span<const ActionDesc>(actions_), Authority::Routine});
    out.back().intent = IntentType::ReturnTo;
    out.back().addFactor("post", score);
}

// ---- investigate -------------------------------------------------------------------------------

InvestigateConsiderer::InvestigateConsiderer(const nlohmann::json* settings)
    : activity_(readString(settings, "activity", "observe")),
      approach_(readFloat(settings, "approach", 3.0f)),
      dwell_(static_cast<double>(readFloat(settings, "dwell", 4.0f))),
      staleSeconds_(readFloat(settings, "staleSeconds", 3.0f)) {
    weightDefault_ = readFloat(settings, "weight", 1.0f);
    taste_.minRange = readFloat(settings, "minRange", 0.0f);
    taste_.maxRange = readFloat(settings, "maxRange", 1000.0f);
    readTaste(settings, taste_);
    if (settings != nullptr && settings->is_object() && settings->contains("kinds") &&
        (*settings)["kinds"].is_array()) {
        // A filter authored at all replaces the default "everything", rather than adding to it.
        for (bool& allowed : kinds_) {
            allowed = false;
        }
        for (const auto& entry : (*settings)["kinds"]) {
            InterestKind kind{};
            if (entry.is_string() && kindFromName(entry.get<std::string>(), kind)) {
                kinds_[static_cast<std::size_t>(kind)] = true;
            }
        }
    }
    tags_ = readStrings(settings, "tags");
    intent_ = readIntent(settings, IntentType::Investigate);
    noveltyWeight_ = readFloat(settings, "novelty", 1.0f);
    affordance_ = readString(settings, "affordance", "");
}

std::uint64_t InvestigateConsiderer::tagMask(const DecisionContext& ctx) const {
    return maskOf(ctx, tags_);
}

void InvestigateConsiderer::registerParameters(params::ParameterSet& params,
                                               const std::string& prefix) {
    registerWeight(params, prefix, weightDefault_);
}

void InvestigateConsiderer::collectParameterPaths(std::vector<std::string>& out) const {
    collectWeightPath(out);
}

float InvestigateConsiderer::scoreOf(const DecisionContext& ctx, const Percept& p) const {
    if (!kinds_[static_cast<std::size_t>(p.kind)]) {
        return 0.0f;
    }
    if (p.distance < taste_.minRange || p.distance > taste_.maxRange) {
        return 0.0f;
    }
    // Salience is already taste times nearness as the *sense* stage weighted it (ADR-290 §3); this
    // multiplies by the *decider's* taste, which is a different set of numbers on purpose. A guard
    // may sense a mushroom keenly and still not care about one.
    float score = p.salience * taste_.weight[static_cast<std::size_t>(p.kind)];
    // Staleness. ADR-290 §3 deliberately leaves `visibility` out of salience, so that what a
    // character knows cannot depend on which percepts won the occlusion budget. **Weighing it here
    // would put it back**, by the same route and with the same consequence, so this does not read
    // `visibility` either -- a percept that was never tested reports 1 and a tested one reports
    // less, and scoring on that would make an untested thing beat a seen one. Age is the honest
    // discount: it is measured, it is the same for every percept, and it is what makes a memory
    // fade rather than a guess.
    if (staleSeconds_ > 0.0f) {
        const auto age = static_cast<float>(std::max(0.0, ctx.time - p.seenAt));
        score *= std::max(0.0f, 1.0f - age / staleSeconds_);
    }
    // Phase D §25: a semantic filter, when one was authored.
    if (!tags_.empty() && (p.tags & tagMask(ctx)) == 0) {
        return 0.0f;
    }
    // Phase D §21/§22/§59, only for a decider running the awareness layer: a target that could not
    // be reached is left alone for a while, and a thing already investigated -- or stared at until
    // it is familiar -- is worth its novelty. Without `ctx.mind` this is the pre-Phase-D score.
    if (ctx.mind != nullptr && score > 0.0f) {
        const SubjectId id = subjectOf(p);
        if (ctx.mind->suppressed(id, ctx.time)) {
            return 0.0f;
        }
        if (noveltyWeight_ != 0.0f) {
            score *= std::pow(std::max(ctx.mind->novelty(id, ctx.time), 0.0f), noveltyWeight_);
        }
    }
    return score;
}

bool InvestigateConsiderer::best(const DecisionContext& ctx, Percept& out, float& score) const {
    bool found = false;
    float bestScore = 0.0f;
    for (const Percept& p : ctx.percepts) {
        const float s = scoreOf(ctx, p);
        if (s <= 0.0f) {
            continue;
        }
        // Ties broken on (kind, source), the way ADR-290 breaks a salience tie on the scan index
        // and `NavGrid`'s A* breaks one on cell index: so the answer cannot depend on the order the
        // sense stage happened to write its working set.
        const bool better = !found || s > bestScore ||
                            (s == bestScore && (p.kind < out.kind ||
                                                (p.kind == out.kind && p.source < out.source)));
        if (better) {
            out = p;
            bestScore = s;
            found = true;
        }
    }
    score = bestScore;
    return found;
}

void InvestigateConsiderer::consider(const DecisionContext& ctx, std::vector<Option>& out) const {
    Percept target{};
    float score = 0.0f;
    if (!best(ctx, target, score)) {
        return; // nothing noticed: no option, rather than an option scoring zero
    }
    // Phase D: a body is looked at where it IS, not where it was perceived -- a saucer crossing the
    // sky, another alien walking past. Only with the awareness layer on, so a decider written
    // before Phase D pushes exactly the actions it always did.
    const bool aware = ctx.mind != nullptr;
    const Entity* body = aware && target.kind == InterestKind::Character && ctx.world != nullptr &&
                                 target.source < ctx.world->entities().size()
                             ? ctx.world->entities()[target.source].get()
                             : nullptr;
    const auto aimAt = [&](ActionTarget& t) {
        if (body != nullptr) {
            t.kind = TargetKind::EntityRef;
            t.name = body->name();
        } else {
            t.kind = TargetKind::Point;
            t.point = target.position;
        }
    };
    // Phase D §16–§18: the thing's own affordance, when this considerer names one and the body can
    // use it. **Capability + affordance = valid interaction**: the prop says what it offers and what
    // that requires; this body's capabilities say whether it qualifies. When it does not, the
    // character observes instead (§59: "no interaction affordance -> observe instead"), which is
    // the pre-§18 behaviour exactly -- so a missing capability degrades, it never fails.
    const InteractionDesc* verb = nullptr;
    affordanceState_ = Affordance::None;
    if (body != nullptr && !affordance_.empty()) {
        verb = body->interaction(affordance_);
        const Entity* self = ctx.self < ctx.world->entities().size()
                                 ? ctx.world->entities()[ctx.self].get()
                                 : nullptr;
        if (verb == nullptr) {
            affordanceState_ = Affordance::NotOffered;
        } else if (self == nullptr || !self->can(verb->required)) {
            affordanceState_ = Affordance::NotCapable;
            verb = nullptr;
        } else {
            affordanceState_ = Affordance::Used;
        }
    }
    // Close enough to use it: inside the verb's own range, with a margin for the arrival tolerance.
    const float approach = verb != nullptr ? std::min(approach_, verb->range * 0.6f) : approach_;
    actions_.clear();
    ActionDesc walk;
    walk.kind = ActionKind::Move;
    walk.name = name_;
    walk.target.kind = TargetKind::Point;
    // R1 again, and it is the whole reason this is safe: a percept's position is the perceived
    // body's `state().position()` and never its `visualPosition()`, so a guard sent to meet a
    // hovering saucer walks to where the saucer is rather than to where it is drawn.
    walk.target.point = standOff(ctx.state != nullptr ? ctx.state->position() : glm::vec3(0.0f),
                                 target.position, approach);
    walk.tolerance = verb != nullptr ? std::min(std::max(approach * 0.5f, 0.3f), verb->range * 0.3f)
                                     : std::max(approach_ * 0.5f, 0.75f);
    if (aware) {
        walk.arrival = kArrival; // slow into it (§14): it is going there to look at it
    }
    actions_.push_back(std::move(walk));

    ActionDesc look;
    look.kind = ActionKind::Face;
    look.name = name_;
    look.target.kind = TargetKind::Point;
    look.target.point = target.position;
    if (aware) {
        aimAt(look.target);
    }
    actions_.push_back(std::move(look));

    if (verb != nullptr) {
        // The interaction replaces the observation: the prop's verb, its activity and duration.
        ActionDesc use;
        use.kind = ActionKind::Interact;
        use.name = name_;
        use.target.kind = TargetKind::Interaction;
        use.target.name = body->name();
        use.target.member = verb->name;
        actions_.push_back(std::move(use));
    } else if (dwell_ > 0.0) {
        ActionDesc attend;
        attend.kind = ActionKind::Pose;
        attend.name = name_;
        attend.activity = activity_;
        attend.duration = dwell_;
        // The **subject**, not the stand-off the walk ends on. `action.cpp`'s Pose case publishes
        // this as `lookTarget`, which is what an aim layer reads (ADR-300); aiming at the point the
        // body is standing on would be a target on top of its own pivot and would resolve to
        // `NoTarget`.
        attend.target.kind = TargetKind::Point;
        attend.target.point = target.position;
        if (aware) {
            aimAt(attend.target);
        }
        actions_.push_back(std::move(attend));
    }
    out.push_back(Option{name_, weight() * score, std::span<const ActionDesc>(actions_),
                         Authority::Routine});
    Option& o = out.back();
    o.intent = intent_;
    o.target = target.position;
    o.hasTarget = true;
    o.stoppingDistance = approach_;
    if (aware) {
        // Part of the option's identity only with the awareness layer on: switching from one
        // mushroom to another is then a change of mind (a new action list), where before it was the
        // same option and the body kept walking to the first one.
        o.subject = subjectOf(target);
        o.addFactor("salience", target.salience);
        o.addFactor("taste", taste_.weight[static_cast<std::size_t>(target.kind)]);
        o.addFactor("novelty", ctx.mind->novelty(o.subject, ctx.time));
        o.addFactor("weight", weight());
        if (affordanceState_ == Affordance::Used) {
            o.intent = IntentType::Interact;
            o.addFactor("affordance", 1.0f);
        }
    }
}

// ---- interest ----------------------------------------------------------------------------------

InterestConsiderer::InterestConsiderer(const nlohmann::json* settings)
    : activity_(readString(settings, "activity", "")),
      approach_(readFloat(settings, "approach", 0.0f)),
      dwell_(static_cast<double>(readFloat(settings, "dwell", 0.0f))) {
    weightDefault_ = readFloat(settings, "weight", 1.0f);
    readTaste(settings, taste_);
    source_ = readString(settings, "source", "perceived") == "omniscient" ? Source::Omniscient
                                                                         : Source::Perceived;
    variety_ = std::clamp(readFloat(settings, "variety", 0.6f), 0.0f, 1.0f);
}

void InterestConsiderer::registerParameters(params::ParameterSet& params, const std::string& prefix) {
    registerWeight(params, prefix, weightDefault_);
}

void InterestConsiderer::collectParameterPaths(std::vector<std::string>& out) const {
    collectWeightPath(out);
}

std::size_t InterestConsiderer::candidates(const DecisionContext& ctx, const GoalTaste& taste,
                                           std::vector<GoalCandidate>& out) const {
    if (source_ == Source::Omniscient) {
        if (ctx.world == nullptr) {
            return 0;
        }
        return scoreGoals(ctx, taste, ctx.world->interestPoints(), out);
    }
    return scoreGoals(ctx, taste, ctx.percepts, out);
}

void InterestConsiderer::consider(const DecisionContext& ctx, std::vector<Option>& out) const {
    scratch_.clear();
    candidates(ctx, taste_, scratch_);
    if (scratch_.empty()) {
        return;
    }
    // Two passes, because the spans handed out in `Option::actions` must survive the whole of this
    // call and a vector that grows moves its storage. The same reason `NavDebug`'s spans are
    // published after the route is built rather than while it is.
    actions_.clear();
    actions_.reserve(scratch_.size() * 3);
    names_.clear();
    names_.reserve(scratch_.size());
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    ranges.reserve(scratch_.size());
    const float w = weight();
    const glm::vec3 here = ctx.state != nullptr ? ctx.state->position() : glm::vec3(0.0f);
    for (const GoalCandidate& candidate : scratch_) {
        const std::size_t first = actions_.size();
        ActionDesc walk;
        walk.kind = ActionKind::Move;
        walk.name = std::string(candidate.name);
        walk.target.kind = TargetKind::Point;
        walk.target.point = standOff(here, candidate.position, approach_);
        if (approach_ > 0.0f) {
            walk.tolerance = std::max(approach_ * 0.5f, 0.75f);
        }
        actions_.push_back(std::move(walk));
        if (dwell_ > 0.0) {
            ActionDesc attend;
            attend.kind = ActionKind::Pose;
            attend.activity = activity_;
            attend.duration = dwell_;
            // The candidate itself, not `walk.target.point` -- that is the stand-off the body ends
            // the walk on, and a look at your own feet is not a look (ADR-300).
            attend.target.kind = TargetKind::Point;
            attend.target.point = candidate.position;
            actions_.push_back(std::move(attend));
        }
        ranges.emplace_back(first, actions_.size() - first);
        // The option's name has to outlive the candidate list, and a derived point has none of its
        // own. Naming it by its kind and its place is what makes the overlay readable -- "vista at
        // 41, -8" says something; "" says the option has no identity and the selector's incumbent
        // lookup would then match every nameless option at once, which is a real bug rather than a
        // cosmetic one.
        if (candidate.name.empty()) {
            names_.push_back(std::string(kKindNames[static_cast<std::size_t>(candidate.kind)]) +
                             "@" + std::to_string(static_cast<int>(std::lround(candidate.position.x))) +
                             "," + std::to_string(static_cast<int>(std::lround(candidate.position.z))));
        } else {
            names_.emplace_back(candidate.name);
        }
    }
    for (std::size_t i = 0; i < scratch_.size(); ++i) {
        out.push_back(Option{names_[i], w * scratch_[i].weight,
                             std::span<const ActionDesc>(actions_.data() + ranges[i].first,
                                                         ranges[i].second),
                             Authority::Routine});
        Option& o = out.back();
        o.intent = IntentType::Wander;
        o.target = scratch_[i].position;
        o.hasTarget = true;
        o.stoppingDistance = approach_;
        o.kind = static_cast<std::uint8_t>(scratch_[i].kind);
        o.addFactor("goal", scratch_[i].weight);
        o.addFactor("weight", w);
        // Variety (§23), aware deciders only. Measured on the autonomy demo before this: a
        // cautious warden walked eleven shore points in a row, 12-16 m apart, for 80 s. Not the
        // shore creep (every errand completed) but argmax over a dense kind: shore points are the
        // nearest unvisited candidates wherever the body stands on a bank, so nearness alone keeps
        // choosing the next one. A body that has just been to the water three times wants
        // something else.
        if (ctx.mind != nullptr && variety_ < 1.0f) {
            int repeats = 0;
            for (const std::uint8_t k : ctx.mind->recentKinds) {
                repeats += k == o.kind ? 1 : 0;
            }
            if (repeats > 0) {
                const float v = std::pow(variety_, static_cast<float>(repeats));
                o.score *= v;
                o.addFactor("variety", v);
            }
        }
    }
}

// ---- route ---------------------------------------------------------------------------------

namespace {

// Metres of a polyline that are in water, each weighted by that water's depth over this walker's
// wade band. The same 0..1 quantity `NavCell::wade` holds, integrated along the route, so the
// number this considerer prices a ford with is the number `NavGrid`'s A* priced it with.
//
// **Measured off the grid and not off the world, and that is the whole reason it is affordable.**
// `Navigator::sample` is an analytic evaluation of the world at 10.325 us (ADR-268); a grid lookup
// is 0.024 us. A 110 m route sampled every two metres is 55 of them -- 0.57 ms against 1.3 us --
// and this runs twice per destination per decision tick. The grid is four metres coarse and that
// is fine here in a way it is not for `pathClear` (ADR-295): this is not deciding whether a body
// may stand somewhere, it is weighing how wet a route already accepted as walkable is, and an
// error of one cell is an error of four metres in a hundred.
//
// The analytic fall-back is for a world with no graph, which `price` refuses to score anyway; it
// exists so that a test holding a bare `Navigator` gets an answer rather than a zero that looks
// like dry land.
float wadeAlong(const Navigator& nav, glm::vec2 from, std::span<const glm::vec2> waypoints) {
    const NavGrid* grid = nav.grid();
    const bool gridded = grid != nullptr && grid->valid() && grid->cellSize() > 0.0f;
    const float band = nav.settings().wadeDepth;
    const float step = gridded ? grid->cellSize() * 0.5f : 2.0f;
    float wet = 0.0f;
    glm::vec2 previous = from;
    for (const glm::vec2& point : waypoints) {
        const float span = glm::length(point - previous);
        if (span > 0.0f) {
            const int pieces = std::max(1, static_cast<int>(std::ceil(span / step)));
            const float piece = span / static_cast<float>(pieces);
            for (int i = 0; i < pieces; ++i) {
                // The midpoint of each piece. Sampling the ends would count every waypoint twice,
                // so a route whose legs are short would read wetter than one whose legs are long
                // -- which is a measurement of the string pull rather than of the water.
                const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(pieces);
                const glm::vec2 at = previous + (point - previous) * t;
                float fraction = 0.0f;
                if (gridded) {
                    const glm::ivec2 cell = grid->cellOf(at);
                    if (grid->inside(cell)) {
                        fraction = static_cast<float>(grid->at(cell).wade) / 255.0f;
                    }
                } else {
                    const NavSample s = nav.sample(at);
                    fraction = band > 0.0f ? std::min(1.0f, s.waterDepth / band)
                                           : (s.waterDepth > 0.0f ? 1.0f : 0.0f);
                }
                wet += fraction * piece;
            }
        }
        previous = point;
    }
    return wet;
}

} // namespace

RouteConsiderer::RouteConsiderer(const nlohmann::json* settings)
    : fordPenalty_(readFloat(settings, "fordPenalty", 0.0f)),
      detourPenalty_(readFloat(settings, "detourPenalty", 40.0f)),
      wadePenaltyDefault_(readFloat(settings, "wadePenalty", 1.2f)),
      falloff_(readFloat(settings, "falloff", 40.0f)),
      goalTolerance_(readFloat(settings, "goalTolerance", 2.0f)),
      approach_(readFloat(settings, "approach", 0.0f)),
      dwell_(static_cast<double>(readFloat(settings, "dwell", 0.0f))),
      activity_(readString(settings, "activity", "")) {
    weightDefault_ = readFloat(settings, "weight", 1.0f);
    readTaste(settings, taste_);
    source_ = readString(settings, "source", "perceived") == "omniscient"
                  ? InterestConsiderer::Source::Omniscient
                  : InterestConsiderer::Source::Perceived;
    const float cap = readFloat(settings, "maxDestinations", 4.0f);
    maxDestinations_ = static_cast<std::uint16_t>(std::clamp(cap, 1.0f, 64.0f));
    if (settings != nullptr && settings->is_object() && settings->contains("destinations") &&
        (*settings)["destinations"].is_array()) {
        for (const auto& entry : (*settings)["destinations"]) {
            Authored place;
            if (entry.is_string()) {
                place.name = entry.get<std::string>();
            } else if (entry.is_object()) {
                place.name = readString(&entry, "name", "");
                if (entry.contains("point") && entry["point"].is_array() &&
                    entry["point"].size() == 3) {
                    place.point = glm::vec3(entry["point"][0].get<float>(),
                                            entry["point"][1].get<float>(),
                                            entry["point"][2].get<float>());
                    place.hasPoint = true;
                }
            }
            if (place.name.empty() && !place.hasPoint) {
                continue;
            }
            authored_.push_back(std::move(place));
        }
    }
}

void RouteConsiderer::registerParameters(params::ParameterSet& params, const std::string& prefix) {
    registerWeight(params, prefix, weightDefault_);
    // The taste, and the only one of the three water numbers that is a taste. ADR-225: a weight an
    // author wrote in a scene file and the engine read once is not a setting, and this one is the
    // knob the lab's case 9 drives from one side of the crossover to the other.
    wadePenaltyPath_ = prefix + "wadePenalty";
    wadePenalty_ = &params.add(floatDesc(wadePenaltyPath_, wadePenaltyDefault_, 0.0f, 40.0f));
}

void RouteConsiderer::collectParameterPaths(std::vector<std::string>& out) const {
    collectWeightPath(out);
    if (!wadePenaltyPath_.empty()) {
        out.push_back(wadePenaltyPath_);
    }
}

float RouteConsiderer::wadePenalty() const {
    return wadePenalty_ != nullptr ? wadePenalty_->value() : wadePenaltyDefault_;
}

std::size_t RouteConsiderer::destinationsFor(const DecisionContext& ctx,
                                             std::vector<Destination>& out) const {
    const std::size_t before = out.size();
    if (!authored_.empty()) {
        // An author named the places. Their weight is 1: this considerer is being used to choose a
        // *way*, and the places have already been chosen.
        for (const Authored& place : authored_) {
            glm::vec3 at = place.point;
            if (!place.hasPoint) {
                // R1: `pointOfInterest` reports simulation positions for bodies and authored ones
                // for nodes. A name the world does not have contributes no option at all, which
                // shows in the overlay as a missing line rather than as a body walking somewhere
                // nobody asked for.
                if (ctx.world == nullptr || !ctx.world->pointOfInterest(place.name, at)) {
                    continue;
                }
            }
            out.push_back(Destination{place.name, at, 1.0f});
        }
        return out.size() - before;
    }
    // Nobody named anything, so the goal model chooses the places and this chooses the ways to
    // them. The top few by weight and no more: each destination costs two A* searches, and an
    // explorer that priced all twenty of its percepts would spend a millisecond of every decision
    // tick on routes it was never going to take.
    scratch_.clear();
    if (source_ == InterestConsiderer::Source::Omniscient) {
        if (ctx.world != nullptr) {
            scoreGoals(ctx, taste_, ctx.world->interestPoints(), scratch_);
        }
    } else {
        scoreGoals(ctx, taste_, ctx.percepts, scratch_);
    }
    // Highest weight first, ties broken on the position so the order cannot depend on the order
    // the sense stage happened to write its working set -- the same rule `investigate` breaks a
    // salience tie with, and for the same reason.
    std::stable_sort(scratch_.begin(), scratch_.end(),
                     [](const GoalCandidate& a, const GoalCandidate& b) {
                         if (a.weight != b.weight) {
                             return a.weight > b.weight;
                         }
                         if (a.position.x != b.position.x) {
                             return a.position.x < b.position.x;
                         }
                         return a.position.z < b.position.z;
                     });
    const std::size_t take = std::min<std::size_t>(scratch_.size(), maxDestinations_);
    for (std::size_t i = 0; i < take; ++i) {
        out.push_back(Destination{scratch_[i].name, scratch_[i].position, scratch_[i].weight});
    }
    return out.size() - before;
}

std::size_t RouteConsiderer::price(const DecisionContext& ctx, std::vector<Priced>& out) const {
    if (ctx.nav == nullptr || ctx.state == nullptr) {
        return 0;
    }
    // No graph, no two ways. `Navigator::requestPath` answers a gridless world with the straight
    // line whatever it is charged for water, so two requests would come back identical and this
    // would report "there is nothing wet between here and there" about a world it never asked.
    const NavGrid* grid = ctx.nav->grid();
    if (grid == nullptr || !grid->valid()) {
        return 0;
    }
    places_.clear();
    destinationsFor(ctx, places_);
    if (places_.empty()) {
        return 0;
    }

    const std::size_t before = out.size();
    const glm::vec3 here = ctx.state->position();
    const glm::vec2 from(here.x, here.z);
    const float w = wadePenalty();
    const float falloff = std::max(falloff_, 1.0f);
    NavPathCost ford;
    ford.wadePenalty = fordPenalty_;
    NavPathCost dry;
    dry.wadePenalty = detourPenalty_;

    for (const Destination& place : places_) {
        PathRequest request;
        request.from = from;
        request.to = glm::vec2(place.position.x, place.position.z);
        request.goalTolerance = goalTolerance_;
        // The two requests. Identical but for what a wet cell costs, which is the only way to ask
        // a planner "what would you do if you minded the water more than you do".
        const PathResult wet = ctx.nav->requestPath(request, ford);
        const PathResult round = ctx.nav->requestPath(request, dry);
        if (wet.status != PathStatus::Ok) {
            // Unreachable, already there, no standable goal -- none of them is a way, and an
            // option scoring zero would be a different claim from no option at all.
            continue;
        }
        const float wetWade = wadeAlong(*ctx.nav, from, wet.waypoints);
        // `place.weight` is 1 for an authored destination and the goal model's own weight for one
        // the goal model chose -- and the goal model's weight already carries a straight-line
        // distance damping, so a candidate that came from there is damped twice: once by how far
        // away it is and once by what the route to it costs. **That is a known simplification and
        // not an oversight.** Undoing it means re-deriving `goalWeight` without its falloff, which
        // is a second copy of the goal model and the thing ADR-333 §3 went to some trouble to have
        // exactly one of. The two terms are monotone in the same direction, so the ordering they
        // produce together is the ordering either produces alone wherever they agree; where they
        // disagree -- a near place behind a river against a far one on this bank -- the route term
        // is the larger of the two and wins, which is the case this class exists for.
        Priced first;
        first.destination = place.name;
        first.at = place.position;
        first.detour = false;
        first.length = wet.length;
        first.wadeMetres = wetWade;
        first.cost = wet.length + w * wetWade;
        first.score = weight() * place.weight / (1.0f + first.cost / falloff);
        first.status = wet.status;
        first.goal = wet.goal;
        first.waypoints = wet.waypoints;
        out.push_back(std::move(first));

        if (round.status != PathStatus::Ok) {
            continue;
        }
        const float roundWade = wadeAlong(*ctx.nav, from, round.waypoints);
        // Two ways or one. A second option identical to the first would put the selector's margin
        // between a route and itself and would print the same line twice in the overlay, so the
        // detour is only published when it is genuinely a different way -- which is what "there is
        // water between this body and that place" means, operationally, in this engine.
        const float quarter = 0.25f;
        if (std::abs(round.length - wet.length) <= quarter &&
            std::abs(roundWade - wetWade) <= quarter) {
            continue;
        }
        Priced second;
        second.destination = place.name;
        second.at = place.position;
        second.detour = true;
        second.length = round.length;
        second.wadeMetres = roundWade;
        second.cost = round.length + w * roundWade;
        second.score = weight() * place.weight / (1.0f + second.cost / falloff);
        second.status = round.status;
        second.goal = round.goal;
        second.waypoints = round.waypoints;
        out.push_back(std::move(second));
    }
    return out.size() - before;
}

void RouteConsiderer::consider(const DecisionContext& ctx, std::vector<Option>& out) const {
    priced_.clear();
    price(ctx, priced_);
    if (priced_.empty()) {
        return;
    }
    // Two passes, for the reason `interest` takes two: the spans handed out in `Option::actions`
    // must survive the whole of this call and a vector that grows moves its storage.
    actions_.clear();
    names_.clear();
    names_.reserve(priced_.size());
    std::size_t legs = 0;
    for (const Priced& p : priced_) {
        legs += p.waypoints.size() + 2;
    }
    actions_.reserve(legs);
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    ranges.reserve(priced_.size());
    const glm::vec3 here = ctx.state != nullptr ? ctx.state->position() : glm::vec3(0.0f);

    for (const Priced& p : priced_) {
        std::string label(p.destination);
        if (label.empty()) {
            label = "route@" + std::to_string(static_cast<int>(std::lround(p.goal.x))) + "," +
                    std::to_string(static_cast<int>(std::lround(p.goal.y)));
        }
        // The detour is the same errand by another way, so it is named for the errand and marked
        // rather than given an identity of its own. The short way keeps the bare name, which is
        // what makes it stable: it exists for every destination in every world, and a name that
        // appeared and vanished with the water would lose the selector its incumbent every time a
        // body stepped onto the far bank.
        names_.push_back(p.detour ? label + " round" : label);

        const std::size_t first = actions_.size();
        // **A `Move` per waypoint, and this is the load-bearing part.** `NavigatorPath::route`
        // answers a move with the straight line to the goal and leaves the rest to local steering
        // (`action.cpp:NavigatorPath::route`), which gets a body round a trunk and cannot get one
        // round a lake. An option called "go round" whose single action was "walk to the far bank"
        // would be a body that waded anyway, with the overlay reporting the detour and the film
        // showing the ford.
        for (std::size_t k = 0; k < p.waypoints.size(); ++k) {
            const bool last = k + 1 == p.waypoints.size();
            ActionDesc walk;
            walk.kind = ActionKind::Move;
            walk.name = names_.back();
            walk.target.kind = TargetKind::Point;
            const glm::vec2 at = last ? p.goal : p.waypoints[k];
            // y is not read: `ActionKind::Move` flattens its target and the grounding behaviour
            // owns the height. Carrying the destination's y on the last leg anyway, so a debug
            // draw of the action's target lands on the thing rather than on the sea floor.
            walk.target.point = glm::vec3(at.x, last ? p.at.y : 0.0f, at.y);
            if (last && approach_ > 0.0f) {
                // Stand off along the **last leg**, not along the line from where the body is
                // standing now. On a detour those are different directions -- the body approaches
                // the far bank from the east and the straight line from its start comes across
                // the river -- and a stand-off measured from the start would put a body that went
                // round on the wrong side of the thing it walked round the water to reach.
                const glm::vec2 previous = k > 0 ? p.waypoints[k - 1] : glm::vec2(here.x, here.z);
                walk.target.point = standOff(glm::vec3(previous.x, here.y, previous.y),
                                             walk.target.point, approach_);
            }
            walk.tolerance = last ? std::max(goalTolerance_, 0.75f) : legTolerance_;
            actions_.push_back(std::move(walk));
        }
        if (dwell_ > 0.0) {
            ActionDesc attend;
            attend.kind = ActionKind::Pose;
            attend.name = names_.back();
            attend.activity = activity_;
            attend.duration = dwell_;
            attend.target.kind = TargetKind::Point;
            attend.target.point = p.at; // the destination, not the leg's stand-off (ADR-300)
            actions_.push_back(std::move(attend));
        }
        ranges.emplace_back(first, actions_.size() - first);
    }
    for (std::size_t i = 0; i < priced_.size(); ++i) {
        out.push_back(Option{names_[i], priced_[i].score,
                             std::span<const ActionDesc>(actions_.data() + ranges[i].first,
                                                         ranges[i].second),
                             Authority::Routine});
        Option& o = out.back();
        o.intent = IntentType::MoveTo;
        o.target = priced_[i].at;
        o.hasTarget = true;
        o.addFactor("route cost", -priced_[i].cost);
    }
}

// ---- react (Phase D §26–§28) ---------------------------------------------------------------------

ReactConsiderer::ReactConsiderer(const nlohmann::json* settings)
    : events_(readStrings(settings, "events")),
      approach_(readFloat(settings, "approach", 4.0f)),
      flee_(readFloat(settings, "flee", 14.0f)),
      dwell_(static_cast<double>(readFloat(settings, "dwell", 3.0f))),
      activity_(readString(settings, "activity", "observe")),
      fleeActivity_(readString(settings, "fleeActivity", "react")),
      fadeSeconds_(std::max(0.1f, readFloat(settings, "fadeSeconds", 8.0f))),
      curiosityPull_(readFloat(settings, "curiosityPull", 2.0f)),
      cautionPull_(readFloat(settings, "cautionPull", 2.0f)) {
    weightDefault_ = readFloat(settings, "weight", 1.0f);
}

void ReactConsiderer::registerParameters(params::ParameterSet& params, const std::string& prefix) {
    registerWeight(params, prefix, weightDefault_);
}

void ReactConsiderer::collectParameterPaths(std::vector<std::string>& out) const {
    collectWeightPath(out);
}

void ReactConsiderer::consider(const DecisionContext& ctx, std::vector<Option>& out) const {
    // Nothing heard without the awareness layer: a decider that did not opt in has no ears.
    if (ctx.mind == nullptr || ctx.state == nullptr || ctx.world == nullptr) {
        return;
    }
    if (approachName_.empty()) {
        approachName_ = std::string(name()) + "/approach";
        fleeName_ = std::string(name()) + "/flee";
    }
    // The strongest event still worth reacting to. Ties on the lower sequence: the earlier event.
    const PerceivedEvent* chosen = nullptr;
    float strength = 0.0f;
    float fresh = 0.0f;
    float novelty = 0.0f;
    for (const PerceivedEvent& e : ctx.mind->events) {
        if (!events_.empty()) {
            const std::string_view type = ctx.world->eventName(e.type);
            if (std::find(events_.begin(), events_.end(), type) == events_.end()) {
                continue;
            }
        }
        const SubjectId id = eventSubject(e.sequence);
        if (ctx.mind->suppressed(id, ctx.time)) {
            continue;
        }
        const float age = static_cast<float>(std::max(0.0, ctx.time - e.time));
        const float f = std::max(0.0f, 1.0f - age / fadeSeconds_);
        const float n = ctx.mind->novelty(id, ctx.time);
        const float s = e.intensity * f * n;
        if (s > strength) {
            chosen = &e;
            strength = s;
            fresh = f;
            novelty = n;
        }
    }
    if (chosen == nullptr || !(strength > 0.0f)) {
        return;
    }
    const Personality& p =
        ctx.mind->personality != nullptr ? *ctx.mind->personality : neutralPersonality();
    const glm::vec3 here = ctx.state->position();
    const SubjectId subject = eventSubject(chosen->sequence);

    // ---- approach: go and see ----
    approachActions_.clear();
    {
        ActionDesc walk;
        walk.kind = ActionKind::Move;
        walk.name = approachName_;
        walk.target.kind = TargetKind::Point;
        walk.target.point = standOff(here, chosen->position, approach_);
        walk.tolerance = std::max(approach_ * 0.5f, 0.75f);
        walk.arrival = kArrival;
        approachActions_.push_back(walk);
        ActionDesc face;
        face.kind = ActionKind::Face;
        face.name = approachName_;
        face.target.kind = TargetKind::Point;
        face.target.point = chosen->position;
        approachActions_.push_back(face);
        if (dwell_ > 0.0) {
            ActionDesc attend;
            attend.kind = ActionKind::Pose;
            attend.name = approachName_;
            attend.activity = activity_;
            attend.duration = dwell_;
            attend.target.kind = TargetKind::Point;
            attend.target.point = chosen->position;
            approachActions_.push_back(attend);
        }
    }
    const float curious = traitFactor(p.curiosity, curiosityPull_) * traitFactor(p.caution, -1.0f);
    out.push_back(Option{approachName_, weight() * strength * curious,
                         std::span<const ActionDesc>(approachActions_), Authority::Routine});
    {
        Option& o = out.back();
        o.intent = IntentType::Investigate;
        o.subject = subject;
        o.target = chosen->position;
        o.hasTarget = true;
        o.urgency = std::clamp(chosen->intensity, 0.0f, 1.0f);
        o.stoppingDistance = approach_;
        o.addFactor("intensity", chosen->intensity);
        o.addFactor("freshness", fresh);
        o.addFactor("novelty", novelty);
        o.addFactor("personality", curious);
        o.addFactor("weight", weight());
    }

    // ---- flee: get away, then watch it ----
    fleeActions_.clear();
    {
        ActionDesc run;
        run.kind = ActionKind::Move;
        run.name = fleeName_;
        run.target.kind = TargetKind::Point;
        run.target.point = awayFrom(here, chosen->position, flee_, ctx.state->yaw);
        run.tolerance = 1.5f;
        fleeActions_.push_back(run);
        ActionDesc face;
        face.kind = ActionKind::Face;
        face.name = fleeName_;
        face.target.kind = TargetKind::Point;
        face.target.point = chosen->position;
        fleeActions_.push_back(face);
        if (dwell_ > 0.0) {
            ActionDesc watch;
            watch.kind = ActionKind::Pose;
            watch.name = fleeName_;
            watch.activity = fleeActivity_;
            watch.duration = dwell_;
            watch.target.kind = TargetKind::Point;
            watch.target.point = chosen->position;
            fleeActions_.push_back(watch);
        }
    }
    const float wary = traitFactor(p.caution, cautionPull_) * traitFactor(p.curiosity, -1.0f);
    out.push_back(Option{fleeName_, weight() * strength * wary,
                         std::span<const ActionDesc>(fleeActions_), Authority::Routine});
    {
        Option& o = out.back();
        o.intent = IntentType::Flee;
        o.subject = subject;
        o.target = chosen->position;
        o.hasTarget = true;
        o.urgency = std::clamp(chosen->intensity * 1.5f, 0.0f, 1.0f);
        o.addFactor("intensity", chosen->intensity);
        o.addFactor("freshness", fresh);
        o.addFactor("novelty", novelty);
        o.addFactor("personality", wary);
        o.addFactor("weight", weight());
    }
}

// ---- social (Phase D §24) -----------------------------------------------------------------------

SocialConsiderer::SocialConsiderer(const nlohmann::json* settings)
    : tags_(readStrings(settings, "tags")),
      relationship_(readString(settings, "relationship", "neutral")),
      distance_(readFloat(settings, "distance", 3.0f)),
      personalSpace_(readFloat(settings, "personalSpace", 2.5f)),
      keepAway_(readFloat(settings, "keepAway", 6.0f)),
      dwell_(static_cast<double>(readFloat(settings, "dwell", 3.0f))),
      activity_(readString(settings, "activity", "observe")),
      sociabilityPull_(readFloat(settings, "sociabilityPull", 2.0f)),
      cautionPull_(readFloat(settings, "cautionPull", 2.0f)) {
    weightDefault_ = readFloat(settings, "weight", 1.0f);
}

void SocialConsiderer::registerParameters(params::ParameterSet& params, const std::string& prefix) {
    registerWeight(params, prefix, weightDefault_);
}

void SocialConsiderer::collectParameterPaths(std::vector<std::string>& out) const {
    collectWeightPath(out);
}

void SocialConsiderer::consider(const DecisionContext& ctx, std::vector<Option>& out) const {
    if (ctx.mind == nullptr || ctx.state == nullptr || ctx.world == nullptr) {
        return;
    }
    if (greetName_.empty()) {
        greetName_ = std::string(name()) + "/greet";
        avoidName_ = std::string(name()) + "/avoid";
    }
    const std::uint64_t mask = tags_.empty() ? ~std::uint64_t{0} : maskOf(ctx, tags_);
    // The most salient other body carrying one of the tags. Ties on the lower entity index.
    const Percept* other = nullptr;
    for (const Percept& p : ctx.percepts) {
        if (p.kind != InterestKind::Character || (p.tags & mask) == 0 ||
            p.source >= ctx.world->entities().size() || p.source == ctx.self) {
            continue;
        }
        if (other == nullptr || p.salience > other->salience ||
            (p.salience == other->salience && p.source < other->source)) {
            other = &p;
        }
    }
    if (other == nullptr) {
        return;
    }
    const Entity& them = *ctx.world->entities()[other->source];
    const SubjectId subject = bodySubject(other->source);
    const Personality& p =
        ctx.mind->personality != nullptr ? *ctx.mind->personality : neutralPersonality();
    const glm::vec3 here = ctx.state->position();
    const glm::vec3 there = them.state().position(); // R1: where it is now
    const float gap = glm::length(glm::vec2(there.x - here.x, there.z - here.z));
    // Never closer than its own personal space allows: a greeting that ended inside the distance
    // this body backs away from would make "approach" and "avoid" take turns (measured: greet ->
    // avoid -> greet inside two seconds on the autonomy demo's cautious warden).
    const float comfortable = std::max(distance_ * (0.5f + p.preferredDistance),
                                       personalSpace_ * (0.5f + p.preferredDistance) * 1.3f);

    const float space = personalSpace_ * (0.5f + p.preferredDistance);

    // ---- greet: only from outside personal space, with a band ----
    //
    // A body does not walk toward someone it is backing away from. Greeting is offered only beyond
    // 1.2x personal space and avoiding only inside it, so between the two neither is on offer and
    // the pair cannot take turns (measured: greet -> avoid -> greet inside two seconds before this).
    if (!ctx.mind->suppressed(subject, ctx.time) && gap > space * 1.2f) {
        greetActions_.clear();
        ActionDesc walk;
        walk.kind = ActionKind::Move;
        walk.name = greetName_;
        walk.target.kind = TargetKind::Point;
        walk.target.point = standOff(here, there, comfortable);
        walk.tolerance = std::max(comfortable * 0.4f, 0.75f);
        walk.arrival = kArrival;
        greetActions_.push_back(walk);
        ActionDesc face;
        face.kind = ActionKind::Face;
        face.name = greetName_;
        face.target.kind = TargetKind::EntityRef;
        face.target.name = them.name();
        greetActions_.push_back(face);
        if (dwell_ > 0.0) {
            ActionDesc attend;
            attend.kind = ActionKind::Pose;
            attend.name = greetName_;
            attend.activity = activity_;
            attend.duration = dwell_;
            attend.target.kind = TargetKind::EntityRef;
            attend.target.name = them.name();
            greetActions_.push_back(attend);
        }
        const float novelty = ctx.mind->novelty(subject, ctx.time);
        const float social = traitFactor(p.sociability, sociabilityPull_) * traitFactor(p.caution, -0.5f);
        out.push_back(Option{greetName_, weight() * other->salience * novelty * social,
                             std::span<const ActionDesc>(greetActions_), Authority::Routine});
        Option& o = out.back();
        o.intent = IntentType::Socialize;
        o.subject = subject;
        o.target = there;
        o.hasTarget = true;
        o.stoppingDistance = comfortable;
        o.addFactor("salience", other->salience);
        o.addFactor("novelty", novelty);
        o.addFactor("personality", social);
        o.addFactor("weight", weight());
    }

    // ---- avoid: only when the other body is inside personal space ----
    if (gap < space) {
        avoidActions_.clear();
        ActionDesc step;
        step.kind = ActionKind::Move;
        step.name = avoidName_;
        step.target.kind = TargetKind::Point;
        step.target.point = awayFrom(here, there, std::max(keepAway_ - gap, 1.0f), ctx.state->yaw);
        step.tolerance = 0.75f;
        avoidActions_.push_back(step);
        ActionDesc face;
        face.kind = ActionKind::Face;
        face.name = avoidName_;
        face.target.kind = TargetKind::EntityRef;
        face.target.name = them.name();
        avoidActions_.push_back(face);
        const float crowding = 1.0f - gap / std::max(space, 1e-3f);
        const float wary = traitFactor(p.caution, cautionPull_) * traitFactor(p.sociability, -1.0f);
        out.push_back(Option{avoidName_, weight() * crowding * wary,
                             std::span<const ActionDesc>(avoidActions_), Authority::Routine});
        Option& o = out.back();
        o.intent = IntentType::Avoid;
        o.subject = subject;
        o.target = there;
        o.hasTarget = true;
        o.urgency = crowding;
        o.addFactor("crowding", crowding);
        o.addFactor("personality", wary);
        o.addFactor("weight", weight());
    }
}

// ---- goal (Phase D §34) --------------------------------------------------------------------------

GoalConsiderer::GoalConsiderer(const nlohmann::json* settings)
    : subject_(readString(settings, "subject", "")),
      affordance_(readString(settings, "affordance", "")),
      intent_(readIntent(settings, IntentType::Investigate)),
      from_(static_cast<double>(readFloat(settings, "from", 0.0f))),
      until_(static_cast<double>(readFloat(settings, "until", 0.0f))),
      approach_(readFloat(settings, "approach", 2.0f)),
      dwell_(static_cast<double>(readFloat(settings, "dwell", 3.0f))),
      activity_(readString(settings, "activity", "observe")) {
    weightDefault_ = readFloat(settings, "weight", 5.0f);
}

void GoalConsiderer::registerParameters(params::ParameterSet& params, const std::string& prefix) {
    registerWeight(params, prefix, weightDefault_);
}

void GoalConsiderer::collectParameterPaths(std::vector<std::string>& out) const {
    collectWeightPath(out);
}

void GoalConsiderer::consider(const DecisionContext& ctx, std::vector<Option>& out) const {
    if (ctx.world == nullptr || ctx.state == nullptr || subject_.empty() || !(weight() > 0.0f) ||
        ctx.time < from_ || (until_ > 0.0 && ctx.time >= until_)) {
        return;
    }
    glm::vec3 at{0.0f};
    if (!ctx.world->pointOfInterest(subject_, at)) {
        return; // the named thing does not exist (any more): no option, and the trace says why not
    }
    // Which entity it is, if it is one, for identity and for its affordances.
    const Entity* body = nullptr;
    std::size_t index = 0;
    for (std::size_t i = 0; i < ctx.world->entities().size(); ++i) {
        if (ctx.world->entities()[i]->name() == subject_) {
            body = ctx.world->entities()[i].get();
            index = i;
        }
    }
    const SubjectId subject = body != nullptr ? bodySubject(index) : kNoSubject;
    float novelty = 1.0f;
    if (ctx.mind != nullptr && subject != kNoSubject) {
        if (ctx.mind->suppressed(subject, ctx.time)) {
            return;
        }
        // Done since the goal opened: the errand is over. Investigated before it opened does not
        // count -- a goal set at 32 s is a new request even if the body saw the thing at 10 s.
        if (const MemoryEntry* e = ctx.mind->memory != nullptr ? ctx.mind->memory->find(subject) : nullptr;
            e != nullptr && e->investigatedAt >= from_) {
            return;
        }
        novelty = 1.0f;
    }
    const InteractionDesc* verb = nullptr;
    if (body != nullptr && !affordance_.empty()) {
        verb = body->interaction(affordance_);
        const Entity* self = ctx.self < ctx.world->entities().size() ? ctx.world->entities()[ctx.self].get() : nullptr;
        if (verb != nullptr && (self == nullptr || !self->can(verb->required))) {
            verb = nullptr;
        }
    }
    const float approach = verb != nullptr ? std::min(approach_, verb->range * 0.6f) : approach_;
    actions_.clear();
    ActionDesc walk;
    walk.kind = ActionKind::Move;
    walk.name = name_;
    walk.target.kind = TargetKind::Point;
    walk.target.point = standOff(ctx.state->position(), at, approach);
    walk.tolerance = std::max(approach * 0.4f, 0.3f);
    walk.arrival = kArrival;
    actions_.push_back(walk);
    ActionDesc face;
    face.kind = ActionKind::Face;
    face.name = name_;
    face.target.kind = TargetKind::Point;
    face.target.point = at;
    actions_.push_back(face);
    if (verb != nullptr) {
        ActionDesc use;
        use.kind = ActionKind::Interact;
        use.name = name_;
        use.target.kind = TargetKind::Interaction;
        use.target.name = subject_;
        use.target.member = verb->name;
        actions_.push_back(use);
    } else if (dwell_ > 0.0) {
        ActionDesc attend;
        attend.kind = ActionKind::Pose;
        attend.name = name_;
        attend.activity = activity_;
        attend.duration = dwell_;
        attend.target.kind = TargetKind::Point;
        attend.target.point = at;
        actions_.push_back(attend);
    }
    out.push_back(Option{name_, weight() * novelty, std::span<const ActionDesc>(actions_), Authority::Routine});
    Option& o = out.back();
    o.intent = verb != nullptr ? IntentType::Interact : intent_;
    o.subject = subject;
    o.target = at;
    o.hasTarget = true;
    o.stoppingDistance = approach;
    o.addFactor("goal", weight());
}

// ---- the factory -------------------------------------------------------------------------------

std::vector<std::string_view> considererKinds() {
    return {"idle", "holdPost", "investigate", "interest", "route", "react", "social", "goal"};
}

std::unique_ptr<StockConsiderer> makeConsiderer(std::string_view kind,
                                                const nlohmann::json* settings) {
    std::unique_ptr<StockConsiderer> made;
    if (kind == "idle") {
        made = std::make_unique<IdleConsiderer>(settings);
    } else if (kind == "holdPost") {
        made = std::make_unique<HoldPostConsiderer>(settings);
    } else if (kind == "investigate") {
        made = std::make_unique<InvestigateConsiderer>(settings);
    } else if (kind == "interest") {
        made = std::make_unique<InterestConsiderer>(settings);
    } else if (kind == "route") {
        made = std::make_unique<RouteConsiderer>(settings);
    } else if (kind == "react") {
        made = std::make_unique<ReactConsiderer>(settings);
    } else if (kind == "social") {
        made = std::make_unique<SocialConsiderer>(settings);
    } else if (kind == "goal") {
        made = std::make_unique<GoalConsiderer>(settings);
    }
    if (made != nullptr) {
        made->setName(readString(settings, "name", std::string(kind)));
    }
    return made;
}

// ---- bounded memory ----------------------------------------------------------------------------

void PerceptMemory::reset() {
    remembered_.clear();
    merged_.clear();
}

std::span<const Percept> PerceptMemory::merge(std::span<const Percept> live, double time) {
    if (!(settings_.seconds > 0.0f) || settings_.capacity == 0) {
        remembered_.clear();
        return live; // ADR-290's behaviour exactly: a thing that left the range is gone
    }
    merged_.assign(live.begin(), live.end());
    // Everything still remembered that this tick did not see again, newest first, until the cap.
    const auto isLive = [&](const Percept& p) {
        return std::any_of(live.begin(), live.end(), [&](const Percept& l) {
            return l.kind == p.kind && l.source == p.source;
        });
    };
    std::erase_if(remembered_, [&](const Percept& p) {
        return isLive(p) || (time - p.seenAt) > static_cast<double>(settings_.seconds);
    });
    for (const Percept& p : remembered_) {
        if (merged_.size() >= static_cast<std::size_t>(settings_.capacity) + live.size()) {
            break;
        }
        merged_.push_back(p);
    }
    // What this tick saw becomes what the next tick may remember. Kept in (kind, source) order so
    // the fade cannot depend on the order the sense stage wrote its working set.
    for (const Percept& p : live) {
        remembered_.push_back(p);
    }
    std::stable_sort(remembered_.begin(), remembered_.end(), [](const Percept& a, const Percept& b) {
        if (a.seenAt != b.seenAt) {
            return a.seenAt > b.seenAt;
        }
        if (a.kind != b.kind) {
            return a.kind < b.kind;
        }
        return a.source < b.source;
    });
    if (remembered_.size() > settings_.capacity) {
        remembered_.resize(settings_.capacity);
    }
    return merged_;
}

} // namespace avgen::entity
