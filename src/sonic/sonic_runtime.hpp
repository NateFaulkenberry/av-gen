#pragma once

// The Sonic subsystem on the signal bus (ADR-1020): Sonic Character (`sonic.*`), raw timbre (`timbre.*`) and
// Musical Context (`notes.*`).
//
// Shaped like `app::MusicRuntime` and living beside it in `SignalClock`, for the same reasons:
//   - the character advances on the ANALYSIS clock (one step per analysis frame, dt = the hop), so a 30 fps render
//     and a 120 fps window agree; it walks the whole-track analysis by time, in both engine modes, which makes it a
//     pure function of the frame sequence;
//   - its state is a few hundred bytes of floats, copied with the rest of `SignalClock` into a seek checkpoint;
//   - the musical context is a pure function of time, evaluated at the render frame's instant.
//
// What it reads is a `SonicSetup`: the character spec, the note track and the timbre track, built once per load
// and shared read-only. With no setup (a project without a `sonic` block) it publishes zeros and does no work.

#include "analysis/analysis_track.hpp"
#include "signals/signal_bus.hpp"
#include "sonic/character.hpp"
#include "sonic/notes.hpp"
#include "sonic/timbre.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

namespace avgen::sonic {

// Divisors that bring the musical context to 0..1 on the bus (the context itself is in musical units).
struct ContextScale {
    float polyphony = 8.0f;   // notes sounding
    float density = 12.0f;    // notes per second
    float rhythm = 8.0f;      // onsets per second
    float lowPitch = 24.0f;   // notes.pitch 0 at C1 ...
    float highPitch = 108.0f; // ... 1 at C8
    float range = 36.0f;      // semitones
    float motion = 12.0f;     // semitones per step
    float shortNote = 0.05f;  // notes.duration 0 (log scale) ...
    float longNote = 4.0f;    // ... 1, seconds
};

struct SonicSetup {
    CharacterSpec character = CharacterSpec::defaults();
    TimbreConfig timbreConfig;
    ContextSettings context;
    ContextScale scale;
    NoteTrack notes;
    std::filesystem::path notesPath;      // as resolved; empty = no notes
    std::vector<TimbreFeatures> timbre;   // parallel to the analysis track's frames; empty until analysed
    std::uint64_t trackFrames = 0;        // the frame count `timbre` was computed for
    double timbreMillis = 0.0;            // what the pass cost (the performance report)
    nlohmann::json document;              // the project's `sonic` block as loaded (saved back as is)

    // Parses a project's `sonic` block. `baseDir` resolves a relative `notes` path. Loads the notes; does not
    // analyse timbre (that needs the track: `analyse`).
    [[nodiscard]] static Result<SonicSetup> fromJson(const nlohmann::json& j, const std::filesystem::path& baseDir);
    // Runs the timbre pass over `track` (a no-op when already done for a track of this length).
    void analyse(const analysis::AnalysisTrack& track);
    // A hash of everything that decides what the runtime publishes (a seek replay's input key).
    [[nodiscard]] std::uint64_t key() const;
};

// The signal ids, declared in a fixed order so every bus that carries them gives each the same id.
struct SonicSignals {
    std::array<signals::SignalId, kDimensionCount> medium{};
    std::array<signals::SignalId, kDimensionCount> slow{};
    signals::SignalId transient = signals::kInvalidSignal;
    // timbre.*
    signals::SignalId loudness = signals::kInvalidSignal, pitch = signals::kInvalidSignal,
                      pitchConfidence = signals::kInvalidSignal, harmonicity = signals::kInvalidSignal,
                      inharmonicity = signals::kInvalidSignal, tonalness = signals::kInvalidSignal,
                      flatness = signals::kInvalidSignal, dissonance = signals::kInvalidSignal,
                      rolloff = signals::kInvalidSignal, bandwidth = signals::kInvalidSignal,
                      rawTransient = signals::kInvalidSignal, levelSlope = signals::kInvalidSignal;
    // notes.*
    signals::SignalId noteOn = signals::kInvalidSignal, noteOff = signals::kInvalidSignal,
                      phraseStart = signals::kInvalidSignal, active = signals::kInvalidSignal,
                      polyphony = signals::kInvalidSignal, density = signals::kInvalidSignal,
                      rhythm = signals::kInvalidSignal, velocity = signals::kInvalidSignal,
                      notePitch = signals::kInvalidSignal, range = signals::kInvalidSignal,
                      motion = signals::kInvalidSignal, direction = signals::kInvalidSignal,
                      duration = signals::kInvalidSignal, legato = signals::kInvalidSignal,
                      regularity = signals::kInvalidSignal, chord = signals::kInvalidSignal,
                      tension = signals::kInvalidSignal, repetition = signals::kInvalidSignal,
                      phrase = signals::kInvalidSignal;
};

class SonicRuntime {
public:
    // Declares sonic.*, timbre.* and notes.* (idempotent, like SignalBus::declare).
    void declare(signals::SignalBus& bus);

    // Consumes every analysis frame of `track` whose centre is at or before `seconds`. Going back in time starts
    // again from the first frame (cheap: the timbre is precomputed), so the state is always the one a play from
    // zero reaches.
    void advance(const SonicSetup& setup, const analysis::AnalysisTrack& track, double seconds);

    // Writes this render frame's values: the character, the timbre, the context at `seconds`, and the events since
    // the last publish. `setup` null publishes zeros (once) and nothing else.
    void publish(const SonicSetup* setup, signals::SignalBus& bus, double seconds);

    void reset();

    // ---- read-outs (the diagnostic view, tests) ----
    [[nodiscard]] const std::array<float, kDimensionCount>& medium() const { return medium_; }
    [[nodiscard]] const std::array<float, kDimensionCount>& slow() const { return slow_; }
    [[nodiscard]] const std::array<float, kDimensionCount>& instant() const { return instant_; }
    [[nodiscard]] const TimbreFeatures& timbre() const { return latest_; }
    [[nodiscard]] const std::array<float, kFeatureCount>& features() const { return features_; }
    [[nodiscard]] const MusicalContext& context() const { return context_; }
    [[nodiscard]] std::size_t consumed() const { return cursor_; }
    [[nodiscard]] const SonicSignals& ids() const { return ids_; }

    // One analysis step (exposed for tests): the features of one frame, dt seconds after the previous one.
    void step(const SonicSetup& setup, const TimbreFeatures& features, double dt);

private:
    SonicSignals ids_;
    // Character state.
    std::array<float, kDimensionCount> medium_{};
    std::array<float, kDimensionCount> slow_{};
    std::array<float, kDimensionCount> instant_{};
    std::array<float, kFeatureCount> features_{};
    TimbreFeatures latest_;
    bool primed_ = false;
    std::array<double, kDimensionCount> openSeconds_{}; // sound seen by each dimension (the tiers' warm-up)
    // The level follower (transient, slope) and the running statistics (stability, movement).
    float followerDb_ = -120.0f;
    float slopeDb_ = 0.0f;
    double logCentroidMean_ = 0.0;
    double logCentroidSquare_ = 0.0;
    float fluxAverage_ = 0.0f;
    bool statsPrimed_ = false;
    // Events.
    bool transientPending_ = false;
    float transientStrength_ = 0.0f;
    bool transientArmed_ = true;
    double sinceTransient_ = 1e9;
    // Where the walk is.
    std::size_t cursor_ = 0;
    double lastSeconds_ = -1.0;
    double lastPublish_ = std::numeric_limits<double>::quiet_NaN();
    bool zeroed_ = false;
    MusicalContext context_;
};

} // namespace avgen::sonic
