// CoreMIDI backend (macOS). One process-wide MIDIClientRef (CoreMIDI documents that disposing a
// client can break later client creation in the same process, so it is created once and kept)
// with a MIDI 1.0 protocol input port per MidiInput. Incoming Universal MIDI Packets are
// converted back to MIDI 1.0 bytes and run through the shared parser, so devices and inject()
// share one code path. The virtual source converts bytes to UMP for MIDIReceivedEventList.
//
// Threading: CoreMIDI calls the receive block on its own high-priority thread; the block only
// parses and pushes into the inbox (mutex). Setup-change notifications are delivered through
// the run loop of the thread that created the client, so the client lives on a small dedicated
// thread that runs a CFRunLoop for the lifetime of the process (verified: without it,
// kMIDIMsgObjectAdded/Removed never arrive and hot-plugged sources could not be auto-connected).

#include "control/midi.hpp"

#include "control/midi_backend.hpp"
#include "core/log.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>

#include <pthread.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace avgen::control {

namespace {

// ---- host time -----------------------------------------------------------------------------

std::uint64_t hostTicksToNs(std::uint64_t ticks) {
    static const mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t info{};
        mach_timebase_info(&info);
        return info;
    }();
    if (timebase.denom == 0) {
        return ticks;
    }
    // Split to avoid overflow: ticks * numer can exceed 64 bits for large uptimes.
    const std::uint64_t whole = ticks / timebase.denom;
    const std::uint64_t rest = ticks % timebase.denom;
    return whole * timebase.numer + (rest * timebase.numer) / timebase.denom;
}

std::uint64_t nowNs() {
    return hostTicksToNs(mach_absolute_time());
}

// ---- CoreFoundation helpers ----------------------------------------------------------------

std::string toStdString(CFStringRef str) {
    if (str == nullptr) {
        return {};
    }
    if (const char* direct = CFStringGetCStringPtr(str, kCFStringEncodingUTF8); direct != nullptr) {
        return direct;
    }
    const CFIndex length = CFStringGetLength(str);
    const CFIndex maxBytes = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(maxBytes), '\0');
    if (!CFStringGetCString(str, out.data(), maxBytes, kCFStringEncodingUTF8)) {
        return {};
    }
    out.resize(std::char_traits<char>::length(out.c_str()));
    return out;
}

CFStringRef makeCFString(const std::string& s) {
    return CFStringCreateWithCString(kCFAllocatorDefault, s.c_str(), kCFStringEncodingUTF8);
}

std::string stringProperty(MIDIObjectRef object, CFStringRef property) {
    CFStringRef value = nullptr;
    if (MIDIObjectGetStringProperty(object, property, &value) != noErr || value == nullptr) {
        return {};
    }
    std::string out = toStdString(value);
    CFRelease(value);
    return out;
}

std::string endpointName(MIDIEndpointRef endpoint) {
    std::string name = stringProperty(endpoint, kMIDIPropertyDisplayName);
    if (name.empty()) {
        name = stringProperty(endpoint, kMIDIPropertyName);
    }
    return name;
}

std::string endpointId(MIDIEndpointRef endpoint, const std::string& fallbackName) {
    SInt32 uniqueId = 0;
    if (MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &uniqueId) == noErr && uniqueId != 0) {
        return std::to_string(uniqueId);
    }
    return fallbackName;
}

bool isVirtualEndpoint(MIDIEndpointRef endpoint) {
    MIDIEntityRef entity = 0;
    return MIDIEndpointGetEntity(endpoint, &entity) != noErr || entity == 0;
}

std::string statusText(OSStatus status) {
    switch (status) {
    case kMIDIInvalidClient: return "invalid client";
    case kMIDIInvalidPort: return "invalid port";
    case kMIDIWrongEndpointType: return "wrong endpoint type";
    case kMIDINoConnection: return "no connection";
    case kMIDIUnknownEndpoint: return "unknown endpoint";
    case kMIDIUnknownProperty: return "unknown property";
    case kMIDIWrongPropertyType: return "wrong property type";
    case kMIDINoCurrentSetup: return "no current setup";
    case kMIDIMessageSendErr: return "message send error";
    case kMIDIServerStartErr: return "MIDI server could not start";
    case kMIDISetupFormatErr: return "setup format error";
    case kMIDIWrongThread: return "wrong thread";
    case kMIDIObjectNotFound: return "object not found";
    case kMIDIIDNotUnique: return "id not unique";
    case kMIDINotPermitted: return "not permitted";
    case kMIDIUnknownError: return "unknown error";
    default: return "OSStatus " + std::to_string(status);
    }
}

// ---- UMP <-> MIDI 1.0 bytes ----------------------------------------------------------------

// Words per UMP message type (MIDI 2.0 spec, table 2).
constexpr std::array<std::uint8_t, 16> kUmpWordCount = {1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4};

// Data bytes following a system common/real-time status byte.
int systemDataCount(std::uint8_t status) {
    switch (status) {
    case 0xF1: case 0xF3: return 1;
    case 0xF2: return 2;
    default: return 0;
    }
}

// Appends the MIDI 1.0 bytes of one MIDI 1.0 protocol event list packet. Sysex (type 3) is not
// forwarded: the parser drops it anyway and it never interacts with running status here.
void umpPacketToBytes(const MIDIEventPacket* packet, std::vector<std::uint8_t>& out) {
    UInt32 i = 0;
    while (i < packet->wordCount) {
        const UInt32 word = packet->words[i];
        const std::uint8_t type = static_cast<std::uint8_t>(word >> 28);
        const std::uint8_t status = static_cast<std::uint8_t>((word >> 16) & 0xFF);
        const std::uint8_t d1 = static_cast<std::uint8_t>((word >> 8) & 0x7F);
        const std::uint8_t d2 = static_cast<std::uint8_t>(word & 0x7F);
        if (type == 0x1) { // system real-time / common
            out.push_back(status);
            const int n = systemDataCount(status);
            if (n >= 1) out.push_back(d1);
            if (n >= 2) out.push_back(d2);
        } else if (type == 0x2) { // MIDI 1.0 channel voice
            out.push_back(status);
            out.push_back(d1);
            const std::uint8_t kind = status & 0xF0;
            if (kind != 0xC0 && kind != 0xD0) {
                out.push_back(d2);
            }
        }
        i += kUmpWordCount[type];
    }
}

// Packs MIDI 1.0 bytes into 32-bit UMP words (group 0). Running status is expanded; real-time
// bytes are emitted where they occur; sysex is packed into 7-bit data messages (type 3).
std::vector<UInt32> bytesToUmp(std::span<const std::uint8_t> bytes) {
    std::vector<UInt32> words;
    std::uint8_t running = 0;
    std::array<std::uint8_t, 2> pending{};
    int pendingCount = 0;
    int needed = 0;
    bool inSysex = false;
    std::array<std::uint8_t, 6> sysex{};
    int sysexCount = 0;
    bool sysexStarted = false;

    auto flushSysex = [&](bool last) {
        // status: 0 complete, 1 start, 2 continue, 3 end
        std::uint8_t status = 0;
        if (sysexStarted) {
            status = last ? 3 : 2;
        } else {
            status = last ? 0 : 1;
        }
        const UInt32 w0 = (0x3u << 28) | (static_cast<UInt32>(status) << 20) |
                          (static_cast<UInt32>(sysexCount) << 16) | (static_cast<UInt32>(sysex[0]) << 8) |
                          static_cast<UInt32>(sysex[1]);
        const UInt32 w1 = (static_cast<UInt32>(sysex[2]) << 24) | (static_cast<UInt32>(sysex[3]) << 16) |
                          (static_cast<UInt32>(sysex[4]) << 8) | static_cast<UInt32>(sysex[5]);
        words.push_back(w0);
        words.push_back(w1);
        sysexStarted = true;
        sysex.fill(0);
        sysexCount = 0;
    };

    for (const std::uint8_t b : bytes) {
        if (b >= 0xF8) {
            words.push_back((0x1u << 28) | (static_cast<UInt32>(b) << 16));
            continue;
        }
        if (b == 0xF0) {
            inSysex = true;
            sysexStarted = false;
            sysexCount = 0;
            running = 0;
            pendingCount = 0;
            continue;
        }
        if (b == 0xF7) {
            if (inSysex) {
                flushSysex(true);
                inSysex = false;
            }
            continue;
        }
        if (inSysex) {
            if (b & 0x80) { // status inside sysex terminates it
                flushSysex(true);
                inSysex = false;
            } else {
                sysex[static_cast<std::size_t>(sysexCount++)] = b;
                if (sysexCount == 6) {
                    flushSysex(false);
                }
                continue;
            }
        }
        if (b & 0x80) {
            pendingCount = 0;
            if (b < 0xF0) {
                running = b;
                needed = ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) ? 1 : 2;
            } else {
                running = 0;
                const int n = systemDataCount(b);
                if (n == 0) {
                    words.push_back((0x1u << 28) | (static_cast<UInt32>(b) << 16));
                } else {
                    running = b; // collect its data bytes below
                    needed = n;
                }
            }
            continue;
        }
        if (running == 0) {
            continue; // stray data byte
        }
        pending[static_cast<std::size_t>(pendingCount++)] = b;
        if (pendingCount >= needed) {
            const UInt32 type = running >= 0xF0 ? 0x1u : 0x2u;
            words.push_back((type << 28) | (static_cast<UInt32>(running) << 16) |
                            (static_cast<UInt32>(pending[0]) << 8) |
                            static_cast<UInt32>(needed == 2 ? pending[1] : 0));
            pendingCount = 0;
            if (running >= 0xF0) {
                running = 0; // system common has no running status
            }
        }
    }
    return words;
}

// ---- process-wide client -------------------------------------------------------------------

class CoreMidiInput;

class ClientHost {
public:
    static ClientHost& instance() {
        static ClientHost* host = new ClientHost(); // intentionally leaked: the client is never disposed
        return *host;
    }

    [[nodiscard]] MIDIClientRef client() const { return client_; }
    [[nodiscard]] OSStatus creationStatus() const { return status_; }

    void addListener(CoreMidiInput* input) {
        const std::lock_guard lock(listenersMutex_);
        listeners_.push_back(input);
    }
    void removeListener(CoreMidiInput* input) {
        const std::lock_guard lock(listenersMutex_);
        std::erase(listeners_, input);
    }

private:
    ClientHost() {
        std::thread([this] { runLoopThread(); }).detach();
        std::unique_lock lock(readyMutex_);
        readyCv_.wait(lock, [this] { return ready_; });
    }

    void runLoopThread();
    void onNotification(const MIDINotification* message);

    MIDIClientRef client_ = 0;
    OSStatus status_ = noErr;
    std::mutex readyMutex_;
    std::condition_variable readyCv_;
    bool ready_ = false;
    std::mutex listenersMutex_;
    std::vector<CoreMidiInput*> listeners_;
};

// ---- input ---------------------------------------------------------------------------------

class CoreMidiInput final : public detail::MidiInputBackend {
public:
    explicit CoreMidiInput(detail::MidiInbox& inbox)
        : inbox_(inbox) {}
    ~CoreMidiInput() override { close(); }

    Result<void> open(const std::string& filter) override {
        close();
        ClientHost& host = ClientHost::instance();
        if (host.client() == 0) {
            return fail("CoreMIDI client unavailable: {}", statusText(host.creationStatus()));
        }
        const OSStatus st = MIDIInputPortCreateWithProtocol(
            host.client(), CFSTR("avgen input"), kMIDIProtocol_1_0, &port_,
            ^(const MIDIEventList* list, void* refCon) { receive(list, refCon); });
        if (st != noErr) {
            port_ = 0;
            return fail("cannot create CoreMIDI input port: {}", statusText(st));
        }
        filter_ = filter;
        wildcard_ = detail::midiFilterIsWildcard(filter);

        std::size_t matched = 0;
        const ItemCount count = MIDIGetNumberOfSources();
        for (ItemCount i = 0; i < count; ++i) {
            if (connectIfMatching(MIDIGetSource(i))) {
                ++matched;
            }
        }
        if (matched == 0 && !wildcard_) {
            close();
            return fail("no MIDI source matches '{}'", filter);
        }
        open_ = true;
        host.addListener(this);
        log::info("MIDI input open: {} source(s) connected (filter '{}')", matched, filter);
        return {};
    }

    void close() override {
        if (!open_ && port_ == 0) {
            return;
        }
        ClientHost::instance().removeListener(this);
        {
            const std::lock_guard lock(mutex_);
            for (auto& conn : connections_) {
                MIDIPortDisconnectSource(port_, conn->endpoint);
            }
        }
        if (port_ != 0) {
            MIDIPortDispose(port_); // no receive callbacks after this returns
            port_ = 0;
        }
        const std::lock_guard lock(mutex_);
        connections_.clear();
        graveyard_.clear();
        open_ = false;
    }

    [[nodiscard]] bool isOpen() const override { return open_; }

    [[nodiscard]] std::vector<std::string> connectedSources() const override {
        const std::lock_guard lock(mutex_);
        std::vector<std::string> names;
        names.reserve(connections_.size());
        for (const auto& conn : connections_) {
            names.push_back(conn->name);
        }
        return names;
    }

    // Called from the client host's run-loop thread.
    void onSourceAdded(MIDIEndpointRef endpoint) {
        if (open_ && wildcard_ && connectIfMatching(endpoint)) {
            log::info("MIDI source connected: '{}'", endpointName(endpoint));
        }
    }
    void onSourceRemoved(MIDIEndpointRef endpoint) {
        if (!open_) {
            return;
        }
        const std::lock_guard lock(mutex_);
        auto it = std::find_if(connections_.begin(), connections_.end(),
                               [&](const auto& c) { return c->endpoint == endpoint; });
        if (it != connections_.end()) {
            MIDIPortDisconnectSource(port_, endpoint);
            log::info("MIDI source removed: '{}'", (*it)->name);
            // The Connection stays alive (moved to the graveyard) in case a packet for it is
            // still in flight on the receive thread.
            graveyard_.push_back(std::move(*it));
            connections_.erase(it);
        }
    }

private:
    struct Connection {
        MIDIEndpointRef endpoint = 0;
        std::string name;
        MidiParserState parser; // touched only by the receive thread
    };

    bool connectIfMatching(MIDIEndpointRef endpoint) {
        if (endpoint == 0) {
            return false;
        }
        const std::string name = endpointName(endpoint);
        if (!detail::midiFilterMatches(filter_, name)) {
            return false;
        }
        const std::lock_guard lock(mutex_);
        if (std::any_of(connections_.begin(), connections_.end(),
                        [&](const auto& c) { return c->endpoint == endpoint; })) {
            return true;
        }
        auto conn = std::make_unique<Connection>();
        conn->endpoint = endpoint;
        conn->name = name;
        const OSStatus st = MIDIPortConnectSource(port_, endpoint, conn.get());
        if (st != noErr) {
            log::warn("cannot connect MIDI source '{}': {}", name, statusText(st));
            return false;
        }
        connections_.push_back(std::move(conn));
        return true;
    }

    // CoreMIDI receive thread.
    void receive(const MIDIEventList* list, void* refCon) {
        auto* conn = static_cast<Connection*>(refCon);
        if (conn == nullptr || list == nullptr) {
            return;
        }
        const MIDIEventPacket* packet = &list->packet[0];
        for (UInt32 p = 0; p < list->numPackets; ++p) {
            bytes_.clear();
            umpPacketToBytes(packet, bytes_);
            if (!bytes_.empty()) {
                const std::uint64_t ts = packet->timeStamp != 0 ? hostTicksToNs(packet->timeStamp) : nowNs();
                inbox_.receive(bytes_, conn->parser, ts, conn->name);
            }
            packet = MIDIEventPacketNext(packet);
        }
    }

    detail::MidiInbox& inbox_;
    MIDIPortRef port_ = 0;
    std::string filter_;
    bool wildcard_ = true;
    std::atomic<bool> open_{false};
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<Connection>> connections_;
    std::vector<std::unique_ptr<Connection>> graveyard_;
    std::vector<std::uint8_t> bytes_; // receive-thread scratch
};

void ClientHost::runLoopThread() {
    pthread_setname_np("avgen.coremidi");
    MIDIClientRef client = 0;
    const OSStatus st = MIDIClientCreateWithBlock(CFSTR("avgen"), &client,
                                                  ^(const MIDINotification* message) { onNotification(message); });
    {
        const std::lock_guard lock(readyMutex_);
        client_ = st == noErr ? client : 0;
        status_ = st;
        ready_ = true;
    }
    readyCv_.notify_all();
    if (st != noErr) {
        return;
    }
    // Keep the run loop alive even when CoreMIDI has not (yet) installed its source.
    CFRunLoopTimerRef keepAlive = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 3600.0,
                                                       3600.0, 0, 0, nullptr, nullptr);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), keepAlive, kCFRunLoopDefaultMode);
    for (;;) {
        CFRunLoopRun();
    }
}

void ClientHost::onNotification(const MIDINotification* message) {
    if (message == nullptr) {
        return;
    }
    if (message->messageID != kMIDIMsgObjectAdded && message->messageID != kMIDIMsgObjectRemoved) {
        return;
    }
    const auto* change = reinterpret_cast<const MIDIObjectAddRemoveNotification*>(message);
    if (change->childType != kMIDIObjectType_Source && change->childType != kMIDIObjectType_ExternalSource) {
        return;
    }
    const MIDIEndpointRef endpoint = static_cast<MIDIEndpointRef>(change->child);
    const std::lock_guard lock(listenersMutex_);
    for (CoreMidiInput* input : listeners_) {
        if (message->messageID == kMIDIMsgObjectAdded) {
            input->onSourceAdded(endpoint);
        } else {
            input->onSourceRemoved(endpoint);
        }
    }
}

// ---- virtual source ------------------------------------------------------------------------

class CoreMidiVirtualSource final : public detail::MidiVirtualSourceBackend {
public:
    ~CoreMidiVirtualSource() override { close(); }

    Result<void> open(const std::string& name) override {
        close();
        ClientHost& host = ClientHost::instance();
        if (host.client() == 0) {
            return fail("CoreMIDI client unavailable: {}", statusText(host.creationStatus()));
        }
        CFStringRef cfName = makeCFString(name);
        const OSStatus st = MIDISourceCreateWithProtocol(host.client(), cfName, kMIDIProtocol_1_0, &endpoint_);
        CFRelease(cfName);
        if (st != noErr) {
            endpoint_ = 0;
            return fail("cannot create virtual MIDI source '{}': {}", name, statusText(st));
        }
        return {};
    }

    void close() override {
        if (endpoint_ != 0) {
            MIDIEndpointDispose(endpoint_);
            endpoint_ = 0;
        }
    }

    [[nodiscard]] bool isOpen() const override { return endpoint_ != 0; }

    Result<void> send(std::span<const std::uint8_t> bytes) override {
        if (endpoint_ == 0) {
            return fail("virtual MIDI source is not open");
        }
        const std::vector<UInt32> words = bytesToUmp(bytes);
        if (words.empty()) {
            return {};
        }
        // MIDIEventList is 4-byte packed; UInt32 storage gives the right alignment without a cast
        // that increases alignment requirements.
        std::array<UInt32, 1024> storage{};
        auto* list = reinterpret_cast<MIDIEventList*>(storage.data());
        const ByteCount storageBytes = storage.size() * sizeof(UInt32);
        MIDIEventPacket* packet = MIDIEventListInit(list, kMIDIProtocol_1_0);
        const MIDITimeStamp now = mach_absolute_time();
        std::size_t i = 0;
        while (i < words.size()) {
            const std::size_t n = kUmpWordCount[words[i] >> 28];
            packet = MIDIEventListAdd(list, storageBytes, packet, now, n, &words[i]);
            if (packet == nullptr) {
                // Flush what fits and start a new list.
                if (const OSStatus st = MIDIReceivedEventList(endpoint_, list); st != noErr) {
                    return fail("cannot send from virtual MIDI source: {}", statusText(st));
                }
                packet = MIDIEventListInit(list, kMIDIProtocol_1_0);
                continue;
            }
            i += n;
        }
        if (const OSStatus st = MIDIReceivedEventList(endpoint_, list); st != noErr) {
            return fail("cannot send from virtual MIDI source: {}", statusText(st));
        }
        return {};
    }

private:
    MIDIEndpointRef endpoint_ = 0;
};

} // namespace

namespace detail {

std::unique_ptr<MidiInputBackend> makeMidiInputBackend(MidiInbox& inbox) {
    return std::make_unique<CoreMidiInput>(inbox);
}

std::unique_ptr<MidiVirtualSourceBackend> makeMidiVirtualSourceBackend() {
    return std::make_unique<CoreMidiVirtualSource>();
}

} // namespace detail

std::vector<MidiDeviceInfo> listMidiInputs() {
    std::vector<MidiDeviceInfo> devices;
    const ItemCount count = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < count; ++i) {
        const MIDIEndpointRef endpoint = MIDIGetSource(i);
        if (endpoint == 0) {
            continue;
        }
        MidiDeviceInfo info;
        info.name = endpointName(endpoint);
        info.id = endpointId(endpoint, info.name);
        info.virtualSource = isVirtualEndpoint(endpoint);
        devices.push_back(std::move(info));
    }
    return devices;
}

bool hasMidiBackend() {
    return true;
}

} // namespace avgen::control
