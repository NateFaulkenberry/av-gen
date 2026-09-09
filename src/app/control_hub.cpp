#include "app/control_hub.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"

#include <algorithm>

namespace avgen::app {

ControlHub::ControlHub() = default;
ControlHub::~ControlHub() { closeAll(); }

void ControlHub::setMap(control::ControlMap map) {
    map_ = std::move(map);
    applyIo();
}

void ControlHub::applyIo() {
    // OSC
    if (map_.oscEnabled) {
        if (!osc_.isOpen() || osc_.port() != map_.oscPort) {
            osc_.close();
            if (auto r = osc_.open(map_.oscPort, map_.oscBind); !r) {
                oscError_ = r.error().message;
                log::warn("osc: {}", oscError_);
            } else {
                oscError_.clear();
                log::info("osc: listening on {}:{} (prefix '{}')", map_.oscBind, osc_.port(), map_.oscPrefix);
            }
        }
    } else {
        osc_.close();
        oscError_.clear();
    }
    // MIDI
    if (map_.midiEnabled && control::hasMidiBackend()) {
        midi_.close();
        if (auto r = midi_.open(map_.midiFilter); !r) {
            midiError_ = r.error().message;
            log::warn("midi: {}", midiError_);
        } else {
            midiError_.clear();
            log::info("midi: {} source(s) connected", midi_.connectedSources().size());
        }
    } else {
        midi_.close();
        midiError_ = map_.midiEnabled ? "no MIDI backend on this platform" : "";
    }
    ioApplied_ = true;
}

void ControlHub::closeAll() {
    osc_.close();
    midi_.close();
}

void ControlHub::injectMidi(std::span<const std::uint8_t> bytes, const std::string& source) {
    midi_.inject(bytes, source);
}

void ControlHub::injectOsc(control::OscMessage message) {
    injectedOsc_.push_back(std::move(message));
}

std::size_t ControlHub::update(Engine& engine) {
    std::size_t count = 0;
    midiScratch_.clear();
    midi_.drain(midiScratch_);
    for (const auto& m : midiScratch_) {
        applyMidi(engine, m);
        ++count;
    }
    oscScratch_.clear();
    osc_.drain(oscScratch_);
    for (auto& m : injectedOsc_) {
        oscScratch_.push_back(std::move(m));
    }
    injectedOsc_.clear();
    for (const auto& m : oscScratch_) {
        applyOsc(engine, m);
        ++count;
    }
    return count;
}

void ControlHub::applyTarget(Engine& engine, const control::BindingTarget& target, const control::Match& match) {
    if (!target.signal.empty()) {
        if (match.event) {
            engine.controlSource().pulse(target.signal, match.value);
        } else {
            engine.controlSource().set(target.signal, match.value);
        }
    }
    if (!target.parameter.empty()) {
        if (auto* p = engine.params().find(target.parameter)) {
            const float v = target.min + (target.max - target.min) * match.value;
            const std::size_t n = p->componentCount();
            if (target.component < 0) {
                for (std::size_t c = 0; c < n; ++c) {
                    p->setBaseComponent(c, v);
                }
            } else if (static_cast<std::size_t>(target.component) < n) {
                p->setBaseComponent(static_cast<std::size_t>(target.component), v);
            }
        }
    }
}

void ControlHub::applyMidi(Engine& engine, const control::MidiMessage& message) {
    if (message.kind == control::MidiKind::Clock) {
        return; // not learnable, not bound (tempo sync is a follow-up)
    }
    lastMidi_ = message;
    bool matched = false;
    for (auto& binding : map_.midi) {
        if (auto match = control::matchMidi(binding, message)) {
            applyTarget(engine, binding.target, *match);
            matched = true;
        }
    }
    ++(matched ? applied_ : unmatched_);
}

void ControlHub::applyOsc(Engine& engine, const control::OscMessage& message) {
    lastOsc_ = message;
    bool matched = false;
    for (const auto& binding : map_.osc) {
        if (auto match = control::matchOsc(binding, message)) {
            applyTarget(engine, binding.target, *match);
            matched = true;
        }
    }
    if (map_.directOsc) {
        if (auto cmd = control::parseDirectOsc(message, map_.oscPrefix)) {
            using K = control::DirectCommand::Kind;
            matched = true;
            switch (cmd->kind) {
            case K::SetParameter:
                if (auto* p = engine.params().find(cmd->path)) {
                    const std::size_t n = std::min(p->componentCount(), cmd->values.size());
                    for (std::size_t c = 0; c < n; ++c) {
                        p->setBaseComponent(c, cmd->values[c]);
                    }
                    if (cmd->values.size() == 1) {
                        for (std::size_t c = 1; c < p->componentCount(); ++c) {
                            p->setBaseComponent(c, cmd->values[0]);
                        }
                    }
                } else {
                    matched = false;
                }
                break;
            case K::SetSignal:
                engine.controlSource().set(cmd->path, cmd->values.front());
                break;
            case K::Pulse:
                engine.controlSource().pulse(cmd->path, cmd->values.front());
                break;
            case K::PresetRecall:
                matched = engine.recallPreset(cmd->path);
                break;
            case K::PresetMorph:
                engine.morphPresets(cmd->path, cmd->second, std::clamp(cmd->values.front(), 0.0f, 1.0f));
                break;
            case K::Play:
                if (auto r = engine.play(); !r) {
                    log::warn("osc transport/play: {}", r.error().message);
                }
                break;
            case K::Pause: engine.pause(); break;
            case K::Stop: engine.stop(); break;
            case K::Toggle: engine.togglePlay(); break;
            case K::Seek: engine.seekSeconds(static_cast<double>(std::max(0.0f, cmd->values.front()))); break;
            }
        }
    }
    ++(matched ? applied_ : unmatched_);
}

bool ControlHub::bindLastMidi(const std::string& signal, const std::string& parameter, bool asEvent) {
    if (!lastMidi_) {
        return false;
    }
    control::MidiBinding b;
    b.source = "*";
    b.channel = lastMidi_->channel;
    b.number = lastMidi_->data1;
    switch (lastMidi_->kind) {
    case control::MidiKind::ControlChange: b.kind = control::MidiBindKind::ControlChange; break;
    case control::MidiKind::NoteOn:
    case control::MidiKind::NoteOff: b.kind = asEvent ? control::MidiBindKind::NoteEvent : control::MidiBindKind::Note; break;
    case control::MidiKind::PitchBend: b.kind = control::MidiBindKind::PitchBend; b.number = -1; break;
    case control::MidiKind::ChannelPressure: b.kind = control::MidiBindKind::ChannelPressure; b.number = -1; break;
    case control::MidiKind::ProgramChange: b.kind = control::MidiBindKind::Program; break;
    default: return false;
    }
    b.target.signal = signal;
    b.target.parameter = parameter;
    map_.midi.push_back(std::move(b));
    return true;
}

bool ControlHub::bindLastOsc(const std::string& signal, const std::string& parameter, bool asEvent) {
    if (!lastOsc_) {
        return false;
    }
    control::OscBinding b;
    b.address = lastOsc_->address;
    b.event = asEvent;
    b.target.signal = signal;
    b.target.parameter = parameter;
    map_.osc.push_back(std::move(b));
    return true;
}

ControlHub::Status ControlHub::status() const {
    Status s;
    s.oscOpen = osc_.isOpen();
    s.oscPort = osc_.port();
    s.oscError = oscError_;
    s.osc = osc_.stats();
    s.midiOpen = midi_.isOpen();
    s.midiError = midiError_;
    s.midiSources = midi_.connectedSources();
    s.midi = midi_.stats();
    s.applied = applied_;
    s.unmatched = unmatched_;
    return s;
}

} // namespace avgen::app
