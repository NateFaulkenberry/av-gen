#include "app/control_hub.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::app {

namespace {

constexpr std::size_t kBundleChunk = 50; // messages per bundle for "<prefix>/query/all"

std::uint64_t frameNanoseconds(const FrameTime& time) {
    return static_cast<std::uint64_t>(std::max(0.0, time.renderTime) * 1e9);
}

} // namespace

ControlHub::ControlHub() = default;
ControlHub::~ControlHub() { closeAll(); }

void ControlHub::setMap(control::ControlMap map) {
    map_ = std::move(map);
    applyIo();
}

void ControlHub::setLiveIo(bool enabled) {
    liveIo_ = enabled;
    applyIo();
}

void ControlHub::applyIo() {
    if (!liveIo_) {
        closeAll();
        oscError_.clear();
        midiError_.clear();
        feedbackError_.clear();
        return;
    }
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
    // OSC feedback sender (host may be empty: replies go to the sender only).
    if (!map_.feedbackHost.empty()) {
        const std::string target = map_.feedbackHost + ":" + std::to_string(map_.feedbackPort);
        if (!feedback_.isOpen() || feedbackTarget_ != target) {
            feedback_.close();
            if (auto r = feedback_.open(map_.feedbackHost, map_.feedbackPort); !r) {
                feedbackError_ = r.error().message;
                feedbackTarget_.clear();
                log::warn("osc feedback: {}", feedbackError_);
            } else {
                feedbackError_.clear();
                feedbackTarget_ = target;
                log::info("osc feedback: sending to {}", target);
            }
        }
    } else {
        feedback_.close();
        feedbackTarget_.clear();
        feedbackError_.clear();
    }
    feedbackCache_.clear(); // re-prime: nothing is pushed until a value changes from here on
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
    feedback_.close();
    feedbackTarget_.clear();
    reply_.close();
    replyTarget_.clear();
}

void ControlHub::injectMidi(std::span<const std::uint8_t> bytes, const std::string& source) {
    midi_.inject(bytes, source);
}

void ControlHub::injectOsc(control::OscMessage message) {
    injectedOsc_.push_back(std::move(message));
}

std::size_t ControlHub::update(Engine& engine, const FrameTime& time) {
    frameNs_ = frameNanoseconds(time);
    std::size_t count = 0;
    midiScratch_.clear();
    midi_.drain(midiScratch_);
    for (const auto& m : midiScratch_) {
        applyMidi(engine, m);
        ++count;
    }
    clock_.advance(frameNs_);
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
    pushFeedback(engine);
    oscWritten_.clear();
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
    using K = control::MidiKind;
    if (message.kind == K::Clock || message.kind == K::Start || message.kind == K::Continue ||
        message.kind == K::Stop) {
        // Tempo sync: not learnable, not bound, never unmatched.
        clock_.feed(message, frameNs_);
        ++clockMessages_;
        return;
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
            if (!binding.target.parameter.empty()) {
                oscWritten_.insert(binding.target.parameter);
            }
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
                    oscWritten_.insert(p->path());
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
            case K::StateGo:
                matched = engine.goToState(cmd->path, !cmd->values.empty() && cmd->values.front() > 0.5f);
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
            case K::Query:
                if (engine.params().find(cmd->path) != nullptr) {
                    replyParameter(engine, message, cmd->path);
                } else {
                    matched = false;
                }
                break;
            case K::QueryAll: replyAll(engine, message); break;
            case K::QueryPresets: replyPresets(engine, message); break;
            }
        }
    }
    ++(matched ? applied_ : unmatched_);
}

// ---- query replies and feedback ----------------------------------------------------------------

control::OscSender* ControlHub::replySender(const control::OscMessage& request) {
    if (feedback_.isOpen()) {
        return &feedback_;
    }
    if (request.sender.empty()) {
        return nullptr;
    }
    if (!reply_.isOpen() || replyTarget_ != request.sender) {
        std::string host;
        std::uint16_t port = 0;
        if (!control::splitHostPort(request.sender, host, port)) {
            return nullptr;
        }
        reply_.close();
        if (auto r = reply_.open(host, port); !r) {
            log::warn("osc reply to {}: {}", request.sender, r.error().message);
            replyTarget_.clear();
            return nullptr;
        }
        replyTarget_ = request.sender;
    }
    return &reply_;
}

control::OscMessage ControlHub::parameterMessage(const params::IParameter& parameter) const {
    control::OscMessage m;
    m.address = control::parameterAddress(map_.oscPrefix, parameter.path());
    const std::size_t n = parameter.componentCount();
    m.args.reserve(n);
    for (std::size_t c = 0; c < n; ++c) {
        m.args.emplace_back(parameter.baseComponent(c));
    }
    return m;
}

void ControlHub::sendMessages(control::OscSender& sender, std::vector<control::OscMessage>& messages) {
    if (messages.empty()) {
        return;
    }
    if (messages.size() == 1) {
        if (auto r = sender.send(messages.front()); !r) {
            log::warn("osc send: {}", r.error().message);
            return;
        }
        ++feedbackSent_;
        return;
    }
    for (std::size_t start = 0; start < messages.size(); start += kBundleChunk) {
        const std::size_t count = std::min(kBundleChunk, messages.size() - start);
        const std::span<const control::OscMessage> chunk(messages.data() + start, count);
        if (auto r = sender.sendBundle(chunk); !r) {
            log::warn("osc send bundle: {}", r.error().message);
            return;
        }
        feedbackSent_ += count;
    }
}

void ControlHub::replyParameter(Engine& engine, const control::OscMessage& request, const std::string& path) {
    auto* sender = replySender(request);
    const auto* p = engine.params().find(path);
    if (sender == nullptr || p == nullptr) {
        return;
    }
    std::vector<control::OscMessage> one{parameterMessage(*p)};
    sendMessages(*sender, one);
}

void ControlHub::replyAll(Engine& engine, const control::OscMessage& request) {
    auto* sender = replySender(request);
    if (sender == nullptr) {
        return;
    }
    std::vector<control::OscMessage> messages;
    for (const auto* p : engine.params().ordered()) {
        if (p->flags().serialized) {
            messages.push_back(parameterMessage(*p));
        }
    }
    sendMessages(*sender, messages);
}

void ControlHub::replyPresets(Engine& engine, const control::OscMessage& request) {
    auto* sender = replySender(request);
    if (sender == nullptr) {
        return;
    }
    control::OscMessage m;
    m.address = map_.oscPrefix + "/presets";
    for (const auto& preset : engine.presets().presets()) {
        m.args.emplace_back(preset.name);
    }
    std::vector<control::OscMessage> one{std::move(m)};
    sendMessages(*sender, one);
}

void ControlHub::pushFeedback(Engine& engine) {
    if (!map_.feedbackEnabled || !feedback_.isOpen()) {
        return;
    }
    const bool primed = !feedbackCache_.empty();
    std::vector<control::OscMessage> changed;
    for (const auto* p : engine.params().ordered()) {
        if (!p->flags().serialized) {
            continue;
        }
        const std::size_t n = p->componentCount();
        auto& cached = feedbackCache_[p->path()];
        bool differs = cached.size() != n;
        if (!differs) {
            for (std::size_t c = 0; c < n; ++c) {
                if (cached[c] != p->baseComponent(c)) {
                    differs = true;
                    break;
                }
            }
        }
        if (!differs) {
            continue;
        }
        cached.resize(n);
        for (std::size_t c = 0; c < n; ++c) {
            cached[c] = p->baseComponent(c);
        }
        if (primed && oscWritten_.count(p->path()) == 0) {
            changed.push_back(parameterMessage(*p));
        }
    }
    sendMessages(feedback_, changed);
}

// ---- learn -----------------------------------------------------------------------------------

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
    s.feedbackOpen = feedback_.isOpen();
    s.feedbackError = feedbackError_;
    s.feedbackSent = feedbackSent_;
    s.clockMessages = clockMessages_;
    return s;
}

} // namespace avgen::app
