#include "control/midi.hpp"

#include "control/midi_backend.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <utility>

namespace avgen::control {

// ---- names and values ----------------------------------------------------------------------

const char* midiKindName(MidiKind kind) {
    switch (kind) {
    case MidiKind::NoteOff: return "NoteOff";
    case MidiKind::NoteOn: return "NoteOn";
    case MidiKind::PolyPressure: return "PolyPressure";
    case MidiKind::ControlChange: return "ControlChange";
    case MidiKind::ProgramChange: return "ProgramChange";
    case MidiKind::ChannelPressure: return "ChannelPressure";
    case MidiKind::PitchBend: return "PitchBend";
    case MidiKind::Clock: return "Clock";
    case MidiKind::Start: return "Start";
    case MidiKind::Continue: return "Continue";
    case MidiKind::Stop: return "Stop";
    case MidiKind::Nrpn: return "NRPN";
    case MidiKind::Rpn: return "RPN";
    case MidiKind::DeviceRemoved: return "DeviceRemoved";
    case MidiKind::Other: return "Other";
    }
    return "Other";
}

float MidiMessage::normalized() const {
    switch (kind) {
    case MidiKind::PitchBend: {
        // ADR-1703: centre exactly 0.5. 8192 is the centre and the two halves are not the same length (8192 steps
        // down, 8191 up), so each half is scaled on its own: a parameter mapped symmetrically rests exactly on its
        // middle with the wheel at rest, where value14 / 16383 rested 0.00003 above it.
        const float v = static_cast<float>(value14);
        return v >= 8192.0f ? 0.5f + 0.5f * (v - 8192.0f) / 8191.0f : 0.5f * v / 8192.0f;
    }
    case MidiKind::Nrpn:
    case MidiKind::Rpn:
        // ADR-1702: 14 bits only when the device sent an LSB; a 7-bit device reaches 1.0 at 127.
        return (flags & kMidiHiRes) != 0 ? static_cast<float>(value14) / 16383.0f
                                         : static_cast<float>(value14 >> 7) / 127.0f;
    default:
        return static_cast<float>(data2) / 127.0f;
    }
}

bool MidiMessage::continuous() const {
    switch (kind) {
    case MidiKind::ControlChange:
    case MidiKind::PitchBend:
    case MidiKind::ChannelPressure:
    case MidiKind::PolyPressure:
    case MidiKind::Nrpn:
    case MidiKind::Rpn:
        return true;
    default:
        return false;
    }
}

// ---- byte parser ---------------------------------------------------------------------------

namespace {

constexpr std::uint8_t kSysexStart = 0xF0;
constexpr std::uint8_t kSysexEnd = 0xF7;
constexpr std::uint8_t kFirstRealtime = 0xF8;

bool isChannelVoice(std::uint8_t status) {
    return status >= 0x80 && status < 0xF0;
}

// Data bytes a channel voice message carries: 1 for program change and channel pressure, 2 for
// the rest.
int channelVoiceDataCount(std::uint8_t status) {
    const std::uint8_t type = status & 0xF0;
    return (type == 0xC0 || type == 0xD0) ? 1 : 2;
}

MidiMessage makeRealtime(std::uint8_t status, std::uint64_t timestampNs, std::uint16_t device) {
    MidiMessage m;
    switch (status) {
    case 0xF8: m.kind = MidiKind::Clock; break;
    case 0xFA: m.kind = MidiKind::Start; break;
    case 0xFB: m.kind = MidiKind::Continue; break;
    case 0xFC: m.kind = MidiKind::Stop; break;
    default: m.kind = MidiKind::Other; break; // F9, FD undefined, FE active sensing, FF reset
    }
    m.data1 = status;
    m.timestampNs = timestampNs;
    m.device = device;
    return m;
}

MidiMessage makeChannelVoice(std::uint8_t status, const std::uint8_t* data, std::uint64_t timestampNs,
                             std::uint16_t device) {
    MidiMessage m;
    m.channel = static_cast<std::uint8_t>(status & 0x0F);
    m.timestampNs = timestampNs;
    m.device = device;
    switch (status & 0xF0) {
    case 0x80:
        m.kind = MidiKind::NoteOff;
        m.data1 = data[0];
        m.data2 = data[1];
        break;
    case 0x90:
        m.kind = data[1] == 0 ? MidiKind::NoteOff : MidiKind::NoteOn;
        m.data1 = data[0];
        m.data2 = data[1];
        break;
    case 0xA0:
        m.kind = MidiKind::PolyPressure;
        m.data1 = data[0];
        m.data2 = data[1];
        break;
    case 0xB0:
        m.kind = MidiKind::ControlChange;
        m.data1 = data[0];
        m.data2 = data[1];
        break;
    case 0xC0:
        m.kind = MidiKind::ProgramChange;
        m.data1 = data[0];
        break;
    case 0xD0:
        m.kind = MidiKind::ChannelPressure;
        m.data2 = data[0]; // pressure is the value (data2), there is no note/controller number
        break;
    case 0xE0:
        m.kind = MidiKind::PitchBend;
        m.data1 = data[0];
        m.data2 = data[1];
        m.value14 = static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[1] & 0x7F) << 7) |
                                               (data[0] & 0x7F));
        break;
    default:
        m.kind = MidiKind::Other;
        break;
    }
    return m;
}

} // namespace

std::size_t parseMidiBytes(std::span<const std::uint8_t> bytes, MidiParserState& state,
                           std::vector<MidiMessage>& out, std::uint64_t timestampNs,
                           std::uint16_t device) {
    const std::size_t before = out.size();
    for (const std::uint8_t byte : bytes) {
        // System real-time may appear anywhere, even inside sysex or between the bytes of a
        // channel message, and never disturbs the parser state.
        if (byte >= kFirstRealtime) {
            out.push_back(makeRealtime(byte, timestampNs, device));
            continue;
        }
        if (byte == kSysexStart) {
            state.inSysex = true;
            state.runningStatus = 0;
            state.pendingCount = 0;
            continue;
        }
        if (byte == kSysexEnd) {
            state.inSysex = false;
            continue;
        }
        if (byte & 0x80) {
            // Any status byte terminates an unterminated sysex.
            state.inSysex = false;
            state.pendingCount = 0;
            if (isChannelVoice(byte)) {
                state.runningStatus = byte;
            } else {
                // System common (F1..F6): delivered as Other on the status byte; their data bytes
                // (if any) are discarded because running status is cleared.
                state.runningStatus = 0;
                MidiMessage m;
                m.kind = MidiKind::Other;
                m.data1 = byte;
                m.timestampNs = timestampNs;
                m.device = device;
                out.push_back(m);
            }
            continue;
        }
        // Data byte.
        if (state.inSysex || state.runningStatus == 0) {
            continue; // sysex payload, or garbage without a status
        }
        state.pending[state.pendingCount++] = byte;
        if (state.pendingCount >= channelVoiceDataCount(state.runningStatus)) {
            out.push_back(makeChannelVoice(state.runningStatus, state.pending, timestampNs, device));
            state.pendingCount = 0;
        }
    }
    return out.size() - before;
}

// ---- inbox ---------------------------------------------------------------------------------

namespace detail {

namespace {

bool overflowKeyEqual(const MidiMessage& a, const MidiMessage& b) {
    return a.device == b.device && a.channel == b.channel && a.kind == b.kind && a.data1 == b.data1;
}

} // namespace

MidiInbox::MidiInbox() {
    ring_.resize(kDefaultQueueLimit);
}

void MidiInbox::pushLocked(const MidiMessage& m) {
    ++stats_.messages;
    const std::size_t capacity = ring_.size();
    if (m.continuous() && size_ >= capacity - capacity / 4) {
        // Three quarters full: a continuous value is worth only its latest position. Fold it into the table.
        for (Overflow& slot : overflow_) {
            if (slot.used && overflowKeyEqual(slot.message, m)) {
                slot.message = m;
                ++stats_.coalesced;
                return;
            }
        }
        if (overflowUsed_ < overflow_.size()) {
            for (Overflow& slot : overflow_) {
                if (!slot.used) {
                    slot.used = true;
                    slot.message = m;
                    ++overflowUsed_;
                    ++stats_.coalesced;
                    return;
                }
            }
        }
        ++stats_.dropped;
        return;
    }
    if (size_ >= capacity) {
        ++stats_.dropped;
        if (m.kind == MidiKind::NoteOn || m.kind == MidiKind::NoteOff) {
            ++stats_.droppedNotes;
            notesLost_ = true;
        }
        return;
    }
    ring_[(head_ + size_) % capacity] = m;
    ++size_;
}

void MidiInbox::push(std::span<const MidiMessage> messages) {
    const std::lock_guard lock(mutex_);
    for (const MidiMessage& m : messages) {
        pushLocked(m);
    }
}

std::size_t MidiInbox::drain(std::vector<MidiMessage>& out) {
    const std::lock_guard lock(mutex_);
    const std::size_t before = out.size();
    const std::size_t capacity = ring_.size();
    for (std::size_t i = 0; i < size_; ++i) {
        out.push_back(ring_[(head_ + i) % capacity]);
    }
    head_ = 0;
    size_ = 0;
    if (overflowUsed_ > 0) {
        for (Overflow& slot : overflow_) {
            if (slot.used) {
                out.push_back(slot.message);
                slot.used = false;
            }
        }
        overflowUsed_ = 0;
    }
    if (notesLost_) {
        MidiMessage release;
        release.kind = MidiKind::DeviceRemoved;
        release.device = kAllMidiDevices;
        out.push_back(release);
        notesLost_ = false;
    }
    return out.size() - before;
}

MidiInput::Stats MidiInbox::stats() const {
    const std::lock_guard lock(mutex_);
    return stats_;
}

void MidiInbox::setQueueLimit(std::size_t limit) {
    const std::lock_guard lock(mutex_);
    limit = std::max<std::size_t>(limit, 4);
    // Keep the newest messages that fit; the rest are dropped and counted, as a full queue would.
    std::vector<MidiMessage> kept;
    kept.reserve(limit);
    const std::size_t capacity = ring_.size();
    const std::size_t skip = size_ > limit ? size_ - limit : 0;
    stats_.dropped += skip;
    for (std::size_t i = skip; i < size_; ++i) {
        kept.push_back(ring_[(head_ + i) % capacity]);
    }
    ring_.assign(limit, MidiMessage{});
    std::copy(kept.begin(), kept.end(), ring_.begin());
    head_ = 0;
    size_ = kept.size();
}

void MidiInbox::clear() {
    const std::lock_guard lock(mutex_);
    head_ = 0;
    size_ = 0;
    for (Overflow& slot : overflow_) {
        slot.used = false;
    }
    overflowUsed_ = 0;
    notesLost_ = false;
}

std::uint16_t MidiInbox::connectDevice(const std::string& id, const std::string& name, bool virtualSource) {
    const std::lock_guard lock(devicesMutex_);
    // "id", then "id#2", "id#3"... : the first candidate with a parked slot is that device coming back (its old slot,
    // so everything that refers to it finds it again); a candidate already connected is a second device with the same
    // id (a backend without unique ids, two identical devices), which moves on to the next name.
    // A device first seen without a unique id was filed under its name (the backend's fallback); when it is seen again
    // with its real id, that parked slot is the same device and takes the id -- one row, not a stale one beside it.
    if (id != name) {
        const auto parked = std::find_if(devices_.begin(), devices_.end(), [&](const MidiDeviceSlot& d) {
            return d.id == name && d.name == name && !d.connected;
        });
        const bool idKnown = std::any_of(devices_.begin(), devices_.end(), [&](const MidiDeviceSlot& d) { return d.id == id; });
        if (parked != devices_.end() && !idKnown) {
            parked->id = id;
            parked->connected = true;
            ++deviceGeneration_;
            return static_cast<std::uint16_t>(parked - devices_.begin());
        }
    }
    for (int n = 1;; ++n) {
        const std::string candidate = n == 1 ? id : id + "#" + std::to_string(n);
        const auto it = std::find_if(devices_.begin(), devices_.end(),
                                     [&](const MidiDeviceSlot& d) { return d.id == candidate; });
        if (it == devices_.end()) {
            if (devices_.size() >= kAllMidiDevices) {
                return kNoMidiDevice; // 65,534 distinct devices in one process: not a case worth a policy
            }
            devices_.push_back(MidiDeviceSlot{candidate, name, true, virtualSource});
            ++deviceGeneration_;
            return static_cast<std::uint16_t>(devices_.size() - 1);
        }
        if (!it->connected) {
            it->connected = true;
            it->name = name;
            ++deviceGeneration_;
            return static_cast<std::uint16_t>(it - devices_.begin());
        }
    }
}

void MidiInbox::disconnectDevice(std::uint16_t slot) {
    {
        const std::lock_guard lock(devicesMutex_);
        if (slot >= devices_.size() || !devices_[slot].connected) {
            return;
        }
        devices_[slot].connected = false;
        ++deviceGeneration_;
    }
    MidiMessage gone;
    gone.kind = MidiKind::DeviceRemoved;
    gone.device = slot;
    const std::lock_guard lock(mutex_);
    pushLocked(gone);
}

std::vector<MidiDeviceSlot> MidiInbox::devices() const {
    const std::lock_guard lock(devicesMutex_);
    return devices_;
}

std::uint64_t MidiInbox::deviceGeneration() const {
    const std::lock_guard lock(devicesMutex_);
    return deviceGeneration_;
}

bool midiFilterIsWildcard(const std::string& filter) {
    return filter.empty() || filter == "*";
}

bool midiFilterMatches(const std::string& filter, const std::string& name) {
    if (midiFilterIsWildcard(filter)) {
        return true;
    }
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::string f(filter.size(), '\0');
    std::string n(name.size(), '\0');
    std::transform(filter.begin(), filter.end(), f.begin(), lower);
    std::transform(name.begin(), name.end(), n.begin(), lower);
    return n.find(f) != std::string::npos;
}

} // namespace detail

// ---- MidiInput -----------------------------------------------------------------------------

struct MidiInput::Impl {
    detail::MidiInbox inbox;
    std::unique_ptr<detail::MidiInputBackend> backend;
    std::mutex injectMutex;
    struct Injected {
        std::uint16_t slot = kNoMidiDevice;
        MidiParserState parser; // running status per source
    };
    std::unordered_map<std::string, Injected> injected;
    std::vector<MidiMessage> injectScratch; // keeps its capacity
};

MidiInput::MidiInput()
    : impl_(std::make_unique<Impl>()) {
    impl_->backend = detail::makeMidiInputBackend(impl_->inbox);
}

MidiInput::~MidiInput() {
    close();
}

Result<void> MidiInput::open(const std::string& filter) {
    close();
    return impl_->backend->open(filter);
}

void MidiInput::close() {
    impl_->backend->close();
}

bool MidiInput::isOpen() const {
    return impl_->backend->isOpen();
}

std::vector<std::string> MidiInput::connectedSources() const {
    return impl_->backend->connectedSources();
}

std::size_t MidiInput::drain(std::vector<MidiMessage>& out) {
    return impl_->inbox.drain(out);
}

MidiInput::Stats MidiInput::stats() const {
    return impl_->inbox.stats();
}

void MidiInput::setQueueLimit(std::size_t limit) {
    impl_->inbox.setQueueLimit(limit);
}

std::vector<MidiDeviceSlot> MidiInput::devices() const {
    return impl_->inbox.devices();
}

std::uint64_t MidiInput::deviceGeneration() const {
    return impl_->inbox.deviceGeneration();
}

void MidiInput::inject(std::span<const std::uint8_t> bytes, const std::string& source) {
    Impl& s = *impl_;
    const std::lock_guard lock(s.injectMutex);
    Impl::Injected& injected = s.injected[source];
    if (injected.slot == kNoMidiDevice) {
        injected.slot = s.inbox.connectDevice("inject:" + source, source, true);
    }
    s.injectScratch.clear();
    parseMidiBytes(bytes, injected.parser, s.injectScratch, 0, injected.slot);
    if (!s.injectScratch.empty()) {
        s.inbox.push(s.injectScratch);
    }
}

void MidiInput::injectRemoval(const std::string& source) {
    Impl& s = *impl_;
    const std::lock_guard lock(s.injectMutex);
    const auto it = s.injected.find(source);
    if (it == s.injected.end() || it->second.slot == kNoMidiDevice) {
        return;
    }
    s.inbox.disconnectDevice(it->second.slot);
    it->second.slot = kNoMidiDevice; // the next inject reconnects it, to the same slot
    it->second.parser = MidiParserState{};
}

// ---- MidiVirtualSource ---------------------------------------------------------------------

struct MidiVirtualSource::Impl {
    std::unique_ptr<detail::MidiVirtualSourceBackend> backend = detail::makeMidiVirtualSourceBackend();
};

MidiVirtualSource::MidiVirtualSource()
    : impl_(std::make_unique<Impl>()) {}

MidiVirtualSource::~MidiVirtualSource() {
    close();
}

Result<void> MidiVirtualSource::open(const std::string& name) {
    close();
    return impl_->backend->open(name);
}

void MidiVirtualSource::close() {
    impl_->backend->close();
}

bool MidiVirtualSource::isOpen() const {
    return impl_->backend->isOpen();
}

Result<void> MidiVirtualSource::send(std::span<const std::uint8_t> bytes) {
    return impl_->backend->send(bytes);
}

} // namespace avgen::control
