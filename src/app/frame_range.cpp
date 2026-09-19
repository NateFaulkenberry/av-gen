#include "app/frame_range.hpp"

#include "core/log.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace avgen::app {

// ---- FrameRange ---------------------------------------------------------------------------------

double FrameRange::resolvedEnd(double audioSeconds, double timelineSeconds) const {
    if (endSeconds >= 0.0) {
        return std::max(endSeconds, startSeconds);
    }
    if (audioSeconds > 0.0) {
        return audioSeconds;
    }
    if (timelineSeconds > 0.0) {
        return timelineSeconds;
    }
    return startSeconds + 10.0;
}

std::uint64_t FrameRange::frameCount(double resolvedEndSeconds) const {
    const double span = std::max(0.0, resolvedEndSeconds - startSeconds);
    // The epsilon is what stops a range that is an exact multiple of the frame time from gaining a
    // frame to floating-point dust: 1.5 - 0.5 at 10 fps must be ten frames, not eleven.
    const auto n = static_cast<std::uint64_t>(std::ceil(span * fps - 1e-9));
    return std::max<std::uint64_t>(1, n);
}

double FrameRange::timeOf(std::uint64_t index) const {
    return startSeconds + static_cast<double>(index) / fps;
}

Result<void> FrameRange::validate() const {
    if (!std::isfinite(fps) || fps <= 0.0) {
        return fail("frame range: fps must be a finite positive number, got {}", fps);
    }
    if (!std::isfinite(startSeconds) || startSeconds < 0.0) {
        return fail("frame range: start must be a finite time at or after zero, got {}", startSeconds);
    }
    if (std::isfinite(endSeconds) && endSeconds >= 0.0 && endSeconds < startSeconds) {
        return fail("frame range: end {} is before start {}", endSeconds, startSeconds);
    }
    return {};
}

// ---- FrameSequenceDriver -------------------------------------------------------------------------

namespace {

std::uint64_t hashBytes(const std::uint8_t* data, std::size_t size) {
    std::uint64_t h = FrameSequenceDriver::kHashSeed;
    for (std::size_t i = 0; i < size; ++i) {
        h = (h ^ data[i]) * 1099511628211ull;
    }
    return h;
}

// The float hash goes over the BIT PATTERNS and not the values, so two frames that differ only by
// the sign of a zero, or by a NaN payload, are two different frames. A renderer that starts
// emitting -0.0 where it emitted 0.0 has changed, and a determinism check that forgave it would be
// forgiving exactly the kind of drift it exists to catch.
std::uint64_t hashFloats(const float* data, std::size_t count) {
    static_assert(sizeof(float) == 4);
    return hashBytes(reinterpret_cast<const std::uint8_t*>(data), count * sizeof(float));
}

unsigned resolveThreads(unsigned requested) {
    if (requested > 0) {
        return std::min(requested, 16u);
    }
    const unsigned hw = std::thread::hardware_concurrency();
    return std::clamp(hw > 1 ? hw - 1 : 1u, 1u, 16u);
}

} // namespace

FrameSequenceDriver::FrameSequenceDriver(FrameSource& source, FrameWriter writer,
                                         unsigned writerThreads)
    : source_(source), writer_(std::move(writer)), requestedThreads_(writerThreads) {}

FrameSequenceDriver::~FrameSequenceDriver() {
    cancelled_ = true;
    {
        std::lock_guard lock(mutex_);
        stopWriters_ = true;
    }
    work_.notify_all();
    space_.notify_all();
    for (auto& t : writers_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void FrameSequenceDriver::fail(std::string message) {
    {
        std::lock_guard lock(mutex_);
        if (error_.empty()) {
            error_ = std::move(message);
        }
    }
    // Releases a producer parked on backpressure. Without it, a writer that fails while the queue
    // is full deadlocks the render thread against threads that are about to stop.
    space_.notify_all();
}

void FrameSequenceDriver::writerLoop() {
    for (;;) {
        SequenceFrame item;
        {
            std::unique_lock lock(mutex_);
            work_.wait(lock, [&] { return stopWriters_ || !queue_.empty(); });
            if (queue_.empty()) {
                return; // stopping, and drained
            }
            item = std::move(queue_.front());
            queue_.pop_front();
        }
        space_.notify_one();
        if (writer_) {
            if (auto r = writer_(item); !r) {
                fail(fmt::format("frame {}: {}", item.index, r.error().message));
                // Stops the producer too. A sequence that keeps rendering after its writer has
                // failed is spending minutes to produce nothing.
                cancelled_ = true;
            }
        }
        {
            std::lock_guard lock(mutex_);
            ++written_;
        }
    }
}

void FrameSequenceDriver::enqueue(SequenceFrame frame) {
    std::unique_lock lock(mutex_);
    if (queue_.size() >= queueLimit_) {
        space_.wait(lock, [&] { return queue_.size() < queueLimit_ || stopWriters_ || !error_.empty(); });
    }
    if (error_.empty() && !stopWriters_) {
        queue_.push_back(std::move(frame));
    }
    lock.unlock();
    work_.notify_one();
}

Result<void> FrameSequenceDriver::start(const FrameRange& range, double audioSeconds,
                                        double timelineSeconds) {
    if (started_) {
        return avgen::fail("frame sequence: already started");
    }
    if (auto ok = range.validate(); !ok) {
        return ok;
    }
    range_ = range;
    end_ = range_.resolvedEnd(audioSeconds, timelineSeconds);
    total_ = range_.frameCount(end_);

    clock_ = FixedStepClock(range_.fps);
    clock_.restartAt(range_.startSeconds);

    if (auto ok = source_.begin(range_, total_); !ok) {
        return ok;
    }

    const unsigned threads = resolveThreads(requestedThreads_);
    // Two frames in flight per writer. Enough that a slow frame does not idle a writer, bounded so
    // a sequence cannot hold a movie's worth of pixels in RAM -- which is the failure the brief's
    // bounded-buffering section names and the reason this is a deque and not a vector.
    queueLimit_ = static_cast<std::size_t>(threads) * 2;
    writers_.reserve(threads);
    for (unsigned i = 0; i < threads; ++i) {
        writers_.emplace_back([this] { writerLoop(); });
    }

    startedAt_ = std::chrono::steady_clock::now();
    started_ = true;
    log::info("sequence: {} frames {:.3f}s..{:.3f}s at {:g} fps, {} writer thread(s)", total_,
              range_.startSeconds, end_, range_.fps, threads);
    return {};
}

bool FrameSequenceDriver::step(int maxFrames, double budgetSeconds) {
    if (!started_ || done_) {
        return true;
    }
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < std::max(1, maxFrames); ++i) {
        if (cancelled_.load() || submitted_ >= total_) {
            break;
        }
        const FrameTime time = clock_.tick();
        if (auto r = source_.submit(submitted_, time); !r) {
            fail(r.error().message);
            cancelled_ = true;
            break;
        }
        ++submitted_;
        // Collect whatever is ready without blocking, so a source that finishes frames behind the
        // submitter does not accumulate them all until the end.
        if (auto r = source_.collect(false, [this](SequenceFrame f) {
                lastHash_ = f.linear() ? hashFloats(f.rgbaF.data(), f.rgbaF.size())
                                       : hashBytes(f.rgba8.data(), f.rgba8.size());
                sequenceHash_ = (sequenceHash_ ^ lastHash_) * 1099511628211ull;
                frameHashes_.push_back(lastHash_);
                ++hashed_;
                enqueue(std::move(f));
            });
            !r) {
            fail(r.error().message);
            cancelled_ = true;
            break;
        }
        if (budgetSeconds > 0.0) {
            const double spent =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
            if (spent >= budgetSeconds) {
                break;
            }
        }
    }
    if (cancelled_.load() || submitted_ >= total_) {
        static_cast<void>(finish());
        return true;
    }
    return false;
}

Result<void> FrameSequenceDriver::finish() {
    if (done_) {
        return {};
    }
    // ORDER IS LOAD-BEARING. Every submitted frame has to reach the queue before the writers are
    // told to stop, because `enqueue` drops anything that arrives after `stopWriters_` -- which is
    // correct for a cancellation and silently truncating for a completion.
    if (auto r = source_.collect(true, [this](SequenceFrame f) {
            lastHash_ = f.linear() ? hashFloats(f.rgbaF.data(), f.rgbaF.size())
                                   : hashBytes(f.rgba8.data(), f.rgba8.size());
            sequenceHash_ = (sequenceHash_ ^ lastHash_) * 1099511628211ull;
            frameHashes_.push_back(lastHash_);
            ++hashed_;
            enqueue(std::move(f));
        });
        !r) {
        fail(r.error().message);
    }
    {
        std::lock_guard lock(mutex_);
        stopWriters_ = true;
    }
    work_.notify_all();
    for (auto& t : writers_) {
        if (t.joinable()) {
            t.join();
        }
    }
    writers_.clear();
    // After the writers, so a sink that has to finalise a file (a muxer) sees every frame first.
    if (auto r = source_.end(); !r) {
        fail(r.error().message);
    }
    done_ = true;
    std::lock_guard lock(mutex_);
    if (!error_.empty()) {
        return avgen::fail("{}", error_);
    }
    log::info("sequence: {} frame(s) written, sequence hash {:016x}", written_, sequenceHash_);
    return {};
}

Result<void> FrameSequenceDriver::run() {
    if (!started_) {
        return avgen::fail("frame sequence: run() before start()");
    }
    while (!done_) {
        static_cast<void>(step(1));
    }
    std::lock_guard lock(mutex_);
    if (!error_.empty()) {
        return avgen::fail("{}", error_);
    }
    return {};
}

void FrameSequenceDriver::cancel() {
    cancelled_ = true;
    space_.notify_all();
}

SequenceProgress FrameSequenceDriver::progress() const {
    SequenceProgress p;
    p.framesSubmitted = submitted_;
    p.framesHashed = hashed_;
    p.framesTotal = total_;
    p.lastFrameHash = lastHash_;
    p.sequenceHash = sequenceHash_;
    p.finished = done_;
    p.cancelled = cancelled_.load();
    if (started_) {
        p.elapsedSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
        if (p.elapsedSeconds > 1e-6) {
            p.framesPerSecond = static_cast<double>(submitted_) / p.elapsedSeconds;
        }
    }
    // Eight frames before an estimate, and it stays negative until then. A rate taken over the
    // first frame is not a rate -- shaders are compiling, the first BVH is building -- and a
    // confident wrong number at the only moment somebody looks teaches them to ignore the number.
    if (submitted_ >= 8 && total_ > submitted_ && p.framesPerSecond > 1e-6) {
        p.estimatedRemainingSeconds =
            static_cast<double>(total_ - submitted_) / p.framesPerSecond;
    }
    {
        std::lock_guard lock(const_cast<std::mutex&>(mutex_));
        p.framesWritten = written_;
        p.error = error_;
    }
    return p;
}

} // namespace avgen::app
