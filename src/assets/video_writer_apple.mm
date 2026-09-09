// Native video backend: AVFoundation's AVAssetWriter (ADR-020, research §8.2). Licence-clean and
// hardware-accelerated on Apple silicon; ProRes 4444 / ProRes 422 / H.264 / HEVC into .mov or .mp4.
//
// Frames arrive as RGBA8 and are swizzled on the CPU into 32BGRA pixel buffers from the adaptor's
// pool (BGRA is the format every VideoToolbox encoder accepts). Audio is read back through
// AVAssetReader as 16-bit PCM, re-timed so that audioOffsetSeconds lands on frame 0, fed
// incrementally half a second ahead of the video (AVAssetWriter interleaves its inputs and blocks
// a video input whose audio lags), trimmed to the video's length in finish(), and encoded to AAC
// (H.264/HEVC) or kept as PCM (ProRes).
//
// Compiled with ARC (src/CMakeLists.txt); CoreMedia/CoreVideo objects are released by hand.

#ifdef __APPLE__

#include "assets/video_writer_native.hpp"
#include "core/log.hpp"

#include <fmt/format.h>

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

namespace avgen::assets {

namespace {

constexpr int32_t kTimescale = 60000; // for non-integer frame rates (29.97, 23.976, ...)
constexpr int32_t kAudioTimescale = 1000000;

std::string lowerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string errorText(NSError* error) {
    if (error == nil) {
        return "unknown error";
    }
    std::string text = [[error localizedDescription] UTF8String];
    if (NSString* reason = [error localizedFailureReason]; reason != nil) {
        text += " (";
        text += [reason UTF8String];
        text += ")";
    }
    return text;
}

NSURL* fileUrl(const std::filesystem::path& path) {
    return [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
}

CMTime frameTime(std::size_t index, double fps) {
    const double integral = std::round(fps);
    if (std::abs(fps - integral) < 1e-9 && integral >= 1.0 && integral <= 1e6) {
        return CMTimeMake(static_cast<int64_t>(index), static_cast<int32_t>(integral));
    }
    const double ticks = static_cast<double>(index) * static_cast<double>(kTimescale) / fps;
    return CMTimeMake(static_cast<int64_t>(std::llround(ticks)), kTimescale);
}

// AVAssetWriter interleaves its inputs: at each chunk boundary the video input stops accepting
// samples until the audio input has been fed past that boundary. Audio is therefore kept this far
// ahead of the video (pumpAudio) and, whenever the video input blocks, served on demand
// (waitVideoReady) instead of only in finish().
constexpr double kAudioLookaheadSeconds = 0.5;
// An input that accepts nothing for this long is stuck (an encoder never takes seconds per frame);
// report it instead of hanging the render thread forever.
constexpr std::chrono::seconds kReadyTimeout{30};

enum class Ready { Yes, WriterFailed, TimedOut };

// Blocks until the input accepts more data.
Ready waitReady(AVAssetWriterInput* input, AVAssetWriter* writer) {
    const auto deadline = std::chrono::steady_clock::now() + kReadyTimeout;
    while (!input.readyForMoreMediaData) {
        if (writer.status != AVAssetWriterStatusWriting) {
            return Ready::WriterFailed;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            return Ready::TimedOut;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return Ready::Yes;
}

std::string notReadyText(Ready ready, const char* what, AVAssetWriter* writer) {
    if (ready == Ready::TimedOut) {
        return fmt::format("the {} input accepted no data for {} s", what, kReadyTimeout.count());
    }
    return fmt::format("writer failed: {}", errorText(writer.error));
}

// Synchronous track loading (the synchronous accessor is deprecated on macOS 15).
NSArray<AVAssetTrack*>* loadTracks(AVAsset* asset, AVMediaType mediaType, NSError** outError) {
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block NSArray<AVAssetTrack*>* result = nil;
    __block NSError* loadError = nil;
    [asset loadTracksWithMediaType:mediaType
                 completionHandler:^(NSArray<AVAssetTrack*>* tracks, NSError* error) {
                   result = tracks;
                   loadError = error;
                   dispatch_semaphore_signal(done);
                 }];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    if (outError != nullptr) {
        *outError = loadError;
    }
    return result;
}

// quality 0..100 -> bits per pixel per frame (0.02 .. 0.32 for H.264; HEVC needs ~35% less).
long long targetBitRate(const VideoSettings& settings, bool hevc) {
    const double q = static_cast<double>(std::clamp(settings.quality, 0, 100)) / 100.0;
    const double bitsPerPixel = (0.02 + q * q * 0.30) * (hevc ? 0.65 : 1.0);
    const double bps = static_cast<double>(settings.width) * static_cast<double>(settings.height) *
                       settings.fps * bitsPerPixel;
    return std::max(200000LL, std::llround(bps));
}

// CVPixelBuffer attributes built with plain CF calls (no bridged casts needed).
NSDictionary* pixelBufferAttributes(std::uint32_t width, std::uint32_t height) {
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const auto set = [attrs](CFStringRef key, int32_t value) {
        CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
        CFDictionarySetValue(attrs, key, number);
        CFRelease(number);
    };
    set(kCVPixelBufferPixelFormatTypeKey, static_cast<int32_t>(kCVPixelFormatType_32BGRA));
    set(kCVPixelBufferWidthKey, static_cast<int32_t>(width));
    set(kCVPixelBufferHeightKey, static_cast<int32_t>(height));
    return CFBridgingRelease(attrs);
}

class NativeWriter final : public VideoWriter {
public:
    NativeWriter(std::filesystem::path file, VideoSettings settings)
        : path_(std::move(file))
        , settings_(std::move(settings)) {}

    ~NativeWriter() override {
        if (!finished_) {
            cancel();
        }
        releasePendingAudio();
    }
    NativeWriter(const NativeWriter&) = delete;
    NativeWriter& operator=(const NativeWriter&) = delete;

    Result<void> open() {
        @autoreleasepool {
            return openInPool();
        }
    }

    Result<void> writeFrame(std::span<const std::uint8_t> rgba) override {
        @autoreleasepool {
            return writeFrameInPool(rgba);
        }
    }

    Result<void> finish() override {
        @autoreleasepool {
            return finishInPool();
        }
    }

    std::size_t framesWritten() const override { return frames_; }
    const std::filesystem::path& path() const override { return path_; }
    std::string backendName() const override { return "native"; }

private:
    std::unexpected<Error> setError(std::string message) {
        error_ = Error{std::move(message)};
        return std::unexpected(*error_);
    }

    Result<void> openInPool() {
        const std::string ext = lowerExtension(path_);
        AVFileType fileType = nil;
        if (ext == ".mov") {
            fileType = AVFileTypeQuickTimeMovie;
        } else if (ext == ".mp4" || ext == ".m4v") {
            fileType = AVFileTypeMPEG4;
        } else {
            return fail("video: the native backend writes .mov or .mp4, not '{}' ('{}')", ext,
                        path_.string());
        }

        AVVideoCodecType codecType = nil;
        bool prores = false;
        bool hevc = false;
        if (settings_.codec == "prores4444") {
            codecType = AVVideoCodecTypeAppleProRes4444;
            prores = true;
        } else if (settings_.codec == "prores422") {
            codecType = AVVideoCodecTypeAppleProRes422;
            prores = true;
        } else if (settings_.codec == "h264") {
            codecType = AVVideoCodecTypeH264;
        } else if (settings_.codec == "hevc") {
            codecType = AVVideoCodecTypeHEVC;
            hevc = true;
        } else {
            return fail("video: '{}' is not a native codec (prores4444, prores422, h264, hevc)",
                        settings_.codec);
        }
        if (prores && fileType != AVFileTypeQuickTimeMovie) {
            return fail("video: ProRes requires a .mov container, not '{}' ('{}')", ext, path_.string());
        }
        if (!prores && (settings_.width % 2 != 0 || settings_.height % 2 != 0)) {
            return fail("video: {} needs even dimensions (4:2:0 chroma), got {}x{}", settings_.codec,
                        settings_.width, settings_.height);
        }

        std::error_code ec;
        if (path_.has_parent_path()) {
            std::filesystem::create_directories(path_.parent_path(), ec);
        }
        std::filesystem::remove(path_, ec); // AVAssetWriter refuses to overwrite

        NSError* error = nil;
        writer_ = [[AVAssetWriter alloc] initWithURL:fileUrl(path_) fileType:fileType error:&error];
        if (writer_ == nil) {
            return fail("video: cannot create '{}': {}", path_.string(), errorText(error));
        }

        NSMutableDictionary* output = [NSMutableDictionary dictionary];
        output[AVVideoCodecKey] = codecType;
        output[AVVideoWidthKey] = @(settings_.width);
        output[AVVideoHeightKey] = @(settings_.height);
        if (!prores) {
            NSMutableDictionary* compression = [NSMutableDictionary dictionary];
            compression[AVVideoAverageBitRateKey] = @(targetBitRate(settings_, hevc));
            compression[AVVideoExpectedSourceFrameRateKey] = @(settings_.fps);
            if (!hevc) {
                compression[AVVideoMaxKeyFrameIntervalKey] =
                    @(std::max(1L, std::lround(settings_.fps * 2.0)));
                compression[AVVideoProfileLevelKey] = AVVideoProfileLevelH264HighAutoLevel;
            }
            output[AVVideoCompressionPropertiesKey] = compression;
        }
        if (![writer_ canApplyOutputSettings:output forMediaType:AVMediaTypeVideo]) {
            return fail("video: AVFoundation rejects {} {}x{} in {}", settings_.codec, settings_.width,
                        settings_.height, ext);
        }
        videoInput_ =
            [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:output];
        videoInput_.expectsMediaDataInRealTime = NO;
        adaptor_ = [AVAssetWriterInputPixelBufferAdaptor
            assetWriterInputPixelBufferAdaptorWithAssetWriterInput:videoInput_
                                       sourcePixelBufferAttributes:pixelBufferAttributes(settings_.width,
                                                                                         settings_.height)];
        if (![writer_ canAddInput:videoInput_]) {
            return fail("video: cannot add a {} video track to '{}'", settings_.codec, path_.string());
        }
        [writer_ addInput:videoInput_];

        if (!settings_.audio.empty()) {
            if (auto audio = openAudio(prores); !audio) {
                return audio;
            }
        }

        if (![writer_ startWriting]) {
            return fail("video: cannot start writing '{}': {}", path_.string(), errorText(writer_.error));
        }
        [writer_ startSessionAtSourceTime:kCMTimeZero];
        log::info("video: native backend, {} {}x{} @ {} fps -> {}{}", settings_.codec, settings_.width,
                  settings_.height, settings_.fps, path_.string(),
                  audioInput_ != nil ? fmt::format(" (+ audio from {})", settings_.audio.string()) : "");
        return {};
    }

    // Opens the audio file with AVAssetReader and adds the matching writer input. The first sample
    // buffer is read here to learn the source format (sample rate, channels, layout).
    Result<void> openAudio(bool prores) {
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:fileUrl(settings_.audio)
                                                options:@{
                                                    AVURLAssetPreferPreciseDurationAndTimingKey : @YES
                                                }];
        NSError* error = nil;
        NSArray<AVAssetTrack*>* tracks = loadTracks(asset, AVMediaTypeAudio, &error);
        if (tracks == nil || tracks.count == 0) {
            return fail("video: '{}' has no audio track{}", settings_.audio.string(),
                        error != nil ? ": " + errorText(error) : std::string());
        }
        reader_ = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (reader_ == nil) {
            return fail("video: cannot read '{}': {}", settings_.audio.string(), errorText(error));
        }
        NSDictionary* pcm = @{
            AVFormatIDKey : @(kAudioFormatLinearPCM),
            AVLinearPCMBitDepthKey : @16,
            AVLinearPCMIsFloatKey : @NO,
            AVLinearPCMIsBigEndianKey : @NO,
            AVLinearPCMIsNonInterleaved : @NO,
        };
        audioOutput_ =
            [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:tracks[0] outputSettings:pcm];
        audioOutput_.alwaysCopiesSampleData = NO;
        if (![reader_ canAddOutput:audioOutput_]) {
            return fail("video: cannot decode '{}' to PCM", settings_.audio.string());
        }
        [reader_ addOutput:audioOutput_];
        const double start = std::max(0.0, settings_.audioOffsetSeconds);
        reader_.timeRange =
            CMTimeRangeMake(CMTimeMakeWithSeconds(start, kAudioTimescale), kCMTimePositiveInfinity);
        if (![reader_ startReading]) {
            return fail("video: cannot read audio '{}': {}", settings_.audio.string(),
                        errorText(reader_.error));
        }
        pendingAudio_ = [audioOutput_ copyNextSampleBuffer];
        if (pendingAudio_ == nullptr) {
            log::warn("video: '{}' has no audio at offset {} s; writing video only", settings_.audio.string(),
                      settings_.audioOffsetSeconds);
            [reader_ cancelReading];
            reader_ = nil;
            audioOutput_ = nil;
            return {};
        }

        double sampleRate = 48000.0;
        std::uint32_t channels = 2;
        NSData* layoutData = nil;
        if (CMAudioFormatDescriptionRef desc = CMSampleBufferGetFormatDescription(pendingAudio_);
            desc != nullptr) {
            if (const AudioStreamBasicDescription* asbd =
                    CMAudioFormatDescriptionGetStreamBasicDescription(desc);
                asbd != nullptr) {
                if (asbd->mSampleRate > 0.0) {
                    sampleRate = asbd->mSampleRate;
                }
                if (asbd->mChannelsPerFrame > 0) {
                    channels = asbd->mChannelsPerFrame;
                }
            }
            size_t layoutSize = 0;
            if (const AudioChannelLayout* layout =
                    CMAudioFormatDescriptionGetChannelLayout(desc, &layoutSize);
                layout != nullptr && layoutSize > 0) {
                layoutData = [NSData dataWithBytes:layout length:layoutSize];
            }
        }

        NSMutableDictionary* out = [NSMutableDictionary dictionary];
        out[AVSampleRateKey] = @(sampleRate);
        out[AVNumberOfChannelsKey] = @(channels);
        if (prores) {
            out[AVFormatIDKey] = @(kAudioFormatLinearPCM);
            out[AVLinearPCMBitDepthKey] = @16;
            out[AVLinearPCMIsFloatKey] = @NO;
            out[AVLinearPCMIsBigEndianKey] = @NO;
            out[AVLinearPCMIsNonInterleaved] = @NO;
        } else {
            out[AVFormatIDKey] = @(kAudioFormatMPEG4AAC);
            out[AVEncoderBitRateKey] = @(std::min<std::uint32_t>(320000, 96000 * channels));
        }
        if (channels > 2 && layoutData != nil) {
            out[AVChannelLayoutKey] = layoutData;
        }
        audioInput_ = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:out];
        audioInput_.expectsMediaDataInRealTime = NO;
        if (![writer_ canAddInput:audioInput_]) {
            audioInput_ = nil;
            return fail("video: cannot add a {} Hz {}-channel audio track to '{}'", sampleRate, channels,
                        path_.string());
        }
        [writer_ addInput:audioInput_];
        return {};
    }

    Result<void> writeFrameInPool(std::span<const std::uint8_t> rgba) {
        if (error_) {
            return std::unexpected(*error_);
        }
        if (finished_) {
            return fail("video: writeFrame() after finish() for '{}'", path_.string());
        }
        const std::size_t expected = static_cast<std::size_t>(settings_.width) * settings_.height * 4;
        if (rgba.size() != expected) {
            return setError(fmt::format("video: frame {} has {} bytes, expected {} ({}x{} RGBA8)", frames_,
                                        rgba.size(), expected, settings_.width, settings_.height));
        }
        const CMTime pts = frameTime(frames_, settings_.fps);
        if (audioInput_ != nil && !audioDone_) {
            const CMTime lookahead = CMTimeMakeWithSeconds(kAudioLookaheadSeconds, kAudioTimescale);
            if (auto pumped = pumpAudio(CMTimeAdd(pts, lookahead), kCMTimePositiveInfinity); !pumped) {
                return setError(pumped.error().message);
            }
        }
        if (auto ready = waitVideoReady(); !ready) {
            return setError(ready.error().message);
        }

        CVPixelBufferRef buffer = nullptr;
        CVReturn rc = kCVReturnError;
        if (CVPixelBufferPoolRef pool = adaptor_.pixelBufferPool; pool != nullptr) {
            rc = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &buffer);
        }
        if (rc != kCVReturnSuccess || buffer == nullptr) {
            rc = CVPixelBufferCreate(kCFAllocatorDefault, settings_.width, settings_.height,
                                     kCVPixelFormatType_32BGRA, nullptr, &buffer);
        }
        if (rc != kCVReturnSuccess || buffer == nullptr) {
            return setError(fmt::format("video: cannot allocate a {}x{} pixel buffer (CVReturn {})",
                                        settings_.width, settings_.height, rc));
        }

        CVPixelBufferLockBaseAddress(buffer, 0);
        auto* base = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddress(buffer));
        const std::size_t stride = CVPixelBufferGetBytesPerRow(buffer);
        const std::size_t rowBytes = static_cast<std::size_t>(settings_.width) * 4;
        for (std::uint32_t y = 0; y < settings_.height; ++y) {
            const std::uint8_t* src = rgba.data() + static_cast<std::size_t>(y) * rowBytes;
            std::uint8_t* dst = base + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < settings_.width; ++x) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
                src += 4;
                dst += 4;
            }
        }
        CVPixelBufferUnlockBaseAddress(buffer, 0);

        const BOOL ok = [adaptor_ appendPixelBuffer:buffer withPresentationTime:pts];
        CVPixelBufferRelease(buffer);
        if (!ok) {
            return setError(fmt::format("video: appending frame {} to '{}' failed: {}", frames_,
                                        path_.string(), errorText(writer_.error)));
        }
        ++frames_;
        return {};
    }

    Result<void> finishInPool() {
        if (finished_) {
            return fail("video: finish() called twice for '{}'", path_.string());
        }
        finished_ = true;
        if (error_) {
            cancel();
            return std::unexpected(*error_);
        }
        if (frames_ == 0) {
            cancel();
            return fail("video: no frames were written to '{}'", path_.string());
        }
        const CMTime end = frameTime(frames_, settings_.fps);
        [videoInput_ markAsFinished];
        if (audioInput_ != nil) {
            auto pumped = pumpAudio(end, end);
            [audioInput_ markAsFinished];
            [reader_ cancelReading];
            if (!pumped) {
                cancel();
                return pumped;
            }
        }
        [writer_ endSessionAtSourceTime:end];

        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [writer_ finishWritingWithCompletionHandler:^{
          dispatch_semaphore_signal(done);
        }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        if (writer_.status != AVAssetWriterStatusCompleted) {
            return fail("video: finishing '{}' failed: {}", path_.string(), errorText(writer_.error));
        }
        log::info("video: wrote {} frames ({:.3f} s) to {}", frames_, CMTimeGetSeconds(end), path_.string());
        return {};
    }

    // Appends the next audio buffer, cut at `limit` (+infinity while frames are still arriving, the
    // video's end in finish(), where the last buffer is trimmed to fit). Timestamps are shifted so
    // that audioOffsetSeconds in the file becomes 0 in the movie. Returns false once there is
    // nothing more to append (reader exhausted or `limit` reached).
    Result<bool> appendNextAudio(CMTime limit) {
        if (audioDone_) {
            return false;
        }
        const CMTime offset = CMTimeMakeWithSeconds(settings_.audioOffsetSeconds, kAudioTimescale);
        CMSampleBufferRef sample = pendingAudio_ != nullptr ? std::exchange(pendingAudio_, nullptr)
                                                            : [audioOutput_ copyNextSampleBuffer];
        if (sample == nullptr) {
            audioDone_ = true;
            if (reader_.status == AVAssetReaderStatusFailed) {
                return fail("video: reading '{}' failed: {}", settings_.audio.string(),
                            errorText(reader_.error));
            }
            return false;
        }
        const CMTime pts = CMTimeSubtract(CMSampleBufferGetPresentationTimeStamp(sample), offset);
        if (CMTimeCompare(pts, limit) >= 0) {
            CFRelease(sample);
            audioDone_ = true;
            return false;
        }
        CMTime sampleEnd = CMTimeAdd(pts, CMSampleBufferGetDuration(sample));
        const CMItemCount numSamples = CMSampleBufferGetNumSamples(sample);
        if (CMTimeCompare(sampleEnd, limit) > 0 && numSamples > 1) {
            const double keepSeconds = CMTimeGetSeconds(CMTimeSubtract(limit, pts));
            const double perSample =
                CMTimeGetSeconds(CMSampleBufferGetDuration(sample)) / static_cast<double>(numSamples);
            const auto keep = static_cast<CMItemCount>(
                std::clamp(std::llround(keepSeconds / perSample), 0LL, static_cast<long long>(numSamples)));
            if (keep == 0) {
                CFRelease(sample);
                audioDone_ = true;
                return false;
            }
            CMSampleBufferRef trimmed = nullptr;
            if (CMSampleBufferCopySampleBufferForRange(kCFAllocatorDefault, sample, CFRangeMake(0, keep),
                                                       &trimmed) == noErr &&
                trimmed != nullptr) {
                CFRelease(sample);
                sample = trimmed;
                sampleEnd = CMTimeAdd(pts, CMSampleBufferGetDuration(sample));
            }
        }

        CMSampleTimingInfo timing{};
        timing.duration = CMSampleBufferGetDuration(sample);
        timing.presentationTimeStamp = pts;
        timing.decodeTimeStamp = kCMTimeInvalid;
        CMSampleBufferRef retimed = nullptr;
        const OSStatus status =
            CMSampleBufferCreateCopyWithNewTiming(kCFAllocatorDefault, sample, 1, &timing, &retimed);
        CFRelease(sample);
        if (status != noErr || retimed == nullptr) {
            return fail("video: cannot re-time audio samples (status {})", status);
        }
        if (const Ready ready = waitReady(audioInput_, writer_); ready != Ready::Yes) {
            CFRelease(retimed);
            return fail("video: while muxing audio into '{}': {}", path_.string(),
                        notReadyText(ready, "audio", writer_));
        }
        const BOOL ok = [audioInput_ appendSampleBuffer:retimed];
        CFRelease(retimed);
        if (!ok) {
            return fail("video: appending audio to '{}' failed: {}", path_.string(),
                        errorText(writer_.error));
        }
        audioEnd_ = sampleEnd;
        if (CMTimeCompare(sampleEnd, limit) >= 0) {
            audioDone_ = true;
        }
        return true;
    }

    // Appends audio until the appended audio reaches `target` or there is nothing more to append.
    Result<void> pumpAudio(CMTime target, CMTime limit) {
        while (!audioDone_ && CMTimeCompare(audioEnd_, target) < 0) {
            auto appended = appendNextAudio(limit);
            if (!appended) {
                return std::unexpected(appended.error());
            }
            if (!*appended) {
                break;
            }
        }
        return {};
    }

    // Blocks until the video input accepts a frame. While it refuses, the writer's interleaver is
    // usually asking for audio (the audio input reads ready), so audio is served on demand here;
    // without that the two sides wait for each other forever.
    Result<void> waitVideoReady() {
        auto deadline = std::chrono::steady_clock::now() + kReadyTimeout;
        while (!videoInput_.readyForMoreMediaData) {
            if (writer_.status != AVAssetWriterStatusWriting) {
                return fail("video: before frame {} of '{}': writer failed: {}", frames_, path_.string(),
                            errorText(writer_.error));
            }
            if (audioInput_ != nil && !audioDone_ && audioInput_.readyForMoreMediaData) {
                auto appended = appendNextAudio(kCMTimePositiveInfinity);
                if (!appended) {
                    return std::unexpected(appended.error());
                }
                if (*appended) {
                    deadline = std::chrono::steady_clock::now() + kReadyTimeout; // progress
                    continue;
                }
            }
            if (std::chrono::steady_clock::now() > deadline) {
                return fail("video: before frame {} of '{}': {}", frames_, path_.string(),
                            notReadyText(Ready::TimedOut, "video", writer_));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return {};
    }

    void releasePendingAudio() {
        if (pendingAudio_ != nullptr) {
            CFRelease(pendingAudio_);
            pendingAudio_ = nullptr;
        }
    }

    // Drops the partial output (AVAssetWriter removes the file) and stops the audio reader.
    void cancel() {
        releasePendingAudio();
        if (reader_ != nil && reader_.status == AVAssetReaderStatusReading) {
            [reader_ cancelReading];
        }
        if (writer_ != nil && writer_.status == AVAssetWriterStatusWriting) {
            [writer_ cancelWriting];
        }
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    std::filesystem::path path_;
    VideoSettings settings_;
    AVAssetWriter* writer_ = nil;
    AVAssetWriterInput* videoInput_ = nil;
    AVAssetWriterInputPixelBufferAdaptor* adaptor_ = nil;
    AVAssetReader* reader_ = nil;
    AVAssetReaderTrackOutput* audioOutput_ = nil;
    AVAssetWriterInput* audioInput_ = nil;
    CMSampleBufferRef pendingAudio_ = nullptr;
    CMTime audioEnd_ = kCMTimeZero; // end of the last appended audio buffer (movie time)
    bool audioDone_ = false;        // reader exhausted or the video's end reached
    std::size_t frames_ = 0;
    bool finished_ = false;
    std::optional<Error> error_;
};

} // namespace

bool hasNativeVideo() {
    return true;
}

std::vector<std::string> nativeCodecs() {
    return {"prores4444", "prores422", "h264", "hevc"};
}

Result<VideoInfo> probeVideo(const std::filesystem::path& file) {
    @autoreleasepool {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec)) {
            return fail("video: '{}' does not exist", file.string());
        }
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:fileUrl(file)
                                                options:@{
                                                    AVURLAssetPreferPreciseDurationAndTimingKey : @YES
                                                }];
        NSError* error = nil;
        NSArray<AVAssetTrack*>* video = loadTracks(asset, AVMediaTypeVideo, &error);
        if (video == nil || video.count == 0) {
            return fail("video: '{}' has no video track{}", file.string(),
                        error != nil ? ": " + errorText(error) : std::string());
        }
        NSArray<AVAssetTrack*>* audio = loadTracks(asset, AVMediaTypeAudio, nullptr);
        AVAssetTrack* track = video[0];

        VideoInfo info;
        const CGSize size = track.naturalSize;
        info.width = static_cast<std::uint32_t>(std::llround(size.width));
        info.height = static_cast<std::uint32_t>(std::llround(size.height));
        info.durationSeconds = CMTimeGetSeconds(track.timeRange.duration);
        info.hasAudio = audio != nil && audio.count > 0;

        // Exact frame count: a pass-through read of the video samples (no decoding). Buffers without
        // samples are edit-list markers (e.g. the B-frame reorder delay of H.264/HEVC), not frames.
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (reader == nil) {
            return fail("video: cannot read '{}': {}", file.string(), errorText(error));
        }
        AVAssetReaderTrackOutput* output =
            [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:nil];
        [reader addOutput:output];
        if (![reader startReading]) {
            return fail("video: cannot read '{}': {}", file.string(), errorText(reader.error));
        }
        while (CMSampleBufferRef sample = [output copyNextSampleBuffer]) {
            if (CMSampleBufferGetNumSamples(sample) > 0) {
                ++info.frames;
            }
            CFRelease(sample);
        }
        if (reader.status == AVAssetReaderStatusFailed) {
            return fail("video: reading '{}' failed: {}", file.string(), errorText(reader.error));
        }
        return info;
    }
}

namespace detail {

Result<std::unique_ptr<VideoWriter>> openNativeVideoWriter(const std::filesystem::path& file,
                                                           const VideoSettings& settings) {
    auto writer = std::make_unique<NativeWriter>(file, settings);
    if (auto opened = writer->open(); !opened) {
        return std::unexpected(opened.error());
    }
    return std::unique_ptr<VideoWriter>(std::move(writer));
}

} // namespace detail

} // namespace avgen::assets

#endif // __APPLE__
