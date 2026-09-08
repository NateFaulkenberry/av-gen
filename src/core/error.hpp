#pragma once

// Error type and Result alias used at every fallible module boundary (file loading, device
// creation, shader compilation). Exceptions are reserved for programming errors.

#include <fmt/format.h>

#include <expected>
#include <string>
#include <utility>

namespace avgen {

struct Error {
    std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

template <typename... Args>
[[nodiscard]] inline std::unexpected<Error> fail(fmt::format_string<Args...> fmt, Args&&... args) {
    return std::unexpected(Error{fmt::format(fmt, std::forward<Args>(args)...)});
}

[[nodiscard]] inline std::unexpected<Error> fail(std::string message) {
    return std::unexpected(Error{std::move(message)});
}

} // namespace avgen
