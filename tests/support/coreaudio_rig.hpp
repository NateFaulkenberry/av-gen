#pragma once

// macOS test rig for the Core Audio capture backend (stems-multichannel M6): a PRIVATE aggregate device (visible only
// to this process, gone when it exits or is destroyed -- nothing on the machine changes) and a known stereo tone
// played into BlackHole 2ch's output, which BlackHole loops to its input. Tests using it SKIP without BlackHole 2ch.

#if defined(__APPLE__)

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <atomic>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace avgen::testsupport {

inline AudioObjectID coreAudioDevice(const std::string& uid) {
    AudioObjectPropertyAddress a{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                 kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size);
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data());
    for (AudioObjectID id : ids) {
        AudioObjectPropertyAddress u{kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
        CFStringRef s = nullptr;
        UInt32 n = sizeof(s);
        if (AudioObjectGetPropertyData(id, &u, 0, nullptr, &n, &s) == noErr && s != nullptr) {
            char buf[256] = {};
            CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8);
            CFRelease(s);
            if (uid == buf) {
                return id;
            }
        }
    }
    return kAudioObjectUnknown;
}

// A private aggregate of the given sub-devices (by UID), the first one the clock. Destroyed with the object.
class PrivateAggregate {
public:
    PrivateAggregate(const std::string& uid, const std::vector<std::string>& subDevices) : uid_(uid) {
        CFMutableArrayRef subs = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
        for (const std::string& sub : subDevices) {
            CFMutableDictionaryRef d = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                 &kCFTypeDictionaryValueCallBacks);
            CFStringRef s = CFStringCreateWithCString(nullptr, sub.c_str(), kCFStringEncodingUTF8);
            CFDictionarySetValue(d, CFSTR(kAudioSubDeviceUIDKey), s);
            CFArrayAppendValue(subs, d);
            CFRelease(s);
            CFRelease(d);
        }
        CFMutableDictionaryRef desc = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                &kCFTypeDictionaryValueCallBacks);
        CFStringRef u = CFStringCreateWithCString(nullptr, uid.c_str(), kCFStringEncodingUTF8);
        CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceUIDKey), u);
        CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceNameKey), CFSTR("AV Gen test aggregate"));
        CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceSubDeviceListKey), subs);
        if (!subDevices.empty()) {
            CFStringRef main = CFStringCreateWithCString(nullptr, subDevices.front().c_str(), kCFStringEncodingUTF8);
            CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceMainSubDeviceKey), main);
            CFRelease(main);
        }
        int one = 1;
        CFNumberRef yes = CFNumberCreate(nullptr, kCFNumberIntType, &one);
        CFDictionarySetValue(desc, CFSTR(kAudioAggregateDeviceIsPrivateKey), yes);
        ok_ = AudioHardwareCreateAggregateDevice(desc, &id_) == noErr;
        CFRelease(yes);
        CFRelease(u);
        CFRelease(subs);
        CFRelease(desc);
    }
    ~PrivateAggregate() { destroy(); }
    PrivateAggregate(const PrivateAggregate&) = delete;
    PrivateAggregate& operator=(const PrivateAggregate&) = delete;
    void destroy() {
        if (ok_) {
            AudioHardwareDestroyAggregateDevice(id_);
            ok_ = false;
        }
    }
    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] const std::string& uid() const { return uid_; }

private:
    std::string uid_;
    AudioObjectID id_ = kAudioObjectUnknown;
    bool ok_ = false;
};

// Plays a stereo tone (L `leftHz`, R `rightHz`, amplitude 0.25) into an output device by UID until destroyed.
class TonePlayer {
public:
    TonePlayer(const std::string& outputUid, double leftHz, double rightHz) : left_(leftHz), right_(rightHz) {
        const AudioObjectID dev = coreAudioDevice(outputUid);
        if (dev == kAudioObjectUnknown) {
            return;
        }
        AudioComponentDescription d{kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple, 0, 0};
        if (AudioComponentInstanceNew(AudioComponentFindNext(nullptr, &d), &unit_) != noErr) {
            return;
        }
        AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev, sizeof dev);
        AudioStreamBasicDescription hw{};
        UInt32 size = sizeof hw;
        AudioUnitGetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &hw, &size);
        rate_ = hw.mSampleRate > 0 ? hw.mSampleRate : 48000.0;
        AudioStreamBasicDescription f{};
        f.mSampleRate = rate_;
        f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBitsPerChannel = 32;
        f.mChannelsPerFrame = 2;
        f.mFramesPerPacket = 1;
        f.mBytesPerFrame = 4;
        f.mBytesPerPacket = 4;
        AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
        AURenderCallbackStruct cb{&TonePlayer::render, this};
        AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
        playing_ = AudioUnitInitialize(unit_) == noErr && AudioOutputUnitStart(unit_) == noErr;
    }
    ~TonePlayer() {
        if (unit_ != nullptr) {
            AudioOutputUnitStop(unit_);
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
        }
    }
    TonePlayer(const TonePlayer&) = delete;
    TonePlayer& operator=(const TonePlayer&) = delete;
    [[nodiscard]] bool playing() const { return playing_; }

private:
    static OSStatus render(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames,
                           AudioBufferList* io) {
        auto* self = static_cast<TonePlayer*>(ref);
        auto* l = static_cast<float*>(io->mBuffers[0].mData);
        auto* r = static_cast<float*>(io->mBuffers[1].mData);
        for (UInt32 i = 0; i < frames; ++i) {
            const double t = static_cast<double>(self->frame_ + i) / self->rate_;
            l[i] = 0.25f * static_cast<float>(std::sin(2.0 * std::numbers::pi * self->left_ * t));
            r[i] = 0.25f * static_cast<float>(std::sin(2.0 * std::numbers::pi * self->right_ * t));
        }
        self->frame_ += frames;
        return noErr;
    }
    AudioUnit unit_ = nullptr;
    double left_, right_, rate_ = 48000.0;
    std::uint64_t frame_ = 0;
    bool playing_ = false;
};

} // namespace avgen::testsupport

#endif
