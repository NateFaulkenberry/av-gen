// Single translation unit that compiles the miniaudio implementation (ADR-003).
// Keep this file free of engine code so warning flags for miniaudio stay isolated.

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING_UNUSED 0
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
#endif
#include <miniaudio.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
