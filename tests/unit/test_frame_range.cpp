// The frame-range driver (ADR-382): the arithmetic, the loop, the hash chain and the bounded
// writer pool, checked with a fake renderer so none of it needs a device.
//
// This is the point of extracting it. `app::RenderJob` has twenty GPU tests pinning the same
// behaviour and every one of them needs an adapter, the GPU lock and a quiet machine; the parts
// that actually go wrong in a sequence -- a frame short at the end, a hash chain that reorders
// under load, a queue that drops its tail on a clean finish -- are none of them graphical.

#include "app/frame_range.hpp"

#include "app/render_settings.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

// A renderer that produces a known, frame-dependent image and nothing else. `holdBack` makes it
// finish frames LATE -- the asynchronous case the readback ring creates for the real one -- so the
// driver's ordering guarantees are exercised rather than assumed.
class FakeSource final : public app::FrameSource {
public:
    explicit FakeSource(std::size_t holdBack = 0) : holdBack_(holdBack) {}

    Result<void> begin(const app::FrameRange& range, std::uint64_t frames) override {
        began = true;
        beganFrames = frames;
        beganRange = range;
        return {};
    }

    Result<void> submit(std::uint64_t index, const FrameTime& time) override {
        if (failAt && *failAt == index) {
            return fail("fake source: refusing frame {}", index);
        }
        submitted.push_back(index);
        times.push_back(time.renderTime);
        app::SequenceFrame f;
        f.index = index;
        f.width = 2;
        f.height = 1;
        // Frame-dependent bytes, so two frames cannot hash the same by accident.
        f.rgba8 = {static_cast<std::uint8_t>(index & 0xff), 1, 2, 255,
                   static_cast<std::uint8_t>((index * 7) & 0xff), 3, 4, 255};
        pending_.push_back(std::move(f));
        return {};
    }

    Result<void> collect(bool all, const std::function<void(app::SequenceFrame)>& sink) override {
        while (!pending_.empty() && (all || pending_.size() > holdBack_)) {
            sink(std::move(pending_.front()));
            pending_.erase(pending_.begin());
        }
        return {};
    }

    Result<void> end() override {
        ended = true;
        return {};
    }

    bool began = false;
    bool ended = false;
    std::uint64_t beganFrames = 0;
    app::FrameRange beganRange{};
    std::vector<std::uint64_t> submitted;
    std::vector<double> times;
    std::optional<std::uint64_t> failAt;

private:
    std::size_t holdBack_ = 0;
    std::vector<app::SequenceFrame> pending_;
};

struct RecordingWriter {
    std::mutex mutex;
    std::vector<std::uint64_t> indices;
    std::optional<std::uint64_t> failAt;

    app::FrameWriter fn() {
        return [this](const app::SequenceFrame& f) -> Result<void> {
            std::lock_guard lock(mutex);
            indices.push_back(f.index);
            if (failAt && *failAt == f.index) {
                return fail("writer: refusing frame {}", f.index);
            }
            return {};
        };
    }
};

} // namespace

TEST_CASE("A frame range resolves its end and counts its frames", "[frame-range]") {
    app::FrameRange r;
    r.fps = 30.0;
    SECTION("the sources are tried in order and ten seconds is the last resort") {
        CHECK(r.resolvedEnd(12.5, 0.0) == 12.5);   // audio wins
        CHECK(r.resolvedEnd(0.0, 4.0) == 4.0);     // then the timeline
        CHECK(r.resolvedEnd(0.0, 0.0) == 10.0);    // then ten seconds
        r.endSeconds = 2.0;
        CHECK(r.resolvedEnd(12.5, 4.0) == 2.0);    // an explicit end outranks all of them
    }
    SECTION("the end is exclusive, which is where the off-by-one lives") {
        r.startSeconds = 0.5;
        r.fps = 10.0;
        CHECK(r.frameCount(1.5) == 10);
        // The arm that says "exclusive" rather than "we rounded": one frame time more is one frame
        // more, and an exact multiple does not gain a frame to floating-point dust.
        CHECK(r.frameCount(1.5 + 1.0 / 10.0) == 11);
        CHECK(r.timeOf(0) == 0.5);
        CHECK_THAT(r.timeOf(9), WithinAbs(1.4, 1e-9));
    }
    SECTION("a range is never zero frames") {
        CHECK(r.frameCount(0.0) == 1);
        CHECK(r.frameCount(-5.0) == 1);
    }
    SECTION("validation refuses what cannot be walked") {
        CHECK(app::FrameRange{0.0, -1.0, 60.0}.validate().has_value());
        CHECK_FALSE((app::FrameRange{0.0, -1.0, 0.0}.validate().has_value()));
        CHECK_FALSE((app::FrameRange{-1.0, -1.0, 60.0}.validate().has_value()));
        CHECK_FALSE((app::FrameRange{5.0, 2.0, 60.0}.validate().has_value()));
    }
}

TEST_CASE("RenderSettings and the driver agree about what a range is", "[frame-range]") {
    // The whole reason the type exists. If these ever disagree, two renderers of one project
    // produce sequences of different lengths and nothing else says so.
    app::RenderSettings s;
    s.startSeconds = 1.25;
    s.endSeconds = 4.0;
    s.fps = 24.0;
    const app::FrameRange r = s.frameRange();
    CHECK(r.startSeconds == s.startSeconds);
    CHECK(r.endSeconds == s.endSeconds);
    CHECK(r.fps == s.fps);
    for (const double audio : {0.0, 9.0}) {
        for (const double timeline : {0.0, 3.0}) {
            CHECK(s.resolvedEnd(audio, timeline) == r.resolvedEnd(audio, timeline));
            CHECK(s.frameCount(s.resolvedEnd(audio, timeline)) ==
                  r.frameCount(r.resolvedEnd(audio, timeline)));
        }
    }
}

TEST_CASE("The driver walks the range and hands every frame to the writer", "[frame-range]") {
    FakeSource source;
    RecordingWriter writer;
    app::FrameSequenceDriver driver(source, writer.fn(), 1);
    REQUIRE(driver.start(app::FrameRange{0.5, 1.5, 10.0}, 0.0, 0.0).has_value());
    CHECK(source.began);
    CHECK(source.beganFrames == 10);
    CHECK(driver.frameCount() == 10);
    CHECK(driver.resolvedEndSeconds() == 1.5);

    REQUIRE(driver.run().has_value());
    const app::SequenceProgress p = driver.progress();
    CHECK(p.finished);
    CHECK_FALSE(p.cancelled);
    CHECK(p.framesSubmitted == 10);
    CHECK(p.framesHashed == 10);
    CHECK(p.framesWritten == 10);
    CHECK(p.error.empty());
    CHECK(source.ended);

    // Frame f is at start + f/fps, and the last frame is one frame SHORT of the end.
    REQUIRE(source.times.size() == 10);
    CHECK_THAT(source.times.front(), WithinAbs(0.5, 1e-9));
    CHECK_THAT(source.times.back(), WithinAbs(1.4, 1e-9));

    // Every frame reached the writer exactly once.
    std::lock_guard lock(writer.mutex);
    CHECK(writer.indices.size() == 10);
    CHECK(std::set<std::uint64_t>(writer.indices.begin(), writer.indices.end()).size() == 10);
}

TEST_CASE("The hash chain is in frame order and survives a source that lags", "[frame-range]") {
    // Two runs of the same range must agree; and a source that finishes frames three behind the
    // submitter -- which is what a three-slot readback ring does -- must not reorder the chain.
    const auto hashesFor = [](std::size_t holdBack) {
        FakeSource source(holdBack);
        RecordingWriter writer;
        app::FrameSequenceDriver driver(source, writer.fn(), 1);
        REQUIRE(driver.start(app::FrameRange{0.0, 1.0, 12.0}, 0.0, 0.0).has_value());
        REQUIRE(driver.run().has_value());
        return std::pair{driver.frameHashes(), driver.progress().sequenceHash};
    };
    const auto prompt = hashesFor(0);
    const auto lagging = hashesFor(3);
    REQUIRE(prompt.first.size() == 12);
    CHECK(prompt.first == lagging.first);
    CHECK(prompt.second == lagging.second);

    // THE CONTROL. If the fake produced the same bytes for every frame the equality above would
    // hold for a driver that hashed nothing at all, or hashed out of order. Twelve distinct frames
    // must give twelve distinct hashes.
    CHECK(std::set<std::uint64_t>(prompt.first.begin(), prompt.first.end()).size() == 12);
    // And the empty sequence hashes to the seed rather than to zero, which is the difference
    // between "nothing was rendered" and "something was rendered and came to nothing".
    CHECK(prompt.second != app::FrameSequenceDriver::kHashSeed);
}

TEST_CASE("Stepping is bounded and cancelling keeps what was already written", "[frame-range]") {
    // The contract `RenderJob::step` established and the editor's frame loop is written against.
    FakeSource source;
    RecordingWriter writer;
    app::FrameSequenceDriver driver(source, writer.fn(), 1);
    REQUIRE(driver.start(app::FrameRange{0.0, 2.5, 10.0}, 0.0, 0.0).has_value());
    REQUIRE(driver.frameCount() == 25);

    CHECK_FALSE(driver.step(3));
    CHECK(driver.progress().framesSubmitted == 3);
    CHECK_FALSE(driver.step(4));
    CHECK(driver.progress().framesSubmitted == 7);

    driver.cancel();
    CHECK(driver.step(100));   // true: the sequence is over
    const app::SequenceProgress p = driver.progress();
    CHECK(p.cancelled);
    CHECK(p.finished);
    CHECK(p.framesSubmitted == 7);
    // Partial output is KEPT. A cancelled render that threw away the seven frames it had already
    // made would be a worse answer than one that never started.
    CHECK(p.framesWritten == 7);
    CHECK(source.ended);
}

TEST_CASE("A finished sequence writes its tail rather than dropping it", "[frame-range]") {
    // The ordering inside finish() is load-bearing: everything still in the source has to reach the
    // queue BEFORE the writers are told to stop, or the last few frames vanish silently. A source
    // that holds five frames back makes that the normal case rather than a rare one.
    FakeSource source(5);
    RecordingWriter writer;
    app::FrameSequenceDriver driver(source, writer.fn(), 1);
    REQUIRE(driver.start(app::FrameRange{0.0, 1.0, 8.0}, 0.0, 0.0).has_value());
    REQUIRE(driver.run().has_value());
    CHECK(driver.progress().framesWritten == 8);
    CHECK(driver.frameHashes().size() == 8);
    std::lock_guard lock(writer.mutex);
    CHECK(writer.indices.size() == 8);
}

TEST_CASE("A failure on either side stops the sequence and is reported", "[frame-range]") {
    SECTION("the renderer failing") {
        FakeSource source;
        source.failAt = 4;
        RecordingWriter writer;
        app::FrameSequenceDriver driver(source, writer.fn(), 1);
        REQUIRE(driver.start(app::FrameRange{0.0, 2.0, 10.0}, 0.0, 0.0).has_value());
        const auto r = driver.run();
        REQUIRE_FALSE(r.has_value());
        INFO(r.error().message);
        CHECK(r.error().message.find("refusing frame 4") != std::string::npos);
        CHECK(driver.progress().framesSubmitted == 4);   // it stopped, rather than carrying on
    }
    SECTION("the writer failing") {
        FakeSource source;
        RecordingWriter writer;
        writer.failAt = 2;
        app::FrameSequenceDriver driver(source, writer.fn(), 1);
        REQUIRE(driver.start(app::FrameRange{0.0, 5.0, 10.0}, 0.0, 0.0).has_value());
        const auto r = driver.run();
        REQUIRE_FALSE(r.has_value());
        INFO(r.error().message);
        CHECK(r.error().message.find("frame 2") != std::string::npos);
        // And the renderer was stopped rather than left to spend minutes producing frames for a
        // file nobody can write. Fifty frames were asked for; far fewer were made.
        CHECK(driver.progress().framesSubmitted < 50);
    }
}

TEST_CASE("Progress refuses to estimate what it cannot yet know", "[frame-range]") {
    FakeSource source;
    RecordingWriter writer;
    app::FrameSequenceDriver driver(source, writer.fn(), 1);
    REQUIRE(driver.start(app::FrameRange{0.0, 10.0, 10.0}, 0.0, 0.0).has_value());
    CHECK(driver.progress().estimatedRemainingSeconds < 0.0);   // before any frame
    CHECK_FALSE(driver.step(3));
    // Still negative: three frames is not a rate. A confident wrong number at the only moment
    // somebody looks at it teaches them to ignore the number.
    CHECK(driver.progress().estimatedRemainingSeconds < 0.0);
    CHECK_FALSE(driver.step(10));
    CHECK(driver.progress().framesSubmitted == 13);
    CHECK(driver.progress().estimatedRemainingSeconds >= 0.0);  // and now it will say
    driver.cancel();
    static_cast<void>(driver.step(1000));
}

TEST_CASE("A sequence of scene-linear frames hashes its floats, not its bytes", "[frame-range]") {
    // The path tracer's frames are float. The hash has to go over the bit patterns of the numbers
    // that reach the EXR, or a determinism check on a path-traced sequence checks nothing.
    class LinearSource final : public app::FrameSource {
    public:
        Result<void> begin(const app::FrameRange&, std::uint64_t) override { return {}; }
        Result<void> submit(std::uint64_t index, const FrameTime&) override {
            app::SequenceFrame f;
            f.index = index;
            f.width = 1;
            f.height = 1;
            f.rgbaF = {static_cast<float>(index) * 0.5f, 2.0f, 3.0f, 1.0f};
            ready_.push_back(std::move(f));
            return {};
        }
        Result<void> collect(bool, const std::function<void(app::SequenceFrame)>& sink) override {
            for (auto& f : ready_) sink(std::move(f));
            ready_.clear();
            return {};
        }
        Result<void> end() override { return {}; }
    private:
        std::vector<app::SequenceFrame> ready_;
    };
    LinearSource source;
    RecordingWriter writer;
    app::FrameSequenceDriver driver(source, writer.fn(), 1);
    REQUIRE(driver.start(app::FrameRange{0.0, 0.5, 8.0}, 0.0, 0.0).has_value());
    REQUIRE(driver.run().has_value());
    const auto& hashes = driver.frameHashes();
    REQUIRE(hashes.size() == 4);
    CHECK(std::set<std::uint64_t>(hashes.begin(), hashes.end()).size() == 4);
}
