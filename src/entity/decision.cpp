#include "entity/decision.hpp"

#include "entity/entity.hpp"

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

void Selector::reset() {
    options_.clear();
    current_.clear();
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
    float runnerUp = 0.0f;
    for (std::size_t i = 0; i < options_.size(); ++i) {
        const float score = options_[i].score;
        if (score <= 0.0f) {
            continue;
        }
        if (best == kNone || score > bestScore) {
            runnerUp = best == kNone ? 0.0f : bestScore;
            best = i;
            bestScore = score;
        } else if (score > runnerUp) {
            runnerUp = score;
        }
    }
    if (best == kNone) {
        ++counts_.empty;
        chosen_ = kNone;
        current_.clear();
        return false;
    }

    // The incumbent, found by name in *this* tick's list. Not by index: a considerer may append a
    // different number of options from one tick to the next -- `interest` does, every time the body
    // moves -- so an index kept across a tick names a different option.
    std::size_t incumbent = kNone;
    for (std::size_t i = 0; i < options_.size(); ++i) {
        if (!current_.empty() && options_[i].name == current_ && options_[i].score > 0.0f) {
            incumbent = i;
            break;
        }
    }

    const bool commit = [&] {
        if (incumbent == kNone) {
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

    if (!commit) {
        chosen_ = incumbent;
        // `margin` on the debug is the winner's lead over the runner-up, and the runner-up of a
        // held choice is whatever beat it -- which is the number that says how close it came.
        (void)runnerUp;
        return false;
    }

    const bool changed = current_ != options_[best].name;
    chosen_ = best;
    current_.assign(options_[best].name);
    if (changed) {
        committedTick_ = tick_;
        ++counts_.decisions;
    }
    return changed;
}

// ---- the goal model ----------------------------------------------------------------------------

// **Do not tidy this function.** Every expression in it is the expression `Explore::pickGoal`
// inlined before ADR-310, in the same order, and the extraction's control is a position trace taken
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
    actions_.clear();
    ActionDesc walk;
    walk.kind = ActionKind::Move;
    walk.name = name_;
    walk.target.kind = TargetKind::Point;
    // R1 again, and it is the whole reason this is safe: a percept's position is the perceived
    // body's `state().position()` and never its `visualPosition()`, so a guard sent to meet a
    // hovering saucer walks to where the saucer is rather than to where it is drawn.
    walk.target.point = standOff(ctx.state != nullptr ? ctx.state->position() : glm::vec3(0.0f),
                                 target.position, approach_);
    walk.tolerance = std::max(approach_ * 0.5f, 0.75f);
    actions_.push_back(std::move(walk));

    ActionDesc look;
    look.kind = ActionKind::Face;
    look.name = name_;
    look.target.kind = TargetKind::Point;
    look.target.point = target.position;
    actions_.push_back(std::move(look));

    if (dwell_ > 0.0) {
        ActionDesc attend;
        attend.kind = ActionKind::Pose;
        attend.name = name_;
        attend.activity = activity_;
        attend.duration = dwell_;
        actions_.push_back(std::move(attend));
    }
    out.push_back(Option{name_, weight() * score, std::span<const ActionDesc>(actions_),
                         Authority::Routine});
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
    }
}

// ---- the factory -------------------------------------------------------------------------------

std::vector<std::string_view> considererKinds() {
    return {"idle", "holdPost", "investigate", "interest"};
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
