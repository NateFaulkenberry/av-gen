#include "signals/beat_grid.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>

namespace avgen::signals {

using nlohmann::json;

namespace {

// The exponential release: e^(-k u) normalised so it is 1 at u = 0 and exactly 0 at u = 1, so an
// envelope ends where it says it ends instead of leaving a tail under the next event.
constexpr double kExpSharpness = 5.0;

double normalisedExp(double u, double k) {
    const double tail = std::exp(-k);
    return (std::exp(-k * u) - tail) / (1.0 - tail);
}

double smoothstep01(double u) {
    u = std::clamp(u, 0.0, 1.0);
    return u * u * (3.0 - 2.0 * u);
}

Result<double> parseNumber(std::string_view text, std::string_view what) {
    // std::from_chars for double is available in libc++ 17+; trim spaces first.
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    double value = 0.0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return fail("'{}' is not a number in {}", text, what);
    }
    return value;
}

std::optional<EnvelopeCurve> curveFromName(std::string_view name) {
    if (name == "exp") {
        return EnvelopeCurve::Exp;
    }
    if (name == "linear") {
        return EnvelopeCurve::Linear;
    }
    if (name == "smooth") {
        return EnvelopeCurve::Smooth;
    }
    return std::nullopt;
}

bool isDivisionName(std::string_view name) {
    for (const auto& d : BeatGridSource::kDivisions) {
        if (d == name) {
            return true;
        }
    }
    return false;
}

} // namespace

// ============================================================================================
// GridEvent
// ============================================================================================

float GridEvent::evaluate(double t) const {
    // A frame computed as k / fps lands a few ulps either side of a beat it is meant to be on; read
    // it as on the beat, so a peak authored on a frame is never missed by round-off.
    constexpr double kOnBeat = 1e-7;
    if (std::abs(t - time) <= kOnBeat) {
        t = time;
    }
    const double start = time - attack;
    const double fallStart = time + hold;
    const double end = fallStart + release;
    if (t < start || t > end) {
        return 0.0f;
    }
    double v = 1.0;
    if (t < time) {
        v = attack > 0.0 ? smoothstep01((t - start) / attack) : 1.0;
    } else if (t > fallStart) {
        if (!(release > 0.0)) {
            v = 0.0;
        } else {
            const double u = std::clamp((t - fallStart) / release, 0.0, 1.0);
            switch (curve) {
            case EnvelopeCurve::Exp: v = normalisedExp(u, kExpSharpness); break;
            case EnvelopeCurve::Linear: v = 1.0 - u; break;
            case EnvelopeCurve::Smooth: v = 1.0 - smoothstep01(u); break;
            }
        }
    }
    return static_cast<float>(v) * strength;
}

// ============================================================================================
// BeatGrid
// ============================================================================================

BeatGrid::BeatGrid() {
    tempo_ = {TempoSegment{1, 120.0}};
    rebuild();
}

void BeatGrid::setBeatsPerBar(int beats) {
    beatsPerBar_ = std::max(beats, 1);
    rebuild();
}

void BeatGrid::setTempo(std::vector<TempoSegment> tempo) {
    if (tempo.empty()) {
        tempo.push_back(TempoSegment{1, 120.0});
    }
    std::stable_sort(tempo.begin(), tempo.end(),
                     [](const TempoSegment& a, const TempoSegment& b) { return a.bar < b.bar; });
    tempo_ = std::move(tempo);
    rebuild();
}

void BeatGrid::rebuild() {
    // A knot at every tempo change's downbeat. The first tempo also governs everything before its
    // bar (a count-in, a pickup), extrapolated backwards from bar 1 beat 1 = origin.
    knots_.clear();
    const double firstSpb = 60.0 / std::max(tempo_.front().bpm, 1e-3);
    Knot first;
    first.beats = beatsOf(tempo_.front().bar, 1.0);
    first.secondsPerBeat = firstSpb;
    first.seconds = origin_ + first.beats * firstSpb; // the first tempo runs through bar 1
    knots_.push_back(first);
    for (std::size_t i = 1; i < tempo_.size(); ++i) {
        const Knot& prev = knots_.back();
        Knot k;
        k.beats = beatsOf(tempo_[i].bar, 1.0);
        k.seconds = prev.seconds + (k.beats - prev.beats) * prev.secondsPerBeat;
        k.secondsPerBeat = 60.0 / std::max(tempo_[i].bpm, 1e-3);
        knots_.push_back(k);
    }
}

double BeatGrid::secondsAt(double beats) const {
    // The last knot at or before `beats` (the first knot extends backwards).
    std::size_t i = 0;
    while (i + 1 < knots_.size() && knots_[i + 1].beats <= beats) {
        ++i;
    }
    const Knot& k = knots_[i];
    return k.seconds + (beats - k.beats) * k.secondsPerBeat;
}

double BeatGrid::beatsAt(double seconds) const {
    std::size_t i = 0;
    while (i + 1 < knots_.size() && knots_[i + 1].seconds <= seconds) {
        ++i;
    }
    const Knot& k = knots_[i];
    return k.beats + (seconds - k.seconds) / k.secondsPerBeat;
}

Result<double> BeatGrid::parsePosition(std::string_view position) const {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= position.size(); ++i) {
        if (i == position.size() || position[i] == ':') {
            parts.push_back(position.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() != 2 && parts.size() != 3) {
        return fail("position '{}' must be 'bar:beat' or 'section:bar:beat'", position);
    }
    int firstBar = 1;
    if (parts.size() == 3) {
        const auto it = sections_.find(std::string(parts[0]));
        if (it == sections_.end()) {
            return fail("position '{}': unknown section '{}'", position, parts[0]);
        }
        firstBar = it->second;
    }
    auto bar = parseNumber(parts[parts.size() - 2], position);
    if (!bar) {
        return std::unexpected(bar.error());
    }
    auto beat = parseNumber(parts.back(), position);
    if (!beat) {
        return std::unexpected(beat.error());
    }
    if (*bar != std::floor(*bar)) {
        return fail("position '{}': the bar must be a whole number", position);
    }
    const int globalBar = firstBar + static_cast<int>(*bar) - 1;
    return beatsOf(globalBar, *beat);
}

// ============================================================================================
// BeatGridSource
// ============================================================================================

BeatGridSource::BeatGridSource(std::string name)
    : Source(std::move(name)) {}

double BeatGridSource::divisionBeats(std::size_t division) const {
    switch (division) {
    case 0: return static_cast<double>(grid_.beatsPerBar());
    case 1: return 2.0;
    case 2: return 1.0;
    case 3: return 0.5;
    default: return 0.25;
    }
}

void BeatGridSource::attach(SignalBus& bus, params::ParameterSet& params) {
    const std::string base = "grid." + name_ + ".";
    for (std::size_t d = 0; d < kDivisions.size(); ++d) {
        const std::string div(kDivisions[d]);
        divisionIds_[d][0] = bus.declare(base + div);
        divisionIds_[d][1] = bus.declare(base + div + ".phase");
        divisionIds_[d][2] = bus.declare(base + div + ".wave");
    }
    channelIds_.clear();
    for (const std::string& channel : channels_) {
        channelIds_.push_back(bus.declare(base + channel, 0.0f, 4.0f));
    }
    pulseDecay_ = &params.add(params::ParamDesc<float>{.path = parameterPrefix() + "pulseDecay",
                                                       .defaultValue = 0.3f,
                                                       .hardMin = 0.01f,
                                                       .hardMax = 4.0f,
                                                       .softMin = 0.05f,
                                                       .softMax = 1.0f});
}

void BeatGridSource::detach(params::ParameterSet& params) {
    if (pulseDecay_ != nullptr) {
        params.remove(pulseDecay_->path());
        pulseDecay_ = nullptr;
    }
}

float BeatGridSource::pulseShape(double phase, float decay) {
    const double k = 1.0 / std::max(static_cast<double>(decay), 1e-3);
    return static_cast<float>(normalisedExp(std::clamp(phase, 0.0, 1.0), k));
}

float BeatGridSource::channelAt(std::string_view channel, double seconds) const {
    for (std::size_t c = 0; c < channels_.size(); ++c) {
        if (channels_[c] == channel) {
            float v = 0.0f;
            for (const std::size_t e : channelEvents_[c]) {
                v = std::max(v, events_[e].evaluate(seconds));
            }
            return v;
        }
    }
    return 0.0f;
}

void BeatGridSource::sample(SignalBus& bus, const SourceContext& context) const {
    if (pulseDecay_ == nullptr) {
        return; // not attached
    }
    const double t = context.time.renderTime;
    const double beats = grid_.beatsAt(t);
    const float decay = pulseDecay_->value();
    for (std::size_t d = 0; d < kDivisions.size(); ++d) {
        const double len = divisionBeats(d);
        // Nudged by a hair (1e-7 of a division) so an instant computed to land on a division does
        // not read as the very end of the one before it.
        const double x = beats / len + 1e-7;
        const double phase = std::max(x - std::floor(x) - 1e-7, 0.0);
        bus.set(divisionIds_[d][0], pulseShape(phase, decay));
        bus.set(divisionIds_[d][1], static_cast<float>(phase));
        bus.set(divisionIds_[d][2], static_cast<float>(0.5 + 0.5 * std::cos(2.0 * std::numbers::pi * phase)));
    }
    const std::size_t n = std::min(channelIds_.size(), channels_.size());
    for (std::size_t c = 0; c < n; ++c) {
        float v = 0.0f;
        for (const std::size_t e : channelEvents_[c]) {
            v = std::max(v, events_[e].evaluate(t));
        }
        bus.set(channelIds_[c], v);
    }
}

json BeatGridSource::settingsToJson() const {
    return authored_.is_object() ? authored_ : json::object();
}

Result<void> BeatGridSource::settingsFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("beatgrid '{}': settings must be an object", name_);
    }
    BeatGrid grid;
    if (const auto it = j.find("origin"); it != j.end()) {
        if (!it->is_number()) {
            return fail("beatgrid '{}': 'origin' must be a number of seconds", name_);
        }
        grid.setOrigin(it->get<double>());
    }
    if (const auto it = j.find("beatsPerBar"); it != j.end()) {
        if (!it->is_number_integer() || it->get<int>() < 1) {
            return fail("beatgrid '{}': 'beatsPerBar' must be a positive integer", name_);
        }
        grid.setBeatsPerBar(it->get<int>());
    }
    if (const auto it = j.find("tempo"); it != j.end()) {
        if (!it->is_array() || it->empty()) {
            return fail("beatgrid '{}': 'tempo' must be a non-empty array", name_);
        }
        std::vector<TempoSegment> tempo;
        for (const auto& seg : *it) {
            if (!seg.is_object() || !seg.contains("bar") || !seg.contains("bpm") || !seg["bar"].is_number_integer() ||
                !seg["bpm"].is_number() || !(seg["bpm"].get<double>() > 0.0)) {
                return fail("beatgrid '{}': each tempo entry needs an integer 'bar' and a positive 'bpm'", name_);
            }
            tempo.push_back(TempoSegment{seg["bar"].get<int>(), seg["bpm"].get<double>()});
        }
        grid.setTempo(std::move(tempo));
    }
    if (const auto it = j.find("sections"); it != j.end()) {
        if (!it->is_object()) {
            return fail("beatgrid '{}': 'sections' must be an object of name -> first bar", name_);
        }
        for (const auto& [section, bar] : it->items()) {
            if (!bar.is_number_integer()) {
                return fail("beatgrid '{}': section '{}' must name an integer bar", name_, section);
            }
            grid.setSection(section, bar.get<int>());
        }
    }

    std::vector<GridEvent> events;
    std::vector<std::string> channels;
    if (const auto it = j.find("events"); it != j.end()) {
        if (!it->is_array()) {
            return fail("beatgrid '{}': 'events' must be an array", name_);
        }
        std::size_t index = 0;
        for (const auto& e : *it) {
            const auto where = [&] { return fmt::format("beatgrid '{}': events[{}]", name_, index); };
            if (!e.is_object()) {
                return fail("{} must be an object", where());
            }
            const std::string channel = e.value("channel", std::string());
            if (channel.empty() || channel.find('.') != std::string::npos || isDivisionName(channel)) {
                return fail("{}: 'channel' must be a name without dots, and not a division ({})", where(), channel);
            }
            // When: a position on the grid, or plain seconds.
            double beats = 0.0;
            if (const auto at = e.find("at"); at != e.end()) {
                if (!at->is_string()) {
                    return fail("{}: 'at' must be a 'section:bar:beat' string", where());
                }
                auto b = grid.parsePosition(at->get<std::string>());
                if (!b) {
                    return fail("{}: {}", where(), b.error().message);
                }
                beats = *b;
            } else if (const auto time = e.find("time"); time != e.end() && time->is_number()) {
                beats = grid.beatsAt(time->get<double>());
            } else {
                return fail("{}: needs 'at' (a grid position) or 'time' (seconds)", where());
            }
            const std::string units = e.value("units", std::string("beats"));
            if (units != "beats" && units != "seconds") {
                return fail("{}: 'units' must be 'beats' or 'seconds'", where());
            }
            const bool inSeconds = units == "seconds";
            const auto number = [&](const char* key, double& out) -> Result<void> {
                if (const auto v = e.find(key); v != e.end()) {
                    if (!v->is_number() || v->get<double>() < 0.0) {
                        return fail("{}: '{}' must be a number >= 0", where(), key);
                    }
                    out = v->get<double>();
                }
                return {};
            };
            double attack = 0.0;
            double hold = 0.0;
            double release = 1.0;
            if (auto r = number("attack", attack); !r) return r;
            if (auto r = number("hold", hold); !r) return r;
            if (auto r = number("release", release); !r) return r;
            std::optional<double> untilBeats;
            if (const auto until = e.find("until"); until != e.end()) {
                if (!until->is_string()) {
                    return fail("{}: 'until' must be a 'section:bar:beat' string", where());
                }
                auto b = grid.parsePosition(until->get<std::string>());
                if (!b) {
                    return fail("{}: {}", where(), b.error().message);
                }
                if (!(*b >= beats)) {
                    return fail("{}: 'until' comes before 'at'", where());
                }
                untilBeats = *b;
            }
            float strength = 1.0f;
            if (const auto s = e.find("strength"); s != e.end()) {
                if (!s->is_number()) {
                    return fail("{}: 'strength' must be a number", where());
                }
                strength = s->get<float>();
            }
            EnvelopeCurve curve = EnvelopeCurve::Exp;
            if (const auto c = e.find("curve"); c != e.end()) {
                const auto parsed = c->is_string() ? curveFromName(c->get<std::string>()) : std::nullopt;
                if (!parsed) {
                    return fail("{}: 'curve' must be exp, linear or smooth", where());
                }
                curve = *parsed;
            }
            int count = 1;
            double every = 0.0;
            if (const auto rep = e.find("repeat"); rep != e.end()) {
                if (!rep->is_object() || !rep->contains("every") || !rep->contains("count") ||
                    !(*rep)["every"].is_number() || !(*rep)["count"].is_number_integer() ||
                    !((*rep)["every"].get<double>() > 0.0) || (*rep)["count"].get<int>() < 1) {
                    return fail("{}: 'repeat' needs 'every' (beats > 0) and 'count' (>= 1)", where());
                }
                every = (*rep)["every"].get<double>();
                count = (*rep)["count"].get<int>();
            }
            for (int k = 0; k < count; ++k) {
                const double b = beats + every * k;
                GridEvent ev;
                ev.channel = channel;
                ev.time = grid.secondsAt(b);
                ev.strength = strength;
                ev.curve = curve;
                if (inSeconds) {
                    ev.attack = attack;
                    ev.hold = hold;
                    ev.release = release;
                    if (untilBeats) {
                        ev.hold = grid.secondsAt(*untilBeats + every * k) - ev.time;
                    }
                } else {
                    // Durations in beats are measured on the grid, so a release across the tempo
                    // step is as long in beats as it says.
                    ev.attack = ev.time - grid.secondsAt(b - attack);
                    const double holdEnd = untilBeats ? *untilBeats + every * k : b + hold;
                    ev.hold = grid.secondsAt(holdEnd) - ev.time;
                    ev.release = grid.secondsAt(holdEnd + release) - grid.secondsAt(holdEnd);
                }
                events.push_back(std::move(ev));
            }
            if (std::find(channels.begin(), channels.end(), channel) == channels.end()) {
                channels.push_back(channel);
            }
            ++index;
        }
    }

    grid_ = std::move(grid);
    events_ = std::move(events);
    channels_ = std::move(channels);
    channelEvents_.assign(channels_.size(), {});
    for (std::size_t i = 0; i < events_.size(); ++i) {
        const auto c = std::find(channels_.begin(), channels_.end(), events_[i].channel) - channels_.begin();
        channelEvents_[static_cast<std::size_t>(c)].push_back(i);
    }
    authored_ = j;
    return {};
}

std::vector<std::string> BeatGridSource::outputs() const {
    std::vector<std::string> out;
    const std::string base = "grid." + name_ + ".";
    for (const auto& d : kDivisions) {
        out.push_back(base + std::string(d));
        out.push_back(base + std::string(d) + ".phase");
        out.push_back(base + std::string(d) + ".wave");
    }
    for (const std::string& c : channels_) {
        out.push_back(base + c);
    }
    return out;
}

} // namespace avgen::signals
