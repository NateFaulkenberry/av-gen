#include "audio/capture_coreaudio.hpp"

#include <numeric>

#if defined(__APPLE__)

#include "core/log.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cstddef>
#include <mutex>

namespace avgen::audio {

namespace {

constexpr AudioObjectPropertyElement kMain = kAudioObjectPropertyElementMain;

std::string cfString(CFStringRef s) {
    if (s == nullptr) {
        return {};
    }
    const CFIndex length = CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(length), '\0');
    if (!CFStringGetCString(s, out.data(), length, kCFStringEncodingUTF8)) {
        return {};
    }
    out.resize(std::char_traits<char>::length(out.c_str()));
    return out;
}

std::string deviceString(AudioObjectID id, AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress a{selector, kAudioObjectPropertyScopeGlobal, kMain};
    CFStringRef s = nullptr;
    UInt32 size = sizeof(s);
    if (AudioObjectGetPropertyData(id, &a, 0, nullptr, &size, &s) != noErr || s == nullptr) {
        return {};
    }
    std::string out = cfString(s);
    CFRelease(s);
    return out;
}

std::vector<AudioObjectID> allDevices() {
    AudioObjectPropertyAddress a{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr) {
        return {};
    }
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data()) != noErr) {
        return {};
    }
    return ids;
}

// Channels per input stream, from the stream configuration (one AudioBuffer per stream).
std::vector<std::uint32_t> inputStreams(AudioObjectID id) {
    AudioObjectPropertyAddress a{kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeInput, kMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(id, &a, 0, nullptr, &size) != noErr || size == 0) {
        return {};
    }
    std::vector<std::byte> raw(size);
    auto* list = reinterpret_cast<AudioBufferList*>(raw.data());
    if (AudioObjectGetPropertyData(id, &a, 0, nullptr, &size, list) != noErr) {
        return {};
    }
    std::vector<std::uint32_t> out;
    for (UInt32 b = 0; b < list->mNumberBuffers; ++b) {
        out.push_back(list->mBuffers[b].mNumberChannels);
    }
    return out;
}

double nominalRate(AudioObjectID id) {
    AudioObjectPropertyAddress a{kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kMain};
    Float64 rate = 0.0;
    UInt32 size = sizeof(rate);
    return AudioObjectGetPropertyData(id, &a, 0, nullptr, &size, &rate) == noErr ? rate : 0.0;
}

AudioObjectID deviceForUid(const std::string& uid) {
    for (const AudioObjectID id : allDevices()) {
        if (deviceString(id, kAudioDevicePropertyDeviceUID) == uid) {
            return id;
        }
    }
    return kAudioObjectUnknown;
}

// Listener notifications are delivered on the HAL's own thread rather than a run loop the process may never run (a
// test, a headless tool). Process-wide, set once, before the first listener.
void useHalNotificationThread() {
    static std::once_flag once;
    std::call_once(once, [] {
        CFRunLoopRef none = nullptr;
        AudioObjectPropertyAddress a{kAudioHardwarePropertyRunLoop, kAudioObjectPropertyScopeGlobal, kMain};
        AudioObjectSetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, sizeof(none), &none);
    });
}

} // namespace

std::uint32_t CoreAudioInputDevice::channels() const {
    return std::accumulate(streamChannels.begin(), streamChannels.end(), 0u);
}

std::vector<CoreAudioInputDevice> coreAudioInputDevices() {
    std::vector<CoreAudioInputDevice> out;
    for (const AudioObjectID id : allDevices()) {
        auto streams = inputStreams(id);
        if (streams.empty()) {
            continue; // output only
        }
        CoreAudioInputDevice d;
        d.uid = deviceString(id, kAudioDevicePropertyDeviceUID);
        d.name = deviceString(id, kAudioObjectPropertyName);
        d.streamChannels = std::move(streams);
        d.sampleRate = nominalRate(id);
        out.push_back(std::move(d));
    }
    return out;
}

bool coreAudioDevicePresent(const std::string& uid) {
    return !uid.empty() && deviceForUid(uid) != kAudioObjectUnknown;
}

struct CoreAudioCapture::State {
    AudioObjectID device = kAudioObjectUnknown;
    AudioUnit unit = nullptr;
    CaptureSink* sink = nullptr;
    std::uint32_t channels = 0;
    std::uint32_t maxFrames = 4096;
    std::vector<std::vector<float>> buffers;
    std::vector<const float*> pointers;
    std::vector<std::byte> listRaw;
    AudioBufferList* list = nullptr;
    double openedRate = 0.0;
    std::vector<std::uint32_t> openedStreams;
    std::atomic<bool> alive{true};
    std::atomic<bool> changed{false};
    std::atomic<std::uint64_t> overloads{0};
    bool listening = false;
    bool running = false;

    static OSStatus input(void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* stamp, UInt32 bus,
                          UInt32 frames, AudioBufferList* /*unused*/);
    static OSStatus deviceListener(AudioObjectID, UInt32 count, const AudioObjectPropertyAddress* addresses, void* ref);
    static OSStatus systemListener(AudioObjectID, UInt32 count, const AudioObjectPropertyAddress* addresses, void* ref);
    void addListeners();
    void removeListeners();
};

// REAL-TIME: AudioUnitRender into buffers allocated at open, then the sink. No allocation, locks or logging.
OSStatus CoreAudioCapture::State::input(void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* stamp,
                                        UInt32 bus, UInt32 frames, AudioBufferList*) {
    auto* s = static_cast<State*>(ref);
    // Measured on the Volt 876 unplugged mid-capture (and on a destroyed aggregate): AUHAL keeps calling this, at the
    // system default input's 512-frame period, with silence. Nothing it delivers after the device has gone is the
    // device's: not rendered, not passed on.
    if (!s->alive.load(std::memory_order_relaxed)) {
        return noErr;
    }
    if (frames > s->maxFrames || s->sink == nullptr) {
        return noErr; // larger than the slice AUHAL was told: cannot happen; never write past the buffers
    }
    for (std::uint32_t c = 0; c < s->channels; ++c) {
        s->list->mBuffers[c].mNumberChannels = 1;
        s->list->mBuffers[c].mDataByteSize = frames * sizeof(float);
        s->list->mBuffers[c].mData = s->buffers[c].data();
    }
    if (AudioUnitRender(s->unit, flags, stamp, bus, frames, s->list) != noErr) {
        return noErr;
    }
    CaptureBlockTiming timing;
    timing.hasSampleTime = stamp != nullptr && (stamp->mFlags & kAudioTimeStampSampleTimeValid) != 0;
    timing.sampleTime = timing.hasSampleTime ? stamp->mSampleTime : 0.0;
    s->sink->onBlock(s->pointers.data(), s->channels, frames, timing);
    return noErr;
}

// HAL notification thread. Atomics only.
OSStatus CoreAudioCapture::State::deviceListener(AudioObjectID, UInt32 count, const AudioObjectPropertyAddress* addresses,
                                                 void* ref) {
    auto* s = static_cast<State*>(ref);
    for (UInt32 i = 0; i < count; ++i) {
        switch (addresses[i].mSelector) {
        case kAudioDevicePropertyDeviceIsAlive: {
            UInt32 alive = 1;
            UInt32 size = sizeof(alive);
            AudioObjectPropertyAddress a{kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, kMain};
            const OSStatus st = AudioObjectGetPropertyData(s->device, &a, 0, nullptr, &size, &alive);
            if (st != noErr || alive == 0) {
                s->alive.store(false);
            }
            break;
        }
        case kAudioDeviceProcessorOverload:
            s->overloads.fetch_add(1, std::memory_order_relaxed);
            break;
        case kAudioDevicePropertyNominalSampleRate:
            if (nominalRate(s->device) != s->openedRate) {
                s->changed.store(true);
            }
            break;
        case kAudioDevicePropertyStreamConfiguration:
            if (inputStreams(s->device) != s->openedStreams) {
                s->changed.store(true);
            }
            break;
        default:
            break;
        }
    }
    return noErr;
}

// HAL notification thread: the device list changed; is ours still in it? (IsAlive alone is not always sent: VLC.)
OSStatus CoreAudioCapture::State::systemListener(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void* ref) {
    auto* s = static_cast<State*>(ref);
    const auto ids = allDevices();
    if (std::find(ids.begin(), ids.end(), s->device) == ids.end()) {
        s->alive.store(false);
    }
    return noErr;
}

namespace {
constexpr std::array<AudioObjectPropertyAddress, 4> kDeviceListened{{
    {kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain},
    {kAudioDeviceProcessorOverload, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain},
}};
constexpr AudioObjectPropertyAddress kDeviceList{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                                 kAudioObjectPropertyElementMain};
} // namespace

void CoreAudioCapture::State::addListeners() {
    useHalNotificationThread();
    for (const auto& a : kDeviceListened) {
        AudioObjectAddPropertyListener(device, &a, &State::deviceListener, this);
    }
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &kDeviceList, &State::systemListener, this);
    listening = true;
}

void CoreAudioCapture::State::removeListeners() {
    if (!listening) {
        return;
    }
    // Removing a listener waits for a running call of it to return, so `this` outlives every notification.
    for (const auto& a : kDeviceListened) {
        AudioObjectRemovePropertyListener(device, &a, &State::deviceListener, this);
    }
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &kDeviceList, &State::systemListener, this);
    listening = false;
}

CoreAudioCapture::CoreAudioCapture(std::string uid, std::uint32_t periodFrames)
    : uid_(std::move(uid))
    , periodFrames_(periodFrames)
    , state_(std::make_unique<State>()) {}

CoreAudioCapture::~CoreAudioCapture() {
    stop();
    State& s = *state_;
    s.removeListeners();
    if (s.unit != nullptr) {
        AudioUnitUninitialize(s.unit);
        AudioComponentInstanceDispose(s.unit);
        s.unit = nullptr;
    }
}

Result<CaptureFormat> CoreAudioCapture::open(CaptureSink& sink) {
    State& s = *state_;
    s.device = deviceForUid(uid_);
    if (s.device == kAudioObjectUnknown) {
        return fail("no input device with id '{}' (is it connected?)", uid_);
    }
    name_ = deviceString(s.device, kAudioObjectPropertyName);
    s.openedStreams = inputStreams(s.device);
    s.openedRate = nominalRate(s.device);
    s.channels = std::accumulate(s.openedStreams.begin(), s.openedStreams.end(), 0u);
    if (s.channels == 0) {
        return fail("'{}' has no input channels", name_);
    }

    AudioComponentDescription description{kAudioUnitType_Output, kAudioUnitSubType_HALOutput,
                                          kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (component == nullptr || AudioComponentInstanceNew(component, &s.unit) != noErr) {
        return fail("cannot create the HAL input unit for '{}'", name_);
    }
    const auto failWith = [&](const char* what, OSStatus status) -> Result<CaptureFormat> {
        AudioComponentInstanceDispose(s.unit);
        s.unit = nullptr;
        return fail("'{}': {} failed (OSStatus {})", name_, what, static_cast<int>(status));
    };
    UInt32 on = 1;
    UInt32 off = 0;
    OSStatus st = AudioUnitSetProperty(s.unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &on, sizeof(on));
    if (st != noErr) return failWith("enabling input", st);
    st = AudioUnitSetProperty(s.unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &off, sizeof(off));
    if (st != noErr) return failWith("disabling output", st);
    st = AudioUnitSetProperty(s.unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &s.device,
                              sizeof(s.device));
    if (st != noErr) return failWith("selecting the device", st);

    // The device side carries every channel of every input stream; ask for all of them, float, planar, at its rate.
    AudioStreamBasicDescription hardware{};
    UInt32 size = sizeof(hardware);
    st = AudioUnitGetProperty(s.unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &hardware, &size);
    if (st != noErr) return failWith("reading the device format", st);
    if (hardware.mChannelsPerFrame != s.channels) {
        log::warn("audio input '{}': the HAL unit reports {} channels and the stream configuration {}; using the unit's",
                  name_, hardware.mChannelsPerFrame, s.channels);
        s.channels = hardware.mChannelsPerFrame;
    }
    AudioStreamBasicDescription client{};
    client.mSampleRate = hardware.mSampleRate;
    client.mFormatID = kAudioFormatLinearPCM;
    client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    client.mBitsPerChannel = 32;
    client.mChannelsPerFrame = s.channels;
    client.mFramesPerPacket = 1;
    client.mBytesPerFrame = sizeof(float);
    client.mBytesPerPacket = sizeof(float);
    st = AudioUnitSetProperty(s.unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &client, sizeof(client));
    if (st != noErr) return failWith("setting the client format", st);
    st = AudioUnitSetProperty(s.unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &s.maxFrames,
                              sizeof(s.maxFrames));
    if (st != noErr) return failWith("setting the slice size", st);
    // The period AudioInput's miniaudio path asks for (ADR-1025's latency measurement). The device may not take it;
    // whatever it takes, the buffers hold maxFrames.
    {
        AudioObjectPropertyAddress a{kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kMain};
        UInt32 period = periodFrames_;
        AudioObjectSetPropertyData(s.device, &a, 0, nullptr, sizeof(period), &period);
    }
    s.buffers.assign(s.channels, std::vector<float>(s.maxFrames, 0.0f));
    s.pointers.resize(s.channels);
    for (std::uint32_t c = 0; c < s.channels; ++c) {
        s.pointers[c] = s.buffers[c].data();
    }
    s.listRaw.assign(offsetof(AudioBufferList, mBuffers) + sizeof(AudioBuffer) * s.channels, std::byte{0});
    s.list = reinterpret_cast<AudioBufferList*>(s.listRaw.data());
    s.list->mNumberBuffers = s.channels;
    AURenderCallbackStruct callback{&State::input, &s};
    st = AudioUnitSetProperty(s.unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &callback,
                              sizeof(callback));
    if (st != noErr) return failWith("installing the input callback", st);
    st = AudioUnitInitialize(s.unit);
    if (st != noErr) return failWith("initialising the unit", st);
    s.sink = &sink;
    s.alive.store(true);
    s.changed.store(false);
    s.addListeners();
    std::string streams;
    for (const std::uint32_t n : s.openedStreams) {
        streams += (streams.empty() ? "" : "+") + std::to_string(n);
    }
    log::info("audio input '{}' (Core Audio, {}): {} ch in {} stream(s) ({}), {:.0f} Hz", name_, uid_, s.channels,
              s.openedStreams.size(), streams, hardware.mSampleRate);
    return CaptureFormat{static_cast<std::uint32_t>(hardware.mSampleRate), s.channels};
}

Result<void> CoreAudioCapture::start() {
    State& s = *state_;
    if (s.unit == nullptr) {
        return fail("not open");
    }
    if (const OSStatus st = AudioOutputUnitStart(s.unit); st != noErr) {
        return fail("'{}': starting the input failed (OSStatus {})", name_, static_cast<int>(st));
    }
    s.running = true;
    return {};
}

void CoreAudioCapture::stop() {
    State& s = *state_;
    if (s.unit != nullptr && s.running) {
        AudioOutputUnitStop(s.unit); // synchronous: the input callback is not running when this returns
        s.running = false;
    }
}

bool CoreAudioCapture::connected() const {
    return state_->alive.load();
}

std::uint64_t CoreAudioCapture::overloads() const {
    return state_->overloads.load(std::memory_order_relaxed);
}

bool CoreAudioCapture::formatChanged() const {
    return state_->changed.load();
}

} // namespace avgen::audio

#else // !__APPLE__: the Core Audio backend does not exist; the miniaudio path serves every input.

namespace avgen::audio {

std::uint32_t CoreAudioInputDevice::channels() const {
    return std::accumulate(streamChannels.begin(), streamChannels.end(), 0u);
}
std::vector<CoreAudioInputDevice> coreAudioInputDevices() { return {}; }
bool coreAudioDevicePresent(const std::string&) { return false; }

struct CoreAudioCapture::State {};
CoreAudioCapture::CoreAudioCapture(std::string uid, std::uint32_t periodFrames)
    : uid_(std::move(uid)), periodFrames_(periodFrames) {}
CoreAudioCapture::~CoreAudioCapture() = default;
Result<CaptureFormat> CoreAudioCapture::open(CaptureSink&) { return fail("Core Audio capture is macOS only"); }
Result<void> CoreAudioCapture::start() { return fail("Core Audio capture is macOS only"); }
void CoreAudioCapture::stop() {}
bool CoreAudioCapture::connected() const { return false; }
std::uint64_t CoreAudioCapture::overloads() const { return 0; }
bool CoreAudioCapture::formatChanged() const { return false; }

} // namespace avgen::audio

#endif
