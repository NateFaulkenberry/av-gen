#pragma once
// ADR-1811: Dawn's persistent blob cache, backed by files.
//
// Dawn asks its embedder to keep compiled shader artefacts between processes
// (`wgpu::DawnCacheDeviceDescriptor`: a load and a store callback over opaque key/value blobs). AV Gen
// did not, so every new build -- every agent worktree, every owner rebuild -- started with an empty
// cache and paid its whole shader warm-up again (the render audit measured 16-26 s on the first run of
// a build). This is that store: one file per entry, in a directory keyed by everything that could make
// an old entry wrong for this process.
//
// Safety, in the order a reader should worry about it:
//   - The directory key is the Dawn commit, the adapter and the OS build (`directoryKey`): a Dawn
//     upgrade, another GPU or an OS update starts a fresh directory instead of reading the old one.
//   - Every file carries the format version, the FULL key and a checksum of the value. A load
//     compares all three and treats any mismatch -- a truncated write, a flipped bit, a hash
//     collision between two keys -- as a miss and deletes the file. Dawn then compiles as if there
//     were no cache and stores a good entry.
//   - Stores write a temporary file and rename it over the entry, so a crash mid-write leaves either
//     the old entry or none, never half of one.
//   - The cache is bounded (`kMaxBytes`): the oldest entries go first when it is opened.
//
// Thread safety: Dawn calls load and store from its worker threads (asynchronous pipeline creation,
// ADR-1562), so both are safe to call concurrently.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace avgen::gpu {

struct BlobCacheStats {
    std::uint64_t loadHits = 0;
    std::uint64_t loadMisses = 0;
    std::uint64_t corrupt = 0;   // entries rejected (and deleted) on load
    std::uint64_t stores = 0;
    std::uint64_t storeFailures = 0;
    std::uint64_t bytesLoaded = 0;
    std::uint64_t bytesStored = 0;
};

class BlobCache {
public:
    static constexpr std::uint32_t kFormatVersion = 1;
    static constexpr std::uint64_t kMaxBytes = 1024ull * 1024ull * 1024ull; // 1 GiB

    // Opens (creating if needed) the cache in `dir`, and trims it to `maxBytes` by age.
    explicit BlobCache(std::filesystem::path dir, std::uint64_t maxBytes = kMaxBytes);

    // Dawn's protocol: a load with `value == nullptr` asks for the size; a second call with a buffer
    // of that size asks for the bytes. Returns 0 on a miss (and on a corrupt entry, which it deletes).
    std::size_t load(const void* key, std::size_t keySize, void* value, std::size_t valueSize);
    void store(const void* key, std::size_t keySize, const void* value, std::size_t valueSize);

    [[nodiscard]] const std::filesystem::path& directory() const { return dir_; }
    [[nodiscard]] BlobCacheStats stats() const;

    // The directory name for a Dawn commit, an adapter description and an OS build: every input that
    // can make a cached artefact wrong for this process. Sanitised for a file name, hashed where long.
    [[nodiscard]] static std::string directoryKey(const std::string& dawnCommit, const std::string& adapter,
                                                  const std::string& osBuild);

    // The file an entry lives in (exposed so a test can damage one).
    [[nodiscard]] std::filesystem::path entryPath(const void* key, std::size_t keySize) const;

private:
    void trim(std::uint64_t maxBytes);

    std::filesystem::path dir_;
    mutable std::mutex mutex_;
    // Dawn reads an entry in two calls (size, then bytes); the first call's validated value is kept
    // here for the second so the file is read and checked once.
    std::unordered_map<std::string, std::vector<std::uint8_t>> pending_;
    std::atomic<std::uint64_t> loadHits_{0}, loadMisses_{0}, corrupt_{0}, stores_{0}, storeFailures_{0},
        bytesLoaded_{0}, bytesStored_{0};
    std::atomic<std::uint64_t> tempCounter_{0};
};

// 128 bits of FNV-1a over two seeds, as 32 hex digits: an entry's file name. Collisions are survivable
// (the full key is compared on load) but should not be routine.
[[nodiscard]] std::string blobCacheHashHex(const void* data, std::size_t size);

} // namespace avgen::gpu
