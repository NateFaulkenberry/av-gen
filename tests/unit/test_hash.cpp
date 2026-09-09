// SHA-256 (core/hash): FIPS 180-4 vectors, streaming across block boundaries, file hashing.
#include "core/hash.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;

TEST_CASE("sha256 matches the known vectors", "[core][hash]") {
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const std::string million(1'000'000, 'a');
    CHECK(sha256Hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256 streams in arbitrary chunks and resets after finish", "[core][hash]") {
    std::vector<std::uint8_t> data(1000);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(i * 7 + 3);
    }
    const std::string oneShot = sha256Hex(data);
    Sha256 h;
    std::size_t offset = 0;
    const std::size_t chunks[] = {1, 63, 64, 65, 200, 7, 600};
    for (const std::size_t n : chunks) {
        const std::size_t take = std::min(n, data.size() - offset);
        h.update(std::span<const std::uint8_t>(data.data() + offset, take));
        offset += take;
    }
    h.update(std::span<const std::uint8_t>(data.data() + offset, data.size() - offset));
    CHECK(h.finishHex() == oneShot);
    h.update("abc");
    CHECK(h.finishHex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // 55, 56 and 64 bytes exercise the padding boundaries.
    CHECK(sha256Hex(std::string(55, 'x')) == sha256Hex(std::span<const std::uint8_t>(
                                               reinterpret_cast<const std::uint8_t*>(std::string(55, 'x').data()), 55)));
    CHECK(sha256Hex(std::string(56, 'x')).size() == 64);
    CHECK(sha256Hex(std::string(64, 'x')) != sha256Hex(std::string(63, 'x')));
}

TEST_CASE("sha256File hashes a file in chunks and reports missing files", "[core][hash]") {
    const auto path = std::filesystem::temp_directory_path() / "avgen_hash_test.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out << "abc";
    }
    auto hash = sha256File(path);
    REQUIRE(hash.has_value());
    CHECK(*hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    {
        // Larger than one chunk so the loop runs more than once.
        std::ofstream out(path, std::ios::binary);
        const std::string block(1 << 20, 'a');
        out << block << block << std::string(100, 'a');
    }
    hash = sha256File(path);
    REQUIRE(hash.has_value());
    CHECK(*hash == sha256Hex(std::string((2 << 20) + 100, 'a')));
    std::filesystem::remove(path);
    CHECK_FALSE(sha256File(path).has_value());
}
