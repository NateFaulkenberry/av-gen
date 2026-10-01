#pragma once

// The beat grid (ADR-1045): a song's bars and beats as the *author* numbers them, and the authored
// events that sit on them.
//
// The analysis already publishes a beat phase (`SourceContext::beatPhase`, `musicalBeats`), but it is
// the analysis's own grid: it numbers a count-in as bar 1, it can drift a few milliseconds, and it has
// no idea that "verse 2, bar 4, beat 2.5" is a word. A music video is directed against the composer's
// numbering, so this source carries an explicit tempo map and an explicit section table and turns
// positions written the way the owner writes them ("bridge2:4:2.5") into seconds.
//
// Everything it publishes is a pure function of the clock (`pureInTime`), so a seek lands exactly
// where a play does and an offline render is bit-identical (ADR-091, ADR-901). There is no "has this
// fired yet": an event is an envelope over time, evaluated, never triggered.
//
// ## Outputs (source name `song`)
//
// Pulses, one family per division (bar = 4 beats in 4/4, half = 2, quarter = 1, eighth = 1/2,
// sixteenth = 1/4):
//   grid.song.<div>         1 on the division, decaying to exactly 0 just before the next
//                           (shape: `sources/song/pulseDecay`, a fraction of the division)
//   grid.song.<div>.phase   0..1 through the division (a saw)
//   grid.song.<div>.wave    0.5 + 0.5 cos(2 pi phase): 1 on the division, 0 between; smooth, for
//                           camera breathing and bobbing
// Events, one output per channel named by the events:
//   grid.song.<channel>     the strongest envelope of that channel's events at this instant
//
// ## Settings (JSON, not parameters)
//
//   "origin":      seconds of bar 1 beat 1
//   "beatsPerBar": 4
//   "tempo":       [{"bar": 1, "bpm": 109}, {"bar": 75, "bpm": 111}]  steps at a bar's downbeat
//   "sections":    {"verse1": 25, ...}  the global bar of each section's own bar 1
//   "events":      [{"at": "verse1:4:4", "channel": "clap", "strength": 1,
//                    "attack": 0, "hold": 0, "release": 1, "curve": "exp",
//                    "until": "verse1:5:1", "units": "beats",
//                    "repeat": {"every": 4, "count": 8}}]
//
// A position is "bar:beat" or "section:bar:beat"; the beat is 1-based and fractional ("4.5" is the
// "and" of 4), and a bar may be 0 or negative (the bar before a section's first: "bridge2:0:4.5").
// `attack` rises (smoothstep) into the event's instant, so an attack anticipates the beat and the
// peak lands on it; `hold` keeps the peak; `release` falls with `curve` (exp, linear, smooth).
// `until`, when given, replaces `hold` with the span to that position. Durations are in beats unless
// `units` is "seconds". `repeat` expands one event into `count` copies `every` beats apart.

#include "signals/source.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::signals {

enum class EnvelopeCurve : std::uint8_t { Exp, Linear, Smooth };

struct TempoSegment {
    int bar = 1;          // the global bar whose downbeat starts this tempo
    double bpm = 120.0;
};

// One authored event, resolved to seconds.
struct GridEvent {
    std::string channel;
    double time = 0.0;    // seconds of the peak
    double attack = 0.0;  // seconds before `time` over which it rises
    double hold = 0.0;    // seconds at the peak after `time`
    double release = 0.0; // seconds of the fall after the hold
    float strength = 1.0f;
    EnvelopeCurve curve = EnvelopeCurve::Exp;

    // The envelope at `t` (0 outside [time - attack, time + hold + release]).
    [[nodiscard]] float evaluate(double t) const;
};

// The tempo map and section table, without the bus. Exposed for tests and the guide.
class BeatGrid {
public:
    BeatGrid();
    [[nodiscard]] double origin() const { return origin_; }
    [[nodiscard]] int beatsPerBar() const { return beatsPerBar_; }
    [[nodiscard]] const std::vector<TempoSegment>& tempo() const { return tempo_; }
    [[nodiscard]] const std::map<std::string, int>& sections() const { return sections_; }

    void setOrigin(double seconds) { origin_ = seconds; rebuild(); }
    void setBeatsPerBar(int beats);
    void setTempo(std::vector<TempoSegment> tempo);
    void setSection(const std::string& name, int globalBar) { sections_[name] = globalBar; }

    // Beats from bar 1 beat 1 (0 there, negative before) <-> seconds of the piece.
    [[nodiscard]] double secondsAt(double beats) const;
    [[nodiscard]] double beatsAt(double seconds) const;
    // The beat count of a global bar and a 1-based fractional beat.
    [[nodiscard]] double beatsOf(int globalBar, double beat) const {
        return static_cast<double>(globalBar - 1) * beatsPerBar_ + (beat - 1.0);
    }
    // "bar:beat" or "section:bar:beat" -> beats from bar 1 beat 1. Fails on an unknown section.
    [[nodiscard]] Result<double> parsePosition(std::string_view position) const;

private:
    void rebuild();
    struct Knot {
        double beats = 0.0;
        double seconds = 0.0;
        double secondsPerBeat = 0.5;
    };
    double origin_ = 0.0;
    int beatsPerBar_ = 4;
    std::vector<TempoSegment> tempo_;
    std::map<std::string, int> sections_;
    std::vector<Knot> knots_; // one per tempo segment, at its downbeat
};

class BeatGridSource final : public Source {
public:
    // The divisions a grid publishes pulses for, in beats (4/4: a bar is 4).
    static constexpr std::array<std::string_view, 5> kDivisions{"bar", "half", "quarter", "eighth", "sixteenth"};

    explicit BeatGridSource(std::string name);
    [[nodiscard]] std::string kind() const override { return "beatgrid"; }
    void attach(SignalBus& bus, params::ParameterSet& params) override;
    void detach(params::ParameterSet& params) override;
    void update(SignalBus& bus, const SourceContext& context) override { sample(bus, context); }
    void reset() override {}
    [[nodiscard]] nlohmann::json settingsToJson() const override;
    Result<void> settingsFromJson(const nlohmann::json& j) override;
    [[nodiscard]] std::vector<std::string> outputs() const override;
    [[nodiscard]] bool pureInTime() const override { return true; }
    void sample(SignalBus& bus, const SourceContext& context) const override;

    [[nodiscard]] const BeatGrid& grid() const { return grid_; }
    [[nodiscard]] const std::vector<GridEvent>& events() const { return events_; }
    [[nodiscard]] const std::vector<std::string>& channels() const { return channels_; }
    // The value a channel / a division's pulse has at `seconds` (tests, the guide's checks).
    [[nodiscard]] float channelAt(std::string_view channel, double seconds) const;
    [[nodiscard]] static float pulseShape(double phase, float decay);
    [[nodiscard]] double divisionBeats(std::size_t division) const;

private:
    BeatGrid grid_;
    nlohmann::json authored_; // the settings as written, for a round trip
    std::vector<GridEvent> events_;
    std::vector<std::string> channels_;
    std::vector<std::vector<std::size_t>> channelEvents_; // per channel, indices into events_
    std::vector<SignalId> channelIds_;
    std::array<std::array<SignalId, 3>, kDivisions.size()> divisionIds_{};
    params::Parameter<float>* pulseDecay_ = nullptr;
};

} // namespace avgen::signals
