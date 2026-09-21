#include "scene/motion_match_explain.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::scene {

const char* matchCauseName(MatchCause cause) {
    switch (cause) {
    case MatchCause::Features: return "features";
    case MatchCause::Weights: return "weights";
    case MatchCause::Filtering: return "filtering";
    case MatchCause::Trajectory: return "trajectory";
    case MatchCause::Coverage: return "coverage";
    case MatchCause::Continuity: return "continuity";
    case MatchCause::Contact: return "contact";
    case MatchCause::Approximation: return "search approximation";
    }
    return "?";
}

std::string motionSampleLabel(const MotionDatabase& db, std::uint32_t sample) {
    if (sample >= db.sampleCount()) {
        return "(none)";
    }
    const std::uint32_t clip = db.sampleClip[sample];
    return fmt::format("{} @ {:.2f}s (sample {})",
                       clip < db.clipNames.size() ? db.clipNames[clip] : std::string("?"),
                       db.sampleTime[sample], sample);
}

MotionCandidateCost motionCandidateCost(const MotionDatabase& db, const MotionQuery& query,
                                        const MotionCostWeights& weights, std::uint32_t sample) {
    MotionCandidateCost out;
    const std::size_t dim = db.dimension;
    if (sample >= db.sampleCount() || query.features.size() != dim) {
        return out;
    }
    out.sample = sample;
    const std::uint32_t tags = db.sampleTags[sample];
    out.passesFilter =
        !((query.requireTags != 0 && (tags & query.requireTags) != query.requireTags) ||
          (query.rejectTags != 0 && (tags & query.rejectTags) != 0));

    // **The same arithmetic as `searchMotion`, in the same order**, so `total` for the search's
    // winner equals `MotionMatch::cost` to the bit. A test holds the two together.
    const std::vector<MotionFeatureGroup> layout = motionFeatureLayout(db.config);
    const std::vector<float> dimWeight = motionFeatureWeights(db.config);
    const bool weighted = dimWeight.size() == dim && layout.size() == dim;
    const float* q = query.features.data();
    const float* f = db.featuresFor(sample);
    float cost = 0.0f;
    for (std::size_t d = 0; d < dim; ++d) {
        const float delta = q[d] - f[d];
        const float term = delta * delta * (weighted ? dimWeight[d] : 1.0f);
        cost += term;
        if (weighted) {
            out.breakdown.terms[static_cast<std::size_t>(layout[d])] += term;
        }
    }
    const bool haveCurrent = query.current != MotionDatabase::kInvalid && query.current < db.sampleCount();
    if (query.current != MotionDatabase::kInvalid) {
        const bool continues =
            query.current < db.sampleNext.size() && db.sampleNext[query.current] == sample;
        if (!continues) {
            float continuity = weights.continuity;
            if (weights.continuityPerSecond > 0.0f && query.current < db.sampleClip.size() &&
                db.sampleClip[sample] == db.sampleClip[query.current]) {
                const float gap = std::abs(db.sampleTime[sample] - db.sampleTime[query.current]);
                continuity = std::min(gap * weights.continuityPerSecond, weights.continuity);
            }
            cost += continuity;
            out.breakdown.continuity = continuity;
            const std::uint32_t currentClip = haveCurrent ? db.sampleClip[query.current] : MotionDatabase::kInvalid;
            const std::uint32_t currentTags = haveCurrent ? db.sampleTags[query.current] : 0u;
            if (db.sampleClip[sample] != currentClip) {
                const std::uint32_t shared = tags & currentTags;
                if (currentTags != 0 && shared != currentTags) {
                    cost += weights.transition;
                    out.breakdown.transition = weights.transition;
                }
            }
        }
    }
    out.total = cost;
    return out;
}

namespace {

constexpr std::size_t kGroups = static_cast<std::size_t>(MotionFeatureGroup::Count);

MatchCause causeForGroup(MotionFeatureGroup g) {
    switch (g) {
    case MotionFeatureGroup::TrajectoryPosition:
    case MotionFeatureGroup::TrajectoryFacing: return MatchCause::Trajectory;
    case MotionFeatureGroup::Contact: return MatchCause::Contact;
    default: return MatchCause::Features;
    }
}

float groupWeight(const MotionFeatureConfig& c, MotionFeatureGroup g) {
    switch (g) {
    case MotionFeatureGroup::JointPosition: return c.jointPositionWeight;
    case MotionFeatureGroup::JointVelocity: return c.jointVelocityWeight;
    case MotionFeatureGroup::TrajectoryPosition: return c.trajectoryPositionWeight;
    case MotionFeatureGroup::TrajectoryFacing: return c.trajectoryFacingWeight;
    case MotionFeatureGroup::RootVelocity: return c.rootVelocityWeight;
    case MotionFeatureGroup::Phase: return c.phaseWeight;
    case MotionFeatureGroup::Contact: return c.contactWeight;
    case MotionFeatureGroup::Count: break;
    }
    return 1.0f;
}

// The best candidate over samples `accept` admits, by full cost.
template <typename Accept>
MotionCandidateCost bestWhere(const MotionDatabase& db, const MotionQuery& query,
                              const MotionCostWeights& weights, Accept accept) {
    MotionCandidateCost best;
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        if (!accept(s)) {
            continue;
        }
        MotionCandidateCost c = motionCandidateCost(db, query, weights, s);
        if (!best.valid() || c.total < best.total) {
            best = c;
        }
    }
    return best;
}

} // namespace

bool MotionMatchExplanation::has(MatchCause cause) const {
    return std::any_of(findings.begin(), findings.end(),
                       [cause](const MatchFinding& f) { return f.cause == cause; });
}

MotionMatchExplanation explainMotionMatch(const MotionDatabase& db, const MotionQuery& query,
                                          const MotionCostWeights& weights,
                                          const MotionExplainOptions& options) {
    MotionMatchExplanation out;
    out.current = query.current;
    out.costSpread = db.stats.costSpread;
    const float spread = std::max(db.stats.costSpread, 1e-6f);

    // ---- the decision being explained ----
    MotionMatch match;
    if (options.plan && !options.plan->exhaustive()) {
        match = searchMotionStaged(db, query, weights, *options.plan);
        const MotionMatch full = searchMotion(db, query, weights);
        out.exhaustive = motionCandidateCost(db, query, weights, full.sample);
        if (full.found() && full.sample != match.sample) {
            out.findings.push_back(
                {MatchCause::Approximation,
                 fmt::format("the staged plan returned {} at {:.4f}; the linear scan finds {} at "
                             "{:.4f} -- the approximation changed the answer",
                             motionSampleLabel(db, match.sample), match.cost,
                             motionSampleLabel(db, full.sample), full.cost)});
        }
    } else {
        match = searchMotion(db, query, weights);
    }
    out.considered = match.considered;
    out.rejected = match.rejected;
    if (!match.found()) {
        out.findings.push_back({MatchCause::Filtering,
                                fmt::format("no candidate survived: {} of {} samples were removed by "
                                            "the tag filter",
                                            match.rejected, db.sampleCount())});
        return out;
    }
    out.selected = motionCandidateCost(db, query, weights, match.sample);

    const auto passes = [&](std::uint32_t s) {
        const std::uint32_t tags = db.sampleTags[s];
        return !((query.requireTags != 0 && (tags & query.requireTags) != query.requireTags) ||
                 (query.rejectTags != 0 && (tags & query.rejectTags) != 0));
    };
    const std::uint32_t winnerClip = db.sampleClip[match.sample];
    out.runnerUp = bestWhere(db, query, weights,
                             [&](std::uint32_t s) { return passes(s) && db.sampleClip[s] != winnerClip; });
    out.bestFiltered = bestWhere(db, query, weights, [&](std::uint32_t s) { return !passes(s); });
    if (query.current < db.sampleCount() && db.sampleNext[query.current] != MotionDatabase::kInvalid) {
        out.continuation = motionCandidateCost(db, query, weights, db.sampleNext[query.current]);
    }

    // ---- filtering ----
    if (out.bestFiltered.valid() && out.bestFiltered.total < out.selected.total) {
        out.findings.push_back(
            {MatchCause::Filtering,
             fmt::format("the tag filter removed {} [{}], which would have won by {:.4f}",
                         motionSampleLabel(db, out.bestFiltered.sample),
                         motionTagNames(db.sampleTags[out.bestFiltered.sample]),
                         out.selected.total - out.bestFiltered.total)});
    }

    // ---- continuity and hysteresis ----
    if (out.continuation.valid()) {
        if (out.continuation.sample == out.selected.sample) {
            out.findings.push_back(
                {MatchCause::Continuity,
                 fmt::format("continued: carrying on costs {:.4f}; the best alternative from another "
                             "clip costs {:.4f}, of which {:.4f} is the continuity and transition "
                             "penalty for leaving",
                             out.selected.total, out.runnerUp.valid() ? out.runnerUp.total : 0.0f,
                             out.runnerUp.valid()
                                 ? out.runnerUp.breakdown.continuity + out.runnerUp.breakdown.transition
                                 : 0.0f)});
        } else {
            out.findings.push_back(
                {MatchCause::Continuity,
                 fmt::format("switched: carrying on would cost {:.4f} and the switch costs {:.4f} "
                             "including {:.4f} of continuity and transition penalty -- the fit "
                             "advantage ({:.4f}) paid for leaving",
                             out.continuation.total, out.selected.total,
                             out.selected.breakdown.continuity + out.selected.breakdown.transition,
                             out.continuation.featureCost() - out.selected.featureCost())});
            if (options.switchMargin) {
                const float margin = *options.switchMargin * db.stats.costSpread;
                out.heldByMargin = out.selected.total > out.continuation.total - margin;
                if (out.heldByMargin) {
                    out.findings.push_back(
                        {MatchCause::Continuity,
                         fmt::format("but the provider HOLDS the continuation: the switch saves "
                                     "{:.4f}, less than the §28 margin {:.4f} ({:.0f}% of the "
                                     "spread {:.4f})",
                                     out.continuation.total - out.selected.total, margin,
                                     100.0f * *options.switchMargin, db.stats.costSpread)});
                }
            }
        }
    }

    // ---- features and weights: what separated the winner from the runner-up ----
    if (out.runnerUp.valid()) {
        const float margin = out.runnerUp.total - out.selected.total;
        std::size_t decisive = kGroups;
        float largest = 0.0f;
        for (std::size_t g = 0; g < kGroups; ++g) {
            const float delta = out.runnerUp.breakdown.terms[g] - out.selected.breakdown.terms[g];
            if (delta > largest) {
                largest = delta;
                decisive = g;
            }
        }
        if (decisive < kGroups) {
            const auto group = static_cast<MotionFeatureGroup>(decisive);
            out.findings.push_back(
                {causeForGroup(group),
                 fmt::format("against the runner-up {} (margin {:.4f}), the largest difference is "
                             "{} (+{:.4f})",
                             motionSampleLabel(db, out.runnerUp.sample), margin,
                             motionFeatureGroupName(group), largest)});
            // Would the decision survive that term at weight 1? If not, the weight decided it.
            const float w = groupWeight(db.config, group);
            if (w > 0.0f && std::abs(w - 1.0f) > 1e-6f) {
                const float reweighted = margin - largest + (largest / w);
                if ((reweighted < 0.0f) != (margin < 0.0f)) {
                    out.findings.push_back(
                        {MatchCause::Weights,
                         fmt::format("the {} weight ({:.2f}) decided it: at 1.00 the runner-up "
                                     "would win by {:.4f}",
                                     motionFeatureGroupName(group), w, -reweighted)});
                }
            }
        }
        if (margin < options.nearTie * spread) {
            out.findings.push_back(
                {MatchCause::Features,
                 fmt::format("near tie: the margin {:.4f} is {:.1f}% of the typical gap {:.4f}, so "
                             "a small change to the query or the weights can flip it",
                             margin, 100.0f * margin / spread, db.stats.costSpread)});
        }
    }

    // ---- trajectory share and coverage ----
    const float fit = out.selected.featureCost();
    const std::size_t tp = static_cast<std::size_t>(MotionFeatureGroup::TrajectoryPosition);
    const std::size_t tf = static_cast<std::size_t>(MotionFeatureGroup::TrajectoryFacing);
    const float trajectory = out.selected.breakdown.terms[tp] + out.selected.breakdown.terms[tf];
    if (fit > 0.0f && trajectory > 0.5f * fit) {
        out.findings.push_back(
            {MatchCause::Trajectory,
             fmt::format("{:.0f}% of the winner's misfit is trajectory: nothing in the database goes "
                         "quite where the query asks",
                         100.0f * trajectory / fit)});
    }
    if (fit > options.farMatch * spread) {
        out.findings.push_back(
            {MatchCause::Coverage,
             fmt::format("even the best match is far: its misfit {:.4f} is {:.0f}% of the typical gap "
                         "-- the database may not contain this motion",
                         fit, 100.0f * fit / spread)});
    }
    return out;
}

std::string MotionMatchExplanation::report(const MotionDatabase& db, const MotionQuery& query) const {
    std::string out;
    out += fmt::format("Current:   {}\n", current == MotionDatabase::kInvalid ? std::string("(first selection)")
                                                                               : motionSampleLabel(db, current));
    out += fmt::format("Selected:  {}\n", motionSampleLabel(db, selected.sample));
    out += fmt::format("Candidates: {} scored, {} removed by the tag filter\n", considered, rejected);

    // The query's trajectory and the winner's, denormalised to metres, at each horizon.
    const std::vector<MotionFeatureGroup> layout = motionFeatureLayout(db.config);
    std::size_t traj = layout.size();
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (layout[d] == MotionFeatureGroup::TrajectoryPosition) {
            traj = d;
            break;
        }
    }
    const auto raw = [&](const float* f, std::size_t d) {
        return db.scale[d] != 0.0f ? (f[d] / db.scale[d]) + db.mean[d] : f[d];
    };
    if (traj < layout.size() && selected.valid() && query.features.size() == db.dimension) {
        const float* w = db.featuresFor(selected.sample);
        for (std::size_t t = 0; t < db.config.trajectoryTimes.size(); ++t) {
            const std::size_t d = traj + (t * 4u);
            out += fmt::format("Trajectory +{:.2f}s: query ({:+.2f}, {:+.2f})  selected ({:+.2f}, {:+.2f}) m\n",
                               db.config.trajectoryTimes[t], raw(query.features.data(), d),
                               raw(query.features.data(), d + 1), raw(w, d), raw(w, d + 1));
        }
    }
    if (selected.valid()) {
        out += fmt::format("Phase:     {:.2f}", db.samplePhase[selected.sample]);
        // Contact state, when the database carries contact dimensions.
        std::size_t contact = layout.size();
        for (std::size_t d = 0; d < layout.size(); ++d) {
            if (layout[d] == MotionFeatureGroup::Contact) {
                contact = d;
                break;
            }
        }
        if (contact < layout.size()) {
            const float* w = db.featuresFor(selected.sample);
            const auto& names = db.config.contactJointNames();
            out += "   contacts:";
            for (std::size_t j = 0; j < names.size() && contact + j < db.dimension; ++j) {
                out += fmt::format(" {} {}", names[j], raw(w, contact + j) > 0.5f ? "planted" : "free");
            }
        } else {
            out += "   contacts: not in this database's features";
        }
        out += "\n";
    }

    // The cost table: a column per compared candidate.
    struct Column {
        const char* name;
        const MotionCandidateCost* cost;
    };
    std::vector<Column> columns{{"selected", &selected}};
    if (continuation.valid() && continuation.sample != selected.sample) {
        columns.push_back({"carry on", &continuation});
    }
    if (runnerUp.valid()) {
        columns.push_back({"runner-up", &runnerUp});
    }
    if (bestFiltered.valid()) {
        columns.push_back({"filtered", &bestFiltered});
    }
    if (exhaustive.valid() && exhaustive.sample != selected.sample) {
        columns.push_back({"exhaustive", &exhaustive});
    }
    out += fmt::format("\nCost {:<20}", "");
    for (const Column& c : columns) {
        out += fmt::format("{:>11}", c.name);
    }
    out += "\n";
    for (std::size_t g = 0; g < kGroups; ++g) {
        bool any = false;
        for (const Column& c : columns) {
            any = any || c.cost->breakdown.terms[g] != 0.0f;
        }
        if (!any) {
            continue;
        }
        out += fmt::format("  {:<23}", motionFeatureGroupName(static_cast<MotionFeatureGroup>(g)));
        for (const Column& c : columns) {
            out += fmt::format("{:>11.4f}", c.cost->breakdown.terms[g]);
        }
        out += "\n";
    }
    out += fmt::format("  {:<23}", "continuity");
    for (const Column& c : columns) {
        out += fmt::format("{:>11.4f}", c.cost->breakdown.continuity);
    }
    out += fmt::format("\n  {:<23}", "transition");
    for (const Column& c : columns) {
        out += fmt::format("{:>11.4f}", c.cost->breakdown.transition);
    }
    out += fmt::format("\n  {:<23}", "TOTAL");
    for (const Column& c : columns) {
        out += fmt::format("{:>11.4f}", c.cost->total);
    }
    out += "\n";
    for (std::size_t i = 1; i < columns.size(); ++i) {
        out += fmt::format("  {:<10} = {}\n", columns[i].name, motionSampleLabel(db, columns[i].cost->sample));
    }

    out += "\nWhy:\n";
    for (const MatchFinding& f : findings) {
        out += fmt::format("  [{}] {}\n", matchCauseName(f.cause), f.text);
    }
    out += fmt::format("  ({})\n", MotionCostBreakdown::caveat());
    return out;
}

} // namespace avgen::scene
