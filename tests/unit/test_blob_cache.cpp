// ADR-1811: the persistent pipeline cache's file store. Dawn trusts what a load hands back, so the store must never
// return bytes it did not write for that key: a damaged, truncated or foreign entry is a miss (and is deleted), and
// Dawn then compiles afresh.

#include "gpu/blob_cache.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / ("avgen-blob-cache-test-" + std::string(name) + "-" +
                                                      std::to_string(std::random_device{}()));
    fs::remove_all(dir);
    return dir;
}

std::vector<std::uint8_t> loadAll(gpu::BlobCache& cache, const std::string& key) {
    const std::size_t size = cache.load(key.data(), key.size(), nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::vector<std::uint8_t> out(size);
    REQUIRE(cache.load(key.data(), key.size(), out.data(), out.size()) == size);
    return out;
}

} // namespace

TEST_CASE("the pipeline cache returns exactly what was stored, across instances", "[adr1811][blobcache]") {
    const fs::path dir = freshDir("roundtrip");
    const std::string key = "a key that Dawn would make: shader source, entry point, layout";
    const std::vector<std::uint8_t> value = {1, 2, 3, 250, 0, 7};
    {
        gpu::BlobCache cache(dir);
        CHECK(loadAll(cache, key).empty()); // a miss before the store
        cache.store(key.data(), key.size(), value.data(), value.size());
        CHECK(cache.stats().stores == 1);
    }
    gpu::BlobCache again(dir); // another process, in effect
    CHECK(loadAll(again, key) == value);
    CHECK(again.stats().loadHits == 1);
    CHECK(loadAll(again, key + "!").empty());
    fs::remove_all(dir);
}

TEST_CASE("a damaged pipeline cache entry is a miss, and is deleted", "[adr1811][blobcache]") {
    const fs::path dir = freshDir("damage");
    const std::string key = "key";
    const std::vector<std::uint8_t> value(4096, 0x5a);
    gpu::BlobCache cache(dir);
    const fs::path entry = cache.entryPath(key.data(), key.size());

    SECTION("a flipped bit in the value") {
        cache.store(key.data(), key.size(), value.data(), value.size());
        {
            std::fstream f(entry, std::ios::in | std::ios::out | std::ios::binary);
            f.seekp(-10, std::ios::end);
            f.put('\x01');
        }
        CHECK(loadAll(cache, key).empty());
        CHECK(cache.stats().corrupt == 1);
        CHECK_FALSE(fs::exists(entry));
        // and Dawn's next store is good again
        cache.store(key.data(), key.size(), value.data(), value.size());
        CHECK(loadAll(cache, key) == value);
    }
    SECTION("a truncated entry (a crash mid-copy, a full disk)") {
        cache.store(key.data(), key.size(), value.data(), value.size());
        fs::resize_file(entry, fs::file_size(entry) - 100);
        CHECK(loadAll(cache, key).empty());
        CHECK(cache.stats().corrupt == 1);
        CHECK_FALSE(fs::exists(entry));
    }
    SECTION("garbage where an entry should be") {
        fs::create_directories(entry.parent_path());
        std::ofstream(entry, std::ios::binary) << "this is not a cache entry at all, just some bytes";
        CHECK(loadAll(cache, key).empty());
        CHECK(cache.stats().corrupt == 1);
        CHECK_FALSE(fs::exists(entry));
    }
    SECTION("an entry written for another key under the same file name is a miss, and is kept") {
        // A hash collision: copy another key's entry into this key's file.
        const std::string other = "another key";
        cache.store(other.data(), other.size(), value.data(), value.size());
        fs::create_directories(entry.parent_path());
        fs::copy_file(cache.entryPath(other.data(), other.size()), entry, fs::copy_options::overwrite_existing);
        CHECK(loadAll(cache, key).empty());
        CHECK(cache.stats().corrupt == 0);
        CHECK(fs::exists(entry));
    }
    fs::remove_all(dir);
}

TEST_CASE("the pipeline cache directory changes with Dawn, the adapter and the OS", "[adr1811][blobcache]") {
    const std::string base = gpu::BlobCache::directoryKey("c4e47b5e", "Apple M2 Max", "25G100");
    CHECK(base == gpu::BlobCache::directoryKey("c4e47b5e", "Apple M2 Max", "25G100"));
    CHECK(base != gpu::BlobCache::directoryKey("d00dfeed", "Apple M2 Max", "25G100"));
    CHECK(base != gpu::BlobCache::directoryKey("c4e47b5e", "Apple M3 Max", "25G100"));
    CHECK(base != gpu::BlobCache::directoryKey("c4e47b5e", "Apple M2 Max", "25H5"));
    CHECK(base.find('/') == std::string::npos);
    CHECK(base.find(' ') == std::string::npos);
}

TEST_CASE("the pipeline cache trims itself, oldest first, and clears unfinished writes", "[adr1811][blobcache]") {
    const fs::path dir = freshDir("trim");
    const std::vector<std::uint8_t> value(1000, 1);
    {
        gpu::BlobCache cache(dir);
        for (int i = 0; i < 20; ++i) {
            const std::string key = "k" + std::to_string(i);
            cache.store(key.data(), key.size(), value.data(), value.size());
            fs::last_write_time(cache.entryPath(key.data(), key.size()),
                                fs::file_time_type::clock::now() - std::chrono::hours(100 - i));
        }
        std::ofstream(dir / "leftover.bin.tmp7") << "half a write";
    }
    gpu::BlobCache trimmed(dir, 10000); // about ten entries' worth
    CHECK_FALSE(fs::exists(dir / "leftover.bin.tmp7"));
    const std::string oldest = "k0";
    const std::string newest = "k19";
    CHECK(loadAll(trimmed, oldest).empty());
    CHECK(loadAll(trimmed, newest) == value);
    std::uintmax_t total = 0;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (e.is_regular_file()) {
            total += e.file_size();
        }
    }
    CHECK(total <= 10000);
    fs::remove_all(dir);
}
