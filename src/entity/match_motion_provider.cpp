#include "entity/match_motion_provider.hpp"

#include "scene/animation.hpp"
#include "scene/skeleton.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::entity {
namespace {

// Where the intent block sits in the feature vector: after the per-joint block, the trajectory
// block, and then the root velocity. Derived from the config rather than assumed, because the
// config is data-driven (§8) and a hardcoded offset would be wrong the first time somebody adds a
// joint.
struct FeatureLayout {
    std::size_t trajectory = 0; // first trajectory dimension
    std::size_t rootVelocity = 0;
    std::size_t total = 0;
};

FeatureLayout layoutOf(const scene::MotionFeatureConfig& config) {
    FeatureLayout out;
    out.trajectory = config.joints.size() * 6u;
    out.rootVelocity = out.trajectory + (config.trajectoryTimes.size() * 4u);
    out.total = config.dimension();
    return out;
}

} // namespace

MotionResult MatchMotionProvider::advance(const MotionRequest& request, const MotionMemory& in,
                                          double time, float dt, MotionMemory& next) const {
    MotionResult result;
    next = in;
    if (db_ == nullptr || clips_ == nullptr || db_->sampleCount() == 0) {
        result.status = MotionStatus::NotReady;
        return result;
    }
    if (request.mode != MovementMode::Ground) {
        // §35: decline rather than guess. A matcher whose database is all ground locomotion has
        // nothing to say about a body in the air, and the chain's clip provider does.
        result.status = MotionStatus::Unsupported;
        return result;
    }

    const FeatureLayout layout = layoutOf(db_->config);
    const bool haveCurrent = in.generation > 0 && in.selection < db_->sampleCount();

    // ---- continue, if it is not time to search (§27/§29) --------------------------------------
    //
    // Following `sampleNext` is an array read. That is the whole reason motion matching is
    // affordable: the scan happens ten times a second, not sixty.
    const double sinceSearch = time - in.decisionTime;
    const bool due = !haveCurrent || sinceSearch >= static_cast<double>(settings_.searchInterval);
    const bool locked = haveCurrent && sinceSearch < static_cast<double>(settings_.minimumContinuation);

    if (haveCurrent && (!due || locked)) {
        const std::uint32_t follow = db_->sampleNext[in.selection];
        if (follow != scene::MotionDatabase::kInvalid) {
            next.selection = follow;
            next.localTime = db_->sampleTime[follow];
            next.phase = db_->samplePhase[follow];
            next.hasPhase = true;
            // §32: the blends' own clocks run on every step, including the ones that do not
            // search.
            next.tickBlends(dt);
            ++counters_.continued;
            result.status = MotionStatus::Produced;
            result.content = db_->clipNames[db_->sampleClip[follow]];
            return result;
        }
        // The clip ended and does not loop, so a search is due whatever the clock says.
    }

    // ---- build the query -----------------------------------------------------------------------
    //
    // **The pose half is a lookup.** The character is playing a database sample, so what it looks
    // like right now is that sample's own feature vector -- already extracted and already
    // standardised. Only the intent half is computed, and it is computed from the request.
    std::vector<float> features(db_->dimension, 0.0f);
    if (haveCurrent) {
        const float* current = db_->featuresFor(in.selection);
        std::copy(current, current + db_->dimension, features.begin());
    }

    // The intent half, in raw units, standardised the same way the database was. Root velocity is
    // what the body wants to be doing; the trajectory block is where it wants to be at each
    // horizon, which for a constant desired velocity is that velocity times the horizon.
    std::vector<float> raw(db_->dimension, 0.0f);
    const glm::vec3 want = request.desiredVelocity + request.steering;
    for (std::size_t t = 0; t < db_->config.trajectoryTimes.size(); ++t) {
        const float ahead = db_->config.trajectoryTimes[t];
        const std::size_t base = layout.trajectory + (t * 4u);
        raw[base + 0] = want.x * ahead;
        raw[base + 1] = want.z * ahead;
        const float len = std::sqrt((want.x * want.x) + (want.z * want.z));
        raw[base + 2] = len > 1e-5f ? want.x / len : 0.0f;
        raw[base + 3] = len > 1e-5f ? want.z / len : 0.0f;
    }
    raw[layout.rootVelocity + 0] = want.x;
    raw[layout.rootVelocity + 1] = want.y;
    raw[layout.rootVelocity + 2] = want.z;
    scene::normaliseQuery(*db_, raw);

    // Overwrite the intent dimensions of the pose-derived query with what the character wants.
    // Blended by `intentWeight` against what it is already doing, so a body already moving the
    // right way is not yanked toward an idealised trajectory it is fractionally off.
    const float w = std::clamp(settings_.intentWeight, 0.0f, 1.0f);
    for (std::size_t d = layout.trajectory; d < layout.total && d < layout.rootVelocity + 3; ++d) {
        features[d] = (features[d] * (1.0f - w)) + (raw[d] * w);
    }

    scene::MotionQuery query;
    query.features = std::move(features);
    query.current = haveCurrent ? in.selection : scene::MotionDatabase::kInvalid;
    // §14: a body on the ground never wants an airborne sample. One AND per sample, and it removes
    // whole clips before anything is scored.
    query.rejectTags = static_cast<std::uint32_t>(scene::MotionTag::Airborne);

    const scene::MotionMatch match = scene::searchMotion(*db_, query, settings_.weights);
    ++counters_.searches;
    counters_.scored += match.considered;
    if (!match.found()) {
        result.status = MotionStatus::NoContent;
        return result;
    }

    // §28: a candidate must beat carrying on by a margin, or the motion is held. Without this a
    // body sitting between two samples re-selects on every search and commits to neither.
    std::uint32_t chosen = match.sample;
    if (haveCurrent) {
        const std::uint32_t follow = db_->sampleNext[in.selection];
        if (follow != scene::MotionDatabase::kInvalid && follow != match.sample) {
            // The continuation's own cost, under the same query, so the comparison is like for
            // like rather than the winner against a remembered number.
            const float* f = db_->featuresFor(follow);
            // **Weighted, because `match.cost` is.** This summed raw squared deltas while the
            // search it is compared against applies per-dimension weights (§10) -- so the two were
            // in different scales and the comparison was invalid. §10 made the weights live for
            // the first time and nothing here was updated to match; at the default weight vector
            // the discrepancy is small, which is exactly why it survived.
            //
            // A comparison between two costs computed by different formulas is worse than no
            // comparison: it has a defensible-looking number on both sides.
            const std::vector<float> dimWeight = scene::motionFeatureWeights(db_->config);
            const bool weighted = dimWeight.size() == db_->dimension;
            float continueCost = 0.0f;
            for (std::size_t d = 0; d < db_->dimension; ++d) {
                const float delta = query.features[d] - f[d];
                continueCost += delta * delta * (weighted ? dimWeight[d] : 1.0f);
            }
            // §28: the margin is a fraction of the measured spread, so it is in the cost
            // function's own scale rather than in raw units that mean nothing without it.
            const float margin = settings_.switchMargin * db_->stats.costSpread;
            if (match.cost > continueCost - margin) {
                chosen = follow;
                ++counters_.heldByMargin;
            }
        }
    }

    const bool switched = !haveCurrent || chosen != db_->sampleNext[in.selection];
    if (switched) {
        ++counters_.switches;
    }
    // §32/ADR-613. **A blend begins where the motion jumps, and nowhere else.** A search whose
    // winner was the continuation changed nothing, so restarting the decay there would throw away
    // a running blend for a transition that did not happen -- and would do it on the majority of
    // searches, because most searches continue. This is the same distinction `counters_.switches`
    // already draws, reused rather than re-derived so the two cannot disagree.
    if (switched && haveCurrent) {
        next.tickBlends(dt);
        next.pushBlend(in.selection, db_->sampleTime[in.selection], chosen,
                       db_->sampleTime[chosen], settings_.blendSlots);
    } else if (switched) {
        // The first selection of a character's life is not a transition: there is nothing to
        // blend from, and pretending otherwise would decay an offset against a bind pose.
        next.clearBlends();
    } else {
        next.tickBlends(dt);
    }
    next.selection = chosen;
    next.localTime = db_->sampleTime[chosen];
    next.phase = db_->samplePhase[chosen];
    next.hasPhase = true;
    // **This counts SEARCHES, not selection changes, and `MotionMemory::generation` is documented
    // as the latter** -- "bumped whenever `selection` changes, so a consumer can tell 'still
    // playing the same thing' from 'playing the same thing again'". `ClipMotionProvider` honours
    // that; this bumps unconditionally, including on the majority of searches whose winner was the
    // continuation and which changed nothing.
    //
    // **One field, two writers, two meanings** -- the same defect as the field three lines above
    // it, which was renamed `decisionTime` for exactly this reason. It is harmless only because
    // every live reader tests `> 0` as a presence check; the first consumer to use it the way the
    // header describes gets a wrong answer under this provider. Left as-is rather than changed
    // inside a documentation pass, because `switched` is right there and the fix is a behaviour
    // change that wants its own before-and-after (ADR-615).
    next.generation = in.generation + 1;
    next.decisionTime = time;
    result.status = MotionStatus::Produced;
    result.content = db_->clipNames[db_->sampleClip[chosen]];
    return result;
}

MotionResult MatchMotionProvider::pose(const MotionMemory& memory, const scene::Skeleton& skeleton,
                                       scene::Pose& out) const {
    MotionResult result;
    if (db_ == nullptr || clips_ == nullptr) {
        result.status = MotionStatus::NotReady;
        return result;
    }
    // `selection` is a SAMPLE index in this provider's space. Validated rather than trusted: the
    // clip provider reads the same field as a clip index, and ADR-556's `MotionMemory::provider`
    // is what stops them being confused -- this is the second lock on that door.
    if (memory.selection >= db_->sampleCount()) {
        result.status = MotionStatus::NoContent;
        return result;
    }
    const std::uint32_t clipIndex = db_->sampleClip[memory.selection];
    if (clipIndex >= clips_->size()) {
        result.status = MotionStatus::NoContent;
        return result;
    }
    const scene::AnimationClip& clip = (*clips_)[clipIndex];
    scene::setRestPose(skeleton, out);
    scene::sampleClip(clip, db_->sampleTime[memory.selection], out);
    result.status = MotionStatus::Produced;
    result.content = clip.name;

    // ---- §32: inertialize across the seam (ADR-613) --------------------------------------------
    //
    // The pose above is the incoming motion, alone. What is added here is **the difference the
    // switch introduced**, decaying to nothing -- so at the instant of the switch the output is
    // exactly the outgoing pose, and from there it converges on the incoming one without ever
    // teleporting. That is ADR-547's inertialization on `AnimationPlayer`, applied at the provider
    // seam, and it is deliberately the same arithmetic rather than a second scheme: the bar for
    // switching `proceduralMotion` on is parity with the player it replaces, and a provider that
    // blended *differently* would be a change of look as well as of architecture.
    //
    // **The offset is recomputed, never remembered.** Both ends are sampled from the two database
    // frames the memory names, so a scrub that lands mid-transition reconstructs the same offset
    // instead of inheriting one from wherever the playhead came from -- the property ADR-360
    // requires and the reason `MotionMemory` can hold two integers where an engine would hold a
    // pose.
    if (settings_.inertializeHalflife <= 0.0f || !memory.blending()) {
        return result;
    }
    // Scratch for the two ends of one offset. `thread_local` rather than a member:
    // `IMotionProvider` is const because one instance serves every character in a scene, so a
    // mutable member would be shared state between bodies the moment anything poses two of them at
    // once.
    thread_local scene::Pose was;
    thread_local scene::Pose became;

    // **Oldest first.** The offsets were introduced in order, so they are re-applied in order;
    // translations add either way, and rotations do not, so the order is the one that matches how
    // the pose was actually built up.
    const std::size_t slots =
        std::min(std::max<std::size_t>(settings_.blendSlots, 1), MotionMemory::kBlendSlots);
    for (std::size_t s = slots; s-- > 0;) {
        const MotionMemory::Blend& blend = memory.blends[s];
        if (!blend.live() || blend.from >= db_->sampleCount() || blend.to >= db_->sampleCount()) {
            continue; // an empty slot, or a memory from another database
        }
        const float decay = inertializationDecay(settings_.inertializeHalflife, blend.elapsed);
        if (decay <= kInertializationFloor) {
            continue; // what is left of this offset is below the floor
        }
        const std::uint32_t fromClip = db_->sampleClip[blend.from];
        const std::uint32_t toClip = db_->sampleClip[blend.to];
        if (fromClip >= clips_->size() || toClip >= clips_->size()) {
            continue;
        }
        scene::setRestPose(skeleton, was);
        scene::sampleClip((*clips_)[fromClip], blend.fromTime, was);
        scene::setRestPose(skeleton, became);
        scene::sampleClip((*clips_)[toClip], blend.toTime, became);
        applyInertializedOffset(was, became, decay, out);
    }
    return result;
}

} // namespace avgen::entity
