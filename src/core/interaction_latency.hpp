#pragma once

// Interaction latency: how long after a person acts does the application answer.
//
// This is deliberately NOT `core::PhaseProfiler`, and the distinction is the reason this file
// exists. PhaseProfiler answers "what did the main thread spend *this frame* doing". It is the
// right instrument for pacing and it cannot see the subject here at all: a frame budget of 14 ms
// GPU / 8 ms CPU / 3 ms UI coexists perfectly happily with a 183 ms click-to-response, because the
// 183 ms is one frame in fifty and a distribution over *frames* drowns it.
//
// So the unit of this instrument is the **interaction**, not the frame. One record per thing a
// person did. Seven stamps per record, from the device's own timestamp to the compositor.
//
//   T0  input        the timestamp the device/SDL put on the event
//   T1  receipt      the application's handler saw it
//   T2  command      some code decided this event means an action  (the earliest instant at which
//                    anything could legitimately be drawn differently)
//   T3  model        the authoritative state changed
//   T4  presentation the derived state a panel or the renderer reads changed
//   T5  submit       the UI that shows the change was recorded
//   T6  visible      present() returned
//
// Derived, and these three are the numbers the product is judged by:
//
//   input_to_ack           T2 - T0
//   input_to_first_visual  T6 - T0 of the first frame that presented *any* change
//   input_to_final_visual  T6 - T0 of the frame that presented the fully evaluated result
//
// Today, for every interaction in this editor, first == final, because every interaction is
// synchronous. Separating them is the whole point of having somewhere to put the difference.
//
// ---- the honesty property, inherited from the Quality Lab (ADR-250) -------------------------
//
// A stage that did not happen is `unavailable`, never zero. An interaction that never reached T3
// did not change the model in 0.0 ms; it did not change the model. A zero in a latency column is
// a claim, and a latency instrument that reports plausible numbers for an interaction it never
// performed is worse than no instrument, because it will be trusted (ADR-182).
//
// ---- distributions, not minima, and why this disagrees with ADR-170 -------------------------
//
// ADR-170 says minima over repeats, because contention is never negative and the minimum is the
// honest estimate of *the work*. That is right for throughput: the question there is how much work
// there is. It is wrong here. The question here is what a person experiences, and a person
// experiences the tail. A scrub whose minimum is 165 ms and whose p99 is 5,009 ms is not a 165 ms
// scrub. So this reports min, median, p90, p95, p99 and max, and the load average is recorded
// beside them so the median can be read for what it is.
//
// ---- thread affinity -------------------------------------------------------------------------
//
// Main thread only. No locks, no atomics. The stamps are taken by the thread that owns the frame,
// which is the thread every one of these stages runs on today; if that ever stops being true the
// stage that moved must be given its own record rather than stamped across a thread boundary.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::core {

// What the person did. Named after the gesture, not after the code it reaches: `TimelineClick` is
// one entry even though three panels call `seekSeconds`, because a person clicking a ruler does
// not care which file answered.
enum class Interaction : std::uint8_t {
    TimelineClick,  // a press on the sequencer ruler
    TimelineDrag,   // one frame of a scrub gesture
    HeroStar,       // starring / unstarring an object
    Selection,      // changing which object is selected
    PropertyDrag,   // one frame of a slider held
    PanelToggle,    // opening or closing a panel
    TabSwitch,      // changing the authoring layer
    CameraOrbit,    // one frame of a canvas drag
    GizmoDrag,      // one frame of a gizmo drag
    WorldEdit,      // a scatter-brush stroke, an undo, a redo
    Count,
};

[[nodiscard]] std::string_view interactionName(Interaction kind);
// Parses the name back. Returns nullopt rather than a default, because a mistyped arm name that
// silently measured `TimelineClick` is the failure mode this whole file is about.
[[nodiscard]] std::optional<Interaction> interactionFromName(std::string_view name);

// Nielsen's thresholds, and the owner's targets on top of them. A band, not a pass/fail, because
// "97 ms" and "103 ms" are the same experience and a boolean at 100 would report them differently.
enum class LatencyBand : std::uint8_t {
    Preferred,   // < 50 ms   -- indistinguishable from instant
    Interactive, // < 100 ms  -- Nielsen's "feels instantaneous" ceiling
    Noticed,     // < 250 ms  -- the user notices the delay but stays in flow
    Defect,      // < 1000 ms -- a serious defect
    FlowBreak,   // >= 1000 ms -- attention goes elsewhere
};

[[nodiscard]] LatencyBand bandFor(double ms);
[[nodiscard]] std::string_view bandName(LatencyBand band);

using Clock = std::chrono::steady_clock;
using Stamp = Clock::time_point;

// One interaction's stamps. Every stage is optional and absent means absent.
struct InteractionRecord {
    Interaction kind = Interaction::Count;
    std::uint64_t frame = 0; // the frame on which T2 was taken
    int group = -1;          // the --ui-ab block this record belongs to; -1 is "not part of an arm"
    // How many further requests arrived while this one was outstanding and were folded into it.
    // Zero for every interaction that is evaluated on the frame it arrives, which today is all of
    // them bar a held scrub. This is the direct measurement of coalescing: a gesture that issues
    // forty-four requests and evaluates two has coalesced forty-two, and no millisecond figure on a
    // contended machine says that as clearly as the count does.
    std::uint64_t coalesced = 0;

    std::optional<Stamp> input;        // T0
    std::optional<Stamp> receipt;      // T1
    std::optional<Stamp> command;      // T2
    std::optional<Stamp> model;        // T3
    std::optional<Stamp> presentation; // T4
    std::optional<Stamp> submit;       // T5
    // T6, twice. `firstVisible` is the first frame that presented *anything* attributable to this
    // interaction; `visible` is the frame that presented the fully evaluated result. They are the
    // same stamp for every interaction in this editor today, because every interaction is
    // synchronous -- which is exactly the fact worth being able to see change.
    std::optional<Stamp> firstVisible; // T6 (first)
    std::optional<Stamp> visible;      // T6 (final)

    // The breakdown the CPU can attribute. `blockedMs` is time the main thread spent waiting on
    // something else (the swapchain, a readback, a join) and is named as a wait for the same reason
    // PhaseProfiler names `gpu.acquire WAIT`: a number that grows when the GPU falls behind must
    // never be read as the CPU getting slower.
    double cpuMs = 0.0;
    double blockedMs = 0.0;
    // GPU execution for the frame that presented this interaction, from `gpu::FrameTimeline`, i.e.
    // a GPU timestamp and never CPU wall-clock around a GPU call. Absent when the timeline had not
    // yet reported a completed frame -- it runs two or three frames behind and that is not zero.
    std::optional<double> gpuMs;

    // What the interaction *caused*, which is the evidence contention cannot corrupt. ADR-170's
    // real lesson: "none where there were twenty-two" survives a load average of forty-four.
    std::uint64_t flattens = 0;
    std::uint64_t seeks = 0;
    std::uint64_t proceduralRegens = 0;
    std::uint64_t texturesUploaded = 0;
    std::uint64_t entitySimBodies = 0;

    // Set by the deliberate-slowdown control (ADR-182). A record carrying this is a calibration
    // sample and must never be pooled with real ones.
    double injectedMs = 0.0;

    // ---- derived -------------------------------------------------------------------------------
    // Each returns nullopt when either end is missing. Callers must render that as "unavailable".
    [[nodiscard]] std::optional<double> inputToAck() const;
    [[nodiscard]] std::optional<double> inputToModel() const;
    [[nodiscard]] std::optional<double> inputToFirstVisual() const;
    [[nodiscard]] std::optional<double> inputToFinalVisual() const;

    // True when the record has enough stamps to mean anything. A record with a command and no
    // visible frame is an interaction that was begun and never finished -- a crash, a run that
    // ended mid-gesture, or an arm that did nothing -- and it is dropped from the distributions
    // and counted separately rather than treated as fast.
    [[nodiscard]] bool complete() const;
};

struct LatencyDistribution {
    std::size_t samples = 0;
    double min = 0.0;
    double median = 0.0;
    double p90 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
    // Present only when `samples > 0`. A distribution over zero samples has no minimum, and
    // reporting 0.0 for one is the exact mistake this file refuses to make.
    [[nodiscard]] bool available() const { return samples > 0; }
};

// Order statistics over a set of samples. Pure, so it is a unit test rather than an argument.
// Percentiles use the nearest-rank method: p95 of 20 samples is the 19th, not an interpolation
// between the 19th and the 20th. Interpolating invents a latency nobody experienced.
[[nodiscard]] LatencyDistribution distributionOf(std::vector<double> samples);

// ---- the log ------------------------------------------------------------------------------------
//
// A ring of the last kHistory records, plus per-kind counters that do not roll off. The ring is for
// distributions; the counters are for "did this interaction ever happen at all", which is the
// question an arm's self-proof asks.
class InteractionLog {
public:
    static constexpr std::size_t kHistory = 4096;

    // Called once at the top of each frame. Clears the frame's input slot, so an interaction opened
    // on a frame with no input cannot inherit the previous frame's event and report a latency that
    // spans a gesture nobody made.
    void beginFrame(std::uint64_t frameIndex);
    // The --ui-ab block a record belongs to. Mirrors `PhaseProfiler::setFrameGroup` and exists for
    // the same reason: two conditions compared inside one process, because comparing a frame time
    // from one run against another is forbidden here.
    void setGroup(int group) { group_ = group; }

    // Called by the event handler for every pointer/key event. Keeps the newest. `t0` is the
    // device's own timestamp translated into this clock; `t1` is when the handler saw it.
    void noteInput(Stamp t0, Stamp t1);
    [[nodiscard]] std::optional<Stamp> latestInput() const { return latestInput_; }

    // Opens a record. `input` is the device timestamp if there was one -- a value-driven command
    // that writes a parameter directly produces no SDL event, and passing nullopt is how that is
    // said rather than substituting `now` and reporting an input latency for an input that never
    // happened.
    void begin(Interaction kind, std::uint64_t frame, std::optional<Stamp> input,
               std::optional<Stamp> receipt);
    // The two forms a call site actually wants. `beginFromInput` attributes the interaction to this
    // frame's newest input event; if there was none it opens the record without one rather than
    // refusing, because the interaction still happened.
    void beginFromInput(Interaction kind);
    void beginWithoutInput(Interaction kind);

    // True while a record is open. At most one interaction is tracked at a time: every stage below
    // runs on the main thread inside one frame today, so a second interaction opening before the
    // first closed would mean the serialisation assumption has broken, and that is worth knowing.
    [[nodiscard]] bool open() const { return open_; }
    [[nodiscard]] std::uint64_t overlaps() const { return overlaps_; }

    // Folds a further request into the record already open, rather than opening a second one. A
    // held drag is one outstanding batch of requests, not one interaction per frame; T0 stays at
    // the batch's *oldest* request, which is the pessimistic end and the one a deferral must be
    // measured against.
    void noteSuperseded();
    void markCommand();
    void markModel();
    // The derived state a panel or the renderer reads now reflects the interaction. For a
    // synchronous interaction this is the same frame as `markCommand`. For a deferred one it is
    // whichever later frame the evaluation landed on, and the gap between them is the thing a
    // deferral is trading away.
    void markPresentation();
    void markSubmit();
    // Called after present() on every frame while a record is open. Stamps `firstVisible` once;
    // files the record when the presentation stage has also been reached. A record whose
    // presentation never arrives stays open, and `abandon()` counts it -- it is not filed as a fast
    // interaction, which is what stamping a final time here unconditionally would do.
    void markFrameVisible();

    // Accumulators the open record collects; no-ops when nothing is open, so a caller does not have
    // to ask first and an engine phase does not have to know whether a person is interacting.
    void addCpuMs(double ms);
    void addBlockedMs(double ms);
    void setGpuMs(double ms);
    void addCounters(std::uint64_t flattens, std::uint64_t seeks, std::uint64_t proceduralRegens,
                     std::uint64_t texturesUploaded, std::uint64_t entitySimBodies);
    void addInjectedMs(double ms);

    // Abandons an open record without filing it. For a gesture that was cancelled.
    void abandon();

    [[nodiscard]] const std::vector<InteractionRecord>& all() const { return ring_; }
    [[nodiscard]] std::uint64_t begun(Interaction kind) const;
    [[nodiscard]] std::uint64_t completed(Interaction kind) const;
    [[nodiscard]] std::uint64_t abandoned(Interaction kind) const;
    [[nodiscard]] Interaction openKind() const { return open_ ? current_.kind : Interaction::Count; }

    void clear();

private:
    std::vector<InteractionRecord> ring_;
    InteractionRecord current_;
    bool open_ = false;
    std::uint64_t overlaps_ = 0;
    std::array<std::uint64_t, static_cast<std::size_t>(Interaction::Count)> begun_{};
    std::array<std::uint64_t, static_cast<std::size_t>(Interaction::Count)> completed_{};
    std::array<std::uint64_t, static_cast<std::size_t>(Interaction::Count)> abandoned_{};
    std::uint64_t frameIndex_ = 0;
    int group_ = -1;
    std::optional<Stamp> latestInput_;
    std::optional<Stamp> latestReceipt_;
};

// One instance, main thread only. `inline` so `avgen_core`, `avgen_app` and the UI share it without
// threading an instrument through three static libraries -- the same reason, and the same shape, as
// the probe this file replaces. The difference is that this one is not temporary and is honest
// about what it could not measure.
inline InteractionLog& interactions() {
    static InteractionLog log;
    return log;
}

// ---- the control (ADR-182) ------------------------------------------------------------------------
//
// A probe that cannot fail proves nothing, and for a latency harness the acute form is that it will
// report plausible numbers for an interaction it never performed. So the harness ships with a way
// to make a named interaction deliberately slower by a known amount, applied *inside the engine
// work the interaction performs* rather than inside the instrument -- an instrument that slowed
// itself would only prove that it can add.
//
// A run with an injection is a calibration run. Its records carry `injectedMs` and are excluded
// from every distribution, so the control cannot leak into a result.
void setInjectedDelay(Interaction kind, double ms);
[[nodiscard]] double injectedDelay(Interaction kind);
// Spends `injectedDelay(kind)` milliseconds busy on this thread and records it on the open record.
// Busy rather than asleep: a sleeping thread is not a working one, and the point of the control is
// to stand in for work the interaction might have done.
void applyInjectedDelay(Interaction kind);
// The form a call site inside engine work wants: slow whichever interaction is currently open. A
// seek reached from the transport's own end-of-piece wrap has no open record and is not slowed,
// which is right -- the control is calibrating the *interactive* path.
void applyInjectedDelayForOpenInteraction();

// ---- the report ---------------------------------------------------------------------------------

struct InteractionSummary {
    Interaction kind = Interaction::Count;
    std::uint64_t begun = 0;
    std::uint64_t completed = 0;
    std::uint64_t abandoned = 0;
    LatencyDistribution ack;         // input -> command
    LatencyDistribution firstVisual; // input -> the first frame showing anything
    LatencyDistribution finalVisual; // input -> the frame showing the evaluated result
    LatencyDistribution cpu;
    LatencyDistribution blocked;
    LatencyDistribution gpu;
    // Sums, not distributions: "one flatten per click" and "one per frame of the drag" are
    // different defects and a percentile of a count tells them apart badly.
    std::uint64_t flattens = 0;
    std::uint64_t seeks = 0;
    std::uint64_t proceduralRegens = 0;
    std::uint64_t texturesUploaded = 0;
    std::uint64_t entitySimBodies = 0;
    std::uint64_t injectedSamples = 0;
    std::uint64_t coalesced = 0;
};

// Summarises every kind present in the log. Records carrying an injection are reported under
// `injectedSamples` and excluded from every distribution: a calibration sample pooled with real
// ones would make the instrument lie in exactly the direction that flatters it.
// `group` selects one --ui-ab block; `kAllGroups` is every record. A summary that pooled two arms
// of an A/B would report their average and call it a result.
inline constexpr int kAllGroups = -2;
[[nodiscard]] std::vector<InteractionSummary> summarise(const InteractionLog& log,
                                                        int group = kAllGroups);

// A fixed-width table. `loadAverage` is printed in the header because ADR-170 requires a timing
// claim to state the load it was taken under, and a median here walks with contention.
[[nodiscard]] std::string formatReport(const std::vector<InteractionSummary>& summaries,
                                       double loadAverage, std::string_view title = {});

// One row per record, for a spreadsheet. Stages that did not happen are written as an empty field,
// never as 0.
[[nodiscard]] std::string formatCsv(const InteractionLog& log);

} // namespace avgen::core
