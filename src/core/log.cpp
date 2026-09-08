#include "core/log.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace avgen::log {

namespace {
spdlog::level::level_enum toSpdlog(Level level) {
    switch (level) {
    case Level::Trace: return spdlog::level::trace;
    case Level::Debug: return spdlog::level::debug;
    case Level::Info: return spdlog::level::info;
    case Level::Warn: return spdlog::level::warn;
    case Level::Error: return spdlog::level::err;
    case Level::Off: return spdlog::level::off;
    }
    return spdlog::level::info;
}
} // namespace

void init(Level level) {
    static bool initialised = false;
    if (!initialised) {
        auto logger = spdlog::stderr_color_mt("avgen");
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
        initialised = true;
    }
    setLevel(level);
}

void setLevel(Level level) { spdlog::set_level(toSpdlog(level)); }

} // namespace avgen::log
