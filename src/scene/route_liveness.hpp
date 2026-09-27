#pragma once

// ADR-902: the scene's half of the liveness rules (`params::liveness`), and the whole audit of a
// project -- every route, timeline track, effect default route and effect, each with a verdict
// (live / dead / hazard) and the evidence -- that `avgen --audit-routes` writes and a project load
// logs.
//
// `SceneLivenessFacts` answers the questions a rule cannot answer from a bus and a parameter set:
// which programs write emission and which are on the GPU, which nodes emit anything at all, which
// effects can fire in this piece, which parameters are phase rates, which timeline sources score
// events and which score pulses. It reads the live composition, so it is built per question (a bind,
// a load, an audit) and is cheap to build: every fact is computed the first time a rule asks for it.

#include "analysis/meter.hpp"
#include "params/liveness.hpp"
#include "params/modulation.hpp"
#include "params/timeline.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_timing.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::analysis {
class AnalysisTrack;
}
namespace avgen::seq {
struct Marker;
}
namespace avgen::world {
class HistoryBank;
}

namespace avgen::scene {

class Composition;

// Everything the facts read. Pointers are borrowed for the facts' lifetime; null means "none".
struct LivenessInputs {
    const signals::SignalBus* bus = nullptr;
    const params::ParameterSet* params = nullptr;
    const signals::SourceRack* sources = nullptr;
    const Composition* composition = nullptr;
    std::span<const world::EffectInstance> effects;
    std::span<const world::ShotSpan> shots;
    const analysis::AnalysisTrack* track = nullptr;
    std::span<const seq::Marker> markers;
    const world::HistoryBank* history = nullptr;
    // The engine's musical time (ADR-896): the downbeat, and the bar, phrase and section lengths
    // an effect's Bar/Phrase/Section trigger counts in. The same one the trigger clock uses in play.
    analysis::Meter meter{};
    double durationSeconds = 0.0; // the piece's length; 0 = unknown
    double frameRate = 60.0;      // the project's render rate
    bool hasAudio = false;        // an analysed track is installed
    bool hasTempo = false;        // a tempo is known (from the audio or an override)
    bool offline = true;          // a render: live MIDI/OSC input is not part of it
    // Whether "no audio" makes an audio signal silent. A question about the project, not about a
    // bind: the engine binds before a project's audio is installed, and an editor session binds with
    // no audio at all, so a bind asks with this off and a load's report and the audit with it on.
    bool judgeSilence = true;
    // The project's routes and tracks, for the rules that ask whether anything drives a parameter a
    // target depends on (ADR-916: a tear setting is read only while the water's tear amount can be
    // above 0). Null = unknown, and those rules then say nothing rather than guess.
    const params::Modulator* modulator = nullptr;
    const params::Timeline* timeline = nullptr;
};

class SceneLivenessFacts final : public params::liveness::Facts {
public:
    explicit SceneLivenessFacts(LivenessInputs inputs);
    ~SceneLivenessFacts() override;
    SceneLivenessFacts(const SceneLivenessFacts&) = delete;
    SceneLivenessFacts& operator=(const SceneLivenessFacts&) = delete;

    [[nodiscard]] params::liveness::SignalFacts signal(std::string_view name) const override;
    [[nodiscard]] const params::IParameter* parameter(std::string_view path) const override;
    [[nodiscard]] std::optional<params::liveness::Finding> deadTarget(std::string_view path,
                                                                      int component) const override;
    [[nodiscard]] std::optional<params::liveness::Finding> phaseRate(std::string_view path) const override;
    [[nodiscard]] double frameRate() const override { return in_.frameRate; }

    // Why `effect` cannot fire in this project, as a finding (`disabled` or `effect-never-fires`);
    // nothing when it can.
    [[nodiscard]] std::optional<params::liveness::Finding> effectNeverFires(const world::EffectInstance& effect) const;

    [[nodiscard]] const LivenessInputs& inputs() const { return in_; }

private:
    struct Cache;
    [[nodiscard]] const Cache& cache() const;
    LivenessInputs in_;
    mutable std::unique_ptr<Cache> cache_;
};

// ---- the phase-rate table ------------------------------------------------------------------------

// One parameter whose phase is (absolute time x its value), found by reading the code that uses it.
// `path` is matched against the LAST segments of a parameter path (a nested composition prefixes
// `nodes/<child>/`), and `*` matches any one segment. `kinds` narrows `sources/*/...` to source kinds
// and `fx/*/...` to effect types, comma-separated (empty = any). `evidence` is where the
// time x rate term is. Three conditions: `whenNonZero` names sibling leaves at least one of which must
// be non-zero for the trap to exist (a scale of a coordinate that only moves while a primary rate
// does), `windBodies` limits it to scenes with wind-body meshes (`scene/windSpeed` is a phase
// rate only through their leaf flutter), and `windTears` to scenes with a water whose tears can show,
// follow the wind and drift (ADR-916: the wind's direction is then the drifting seams' direction).
struct PhaseRateEntry {
    std::string_view path;
    std::string_view kinds;
    std::string_view evidence;
    std::string_view whenNonZero = {};
    bool windBodies = false;
    bool windTears = false;
};
[[nodiscard]] std::span<const PhaseRateEntry> phaseRateTable();

// ---- the audit ---------------------------------------------------------------------------------

struct AuditEntry {
    std::size_t index = 0;
    std::string label;       // one line for a log: "audio.bass -> nodes/valley/water/ripple"
    nlohmann::json detail;   // the entry's fields as the JSON report lists them
    params::liveness::Verdict verdict = params::liveness::Verdict::Live;
    std::vector<params::liveness::Finding> findings;
};

struct RouteAudit {
    std::vector<AuditEntry> routes;
    std::vector<AuditEntry> tracks;
    std::vector<AuditEntry> effectDefaultRoutes;
    std::vector<AuditEntry> effects;
};

// Every route, track, effect default route and effect of the project, put through the registry.
[[nodiscard]] RouteAudit auditProject(const LivenessInputs& inputs, const params::Modulator& modulator,
                                      const params::Timeline& timeline);

// The report `avgen --audit-routes` writes (format "avgen-route-audit", version 1; ADR-902 has the
// schema). `project` is the path it names.
[[nodiscard]] nlohmann::json routeAuditToJson(const RouteAudit& audit, const LivenessInputs& inputs,
                                              std::string_view project);

// "no rule found ..." for a live entry, otherwise the first finding of the entry's verdict.
[[nodiscard]] std::string auditReason(const AuditEntry& entry);

} // namespace avgen::scene
