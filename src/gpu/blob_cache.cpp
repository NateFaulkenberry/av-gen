#include "gpu/blob_cache.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

namespace avgen::gpu {

namespace fs = std::filesystem;

namespace {

constexpr char kMagic[8] = {'A', 'V', 'G', 'B', 'L', 'O', 'B', '\0'};

struct Header {
    char magic[8];
    std::uint32_t version;
    std::uint32_t reserved;
    std::uint64_t keySize;
    std::uint64_t valueSize;
    std::uint64_t valueChecksum;
};
static_assert(sizeof(Header) == 40);

std::uint64_t fnv1a(const void* data, std::size_t size, std::uint64_t seed) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

// A second, differently mixed 64-bit hash, so the file name is not two correlated FNV runs.
std::uint64_t mix64(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint64_t h = 0x9e3779b97f4a7c15ull ^ size;
    for (std::size_t i = 0; i < size; ++i) {
        h = (h ^ p[i]) * 0xff51afd7ed558ccdull;
        h ^= h >> 29;
    }
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

std::string sanitise(const std::string& s, std::size_t maxLen) {
    std::string out;
    for (const char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '-' || c == '_';
        out += ok ? c : '_';
    }
    if (out.size() > maxLen) {
        out.resize(maxLen);
    }
    return out;
}

} // namespace

std::string blobCacheHashHex(const void* data, std::size_t size) {
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                  static_cast<unsigned long long>(fnv1a(data, size, 0xcbf29ce484222325ull)),
                  static_cast<unsigned long long>(mix64(data, size)));
    return buf;
}

std::string BlobCache::directoryKey(const std::string& dawnCommit, const std::string& adapter,
                                    const std::string& osBuild) {
    const std::string all = dawnCommit + "|" + adapter + "|" + osBuild + "|v" + std::to_string(kFormatVersion);
    return "dawn-" + sanitise(dawnCommit, 12) + "-" + sanitise(adapter, 24) + "-" + sanitise(osBuild, 16) + "-" +
           blobCacheHashHex(all.data(), all.size()).substr(0, 8);
}

BlobCache::BlobCache(fs::path dir, std::uint64_t maxBytes) : dir_(std::move(dir)) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (ec) {
        log::warn("pipeline cache: cannot create {}: {}", dir_.string(), ec.message());
        return;
    }
    trim(maxBytes);
}

fs::path BlobCache::entryPath(const void* key, std::size_t keySize) const {
    const std::string name = blobCacheHashHex(key, keySize);
    // Two-level fan-out keeps any one directory small.
    return dir_ / name.substr(0, 2) / (name + ".bin");
}

std::size_t BlobCache::load(const void* key, std::size_t keySize, void* value, std::size_t valueSize) {
    const std::string keyString(static_cast<const char*>(key), keySize);
    if (value != nullptr) {
        // The second call: hand over what the first one validated.
        std::lock_guard lock(mutex_);
        auto it = pending_.find(keyString);
        if (it == pending_.end() || it->second.size() != valueSize) {
            return 0;
        }
        std::memcpy(value, it->second.data(), valueSize);
        pending_.erase(it);
        return valueSize;
    }
    const fs::path path = entryPath(key, keySize);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ++loadMisses_;
        return 0;
    }
    auto reject = [&](const char* why) -> std::size_t {
        in.close();
        std::error_code ec;
        fs::remove(path, ec);
        ++corrupt_;
        ++loadMisses_;
        log::warn("pipeline cache: discarded {} ({}); Dawn compiles it afresh", path.filename().string(), why);
        return 0;
    };
    Header h{};
    if (!in.read(reinterpret_cast<char*>(&h), sizeof(h))) {
        return reject("truncated header");
    }
    if (std::memcmp(h.magic, kMagic, sizeof(kMagic)) != 0) {
        return reject("not a cache entry");
    }
    if (h.version != kFormatVersion) {
        return reject("another format version");
    }
    if (h.keySize != keySize) {
        // Another key with the same file name: a collision, not damage. Leave the other entry be.
        ++loadMisses_;
        return 0;
    }
    std::string storedKey(keySize, '\0');
    if (!in.read(storedKey.data(), static_cast<std::streamsize>(keySize))) {
        return reject("truncated key");
    }
    if (storedKey != keyString) {
        ++loadMisses_;
        return 0;
    }
    if (h.valueSize > (1ull << 31)) {
        return reject("implausible size");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(h.valueSize));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return reject("truncated value");
    }
    if (in.peek() != std::char_traits<char>::eof()) {
        return reject("trailing bytes");
    }
    if (fnv1a(bytes.data(), bytes.size(), 0xcbf29ce484222325ull) != h.valueChecksum) {
        return reject("checksum mismatch");
    }
    ++loadHits_;
    bytesLoaded_ += bytes.size();
    const std::size_t size = bytes.size();
    {
        std::lock_guard lock(mutex_);
        pending_[keyString] = std::move(bytes);
    }
    // Touch it, so the trim's age order is least-recently-USED rather than least-recently-written.
    std::error_code ec;
    fs::last_write_time(path, fs::file_time_type::clock::now(), ec);
    return size;
}

void BlobCache::store(const void* key, std::size_t keySize, const void* value, std::size_t valueSize) {
    const fs::path path = entryPath(key, keySize);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path temp = path.parent_path() / (path.filename().string() + ".tmp" + std::to_string(tempCounter_++));
    Header h{};
    std::memcpy(h.magic, kMagic, sizeof(kMagic));
    h.version = kFormatVersion;
    h.keySize = keySize;
    h.valueSize = valueSize;
    h.valueChecksum = fnv1a(value, valueSize, 0xcbf29ce484222325ull);
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        out.write(static_cast<const char*>(key), static_cast<std::streamsize>(keySize));
        out.write(static_cast<const char*>(value), static_cast<std::streamsize>(valueSize));
        if (!out) {
            out.close();
            fs::remove(temp, ec);
            ++storeFailures_;
            return;
        }
    }
    fs::rename(temp, path, ec); // atomic on one volume: readers see the old entry or the new one
    if (ec) {
        fs::remove(temp, ec);
        ++storeFailures_;
        return;
    }
    ++stores_;
    bytesStored_ += valueSize;
}

void BlobCache::trim(std::uint64_t maxBytes) {
    struct Entry {
        fs::path path;
        fs::file_time_type time;
        std::uint64_t size;
    };
    std::vector<Entry> entries;
    std::uint64_t total = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir_, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const fs::path p = it->path();
        if (p.filename().string().find(".tmp") != std::string::npos) {
            fs::remove(p, ec); // a store that never finished
            continue;
        }
        const std::uint64_t size = it->file_size(ec);
        entries.push_back({p, it->last_write_time(ec), size});
        total += size;
    }
    if (total <= maxBytes) {
        return;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.time < b.time; });
    for (const Entry& e : entries) {
        if (total <= maxBytes * 3 / 4) {
            break;
        }
        fs::remove(e.path, ec);
        total -= e.size;
    }
}

BlobCacheStats BlobCache::stats() const {
    return {loadHits_.load(), loadMisses_.load(), corrupt_.load(), stores_.load(), storeFailures_.load(),
            bytesLoaded_.load(), bytesStored_.load()};
}

} // namespace avgen::gpu
