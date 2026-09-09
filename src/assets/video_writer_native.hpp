#pragma once

// Internal seam between the backend selector (video_writer.cpp) and the native backend
// (video_writer_apple.mm on Apple platforms, video_writer_native_stub.cpp elsewhere). Not part of
// the public interface; include "assets/video_writer.hpp" instead.

#include "assets/video_writer.hpp"

namespace avgen::assets::detail {

[[nodiscard]] Result<std::unique_ptr<VideoWriter>> openNativeVideoWriter(const std::filesystem::path& file,
                                                                         const VideoSettings& settings);

} // namespace avgen::assets::detail
