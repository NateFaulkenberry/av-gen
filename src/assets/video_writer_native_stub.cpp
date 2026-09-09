// Native video backend for platforms without AVFoundation: reports itself as unavailable so the
// selector falls through to a user-supplied ffmpeg. Compiled only when not APPLE (src/CMakeLists.txt).

#include "assets/video_writer_native.hpp"

#ifndef __APPLE__

namespace avgen::assets {

bool hasNativeVideo() {
    return false;
}

std::vector<std::string> nativeCodecs() {
    return {};
}

Result<VideoInfo> probeVideo(const std::filesystem::path& file) {
    return fail("video: probing '{}' needs the native backend, which is not available on this platform",
                file.string());
}

namespace detail {

Result<std::unique_ptr<VideoWriter>> openNativeVideoWriter(const std::filesystem::path& file,
                                                           const VideoSettings& settings) {
    return fail("video: the native backend is not available on this platform (cannot write '{}' with {})",
                file.string(), settings.codec);
}

} // namespace detail

} // namespace avgen::assets

#endif // !__APPLE__
