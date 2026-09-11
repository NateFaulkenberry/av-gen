// Per-thread heap allocation counters, interposed on the global operator new/delete.
//
// Why interpose rather than instrument call sites: the allocations that cost a frame are almost
// never the ones somebody wrote down. They are a std::string a formatter built, a vector a
// std::unordered_map grew, a shared_ptr control block, a std::function's captured state. Counting
// at the call site finds the allocations you already suspected; counting here finds the ones you
// did not.
//
// Why thread_local and not atomic: an increment is a load-add-store on a line no other thread
// touches, which is a handful of cycles and no bus traffic. That is cheap enough to leave compiled
// in on the audio callback, where the reading that matters is binary -- did this callback allocate
// at all -- and an instrument you have to rebuild to use is an instrument nobody uses.
//
// These replace libc++'s definitions for the whole program, including inside Dawn, SDL and ImGui.
// Every one forwards to malloc/free exactly as libc++'s own do, so the pairing rules are unchanged:
// anything malloc'd here is free'd here, and the aligned forms use the aligned allocator whose
// memory macOS's free() accepts.
//
// OFF BY DEFAULT (-DAVGEN_ALLOC_COUNTERS=ON). Not because a cost was measured -- it was not.
//
// A first A/B said the interposition cost about 0.4 ms of main-thread work per frame. That number
// was wrong: it compared two runs taken minutes apart on a machine running builds. Repeated back to
// back, three runs each, the counters-on build came out *faster* than the counters-off one
// (ui.build min 0.086 vs 0.114 ms), which is causally impossible and is therefore the answer -- on
// this machine the cost does not rise above run-to-run variance. The honest statement is that it
// was not resolvable, not that it is 0.4 ms.
//
// It is off by default anyway, for a reason needing no measurement: this replaces the global
// operator new and delete for the entire program, Dawn and SDL and ImGui included, and routes
// aligned allocations through posix_memalign instead of libc++'s own path. That is a real change to
// a shipping binary in exchange for a diagnostic, and a diagnostic should be asked for.
//
// With it off, allocCounters() still exists and reads zero, so nothing has to be compiled
// conditionally at the call sites and a profile report simply shows no allocations.

#include "core/phase_profiler.hpp"

#include <cstddef>
#include <cstdlib>
#include <new>

namespace avgen::core {
namespace {
thread_local AllocCounters tlsCounters{};
} // namespace

const AllocCounters& allocCounters() noexcept { return tlsCounters; }

} // namespace avgen::core

#ifdef AVGEN_ALLOC_COUNTERS

namespace {

inline void* countedAlloc(std::size_t size) {
    auto& c = const_cast<avgen::core::AllocCounters&>(avgen::core::allocCounters());
    ++c.allocations;
    c.bytes += size;
    // malloc(0) may return nullptr, which operator new must not.
    return std::malloc(size != 0 ? size : 1);
}

inline void* countedAlignedAlloc(std::size_t size, std::size_t alignment) {
    auto& c = const_cast<avgen::core::AllocCounters&>(avgen::core::allocCounters());
    ++c.allocations;
    c.bytes += size;
    void* p = nullptr;
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    if (posix_memalign(&p, alignment, size != 0 ? size : 1) != 0) {
        return nullptr;
    }
    return p;
}

inline void countedFree(void* p) noexcept {
    if (p != nullptr) {
        ++const_cast<avgen::core::AllocCounters&>(avgen::core::allocCounters()).frees;
    }
    std::free(p);
}

} // namespace

void* operator new(std::size_t size) {
    void* p = countedAlloc(size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
void* operator new[](std::size_t size) {
    void* p = countedAlloc(size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return countedAlloc(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return countedAlloc(size); }

void* operator new(std::size_t size, std::align_val_t alignment) {
    void* p = countedAlignedAlloc(size, static_cast<std::size_t>(alignment));
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    void* p = countedAlignedAlloc(size, static_cast<std::size_t>(alignment));
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return countedAlignedAlloc(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return countedAlignedAlloc(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { countedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }
void operator delete(void* p, std::align_val_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { countedFree(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { countedFree(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { countedFree(p); }

#endif // AVGEN_ALLOC_COUNTERS
