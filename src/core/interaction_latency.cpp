#include "core/interaction_latency.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

namespace avgen::core {

namespace {

struct Name {
    Interaction kind;
    std::string_view name;
};

// The table is the single source of truth for both directions, so a new interaction cannot be
// added to one and forgotten in the other.
constexpr std::array<Name, static_cast<std::size_t>(Interaction::Count)> kNames{{
    {Interaction::TimelineClick, "timeline-click"},
    {Interaction::TimelineDrag, "timeline-drag"},
    {Interaction::HeroStar, "hero-star"},
    {Interaction::Selection, "selection"},
    {Interaction::PropertyDrag, "property-drag"},
    {Interaction::PanelToggle, "panel-toggle"},
    {Interaction::TabSwitch, "tab-switch"},
    {Interaction::CameraOrbit, "camera-orbit"},
    {Interaction::GizmoDrag, "gizmo-drag"},
    {Interaction::WorldEdit, "world-edit"},
}};

[[nodiscard]] double millis(Stamp from, Stamp to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

[[nodiscard]] std::optional<double> span(const std::optional<Stamp>& from,
                                         const std::optional<Stamp>& to) {
    if (!from || !to) {
        return std::nullopt;
    }
    return millis(*from, *to);
}

// "12.34" or "  --  ". An unavailable statistic is rendered as a dash rather than a zero, in the
// report as well as in the type, because a table is where a zero gets quoted from.
[[nodiscard]] std::string cell(const std::optional<double>& v, int width) {
    if (!v) {
        return fmt::format("{:>{}}", "--", width);
    }
    return fmt::format("{:>{}.2f}", *v, width);
}

[[nodiscard]] std::string cell(const LatencyDistribution& d, double LatencyDistribution::*field,
                               int width) {
    if (!d.available()) {
        return fmt::format("{:>{}}", "--", width);
    }
    return fmt::format("{:>{}.1f}", d.*field, width);
}

} // namespace

std::string_view interactionName(Interaction kind) {
    for (const Name& n : kNames) {
        if (n.kind == kind) {
            return n.name;
        }
    }
    return "unknown";
}

std::optional<Interaction> interactionFromName(std::string_view name) {
    for (const Name& n : kNames) {
        if (n.name == name) {
            return n.kind;
        }
    }
    return std::nullopt;
}

LatencyBand bandFor(double ms) {
    if (ms < 50.0) {
        return LatencyBand::Preferred;
    }
    if (ms < 100.0) {
        return LatencyBand::Interactive;
    }
    if (ms < 250.0) {
        return LatencyBand::Noticed;
    }
    if (ms < 1000.0) {
        return LatencyBand::Defect;
    }
    return LatencyBand::FlowBreak;
}

std::string_view bandName(LatencyBand band) {
    switch (band) {
    case LatencyBand::Preferred: return "preferred";
    case LatencyBand::Interactive: return "interactive";
    case LatencyBand::Noticed: return "noticed";
    case LatencyBand::Defect: return "defect";
    case LatencyBand::FlowBreak: return "flow-break";
    }
    return "unknown";
}

// ---- InteractionRecord -------------------------------------------------------------------------

std::optional<double> InteractionRecord::inputToAck() const { return span(input, command); }
std::optional<double> InteractionRecord::inputToModel() const { return span(input, model); }
std::optional<double> InteractionRecord::inputToFirstVisual() const {
    return span(input, firstVisible);
}
std::optional<double> InteractionRecord::inputToFinalVisual() const { return span(input, visible); }

bool InteractionRecord::complete() const {
    return kind != Interaction::Count && command.has_value() && visible.has_value();
}

// ---- distributions -------------------------------------------------------------------------------

LatencyDistribution distributionOf(std::vector<double> samples) {
    LatencyDistribution d;
    if (samples.empty()) {
        return d; // samples == 0, and `available()` is what a caller must ask.
    }
    std::sort(samples.begin(), samples.end());
    d.samples = samples.size();
    d.min = samples.front();
    d.max = samples.back();
    // Nearest-rank: the p-th percentile is the sample at ceil(p/100 * n), 1-based. No interpolation,
    // because an interpolated percentile is a latency that nobody actually experienced, and the
    // whole point of this instrument is that the tail is a real experience rather than a statistic.
    const auto rank = [&](double p) {
        const auto n = static_cast<double>(samples.size());
        auto idx = static_cast<std::size_t>(std::ceil(p * n));
        if (idx == 0) {
            idx = 1;
        }
        if (idx > samples.size()) {
            idx = samples.size();
        }
        return samples[idx - 1];
    };
    d.median = rank(0.50);
    d.p90 = rank(0.90);
    d.p95 = rank(0.95);
    d.p99 = rank(0.99);
    return d;
}

// ---- InteractionLog -----------------------------------------------------------------------------

void InteractionLog::beginFrame(std::uint64_t frameIndex) {
    frameIndex_ = frameIndex;
    latestInput_.reset();
    latestReceipt_.reset();
}

void InteractionLog::noteInput(Stamp t0, Stamp t1) {
    if (!latestInput_ || t0 > *latestInput_) {
        latestInput_ = t0;
        latestReceipt_ = t1;
    }
}

void InteractionLog::beginFromInput(Interaction kind) {
    begin(kind, frameIndex_, latestInput_, latestReceipt_);
}

void InteractionLog::beginWithoutInput(Interaction kind) {
    begin(kind, frameIndex_, std::nullopt, std::nullopt);
}

void InteractionLog::begin(Interaction kind, std::uint64_t frame, std::optional<Stamp> input,
                           std::optional<Stamp> receipt) {
    if (kind == Interaction::Count) {
        return;
    }
    if (open_) {
        // Two interactions in flight at once. Today that cannot happen -- every stage runs on the
        // main thread inside one frame -- so it means the serialisation assumption has broken, and
        // it is counted rather than silently resolved. The older record is abandoned: a record
        // whose stages are interleaved with another's is not a measurement of either.
        ++overlaps_;
        abandon();
    }
    current_ = InteractionRecord{};
    current_.kind = kind;
    current_.frame = frame;
    current_.input = input;
    current_.receipt = receipt;
    open_ = true;
    ++begun_[static_cast<std::size_t>(kind)];
}

void InteractionLog::markCommand() {
    if (open_ && !current_.command) {
        current_.command = Clock::now();
    }
}

void InteractionLog::markModel() {
    if (open_ && !current_.model) {
        current_.model = Clock::now();
    }
}

void InteractionLog::markPresentation() {
    if (open_ && !current_.presentation) {
        current_.presentation = Clock::now();
    }
}

void InteractionLog::markSubmit() {
    if (open_ && !current_.submit) {
        current_.submit = Clock::now();
    }
}

void InteractionLog::markFrameVisible() {
    if (!open_) {
        return;
    }
    const Stamp now = Clock::now();
    if (!current_.firstVisible) {
        current_.firstVisible = now;
    }
    if (!current_.presentation) {
        // The evaluated result is not in the scene yet. The frame that just presented showed the
        // cheap immediate response, which is `firstVisible`; the record stays open until the work
        // lands. Filing it now would report a deferral as a fast interaction, which is the single
        // most flattering lie this instrument could tell.
        return;
    }
    current_.visible = now;
    if (ring_.size() >= kHistory) {
        ring_.erase(ring_.begin());
    }
    ++completed_[static_cast<std::size_t>(current_.kind)];
    ring_.push_back(current_);
    open_ = false;
    current_ = InteractionRecord{};
}

void InteractionLog::addCpuMs(double ms) {
    if (open_) {
        current_.cpuMs += ms;
    }
}
void InteractionLog::addBlockedMs(double ms) {
    if (open_) {
        current_.blockedMs += ms;
    }
}
void InteractionLog::setGpuMs(double ms) {
    if (open_) {
        current_.gpuMs = ms;
    }
}
void InteractionLog::addCounters(std::uint64_t flattens, std::uint64_t seeks,
                                 std::uint64_t proceduralRegens, std::uint64_t texturesUploaded,
                                 std::uint64_t entitySimBodies) {
    if (!open_) {
        return;
    }
    current_.flattens += flattens;
    current_.seeks += seeks;
    current_.proceduralRegens += proceduralRegens;
    current_.texturesUploaded += texturesUploaded;
    current_.entitySimBodies += entitySimBodies;
}
void InteractionLog::addInjectedMs(double ms) {
    if (open_) {
        current_.injectedMs += ms;
    }
}

void InteractionLog::abandon() {
    if (!open_) {
        return;
    }
    ++abandoned_[static_cast<std::size_t>(current_.kind)];
    open_ = false;
    current_ = InteractionRecord{};
}

std::uint64_t InteractionLog::begun(Interaction kind) const {
    return kind == Interaction::Count ? 0 : begun_[static_cast<std::size_t>(kind)];
}
std::uint64_t InteractionLog::completed(Interaction kind) const {
    return kind == Interaction::Count ? 0 : completed_[static_cast<std::size_t>(kind)];
}
std::uint64_t InteractionLog::abandoned(Interaction kind) const {
    return kind == Interaction::Count ? 0 : abandoned_[static_cast<std::size_t>(kind)];
}

void InteractionLog::clear() {
    ring_.clear();
    current_ = InteractionRecord{};
    open_ = false;
    overlaps_ = 0;
    begun_.fill(0);
    completed_.fill(0);
    abandoned_.fill(0);
}

// ---- the control (ADR-182) ------------------------------------------------------------------------

namespace {
// Main thread only, like everything else here.
std::array<double, static_cast<std::size_t>(Interaction::Count)>& injections() {
    static std::array<double, static_cast<std::size_t>(Interaction::Count)> table{};
    return table;
}
} // namespace

void setInjectedDelay(Interaction kind, double ms) {
    if (kind == Interaction::Count) {
        return;
    }
    injections()[static_cast<std::size_t>(kind)] = ms > 0.0 ? ms : 0.0;
}

double injectedDelay(Interaction kind) {
    return kind == Interaction::Count ? 0.0 : injections()[static_cast<std::size_t>(kind)];
}

void applyInjectedDelay(Interaction kind) {
    const double ms = injectedDelay(kind);
    if (ms <= 0.0) {
        return;
    }
    const Stamp until =
        Clock::now() +
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(ms));
    while (Clock::now() < until) {
    }
    interactions().addInjectedMs(ms);
}

void applyInjectedDelayForOpenInteraction() { applyInjectedDelay(interactions().openKind()); }

// ---- the report ---------------------------------------------------------------------------------

std::vector<InteractionSummary> summarise(const InteractionLog& log) {
    std::vector<InteractionSummary> out;
    for (std::size_t k = 0; k < static_cast<std::size_t>(Interaction::Count); ++k) {
        const auto kind = static_cast<Interaction>(k);
        InteractionSummary s;
        s.kind = kind;
        s.begun = log.begun(kind);
        s.completed = log.completed(kind);
        s.abandoned = log.abandoned(kind);

        std::vector<double> ack;
        std::vector<double> first;
        std::vector<double> final;
        std::vector<double> cpu;
        std::vector<double> blocked;
        std::vector<double> gpu;
        for (const InteractionRecord& r : log.all()) {
            if (r.kind != kind) {
                continue;
            }
            if (r.injectedMs > 0.0) {
                // A calibration sample. Counted so the control can be seen to have run, and kept
                // out of every distribution so it cannot be quoted as a real latency.
                ++s.injectedSamples;
                continue;
            }
            s.flattens += r.flattens;
            s.seeks += r.seeks;
            s.proceduralRegens += r.proceduralRegens;
            s.texturesUploaded += r.texturesUploaded;
            s.entitySimBodies += r.entitySimBodies;
            if (auto v = r.inputToAck()) {
                ack.push_back(*v);
            }
            if (auto v = r.inputToFirstVisual()) {
                first.push_back(*v);
            }
            if (auto v = r.inputToFinalVisual()) {
                final.push_back(*v);
            }
            cpu.push_back(r.cpuMs);
            blocked.push_back(r.blockedMs);
            if (r.gpuMs) {
                gpu.push_back(*r.gpuMs);
            }
        }
        if (s.begun == 0 && s.completed == 0) {
            continue; // an interaction nobody performed is absent, not a row of zeros.
        }
        s.ack = distributionOf(std::move(ack));
        s.firstVisual = distributionOf(std::move(first));
        s.finalVisual = distributionOf(std::move(final));
        s.cpu = distributionOf(std::move(cpu));
        s.blocked = distributionOf(std::move(blocked));
        s.gpu = distributionOf(std::move(gpu));
        out.push_back(s);
    }
    return out;
}

std::string formatReport(const std::vector<InteractionSummary>& summaries, double loadAverage) {
    std::string out;
    out += fmt::format("\ninteraction latency -- one record per interaction, not per frame\n");
    out += fmt::format("one-minute load average {:.2f}; distributions are nearest-rank, no "
                       "interpolation; '--' means the stage did not happen\n",
                       loadAverage);
    out += fmt::format("bands: <50 preferred, <100 interactive, <250 noticed, <1000 defect, "
                       ">=1000 flow-break\n\n");
    out += fmt::format("{:<16}{:>5}{:>5}{:>5} | {:>28} | {:>28} | {:>17}\n", "interaction", "beg",
                       "done", "aband", "input->ack ms", "input->final visual ms", "caused");
    out += fmt::format("{:<16}{:>5}{:>5}{:>5} | {:>6}{:>7}{:>7}{:>8} | {:>6}{:>7}{:>7}{:>8} | "
                       "{:>5}{:>6}{:>6}\n",
                       "", "", "", "", "min", "med", "p95", "max", "min", "med", "p95", "max",
                       "flat", "seeks", "tex");
    for (const InteractionSummary& s : summaries) {
        out += fmt::format(
            "{:<16}{:>5}{:>5}{:>5} | {}{}{}{} | {}{}{}{} | {:>5}{:>6}{:>6}\n",
            interactionName(s.kind), s.begun, s.completed, s.abandoned,
            cell(s.ack, &LatencyDistribution::min, 6), cell(s.ack, &LatencyDistribution::median, 7),
            cell(s.ack, &LatencyDistribution::p95, 7), cell(s.ack, &LatencyDistribution::max, 8),
            cell(s.finalVisual, &LatencyDistribution::min, 6),
            cell(s.finalVisual, &LatencyDistribution::median, 7),
            cell(s.finalVisual, &LatencyDistribution::p95, 7),
            cell(s.finalVisual, &LatencyDistribution::max, 8), s.flattens, s.seeks,
            s.texturesUploaded);
    }
    out += "\n";
    // The second table is the one that answers the brief's question about deferral: when
    // `first visual` and `final visual` are the same number the interaction is synchronous, and
    // nothing has been decoupled no matter what the architecture diagram says.
    out += fmt::format("{:<16} | {:>26} | {:>26} | {:>20}\n", "interaction",
                       "input->first visual ms", "input->final visual ms", "cpu / blocked / gpu ms");
    out += fmt::format("{:<16} | {:>6}{:>7}{:>7}{:>7} | {:>6}{:>7}{:>7}{:>7} | {:>6}{:>7}{:>7}\n",
                       "", "min", "med", "p95", "p99", "min", "med", "p95", "p99", "cpu", "blkd",
                       "gpu");
    for (const InteractionSummary& s : summaries) {
        out += fmt::format(
            "{:<16} | {}{}{}{} | {}{}{}{} | {}{}{}\n", interactionName(s.kind),
            cell(s.firstVisual, &LatencyDistribution::min, 6),
            cell(s.firstVisual, &LatencyDistribution::median, 7),
            cell(s.firstVisual, &LatencyDistribution::p95, 7),
            cell(s.firstVisual, &LatencyDistribution::p99, 7),
            cell(s.finalVisual, &LatencyDistribution::min, 6),
            cell(s.finalVisual, &LatencyDistribution::median, 7),
            cell(s.finalVisual, &LatencyDistribution::p95, 7),
            cell(s.finalVisual, &LatencyDistribution::p99, 7),
            cell(s.cpu, &LatencyDistribution::median, 6),
            cell(s.blocked, &LatencyDistribution::median, 7),
            cell(s.gpu, &LatencyDistribution::median, 7));
    }
    for (const InteractionSummary& s : summaries) {
        if (s.injectedSamples > 0) {
            out += fmt::format("\n{} carried {} calibration sample(s) with a deliberate slowdown "
                               "(ADR-182); they are excluded from every distribution above.\n",
                               interactionName(s.kind), s.injectedSamples);
        }
        if (s.begun > 0 && s.completed == 0) {
            out += fmt::format("\n{}: {} begun, 0 completed -- THIS INTERACTION MEASURED NOTHING.\n",
                               interactionName(s.kind), s.begun);
        }
    }
    if (summaries.empty()) {
        out += "no interactions were recorded -- nothing here measured anything.\n";
    }
    return out;
}

std::string formatCsv(const InteractionLog& log) {
    std::string out =
        "interaction,frame,input_to_ack_ms,input_to_model_ms,input_to_first_visual_ms,"
        "input_to_final_visual_ms,cpu_ms,blocked_ms,gpu_ms,flattens,seeks,procedural_regens,"
        "textures_uploaded,entity_sim_bodies,injected_ms\n";
    const auto f = [](const std::optional<double>& v) {
        return v ? fmt::format("{:.4f}", *v) : std::string{};
    };
    for (const InteractionRecord& r : log.all()) {
        out += fmt::format("{},{},{},{},{},{},{:.4f},{:.4f},{},{},{},{},{},{},{:.4f}\n",
                           interactionName(r.kind), r.frame, f(r.inputToAck()), f(r.inputToModel()),
                           f(r.inputToFirstVisual()), f(r.inputToFinalVisual()), r.cpuMs,
                           r.blockedMs, f(r.gpuMs), r.flattens, r.seeks, r.proceduralRegens,
                           r.texturesUploaded, r.entitySimBodies, r.injectedMs);
    }
    return out;
}

} // namespace avgen::core
