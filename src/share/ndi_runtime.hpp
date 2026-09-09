#pragma once

// Runtime loader for the NDI library (libndi). The SDK is never linked or shipped: the functions
// are resolved with dlopen/dlsym from the user's NDI Runtime / SDK installation and the few
// structures we need are declared here from the public SDK headers (NDI SDK 6, Processing.NDI.*
// headers; the layout has been stable since v4/v5). See docs/dependencies.md.

#include <cstdint>
#include <string>

namespace avgen::share::ndi {

// NDIlib_send_create_t
struct SendCreate {
    const char* ndiName = nullptr;
    const char* groups = nullptr;
    bool clockVideo = true;
    bool clockAudio = false;
};

// NDIlib_frame_format_type_e
enum FrameFormatType : std::int32_t { Interleaved = 0, Progressive = 1, Field0 = 2, Field1 = 3 };

constexpr std::uint32_t fourCC(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
}
constexpr std::uint32_t kFourCCBGRA = fourCC('B', 'G', 'R', 'A');
constexpr std::uint32_t kFourCCRGBA = fourCC('R', 'G', 'B', 'A');
constexpr std::int64_t kTimecodeSynthesize = INT64_MAX; // NDIlib_send_timecode_synthesize

// NDIlib_video_frame_v2_t
struct VideoFrameV2 {
    std::int32_t xres = 0;
    std::int32_t yres = 0;
    std::uint32_t fourCC = kFourCCBGRA;      // NDIlib_FourCC_video_type_e
    std::int32_t frameRateN = 60000;
    std::int32_t frameRateD = 1000;
    float pictureAspectRatio = 0.0f;         // 0 = square pixels
    std::int32_t frameFormatType = Progressive;
    std::int64_t timecode = kTimecodeSynthesize;
    std::uint8_t* data = nullptr;
    std::int32_t lineStrideInBytes = 0;      // union with data_size_in_bytes (compressed formats)
    const char* metadata = nullptr;
    std::int64_t timestamp = 0;
};
static_assert(sizeof(VideoFrameV2) == 72, "NDIlib_video_frame_v2_t layout");
static_assert(sizeof(SendCreate) == 24, "NDIlib_send_create_t layout");

using SendInstance = void*;

struct Runtime {
    bool (*initialize)() = nullptr;                                     // NDIlib_initialize
    void (*destroy)() = nullptr;                                        // NDIlib_destroy
    const char* (*version)() = nullptr;                                 // NDIlib_version
    SendInstance (*sendCreate)(const SendCreate*) = nullptr;            // NDIlib_send_create
    void (*sendDestroy)(SendInstance) = nullptr;                        // NDIlib_send_destroy
    void (*sendVideoV2)(SendInstance, const VideoFrameV2*) = nullptr;   // NDIlib_send_send_video_v2
    void (*sendVideoAsyncV2)(SendInstance, const VideoFrameV2*) = nullptr; // NDIlib_send_send_video_async_v2
    int (*sendGetNoConnections)(SendInstance, std::uint32_t) = nullptr; // NDIlib_send_get_no_connections

    std::string path;        // library that was loaded
    std::string versionText; // NDIlib_version()
    std::string error;       // why loading failed
    [[nodiscard]] bool loaded() const { return sendCreate != nullptr; }

    // Loads once per process (thread-safe); later calls return the cached result.
    static const Runtime& get();
};

// Candidate library paths in search order (for diagnostics and docs).
std::string searchDescription();

} // namespace avgen::share::ndi
