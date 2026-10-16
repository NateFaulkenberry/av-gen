#pragma once

// The resonance field (ADR-1400): a shared, audio-excited modal field every field consumer can read.
//
//     D(x, t) = sum_i a_i(t) cos(Omega_i t + psi_i) phi_i(x)
//
// A *resonator* is a FieldSpec of kind Resonance with no `reference`. It owns N modes. Mode i sits at
// u_i = i / (N - 1) on two log axes -- that is the field's dispersion relation:
//     f_i      = fLow (fHigh / fLow)^u_i                (the frequency it hears)
//     lambda_i = lambdaMax (lambdaMin / lambdaMax)^u_i  (the wavelength it draws), k_i = 2 pi scale / lambda_i
// so a low note draws a broad, simple figure and a high note a fine one.
//
// Each mode belongs to one source (ResonanceSource): a `point` source draws a standing wave about its
// centre, R_m(k r) cos(m (alpha - theta_i)) with R_m the asymptotic Bessel form given a regular core and r
// the 3D distance (rings on the ground, nodal shells around the centre); a `lattice` source draws a
// Faraday-wave pattern, n^-1/2 sum_j cos(k x.d_j) over n directions half a turn apart.
//
// The envelope a_i(t) is a PURE FUNCTION of the audio history at the clock (no state is carried from
// frame to frame, so a seek lands exactly where a play does, a reset is free, and offline and live
// agree): the spectrogram, filtered through each mode's resonance curve G(f) = 1 / (1 + Q^2 (f/f_i -
// f_i/f)^2) and sharpened by a spectral contrast, is convolved with a unit-area rise-and-decay kernel
// K_i(tau) = (e^{-tau/td_i} - e^{-tau/ta_i}) / (td_i - ta_i); onsets add a struck-plate impulse into the
// modes their band reaches. td_i = persistence (fLow / f_i)^0.35 (low modes ring longest), ta_i =
// buildup td_i. A soft limiter on the total energy makes dense music ring more modes, not louder ones.
// `idle` keeps the lowest modes breathing in silence.
//
// Readers (any Resonance field; a field whose `reference` names a resonator shares its modes) ask
// one channel:
//     displacement  D
//     amplitude     |F|, F = sum e_i phi_i -- the standing figure (the modes in their resting phase), E = F^2
//     sand          sqrt(Ebar) exp(-E / (s^2 Ebar)), Ebar = sum e_i^2 <phi_i^2> -- where Chladni sand
//                   gathers: the nodal LINES of the figure, F = 0 (s = sharpness). Sand reads the SLOW
//                   envelope (`memory`): it settles over a second or two instead of jumping with every hit.
//     force         -grad F_slow^2 (vector): towards the nodes of the slow figure, the radiation force on a grain
//     gradient      grad D (vector)
// optionally through a listening window over u (a tuned structure hears only some modes; laid along
// the field's axis, each part of it hears its own band).
//
// shaders/fields.wgsl mirrors resonanceModeShape / resonanceSum / the channels operation for
// operation; tests/rendering/test_resonance_gpu.cpp holds them together.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace avgen::spatial {

class AudioHistory;
struct FieldSpec;
struct FieldSet;

inline constexpr int kMaxResonanceModes = 24;     // per resonator
inline constexpr int kMaxResonanceRipples = 12;   // ADR-1403/1404: travelling packets per resonator, after its modes
inline constexpr int kMaxRipplesPerBand = 3;      // ADR-1404: the most rings a band keeps alive at once
inline constexpr int kMaxResonanceRows = kMaxResonanceModes + kMaxResonanceRipples;
// ADR-1406: a row whose every drive (d, e, s) is below this adds nothing visible; samplers skip it (fields.wgsl
// RES_SILENT, the same rule), so a mode out of its focus or a faded ring costs no shape evaluation
inline constexpr float kResonanceSilent = 1e-5f;
inline constexpr int kMaxResonanceSources = 4;    // per resonator
inline constexpr int kMaxGpuResonanceModes = 48;  // the FieldBlock's mode table, every resonator together
inline constexpr int kResonanceModeVec4s = 3;     // shape, place, drive

// Relief (appended): sand(sharpness) + reliefLine x sand(reliefLineSharpness) + reliefVibration x D in ONE pass over
// the modes -- a ground's whole displacement for the price of one deformer (and one finite-difference normal).
// Shading (appended): a COLOUR field, rgb = (sand, sand at reliefLineSharpness, amplitude): what a surface's
// material needs, read by one `field` op in one pass over the modes.
enum class ResonanceChannel : std::uint8_t { Displacement, Amplitude, Sand, Force, Gradient, Relief, Shading };
[[nodiscard]] const char* resonanceChannelName(ResonanceChannel channel);
[[nodiscard]] std::optional<ResonanceChannel> resonanceChannelFromName(std::string_view name);
[[nodiscard]] inline bool resonanceChannelIsVector(ResonanceChannel c) {
    return c == ResonanceChannel::Force || c == ResonanceChannel::Gradient;
}

enum class ResonanceSourceKind : std::uint8_t { Point, Lattice };
[[nodiscard]] const char* resonanceSourceKindName(ResonanceSourceKind kind);
[[nodiscard]] std::optional<ResonanceSourceKind> resonanceSourceKindFromName(std::string_view name);

struct ResonanceSource {
    ResonanceSourceKind kind = ResonanceSourceKind::Point;
    glm::vec3 position{0.0f}; // in the field's frame
    int order = 4;            // point: the highest angular order m; lattice: the number of directions (1..8)
    float bandLow = 0.0f;     // the modes it owns: u in [bandLow, bandHigh]
    float bandHigh = 1.0f;
    float reach = 0.0f;       // metres: amplitude 1 / (1 + (r / reach)^2); 0 = unbounded
    float gain = 1.0f;
    float depth = 0.0f;       // whole metres: amplitude e^{-|y - y0| / depth} away from its height (a surface wave's
                              // layer); 0 = none
    float drive = 0.0f;       // ADR-1402: excitation added straight into its modes (both envelopes): what a route --
                              // a coupling -- uses to make one body ring because another does
    float scale = 1.0f;       // multiplies its modes' wavenumbers: a smaller body draws finer figures at the same pitch
    // ADR-1403, kinematics: the figure itself moves, as a pure function of the clock (and, through driftAudio, of the
    // memory envelope) -- so a seek still lands where a play does. Each mode wanders its own way: its centre on a
    // Lissajous of radius `drift` (metres) and period about `driftPeriod` (seconds; finer modes a little faster, each
    // with its own phase), and a point mode's petals turn at `spin` rad/s times a per-mode factor in [-1.3, 1.3] (some
    // against the others); a lattice turns as one at `spin`. Modes sliding across each other move the nodal lines:
    // ridges migrate, split and reconnect. `driftAudio` widens a mode's wander by its memory envelope (1 + driftAudio s).
    float drift = 0.0f;
    float driftPeriod = 60.0f;
    // ADR-1403: the source's amplitude AFTER competition and the limiter (gain acts before them, so at saturation the
    // limiter cancels it): what sets a family's height against the others' -- broad modes large, fine modes small.
    float amplitude = 1.0f;
    // ADR-1403: multiplies the resonator's `breath` for this source's modes -- a broad swell can morph slowly while a
    // fine carpet shivers (0 = its modes hold their phase: they move only by their kinematics and their envelopes).
    float breath = 1.0f;
    float spin = 0.0f;
    float driftAudio = 0.0f;
    // ADR-1405: the source's modes' orders, in their order along u (cycled): a point mode's angular order m (0..8), a
    // lattice mode's number of directions (1 bands, 2 squares, 3 hexagons, 1..8). Empty = ADR-1400 (a seeded order up
    // to `order`, growing with u). A designed bank -- rings, then 4 lobes, 6, 3, ... -- instead of a hashed one.
    std::vector<int> orders;
    // ADR-1405, focus: which of the source's modes lead. A Gaussian window over u, e^{-((u - f) / focusWidth)^2}, on
    // the modes' weights (both envelopes, so every reader -- geometry and colour -- sees the same figure), at
    // f = focus + focusSweep sin(2 pi t / focusPeriod + phase) clamped to the source's band. One or two adjacent modes
    // lead at a time and the figure crossfades from one designed mode to the next as f moves (a route moves `focus`
    // with the music; the sweep keeps a steady passage evolving). focusFloor keeps the others faintly present.
    // focus < 0 = off (every mode as ADR-1400).
    float focus = -1.0f;
    float focusWidth = 0.06f;
    float focusSweep = 0.0f;
    float focusPeriod = 60.0f;
    float focusFloor = 0.0f;
    [[nodiscard]] bool operator==(const ResonanceSource&) const = default;
};

struct ResonanceSpec {
    // ---- the resonator (read only from a field with no `reference`) ------------------------------
    float modes = 12.0f;        // 1..kMaxResonanceModes; a fraction fades the last mode in
    // ADR-1404: the modes' fixed places on the dispersion axis, u_i = i / (layout - 1). 0 = `modes` sets them (ADR-1400:
    // then a route on `modes` slides EVERY mode along the axis -- frequencies, wavelengths, sources and phases all
    // shift during play). With a layout, `modes` only says how many of the fixed modes ring, from the lowest up.
    float layout = 0.0f;
    // ADR-1404: response times per band (low, mid, high: u = 0, 0.5, 1, log-interpolated between), seconds. attack is
    // a mode's rise -- of its spectral drive and of an onset's strike (a bounded two-time-constant impulse, never a
    // step); release its ring-down. All three 0 = the ADR-1400 rule (persistence, buildup).
    std::array<float, 3> attack{0.0f, 0.0f, 0.0f};
    std::array<float, 3> release{0.0f, 0.0f, 0.0f};
    float fLow = 40.0f;         // Hz, the lowest mode's eigenfrequency
    float fHigh = 6000.0f;      // Hz, the highest's
    float lambdaMax = 40.0f;    // metres, the lowest mode's wavelength
    float lambdaMin = 3.0f;     // metres, the highest's
    float scale = 1.0f;         // spatial scale: multiplies every wavenumber
    float selectivity = 3.0f;   // Q of each mode's resonance curve
    float contrast = 0.5f;      // 0..0.95: how much of each row's mean level is subtracted (spectral contrast)
    float persistence = 2.5f;   // seconds the lowest mode takes to ring down by 1/e
    float buildup = 0.25f;      // a mode's rise time as a fraction of its ring-down
    float memory = 1.5f;        // seconds: the slow envelope the sand channel reads rises over memory / 2 and
                                // rings down over the mode's ring-down plus this (where sand settles, not where it jumps)
    float competition = 1.0f;   // mode competition within each source: a_i (a_i / a_max)^competition (0 = off)
    float sustain = 1.0f;       // gain of the spectral drive
    float transient = 0.8f;     // gain of the onsets' strikes
    float idle = 0.12f;         // the lowest modes' level in silence
    float breath = 0.2f;        // Hz: the lowest mode's visible oscillation (higher modes a little faster)
    float limit = 1.0f;         // the soft limit of sqrt(sum a_i^2)
    // ADR-1403, ripples: every heard onset launches a travelling ring -- a wave packet cos(k (rho - R)) e^{-((rho - R) /
    // w)^2} / sqrt(1 + rho / lambda) whose radius R = speed x age grows from a point the onset's time hashes to within
    // `rippleArea` metres of the field's origin (horizontal distance rho). A low onset (kick) draws a broad, slow, long
    // ring; a mid one (snare) a sharper, faster, shorter, more local one; a high one (hat) a fine, faint one. Amplitude
    // `ripple` x heard strength x e^{-age / life}; the newest `rippleCount` (at most kMaxResonanceRipples) still alive.
    // Pure functions of the onset list and the clock: exact under seek. ripple 0 = none.
    float ripple = 0.0f;
    float rippleSpeed = 9.0f;   // m/s, the kick's ring (a snare's runs 1.6x, a hat's 2.2x)
    float rippleLife = 1.6f;    // seconds to 1/e, the kick's (a snare's 0.55x, a hat's 0.35x)
    float rippleWidth = 0.9f;   // the packet's half-width, in its wavelengths
    float rippleArea = 30.0f;   // metres: launch points lie within this of the origin
    float rippleCount = 2.0f;   // ADR-1404: the most rings each band keeps alive (0..kMaxRipplesPerBand); a ring a newer
                                // one displaces fades out over rippleFade instead of vanishing
    float rippleFade = 0.35f;   // seconds
    // ADR-1405: where each band's ring (low, mid, high) sits on the dispersion axis -- its wavelength, and which readers
    // hear it (a reader's listening window): a kick's ring can travel through the medium scale, leaving the large figure
    // to answer the kick through its strikes
    std::array<float, 3> rippleU{0.1f, 0.45f, 0.85f};
    std::vector<ResonanceSource> sources; // empty = one point source at the origin, order 4
    // ---- the reader (any resonance field) --------------------------------------------------------
    ResonanceChannel channel = ResonanceChannel::Displacement;
    float listenLow = 0.0f;     // the modes heard: u in [listenLow, listenHigh] ...
    float listenHigh = 1.0f;
    float listenWidth = 0.08f;  // ... with soft edges this wide (along the axis: the Gaussian's width)
    bool listenAlongAxis = false; // the window's centre runs listenLow -> listenHigh along `axis` over `length`
    float sharpness = 0.35f;    // sand: the nodal line's width relative to the mean energy
    // ADR-1405: the sand channel without its sqrt(Ebar) factor -- the nodal network as a 0..1 mask whatever the
    // loudness (a material that marks the figure in a quiet passage too; the excitation is the amplitude channel's)
    bool sandNormalized = false;
    float reliefLine = 0.0f;           // relief: the crest's weight (a second, narrower sand line) ...
    float reliefLineSharpness = 0.3f;  // ... and its width
    float reliefVibration = 0.0f;      // relief: the vibration's weight (D)
    // ADR-1405, the relief's other terms (on the SLOW figure F = sum s_i phi_i, the standing pattern): the base sand
    // ridge's weight (1 = ADR-1400), the signed figure's (F: lobes up, opposite lobes down) and its magnitude's (|F|:
    // with a negative weight the antinodes sink into basins, so the nodal lines stand as ridges between them).
    float reliefSand = 1.0f;
    float reliefFigure = 0.0f;
    float reliefAmplitude = 0.0f;
    // ... and how round the magnitude's crease is: |F| becomes sqrt(F^2 + (c sigma)^2) - c sigma, sigma = sqrt(Ebar) the
    // figure's local rms. 0 = the exact |F|, whose cusp on every nodal line is a crest no mesh can carry (a sawtooth).
    float reliefRound = 0.0f;
    // ADR-1402: a reader may publish its value at one world point to the signal bus every frame, as
    // `field.<name>.probe` (the coupling hook: a route can let one part of the field drive another).
    std::optional<glm::vec3> probe;
    float derivativeStep = 0.0f;// metres, the finite-difference step of the vector channels (0 = lambdaMin / 24)

    [[nodiscard]] Result<void> validate(std::string_view field) const;
    [[nodiscard]] std::uint64_t structuralHash() const; // every member
    [[nodiscard]] nlohmann::json toJson() const;
    // Reads the members present in `j` (the field's `resonance` block) over the defaults.
    static Result<ResonanceSpec> fromJson(const nlohmann::json& j);
};

// One mode, as packed for the GPU (three vec4 a mode in the FieldBlock's table).
struct ResonanceMode {
    glm::vec4 shape{0.0f}; // k (rad/m), m (angular order / lattice directions), theta (rad), geometry + 4 x depth
                           // (geometry 0 point, 1 lattice, 2 ripple; depth whole metres, 0 = no vertical decay).
                           // A ripple (ADR-1403): k, the packet's half-width (m), its radius now (m), 2.
    glm::vec4 place{0.0f}; // centre (field frame), reach (0 = unbounded)
    glm::vec4 drive{0.0f}; // d = a cos(Omega t + psi) (displacement), e = a (envelope), u, s (the slow envelope, the
                           // sand's memory); d, e and s include the mode's weight (fade-in x source gain)
};

// What a resolver derives for one resonator at one clock: its modes, ready to sample.
struct ResonanceState {
    int count = 0;       // rows to sample: the modes, then the ripples
    int modeCount = 0;   // the modes (the arrays below are per mode)
    std::array<ResonanceMode, kMaxResonanceRows> modes{};
    std::array<float, kMaxResonanceRows> frequency{}; // Hz, per mode (diagnostics and tests)
    std::array<float, kMaxResonanceRows> envelope{};  // a_i before competition and the limiter (diagnostics, tests)
    std::array<float, kMaxResonanceRows> memory{};    // the slow envelope, likewise
    std::array<float, kMaxResonanceRows> weight{};    // fade-in x source gain
    std::array<int, kMaxResonanceRows> source{};      // the source each mode belongs to
    double clock = 0.0;
    float limiterGain = 1.0f;
    float lambdaMin = 3.0f; // the shortest wavelength drawn (the vector channels' default step)
};

// The spectrogram rows a resonator hears, filtered through the resonance curve and sharpened, kept
// between frames so a frame filters only the rows it has not seen. A cache, never state: dropping it
// changes nothing but the cost.
class ResonanceCache {
public:
    // Filtered rows [lo, hi] of `audio` for (selectivity, contrast); row r is at index r - lo. Rows the
    // history does not hold read 0.
    const std::deque<std::array<float, 64>>& rows(const std::string& resonator, const std::shared_ptr<const AudioHistory>& audio,
                                                  std::int64_t lo, std::int64_t hi, float selectivity, float contrast);
    [[nodiscard]] std::uint64_t rowsFiltered() const { return filtered_; } // total, for tests
    void clear() { entries_.clear(); }

private:
    struct Entry {
        std::weak_ptr<const AudioHistory> audio;
        std::uint64_t revision = 0;
        float selectivity = 0.0f;
        float contrast = 0.0f;
        std::int64_t lo = 0; // rows_[0] is row lo
        std::deque<std::array<float, 64>> rows;
    };
    std::unordered_map<std::string, Entry> entries_;
    std::uint64_t filtered_ = 0;
};

// The resonator `field` at `clock` (seconds on the history's clock). `audio` may be null (silence:
// only `idle` sounds). `cache` may be null (every row is filtered afresh).
[[nodiscard]] ResonanceState evaluateResonance(const FieldSpec& field, const std::shared_ptr<const AudioHistory>& audio,
                                               double clock, ResonanceCache* cache = nullptr);

// Every resonator of `fields` at transport second `time`: writes FieldSpec::resonanceState (runtime
// only). Readers (a `reference`) are pointed at their resonator's state. Called once a frame after the
// parameters are applied (Composition, beside resolveFieldTriggers).
void resolveResonance(FieldSet& fields, ResonanceCache& cache, double time);

// ADR-1402: every resonance field with a `probe`: its signal name (`field.<name>.probe`) and its scalar value at the
// probe point at `time` (0 when disabled or unresolved). Called once a frame after `resolveResonance`.
struct ResonanceProbe {
    std::string signal;
    float value = 0.0f;
};
[[nodiscard]] std::vector<ResonanceProbe> resonanceProbes(const FieldSet& fields, double time);

// The state a resonance field reads: its own, or its resonator's. Null when there is none.
[[nodiscard]] const ResonanceState* resonanceStateOf(const FieldSpec& field, const FieldSet* set);

// ---- sampling (field frame q); shaders/fields.wgsl mirrors these ---------------------------------

// phi_i(q); `env2` (optional) receives the mode's local mean square, <phi^2> over a cycle.
[[nodiscard]] float resonanceModeShape(const ResonanceMode& mode, const glm::vec3& q, float* env2 = nullptr);
// The listening weight of a mode at u for a reader at q.
[[nodiscard]] float resonanceListen(const FieldSpec& reader, float u, const glm::vec3& q);
struct ResonanceSums {
    float displacement = 0.0f; // D
    float energy = 0.0f;       // E
    float meanEnergy = 0.0f;   // Ebar
    float fast = 0.0f;         // the fast figure (sum e_i phi_i), whatever `memory` chose for `energy`
    float figure = 0.0f;       // ADR-1405: the figure sum e_i phi_i itself (signed), from the envelope `memory` chose
};
// The shading channel's rgb at q.
[[nodiscard]] glm::vec3 resonanceShading(const ResonanceState& state, const FieldSpec& reader, const glm::vec3& q);
// `memory`: E and Ebar from the slow envelope (the sand channel) instead of the fast one.
[[nodiscard]] ResonanceSums resonanceSums(const ResonanceState& state, const FieldSpec& reader, const glm::vec3& q,
                                         bool memory = false);
// The reader's scalar channel at q (displacement, amplitude, sand; the vector channels' length).
[[nodiscard]] float resonanceScalar(const ResonanceState& state, const FieldSpec& reader, const glm::vec3& q);
// The reader's vector channel at q, in the field frame (force, gradient; a scalar channel gives 0).
[[nodiscard]] glm::vec3 resonanceVector(const ResonanceState& state, const FieldSpec& reader, const glm::vec3& q);

} // namespace avgen::spatial
