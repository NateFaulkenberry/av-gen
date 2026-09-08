#pragma once

// Logging facade. All engine code logs through this header so that the sink configuration
// lives in one place. Never call these from the real-time audio callback.

#include <spdlog/spdlog.h>

#include <string_view>

namespace avgen::log {

enum class Level { Trace, Debug, Info, Warn, Error, Off };

// Installs the default logger (stderr, coloured) at the given level. Safe to call more than once.
void init(Level level = Level::Info);

void setLevel(Level level);

template <typename... Args>
inline void trace(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    spdlog::trace(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void debug(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    spdlog::debug(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    spdlog::info(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    spdlog::warn(fmt, std::forward<Args>(args)...);
}
template <typename... Args>
inline void error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
    spdlog::error(fmt, std::forward<Args>(args)...);
}

} // namespace avgen::log
