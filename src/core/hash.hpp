#pragma once

// SHA-256 (FIPS 180-4), a small in-house implementation used to identify asset files by content
// (project asset relinking, ADR-019 follow-up). Streaming interface plus one-call helpers.

#include "core/error.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace avgen {

class Sha256 {
public:
    Sha256();
    void update(std::span<const std::uint8_t> bytes);
    void update(std::string_view text);
    // Finishes the hash and returns the 32-byte digest. The object is reset afterwards.
    [[nodiscard]] std::array<std::uint8_t, 32> finish();
    [[nodiscard]] std::string finishHex(); // lower-case, 64 characters
    void reset();

private:
    void processBlock(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t totalBytes_ = 0;
};

[[nodiscard]] std::string sha256Hex(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::string sha256Hex(std::string_view text);
// Streams the file in 1 MB chunks. Errors: cannot open / read.
[[nodiscard]] Result<std::string> sha256File(const std::filesystem::path& path);
[[nodiscard]] std::string hexString(std::span<const std::uint8_t> bytes);

} // namespace avgen
