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
    case MidiKind::Other: return "Other";
    }
    return "Other";
}

float MidiMessage::normalized() const {
    if (kind == MidiKind::PitchBend) {
        return static_cast<float>(value14) / 16383.0f;
    }
    return static_cast<float>(data2) / 127.0f;
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

MidiMessage makeRealtime(std::uint8_t status, std::uint64_t timestampNs, const std::string& source) {
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
    m.source = source;
    return m;
}

MidiMessage makeChannelVoice(std::uint8_t status, const std::uint8_t* data, std::uint64_t timestampNs,
                             const std::string& source) {
    MidiMessage m;
    m.channel = static_cast<std::uint8_t>(status & 0x0F);
    m.timestampNs = timestampNs;
    m.source = source;
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
                           const std::string& source) {
    const std::size_t before = out.size();
    for (const std::uint8_t byte : bytes) {
        // System real-time may appear anywhere, even inside sysex or between the bytes of a
        // channel message, and never disturbs the parser state.
        if (byte >= kFirstRealtime) {
            out.push_back(makeRealtime(byte, timestampNs, source));
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
                m.source = source;
                out.push_back(std::move(m));
            }
            continue;
        }
        // Data byte.
        if (state.inSysex || state.runningStatus == 0) {
            continue; // sysex payload, or garbage without a status
        }
        state.pending[state.pendingCount++] = byte;
        if (state.pendingCount >= channelVoiceDataCount(state.runningStatus)) {
            out.push_back(makeChannelVoice(state.runningStatus, state.pending, timestampNs, source));
            state.pendingCount = 0;
        }
    }
    return out.size() - before;
}

// ---- inbox ---------------------------------------------------------------------------------

namespace detail {

void MidiInbox::receive(std::span<const std::uint8_t> bytes, MidiParserState& state,
                        std::uint64_t timestampNs, const std::string& source) {
    std::vector<MidiMessage> parsed;
    parseMidiBytes(bytes, state, parsed, timestampNs, source);
    if (!parsed.empty()) {
        push(parsed);
    }
}

void MidiInbox::push(std::vector<MidiMessage>& messages) {
    const std::lock_guard lock(mutex_);
    for (auto& m : messages) {
        stats_.lastSource = m.source;
        ++stats_.messages;
        queue_.push_back(std::move(m));
        while (queue_.size() > limit_) {
            queue_.pop_front();
            ++stats_.dropped;
        }
    }
    messages.clear();
}

std::size_t MidiInbox::drain(std::vector<MidiMessage>& out) {
    const std::lock_guard lock(mutex_);
    const std::size_t count = queue_.size();
    out.reserve(out.size() + count);
    for (auto& m : queue_) {
        out.push_back(std::move(m));
    }
    queue_.clear();
    return count;
}

MidiInput::Stats MidiInbox::stats() const {
    const std::lock_guard lock(mutex_);
    return stats_;
}

void MidiInbox::setQueueLimit(std::size_t limit) {
    const std::lock_guard lock(mutex_);
    limit_ = std::max<std::size_t>(limit, 1);
    while (queue_.size() > limit_) {
        queue_.pop_front();
        ++stats_.dropped;
    }
}

void MidiInbox::clear() {
    const std::lock_guard lock(mutex_);
    queue_.clear();
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
    std::unordered_map<std::string, MidiParserState> injectStates; // running status per source
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

void MidiInput::inject(std::span<const std::uint8_t> bytes, const std::string& source) {
    Impl& s = *impl_;
    const std::lock_guard lock(s.injectMutex);
    s.inbox.receive(bytes, s.injectStates[source], 0, source);
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
