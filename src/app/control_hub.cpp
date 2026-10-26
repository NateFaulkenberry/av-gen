#include "app/control_hub.hpp"

#include "app/engine.hpp"
#include "control/midi_rig.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace avgen::app {

namespace {

constexpr std::size_t kBundleChunk = 50; // messages per bundle for "<prefix>/query/all"

std::uint64_t frameNanoseconds(const FrameTime& time) {
    return static_cast<std::uint64_t>(std::max(0.0, time.renderTime) * 1e9);
}

std::uint64_t steadyNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ADR-1700: the mapper's destinations on the engine: parameter bases by path (found per write, never a cached pointer,
// so a scene load that clears the parameter set cannot leave a mapping holding a dangling one) and the control source.
//
// The control source is looked up once per frame, not per write: `Engine::controlSource()` re-declares every channel
// of the map each time it is called (ensureControlSource), which measured 16 us a message under a CC flood -- 7.7 ms of a
// 33 ms frame at four devices x 3,000 messages a second. Once per frame it is nothing.
class EngineTargets final : public control::MidiTargets {
public:
    EngineTargets(Engine& engine, const control::ControlMap& map, ControlHub& hub)
        : engine_(engine), map_(map), control_(engine.controlSource()), hub_(hub) {}
    std::optional<float> parameter(const std::string& path, int component) override {
        const params::IParameter* p = engine_.params().find(path);
        const std::size_t c = component < 0 ? 0 : static_cast<std::size_t>(component);
        if (p == nullptr || c >= p->componentCount()) {
            return std::nullopt;
        }
        return p->baseComponent(c);
    }
    void setParameter(const std::string& path, int component, float value) override {
        params::IParameter* p = engine_.params().find(path);
        if (p == nullptr) {
            return;
        }
        const std::size_t n = p->componentCount();
        if (component < 0) {
            for (std::size_t c = 0; c < n; ++c) {
                p->setBaseComponent(c, value);
            }
        } else if (static_cast<std::size_t>(component) < n) {
            p->setBaseComponent(static_cast<std::size_t>(component), value);
        }
    }
    float signal(const std::string& name) override { return control_.value(name); }
    bool signalHasRest(const std::string& name) override { return map_.restFor(name) != nullptr; }
    void setSignal(const std::string& name, float value) override { control_.set(name, value); }
    void pulseSignal(const std::string& name, float strength) override { control_.pulse(name, strength); }
    void action(control::MidiAction a) override;

private:
    Engine& engine_;
    const control::ControlMap& map_;
    signals::ControlSource& control_;
    ControlHub& hub_;
};

} // namespace

// ADR-1705: the hub's private queue, reached through a friend.
class HubTargets {
public:
    static void queue(ControlHub& hub, control::MidiAction a) { hub.queueAction(a); }
};

namespace {
void EngineTargets::action(control::MidiAction a) { HubTargets::queue(hub_, a); }
} // namespace

ControlHub::ControlHub()
    : startNs_(steadyNs()) {}

double ControlHub::clockSeconds() const {
    return static_cast<double>(steadyNs() - startNs_) * 1e-9;
}

void ControlHub::setSceneKey(const std::string& key) {
    if (key == sceneKey_) {
        return;
    }
    sceneKey_ = key;
    rebuildPending_ = true;
    freshRebuild_ = true;
}

void ControlHub::setDisabledDevices(std::vector<std::string> ids) {
    disabledDevices_ = std::move(ids);
    deviceGeneration_ = ~0ull; // re-derive the per-slot flags
    syncDevices();
}

bool ControlHub::deviceEnabled(std::uint16_t slot) const {
    return slot >= deviceDisabled_.size() || !deviceDisabled_[slot];
}

void ControlHub::injectMidiRemoval(const std::string& source) {
    midi_.injectRemoval(source);
}

void ControlHub::syncDevices() {
    const std::uint64_t generation = midi_.deviceGeneration();
    if (generation == deviceGeneration_) {
        return;
    }
    deviceGeneration_ = generation;
    devices_ = midi_.devices();
    activity_.resize(devices_.size());
    deviceDisabled_.assign(devices_.size(), false);
    for (std::size_t i = 0; i < devices_.size(); ++i) {
        deviceDisabled_[i] = std::find(disabledDevices_.begin(), disabledDevices_.end(), devices_[i].id) !=
                             disabledDevices_.end();
    }
    mapper_.setDevices(devices_);
}

void ControlHub::noteRigChange() {
    std::string fingerprint = control::midiRigFingerprint(map_.midiRig);
    if (fingerprint != rigFingerprint_) {
        rigFingerprint_ = std::move(fingerprint);
        ++rigRevision_;
    }
}

void ControlHub::setRig(std::vector<control::MidiBinding> rig) {
    for (auto& b : rig) {
        b.scope = control::MidiScope::Rig;
    }
    map_.midiRig = std::move(rig);
    rigFingerprint_ = control::midiRigFingerprint(map_.midiRig); // what was loaded is what is saved: no revision
    rebuildPending_ = true;
    freshRebuild_ = true;
}

void ControlHub::queueAction(control::MidiAction a) {
    ++actionsFired_;
    switch (a) {
    case control::MidiAction::None: break;
    case control::MidiAction::TransportPlay: ++pendingPlayToggles_; break;
    case control::MidiAction::TransportReturnToStart: pendingReturnToStart_ = true; break;
    case control::MidiAction::LiveNextScene: ++liveSceneSteps_; break;
    case control::MidiAction::LivePreviousScene: --liveSceneSteps_; break;
    }
}

// After the frame's messages: the transport actions as the keyboard does them (Space, Return). Two presses in one
// frame toggle twice.
void ControlHub::performActions(Engine& engine) {
    if (pendingReturnToStart_) {
        pendingReturnToStart_ = false;
        engine.seekSeconds(engine.transport().playStartSeconds());
    }
    for (; pendingPlayToggles_ > 0; --pendingPlayToggles_) {
        engine.togglePlay();
    }
}

void ControlHub::rebuildMappings(control::MidiTargets& targets) {
    noteRigChange(); // an edit may have changed the rig, which the app then saves (on rebuild only, never per frame)
    mapper_.rebuild(map_, sceneKey_, targets, !freshRebuild_);
    mapper_.setDevices(devices_);
    rebuildPending_ = false;
    freshRebuild_ = false;
}
ControlHub::~ControlHub() { closeAll(); }

void ControlHub::setMap(control::ControlMap map) {
    map.midiRig = std::move(map_.midiRig); // ADR-1705: the rig is the performer's, not the project's
    map_ = std::move(map);
    rebuildPending_ = true; // ADR-1700: a new map starts fresh (held gates released on the next update)
    freshRebuild_ = true;
    nrpn_.reset();
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
    EngineTargets targets(engine, map_, *this);
    syncDevices();
    if (rebuildPending_) {
        rebuildMappings(targets);
    }
    midiScratch_.clear();
    midi_.drain(midiScratch_);
    syncDevices(); // a device that connected while those were queued (its slot is in them)
    const double now = clockSeconds();
    for (const auto& m : midiScratch_) {
        applyMidiWith(engine, m, targets, now);
        ++count;
    }
    mapper_.advance(time.deltaTime, targets); // smoothing, this frame
    performActions(engine);                    // ADR-1705
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
    EngineTargets targets(engine, map_, *this);
    syncDevices();
    if (rebuildPending_) {
        rebuildMappings(targets);
    }
    applyMidiWith(engine, message, targets, clockSeconds());
    performActions(engine);
}

void ControlHub::applyMidiWith(Engine& engine, const control::MidiMessage& message, control::MidiTargets& targets,
                               double now) {
    using K = control::MidiKind;
    const bool known = message.device < activity_.size();
    if (message.kind == K::Clock || message.kind == K::Start || message.kind == K::Continue ||
        message.kind == K::Stop) {
        // Tempo sync: not learnable, not mapped, never unmatched (and its own counter, not the monitor's rows).
        clock_.feed(message, frameNs_);
        ++clockMessages_;
        if (known) {
            ++activity_[message.device].clock;
            activity_[message.device].lastSeconds = now;
        }
        if (monitor_.showClock()) {
            monitor_.push(message, now, 0, false);
        }
        return;
    }
    if (message.kind == K::DeviceRemoved) {
        // ADR-1701: its note-offs will never come. Its held gates are released and its NRPN state forgotten.
        if (message.device == control::kAllMidiDevices) {
            nrpn_.reset();
        } else {
            nrpn_.resetDevice(message.device);
        }
        mapper_.apply(message, targets, now);
        monitor_.push(message, now, 0, false);
        return;
    }
    if (known) {
        DeviceActivity& a = activity_[message.device];
        ++a.messages;
        a.lastSeconds = now;
        if (message.channel < 16) {
            a.channelSeconds[message.channel] = now;
        }
    }
    control::MidiMessage raw = message;
    const std::optional<control::MidiMessage> decoded = nrpn_.feed(raw); // marks raw as an NRPN part when it is one
    if (!deviceEnabled(message.device)) {
        // Switched off on this machine: seen (so the person can tell it is sending), never applied.
        monitor_.push(raw, now, 0, true);
        if (decoded) {
            monitor_.push(*decoded, now, 0, true);
        }
        ++ignored_;
        return;
    }
    lastMidi_ = raw;
    learn_.feed(raw);
    // ADR-1025: live Sonic input takes the notes (and the sustain pedal) into its note model. Mappings still see every
    // message, so a project can also map a key or a CC as it always could.
    bool matched = engine.liveSonic() && engine.liveSonicMidi(raw);
    if (engine.liveSonic() && raw.kind == K::ProgramChange) {
        programChange_ = static_cast<int>(raw.data1); // ADR-1063: the scene switcher
        matched = true;
    }
    const int moved = mapper_.apply(raw, targets, now);
    monitor_.push(raw, now, static_cast<std::uint16_t>(moved), false);
    matched = matched || moved > 0;
    if (decoded) {
        learn_.feed(*decoded);
        const int movedDecoded = mapper_.apply(*decoded, targets, now);
        monitor_.push(*decoded, now, static_cast<std::uint16_t>(movedDecoded), false);
        matched = matched || movedDecoded > 0;
    } else if ((raw.flags & control::kMidiNrpnPart) != 0 && !matched) {
        return; // a parameter select: half of a message, neither applied nor unmatched
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
            case K::ControlReset: // ADR-1612
                engine.controlSource().resetToRest();
                matched = true;
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
    s.pickupHeld = mapper_.pickupHeld();
    s.unmatched = unmatched_;
    s.ignored = ignored_;
    s.feedbackOpen = feedback_.isOpen();
    s.feedbackError = feedbackError_;
    s.feedbackSent = feedbackSent_;
    s.clockMessages = clockMessages_;
    return s;
}

} // namespace avgen::app
