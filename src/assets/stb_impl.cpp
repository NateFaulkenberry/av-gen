// Single translation unit that compiles the stb_image / stb_image_write implementations (ADR-005).
// Keep this file free of engine code so warning flags for the third-party code stay isolated.
//
// PNG deflate goes through miniz (bundled with tinyexr, see cmake/Dependencies.cmake) instead of
// stb_image_write's built-in compressor: PNG encoding is what bounds offline sequences once the
// readback no longer stalls (ADR-020 revision), and miniz at a low level is about twice as fast as
// stb's compressor while still producing smaller files (measured: 1080p orb frames 0.87 MB vs
// 1.13 MB; level 6 is 30% smaller again but slower than stb).

#include <miniz.h>

#include <algorithm>
#include <cstdlib>

namespace {

// stb passes its `stbi_write_png_compression_level` (default 8) as `quality`; the level is capped
// at 2 (greedy matching), the throughput sweet spot for frame sequences (levels 1 and 2 encode at
// the same speed, 3+ start to cost frames).
unsigned char* avgenZlibCompress(unsigned char* data, int dataLen, int* outLen, int quality) {
    const mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(dataLen));
    auto* out = static_cast<unsigned char*>(std::malloc(bound));
    if (out == nullptr) {
        return nullptr;
    }
    mz_ulong size = bound;
    const int level = std::clamp(quality, 1, 2);
    if (mz_compress2(out, &size, data, static_cast<mz_ulong>(dataLen), level) != MZ_OK) {
        std::free(out);
        return nullptr;
    }
    *outLen = static_cast<int>(size);
    return out;
}

} // namespace

#define STBIW_ZLIB_COMPRESS avgenZlibCompress
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_NO_GIF // unused decoders: keep the binary and the attack surface small
#define STBI_NO_PIC
#define STBI_NO_PNM
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wnull-dereference"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wcomma"
#endif
#include <stb_image.h>
#include <stb_image_write.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
