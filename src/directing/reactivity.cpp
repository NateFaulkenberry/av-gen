#include "directing/reactivity.hpp"

#include "directing/text.hpp"
#include "params/parameter_set.hpp"
#include "params/serialization.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace avgen::directing {
namespace {

using json = nlohmann::json;
using params::liveness::Finding;
using params::liveness::SignalFacts;
using params::liveness::Verdict;

// The scene's facts, plus the signals the plan's own sources will publish: a route may read a signal
// the plan makes, and the rules must see it as the bus will.
class PlanSignalFacts final : public params::liveness::Facts {
public:
    PlanSignalFacts(const params::liveness::Facts* scene, const std::map<std::string, SignalFacts, std::less<>>& extra)
        : scene_(scene), extra_(extra) {}
    [[nodiscard]] SignalFacts signal(std::string_view name) const override {
        if (const auto it = extra_.find(name); it != extra_.end()) {
            return it->second;
        }
        return scene_ != nullptr ? scene_->signal(name) : SignalFacts{};
    }
    [[nodiscard]] const params::IParameter* parameter(std::string_view path) const override {
        return scene_ != nullptr ? scene_->parameter(path) : nullptr;
    }
    [[nodiscard]] std::optional<Finding> deadTarget(std::string_view path, int component) const override {
        return scene_ != nullptr ? scene_->deadTarget(path, component) : std::nullopt;
    }
    [[nodiscard]] std::optional<Finding> phaseRate(std::string_view path) const override {
        return scene_ != nullptr ? scene_->phaseRate(path) : std::nullopt;
    }
    [[nodiscard]] double frameRate() const override { return scene_ != nullptr ? scene_->frameRate() : 60.0; }

private:
    const params::liveness::Facts* scene_;
    const std::map<std::string, SignalFacts, std::less<>>& extra_;
};

// The chain's output once an input held at x has settled.
float settled(const params::ProcessorChain& chain, params::Polarity polarity, float x) {
    params::ProcessorChain::State state;
    const double dt = 1.0 / 60.0;
    const float in = polarity == params::Polarity::Bipolar ? x * 2.0f - 1.0f : x;
    const float slowest = std::max({chain.attackMs, chain.decayMs, 1.0f}) + chain.delayMs + chain.envelopeHoldMs;
    const int frames = std::min(60 + static_cast<int>(7.0 * static_cast<double>(slowest) / 1000.0 / dt), 60 * 90);
    float y = 0.0f;
    for (int i = 0; i < frames; ++i) {
        y = chain.process(in, false, dt, state);
    }
    return y;
}

float applyDepth(params::ModOp op, float base, float y, float depth) {
    const float full = params::applyModOp(op, base, y);
    return base + (full - base) * depth;
}

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
}

std::string heroOfNode(const ReactiveCatalog& catalog, std::string_view node) {
    for (const ReactiveHero& h : catalog.heroes) {
        if (std::find(h.members.begin(), h.members.end(), node) != h.members.end()) {
            return h.name;
        }
    }
    return {};
}

std::string pathOwner(const ReactiveCatalog& catalog, const std::string& path) {
    const auto first = path.find('/');
    if (first == std::string::npos) {
        return "world";
    }
    const std::string head = path.substr(0, first);
    const auto second = path.find('/', first + 1);
    const std::string name = path.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1);
    if (head == "nodes" || head == "procedural" || head == "particles") {
        const std::string hero = heroOfNode(catalog, name);
        return hero.empty() ? name : hero;
    }
    if (head == "material") {
        return "material/" + name;
    }
    if (head == "fx") {
        return name;
    }
    return "world";
}

std::string lagText(float ms) {
    return fmt::format("{:.0f} ms", ms);
}

} // namespace

// ---- analysis ---------------------------------------------------------------------------------------

Excursion routeExcursion(const params::ModRoute& route, float base, float sourceMin, float sourceMax) {
    Excursion out{base, base, base};
    std::vector<float> depths{1.0f};
    if (!route.depthSource.empty()) {
        depths = {route.depthMin, route.depthMax};
    }
    bool first = true;
    constexpr int kSamples = 17;
    for (int i = 0; i < kSamples; ++i) {
        const float x = sourceMin + (sourceMax - sourceMin) * static_cast<float>(i) / static_cast<float>(kSamples - 1);
        const float y = settled(route.chain, route.polarity, x) * route.amount;
        for (const float d : depths) {
            const float v = applyDepth(route.op, base, y, d);
            out.low = first ? v : std::min(out.low, v);
            out.high = first ? v : std::max(out.high, v);
            first = false;
        }
        if (i == 0) {
            out.rest = applyDepth(route.op, base, y, *std::max_element(depths.begin(), depths.end()));
        }
    }
    return out;
}

std::vector<std::string> targetOwners(const ReactiveCatalog& catalog, const std::string& path) {
    if (const ReactiveTarget* t = catalog.find(path)) {
        if (t->group == ReactiveGroup::MaterialEmission && !t->sharedBy.empty()) {
            std::vector<std::string> out;
            for (const std::string& surface : t->sharedBy) {
                const std::string hero = heroOfNode(catalog, surface);
                const std::string owner = hero.empty() ? surface : hero;
                if (std::find(out.begin(), out.end(), owner) == out.end()) {
                    out.push_back(owner);
                }
            }
            return out;
        }
        return {t->hero.empty() ? t->owner : t->hero};
    }
    return {pathOwner(catalog, path)};
}

std::string describeRoute(const params::ModRoute& r) {
    static constexpr const char* kOps[] = {"add", "multiply", "replace", "min", "max"};
    std::string out = fmt::format("{} -> {}{}: {} {:.3g}", r.source, r.target,
                                  r.component >= 0 ? fmt::format("[{}]", r.component) : std::string(),
                                  kOps[static_cast<int>(r.op)], r.amount);
    if (r.chain.remapEnabled) {
        out += fmt::format(" (remap {:.3g}..{:.3g} -> {:.3g}..{:.3g})", r.chain.remapInMin, r.chain.remapInMax,
                           r.chain.remapOutMin, r.chain.remapOutMax);
    }
    out += fmt::format(", delay {:.0f} ms, attack {:.0f} ms, decay {:.0f} ms", r.chain.delayMs, r.chain.attackMs,
                       r.chain.decayMs);
    if (!r.depthSource.empty()) {
        out += fmt::format(", depth {} {:.2f}..{:.2f}", r.depthSource, r.depthMin, r.depthMax);
    }
    return out;
}

// ---- validation -------------------------------------------------------------------------------------

void validateReactivity(Plan& plan, const SceneFacts& facts, Validation& v) {
    if (plan.routes.empty() && plan.sources.empty()) {
        return;
    }
    const ReactiveCatalog& catalog = facts.capabilities.reactive();
    const params::liveness::Registry& registry = params::liveness::Registry::standard();
    const auto add = [&](Severity severity, IssueCode code, const std::string& item, std::string location,
                         std::string message) -> Issue& {
        Issue issue;
        issue.severity = severity;
        issue.code = code;
        issue.item = item;
        issue.location = std::move(location);
        issue.message = std::move(message);
        if (severity == Severity::Error && !item.empty()) {
            v.blocked.insert(item);
        }
        v.issues.push_back(std::move(issue));
        return v.issues.back();
    };

    // What this plan's previous revision made: its routes are replaced by this one, and its sources'
    // names are this plan's to make again.
    std::set<std::string> ownSources;
    std::set<std::string> ownRoutes;
    if (const Plan* previous = facts.plan(plan.id)) {
        for (const ContentRef& ref : previous->produced) {
            if (ref.domain == ContentDomain::ModSource) {
                ownSources.insert(ref.id);
            } else if (ref.domain == ContentDomain::ModRoute) {
                ownRoutes.insert(ref.id);
            }
        }
    }

    // ---- sources ------------------------------------------------------------------------------------
    std::map<std::string, SignalFacts, std::less<>> planSignals;
    std::map<std::string, std::string> blockedSignals; // signal -> the blocked source's key
    std::set<std::string> seen;
    for (std::size_t i = 0; i < plan.sources.size(); ++i) {
        const PlanSource& src = plan.sources[i];
        const std::string at = fmt::format("/sources/{}", i);
        const std::string signal = src.signal();
        std::unique_ptr<signals::Source> made = signals::SourceRack::create(src.kind, src.name);
        // What it would publish, by the source's own names ("env.<name>" for an envelope): a route
        // reading any of them is blocked with it.
        const std::vector<std::string> outputs = made != nullptr ? made->outputs() : std::vector<std::string>{signal};
        const auto refuse = [&](IssueCode code, std::string message) -> Issue& {
            for (const std::string& o : outputs) {
                blockedSignals[o] = src.key;
            }
            return add(Severity::Error, code, src.key, at, std::move(message));
        };
        if (made == nullptr) {
            Issue& i2 = refuse(IssueCode::SchemaInvalid, fmt::format("'{}' is not a source kind", src.kind));
            i2.suggestions = {"lfo", "noise", "timeline"};
            continue;
        }
        if (!made->pureInTime()) {
            Issue& i2 = refuse(IssueCode::NonDeterministic,
                               fmt::format("a {} source integrates what it hears, and a seek does not replay it "
                                           "(ADR-901): a render started mid-film would not match a play",
                                           src.kind));
            i2.suggestions = {"a beat-synced lfo for a slow swell", "a timeline keyed at the sections for an arc"};
            continue;
        }
        if (auto r = made->settingsFromJson(src.settings); !r) {
            refuse(IssueCode::SchemaInvalid, fmt::format("the source's settings: {}", r.error().message));
            continue;
        }
        signals::SignalBus bus;
        params::ParameterSet scratch;
        made->attach(bus, scratch);
        bool parametersOk = true;
        for (const auto& [leaf, value] : src.parameters) {
            if (scratch.find("sources/" + src.name + "/" + leaf) == nullptr) {
                std::vector<std::string> leaves;
                for (const params::IParameter* p : scratch.ordered()) {
                    leaves.push_back(p->path().substr(("sources/" + src.name + "/").size()));
                }
                Issue& i2 = refuse(IssueCode::SchemaInvalid,
                                   fmt::format("a {} source has no parameter '{}'", src.kind, leaf));
                i2.suggestions = text::nearest(leaf, leaves);
                if (i2.suggestions.empty()) {
                    i2.suggestions = leaves;
                }
                parametersOk = false;
            }
        }
        if (!parametersOk) {
            continue;
        }
        if (!seen.insert(signal).second) {
            refuse(IssueCode::DuplicateKey, fmt::format("two of the plan's sources publish '{}'", signal));
            continue;
        }
        const bool taken = std::any_of(facts.staged.sources.begin(), facts.staged.sources.end(), [&](const json& s) {
            return s.value("kind", std::string()) == src.kind && s.value("name", std::string()) == src.name;
        });
        if (taken && !ownSources.contains(signal)) {
            Issue& i2 = refuse(IssueCode::DuplicateKey,
                               fmt::format("the project already has a source '{}' this plan did not make", signal));
            i2.suggestions = {fmt::format("name the plan's source something other than '{}'", src.name)};
            continue;
        }
        const auto* timeline = dynamic_cast<const signals::TimelineSource*>(made.get());
        for (const signals::SignalInfo& info : bus.infos()) {
            SignalFacts f;
            f.exists = true;
            f.isEvent = info.isEvent;
            f.minValue = info.minValue;
            f.maxValue = info.maxValue;
            if (timeline != nullptr && !timeline->keys().empty()) {
                if (timeline->mode() == signals::TimelineMode::Event) {
                    f.isEvent = true;
                    std::vector<float> hits;
                    for (const signals::Keyframe& k : timeline->keys()) {
                        if (k.value > 0.0f) {
                            hits.push_back(k.value);
                        }
                    }
                    if (!hits.empty()) {
                        std::nth_element(hits.begin(), hits.begin() + static_cast<std::ptrdiff_t>(hits.size() / 2), hits.end());
                        f.typicalEventStrength = hits[hits.size() / 2];
                    }
                } else {
                    const auto [lo, hi] = std::minmax_element(timeline->keys().begin(), timeline->keys().end(),
                                                              [](const auto& a, const auto& b) { return a.value < b.value; });
                    f.minValue = lo->value;
                    f.maxValue = hi->value;
                }
            }
            planSignals[info.name] = f;
        }
    }
    const PlanSignalFacts signalFacts(facts.liveness.get(), planSignals);

    // ---- routes -------------------------------------------------------------------------------------
    // The project's routes as they will stand beside the plan's: the previous revision's taken out,
    // this revision's in -- the set a later Replace route is judged against.
    std::vector<params::ModRoute> standing;
    for (const params::ModRoute& r : facts.staged.routes) {
        if (!ownRoutes.contains(r.planItem)) {
            standing.push_back(r);
        }
    }
    const std::size_t authored = standing.size();
    for (const PlanRoute& item : plan.routes) {
        params::ModRoute r = item.route;
        r.planItem = planItemId(plan.id, item.key);
        standing.push_back(std::move(r));
    }
    const params::liveness::Registry::SetFindings set = registry.checkSet(standing, {});
    if (facts.liveness == nullptr) {
        add(Severity::Info, IssueCode::RouteHazard, {}, "/routes",
            "no scene facts were given, so the routes' liveness is not judged");
    }
    std::vector<std::string> catalogue;
    for (const ReactiveTarget& t : catalog.targets) {
        catalogue.push_back(t.path);
    }

    for (std::size_t k = 0; k < plan.routes.size(); ++k) {
        const PlanRoute& item = plan.routes[k];
        const params::ModRoute& route = standing[authored + k];
        const std::string at = fmt::format("/routes/{}", k);
        if (const auto blocked = blockedSignals.find(route.source); blocked != blockedSignals.end()) {
            add(Severity::Error, IssueCode::Blocked, item.key, at + "/route/source",
                fmt::format("reads '{}', which the plan's source '{}' cannot make", route.source, blocked->second));
            continue;
        }
        if (facts.liveness != nullptr) {
            std::vector<Finding> findings = registry.checkRoute(route, signalFacts);
            findings.insert(findings.end(), set.routes[authored + k].begin(), set.routes[authored + k].end());
            for (const Finding& f : findings) {
                if (f.verdict == Verdict::Dead) {
                    Issue& issue = add(Severity::Error, IssueCode::DeadTarget, item.key, at + "/route",
                                       fmt::format("{} -> {} cannot reach the picture: {} [{}]", route.source,
                                                   route.target, f.reason, f.rule));
                    issue.subject = route.target;
                    issue.details = {{"rule", f.rule}, {"verdict", "dead"}};
                    if (f.rule == "unknown-target") {
                        issue.suggestions = text::nearest(route.target, catalogue);
                    }
                } else if (f.rule == "phase-rate") {
                    Issue& issue = add(Severity::Warning, IssueCode::PhaseRateTrap, item.key, at + "/route/target",
                                       fmt::format("{}: {}", route.target, f.reason));
                    issue.details = {{"rule", f.rule}, {"verdict", "hazard"}};
                    issue.suggestions = {"move an amplitude or a phase, not a rate"};
                } else {
                    Issue& issue = add(Severity::Warning, IssueCode::RouteHazard, item.key, at + "/route",
                                       fmt::format("{} [{}]", f.reason, f.rule));
                    issue.details = {{"rule", f.rule}, {"verdict", "hazard"}};
                }
            }
        }
        if (v.isBlocked(item.key)) {
            continue;
        }
        // ---- the traps no liveness rule knows, from the GV3 revision's reports ----
        const SignalFacts source = signalFacts.signal(route.source);
        if (!route.depthSource.empty()) {
            const SignalFacts depth = signalFacts.signal(route.depthSource);
            if (depth.isEvent) {
                add(Severity::Warning, IssueCode::RouteHazard, item.key, at + "/route/depthSource",
                    fmt::format("'{}' is a one-frame event: as a depth it holds the route at depthMin except on the "
                                "frame it fires; use a signal that holds its value, such as section.energy",
                                route.depthSource));
            } else if (route.depthSource == "audio.rms" || route.depthSource == "audio.peak") {
                add(Severity::Warning, IssueCode::RouteHazard, item.key, at + "/route/depthSource",
                    fmt::format("'{}' is read unsmoothed, so as a depth it flickers with every frame; use "
                                "section.energy or audio.energy",
                                route.depthSource));
            }
        }
        const ReactiveTarget* entry = catalog.find(route.target);
        const bool hue = (entry != nullptr && entry->kind == ReactiveKind::Hue) ||
                         route.target.ends_with("/hueOffset") || route.target.ends_with("/hueShift");
        if (hue && (startsWith(route.source, "audio.") || startsWith(route.source, "music.") ||
                    startsWith(route.source, "beat."))) {
            Issue& issue = add(Severity::Warning, IssueCode::RouteHazard, item.key, at + "/route/source",
                               fmt::format("a hue driven by the audio ('{}') reads as noise: key the colour by "
                                           "section instead",
                                           route.source));
            issue.suggestions = {"a timeline source keyed at the section boundaries"};
        }
        if (route.op == params::ModOp::Multiply && source.isEvent) {
            const float rest = settled(route.chain, route.polarity, 0.0f) * route.amount;
            if (std::abs(rest - 1.0f) > 0.05f) {
                add(Severity::Warning, IssueCode::RouteHazard, item.key, at + "/route/chain",
                    fmt::format("between events this multiply scales {} by {:.2f}: the target sits {} except on "
                                "the hit; remap the rest to 1",
                                route.target, rest, rest < 1.0f ? "dark" : "overdriven"));
            }
        }
        if (entry != nullptr && source.exists) {
            const Excursion ex = routeExcursion(route, entry->base, source.minValue, source.maxValue);
            const float tolerance = 1e-3f + 0.01f * std::max(std::abs(entry->safeMax), std::abs(entry->safeMin));
            if (ex.high > entry->safeMax + tolerance || ex.low < entry->safeMin - tolerance) {
                Issue& issue = add(Severity::Warning, IssueCode::OverSaturated, item.key, at + "/route",
                                   fmt::format("this route can take {} ({}) to {:.3g}..{:.3g}, past its safe range "
                                               "{:.3g}..{:.3g}",
                                               entry->label, route.target, ex.low, ex.high, entry->safeMin, entry->safeMax));
                issue.details = {{"low", ex.low}, {"high", ex.high}, {"safeMin", entry->safeMin}, {"safeMax", entry->safeMax}};
            }
        }
    }

    // ---- the plan as a whole: "everything pulses to the beat" --------------------------------------
    std::vector<const params::ModRoute*> live;
    for (std::size_t k = 0; k < plan.routes.size(); ++k) {
        if (!v.isBlocked(plan.routes[k].key)) {
            live.push_back(&standing[authored + k]);
        }
    }
    std::map<std::string, std::vector<const params::ModRoute*>> bySource;
    for (const params::ModRoute* r : live) {
        bySource[r->source].push_back(r);
    }
    if (live.size() >= kOneSourceMinimumRoutes) {
        const auto top = std::max_element(bySource.begin(), bySource.end(),
                                          [](const auto& a, const auto& b) { return a.second.size() < b.second.size(); });
        const float share = static_cast<float>(top->second.size()) / static_cast<float>(live.size());
        if (share >= kOneSourceShare) {
            Issue& issue = add(Severity::Warning, IssueCode::OneSource, {}, "/routes",
                               fmt::format("{} of the plan's {} routes follow '{}': everything answering one signal "
                                           "reads as a visualiser, not a world listening; give each layer of the "
                                           "music its own owner",
                                           top->second.size(), live.size(), top->first));
            issue.subject = top->first;
            issue.details = {{"source", top->first}, {"routes", top->second.size()}, {"of", live.size()}, {"share", share}};
            issue.suggestions = {"hats and claps on small things, the kick and the bar on heroes, section energy on "
                                 "the light, the air and the colour"};
        }
    }
    const auto lag = [](const params::ModRoute* r) { return std::round(r->chain.delayMs + r->chain.attackMs); };
    for (const auto& [src, routes] : bySource) {
        if (routes.size() < kOnePhaseMinimumRoutes) {
            continue;
        }
        if (std::all_of(routes.begin(), routes.end(), [&](const params::ModRoute* r) { return lag(r) == lag(routes.front()); })) {
            Issue& issue = add(Severity::Warning, IssueCode::OnePhase, {}, "/routes",
                               fmt::format("the {} routes on '{}' all reach their targets at +{}: they move in "
                                           "lockstep; stagger them with delayMs so the response travels",
                                           routes.size(), src, lagText(lag(routes.front()))));
            issue.subject = src;
            issue.details = {{"source", src}, {"routes", routes.size()}, {"lagMs", lag(routes.front())}};
        }
    }
    if (live.size() >= kOnePhaseMinimumRoutes && bySource.size() > 1 &&
        std::all_of(live.begin(), live.end(), [&](const params::ModRoute* r) { return lag(r) == lag(live.front()); })) {
        Issue& issue = add(Severity::Warning, IssueCode::OnePhase, {}, "/routes",
                           fmt::format("every one of the plan's {} routes reaches its target at +{}: stagger them",
                                       live.size(), lagText(lag(live.front()))));
        issue.details = {{"routes", live.size()}, {"lagMs", lag(live.front())}};
    }

    // Entities answering too many signals, counting the project's own routes beside the plan's.
    std::map<std::string, std::set<std::string>> sourcesOf;
    std::set<std::string> touched;
    for (std::size_t i = 0; i < standing.size(); ++i) {
        const params::ModRoute& r = standing[i];
        const bool mine = i >= authored;
        if (!r.enabled || (mine && v.isBlocked(plan.routes[i - authored].key))) {
            continue;
        }
        for (const std::string& owner : targetOwners(catalog, r.target)) {
            if (owner == "world") {
                continue; // the world answers the arc through many things; it is not one entity
            }
            sourcesOf[owner].insert(r.source);
            if (mine) {
                touched.insert(owner);
            }
        }
    }
    for (const std::string& owner : touched) {
        const std::set<std::string>& sources = sourcesOf[owner];
        if (sources.size() > kSaturatedSources) {
            Issue& issue = add(Severity::Warning, IssueCode::OverSaturated, {}, "/routes",
                               fmt::format("'{}' answers {} different signals ({}): it cannot read as answering any "
                                           "one; give it one layer of the music, two at most",
                                           owner, sources.size(), fmt::join(sources, ", ")));
            issue.subject = owner;
            issue.details = {{"owner", owner}, {"sources", std::vector<std::string>(sources.begin(), sources.end())}};
        }
    }
    // Routes stacked on one target past its safe range.
    std::map<std::string, std::vector<std::size_t>> byTarget;
    for (std::size_t i = 0; i < standing.size(); ++i) {
        const bool mine = i >= authored;
        if (!standing[i].enabled || (mine && v.isBlocked(plan.routes[i - authored].key))) {
            continue;
        }
        byTarget[standing[i].target].push_back(i);
    }
    for (const auto& [target, indices] : byTarget) {
        const ReactiveTarget* entry = catalog.find(target);
        const bool anyMine = std::any_of(indices.begin(), indices.end(), [&](std::size_t i) { return i >= authored; });
        if (entry == nullptr || indices.size() < 2 || !anyMine) {
            continue;
        }
        float high = entry->base;
        float low = entry->base;
        for (const std::size_t i : indices) {
            const SignalFacts s = signalFacts.signal(standing[i].source);
            const Excursion ex = routeExcursion(standing[i], entry->base, s.exists ? s.minValue : 0.0f,
                                                s.exists ? s.maxValue : 1.0f);
            high += std::max(0.0f, ex.high - entry->base);
            low -= std::max(0.0f, entry->base - ex.low);
        }
        const float tolerance = 1e-3f + 0.01f * std::max(std::abs(entry->safeMax), std::abs(entry->safeMin));
        if (high > entry->safeMax + tolerance || low < entry->safeMin - tolerance) {
            std::string item;
            for (const std::size_t i : indices) {
                if (i >= authored) {
                    item = plan.routes[i - authored].key;
                }
            }
            Issue& issue = add(Severity::Warning, IssueCode::OverSaturated, item, "/routes",
                               fmt::format("{} routes on {} ({}) together can take it to {:.3g}..{:.3g}, past its "
                                           "safe range {:.3g}..{:.3g}",
                                           indices.size(), entry->label, target, low, high, entry->safeMin, entry->safeMax));
            issue.details = {{"target", target}, {"routes", indices.size()}, {"low", low}, {"high", high}};
        }
    }
}

// ---- compilation ------------------------------------------------------------------------------------

namespace {

// A source's content as the host stages it: kind, name, settings, and every parameter it registers
// with its value -- the plan's, or the source's own default.
std::optional<json> stagedSource(const PlanSource& src) {
    std::unique_ptr<signals::Source> made = signals::SourceRack::create(src.kind, src.name);
    if (made == nullptr || !made->settingsFromJson(src.settings)) {
        return std::nullopt;
    }
    signals::SignalBus bus;
    params::ParameterSet scratch;
    made->attach(bus, scratch);
    for (const auto& [leaf, value] : src.parameters) {
        if (params::IParameter* p = scratch.find("sources/" + src.name + "/" + leaf)) {
            p->setBaseComponent(0, value);
        }
    }
    json parameters = json::object();
    const std::string prefix = "sources/" + src.name + "/";
    for (const params::IParameter* p : scratch.ordered()) {
        if (p->componentCount() == 1) {
            parameters[p->path().substr(prefix.size())] = p->baseComponent(0);
        }
    }
    return json{{"kind", made->kind()}, {"name", made->name()}, {"settings", made->settingsToJson()},
                {"parameters", std::move(parameters)}};
}

std::string signalOf(const json& staged) {
    return staged.value("kind", std::string()) + "." + staged.value("name", std::string());
}

// Takes the staged sources publishing `signal` out of the array; true when one was there.
bool eraseSources(json& sources, const std::string& signal) {
    json kept = json::array();
    bool removed = false;
    for (const json& s : sources) {
        if (signalOf(s) == signal) {
            removed = true;
        } else {
            kept.push_back(s);
        }
    }
    sources = std::move(kept);
    return removed;
}

} // namespace

void compileReactivity(const Plan& plan, const Validation& v, Staging& staged, const ReactivityCompileSink& sink) {
    for (const PlanSource& src : plan.sources) {
        if (v.isBlocked(src.key)) {
            continue;
        }
        std::optional<json> entry = stagedSource(src);
        if (!entry) {
            continue; // the validator refused it
        }
        eraseSources(staged.sources, src.signal());
        staged.sources.push_back(*entry);
        sink.record(src.key, ContentDomain::ModSource, src.signal());
        sink.line(sink.sign(src.key), src.key,
                  fmt::format("Source {} ({}){}", src.signal(), src.kind, src.reason.empty() ? "" : " -- " + src.reason));
    }
    for (const PlanRoute& item : plan.routes) {
        if (v.isBlocked(item.key)) {
            continue;
        }
        params::ModRoute route;
        // Only the serialised fields: a staged route carries no runtime state.
        if (auto copy = params::routeFromJson(params::routeToJson(item.route)); copy) {
            route = std::move(*copy);
        }
        route.planItem = planItemId(plan.id, item.key);
        std::erase_if(staged.routes, [&](const params::ModRoute& r) { return r.planItem == route.planItem; });
        sink.record(item.key, ContentDomain::ModRoute, route.planItem);
        sink.line(sink.sign(item.key), item.key,
                  fmt::format("Route {} [{}{}{}]{}", describeRoute(route), reactiveLevelName(item.level),
                              item.owner.empty() ? "" : ", " + item.owner, item.layer.empty() ? "" : ", " + item.layer,
                              item.reason.empty() ? "" : " -- " + item.reason));
        staged.routes.push_back(std::move(route));
    }
}

std::optional<json> reactivityContent(const ContentRef& ref, const Staging& staged) {
    if (ref.domain == ContentDomain::ModRoute) {
        for (const params::ModRoute& r : staged.routes) {
            if (r.planItem == ref.id) {
                return params::routeToJson(r);
            }
        }
    } else if (ref.domain == ContentDomain::ModSource) {
        for (const json& s : staged.sources) {
            if (signalOf(s) == ref.id) {
                return s;
            }
        }
    }
    return std::nullopt;
}

bool removeReactivityContent(const ContentRef& ref, Staging& staged) {
    if (ref.domain == ContentDomain::ModRoute) {
        return std::erase_if(staged.routes, [&](const params::ModRoute& r) { return r.planItem == ref.id; }) > 0;
    }
    if (ref.domain == ContentDomain::ModSource) {
        return eraseSources(staged.sources, ref.id);
    }
    return false;
}

} // namespace avgen::directing
