// Feeds (ADR-1580/1581/1582): the project block, the fold of a source's hops onto the mix's, the merge, the trim, and
// the live sources' beat tracker and feed queue -- on the generated stem fixture and the synthetic capture backend
// (no hardware, no real device).

#include "analysis/channel_analysis.hpp"
#include "app/engine.hpp"
#include "core/time.hpp"
#include "analysis/response_feed.hpp"
#include "analysis/stem_analysis.hpp"
#include "audio/audio_file.hpp"
#include "audio/audio_input.hpp"
#include "audio/feeds.hpp"
#include "audio/synthetic_capture.hpp"
#include "sonic/response.hpp"
#include "sonic/timbre.hpp"
#include "support/stem_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace avgen;
using nlohmann::json;

namespace {

analysis::TimedFeed hop(double t, float levelDb, int hitClass = -1, float strength = 0.0f, float drumness = 1.0f) {
    analysis::TimedFeed h;
    h.seconds = t;
    h.feed.valid = true;
    h.feed.levelDb = levelDb;
    h.feed.bassDb = levelDb - 6.0f;
    h.feed.onsetRatio = 1.0f;
    if (hitClass >= 0) {
        const auto k = static_cast<std::size_t>(hitClass);
        h.feed.hit[k] = true;
        h.feed.strength[k] = strength;
        h.feed.drumness[k] = drumness;
    }
    return h;
}

} // namespace

TEST_CASE("feeds: the project block parses, round-trips, and refuses what is not a feed", "[feeds]") {
    const json j = {{"beat", "stem.drums"}, {"hits", "stem.drums"}, {"kick", "stem.kick_in"}, {"hat", "audio"},
                    {"levels", "audio"}};
    auto f = audio::Feeds::fromJson(j);
    REQUIRE(f.has_value());
    CHECK(f->beat == "drums");
    CHECK(f->hitSource(0) == "kick_in");
    CHECK(f->hitSource(1) == "drums"); // low: from hits
    CHECK(f->hitSource(3).empty());    // hat: the mix, over hits
    CHECK(f->levels.empty());
    CHECK(f->feedsResponse("drums"));
    CHECK(f->feedsResponse("kick_in"));
    CHECK_FALSE(f->feedsResponse("bass"));
    CHECK(f->anyResponse());
    // Written back: every stated key, the mix as "audio"; "levels": "audio" is the default and is not written.
    const json out = f->toJson();
    CHECK(out == json{{"beat", "stem.drums"}, {"hits", "stem.drums"}, {"kick", "stem.kick_in"}, {"hat", "audio"}});
    REQUIRE(audio::Feeds::fromJson(out).has_value());
    CHECK(*audio::Feeds::fromJson(out) == *audio::Feeds::fromJson(out));
    CHECK(audio::Feeds{}.toJson().empty());
    CHECK(audio::Feeds{}.empty());
    // Not feeds.
    CHECK_FALSE(audio::Feeds::fromJson(json{{"beat", "drums"}}).has_value());          // not "stem.<key>"
    CHECK_FALSE(audio::Feeds::fromJson(json{{"beat", "stem.Drums!"}}).has_value());    // not a key
    CHECK_FALSE(audio::Feeds::fromJson(json{{"bass", "stem.bass"}}).has_value());      // not a feed name
    CHECK_FALSE(audio::Feeds::fromJson(json{{"beat", 3}}).has_value());
    CHECK_FALSE(audio::Feeds::fromJson(json::array()).has_value());
}

TEST_CASE("feeds: the fold ORs a window's events at their strongest and keeps the newest levels", "[feeds]") {
    analysis::FeedFolder folder;
    const std::vector<analysis::TimedFeed> first{hop(0.01, -20.0f), hop(0.02, -18.0f, 0, 0.4f, 1.0f),
                                                 hop(0.03, -17.0f, 0, 0.7f, 0.8f), hop(0.04, -16.0f, 2, 0.5f, 0.6f)};
    const analysis::ResponseFeed a = folder.fold(first, 0.045);
    CHECK(a.valid);
    CHECK(a.hit[0]);
    CHECK(a.strength[0] == 0.7f);
    CHECK(a.drumness[0] == 0.8f); // the least of the window's
    CHECK(a.hit[2]);
    CHECK_FALSE(a.hit[3]);
    CHECK(a.levelDb == -16.0f); // the newest
    // A window with no new hop: no events, the newest levels held while it is recent...
    const analysis::ResponseFeed b = folder.fold({}, 0.1);
    CHECK(b.valid);
    CHECK_FALSE(b.hit[0]);
    CHECK(b.levelDb == -16.0f);
    // ...and inactive once it is stale: no event, the levels below any gate.
    const analysis::ResponseFeed c = folder.fold({}, 0.04 + analysis::FeedFolder::kStaleSeconds + 0.01);
    CHECK(c == analysis::ResponseFeed::inactive());
    // Outside a stem's span: inactive whatever arrives.
    const std::vector<analysis::TimedFeed> late{hop(1.0, -10.0f, 0, 0.9f)};
    CHECK(folder.fold(late, 1.0, false) == analysis::ResponseFeed::inactive());
}

TEST_CASE("feeds: the merge replaces only the routed parts; no routing is the mix float for float", "[feeds]") {
    analysis::ResponseFeed mix;
    mix.valid = true;
    mix.hit = {true, true, false, true, true};
    mix.strength = {0.9f, 0.8f, 0.0f, 0.3f, 0.5f};
    mix.drumness = {1.0f, 1.0f, 1.0f, 0.7f, 1.0f};
    mix.levelDb = -12.0f;
    mix.bassDb = -15.0f;
    mix.onsetRatio = 2.0f;
    analysis::ResponseFeed drums = analysis::ResponseFeed::inactive();
    drums.levelDb = -30.0f;
    drums.hit[2] = true;
    drums.strength[2] = 0.6f;
    const std::vector<analysis::ResponseFeed> sources{drums};
    CHECK(analysis::mergeFeeds(mix, sources, analysis::FeedRouting{}) == mix);
    analysis::FeedRouting r;
    r.hit = {0, 0, 0, -1, -1}; // kick, low, snare from the drums; hat, onset from the mix
    const analysis::ResponseFeed m = analysis::mergeFeeds(mix, sources, r);
    CHECK_FALSE(m.hit[0]); // the drums had no kick here: the mix's kick does not leak through
    CHECK(m.hit[2]);
    CHECK(m.strength[2] == 0.6f);
    CHECK(m.hit[3]); // the mix's hat
    CHECK(m.levelDb == -12.0f);
    r.levels = 0;
    const analysis::ResponseFeed l = analysis::mergeFeeds(mix, sources, r);
    CHECK(l.levelDb == -30.0f);
    CHECK(l.onsetRatio == drums.onsetRatio);
    CHECK(l.hit[3]); // levels do not move hits
}

TEST_CASE("feeds: the response model on the mix's own feed is the model as it was", "[feeds][sonic]") {
    // The fixture's mix, analysed with the causal detector as the file path does, stepped both ways.
    const auto& fx = testsupport::requireStemFixture();
    auto file = audio::AudioFile::load(fx.file("mix.wav"));
    REQUIRE(file.has_value());
    analysis::StemAnalysisConfig cfg;
    cfg.hits = true;
    const analysis::StemTrack track = analysis::StemTrack::analyze(*file, cfg);
    sonic::ResponseModel a, b;
    signals::SignalBus busA, busB;
    a.declare(busA);
    b.declare(busB);
    const sonic::ResponseSettings s;
    const sonic::ResponseControls c;
    for (const analysis::StemFrame& f : track.frames()) {
        sonic::TimbreFeatures t;
        t.causal.valid = f.feed.valid;
        t.causal.hit = f.feed.hit;
        t.causal.strength = f.feed.strength;
        t.causal.drumness = f.feed.drumness;
        t.causal.levelDb = f.feed.levelDb;
        t.causal.bassDb = f.feed.bassDb;
        t.causal.ratio[static_cast<std::size_t>(analysis::HitClass::Onset)] = f.feed.onsetRatio;
        a.step(t, track.hopSeconds(), c, s);
        b.step(t, analysis::ResponseFeed::fromCausal(t.causal), track.hopSeconds(), c, s);
        REQUIRE(a.envelope(sonic::ResponseHit::Kick) == b.envelope(sonic::ResponseHit::Kick));
        REQUIRE(a.referenceDb() == b.referenceDb());
    }
    CHECK(a.hits(sonic::ResponseHit::Kick) > 10);
    CHECK(a.hits(sonic::ResponseHit::Kick) == b.hits(sonic::ResponseHit::Kick));
}

TEST_CASE("feeds: a stem's trim is exactly nothing at 0 dB and brings a quiet stem's hits back", "[feeds][stems]") {
    const auto& fx = testsupport::requireStemFixture();
    auto drums = audio::AudioFile::load(fx.file("drums.wav"));
    REQUIRE(drums.has_value());
    analysis::StemAnalysisConfig cfg;
    cfg.hits = true;
    const analysis::StemTrack plain = analysis::StemTrack::analyze(*drums, cfg);
    cfg.gainDb = 0.0f;
    CHECK(analysis::StemTrack::analyze(*drums, cfg).frames() == plain.frames());
    // The same drums 42 dB down: under the kick detector's absolute floor (-45 dBFS in its band).
    std::vector<float> quiet(drums->interleaved().begin(), drums->interleaved().end());
    const float down = std::pow(10.0f, -42.0f / 20.0f);
    for (float& v : quiet) {
        v *= down;
    }
    const audio::AudioFile soft = audio::AudioFile::fromInterleaved(quiet, drums->channels(), drums->sampleRate());
    const auto kicks = [](const analysis::StemTrack& t) {
        return std::count_if(t.frames().begin(), t.frames().end(), [](const analysis::StemFrame& f) { return f.kick; });
    };
    cfg.gainDb = 0.0f;
    const auto lost = kicks(analysis::StemTrack::analyze(soft, cfg));
    cfg.gainDb = 24.0f;
    const auto back = kicks(analysis::StemTrack::analyze(soft, cfg));
    INFO("kicks: plain " << kicks(plain) << ", -42 dB " << lost << ", -42 dB with +24 dB trim " << back);
    CHECK(lost < kicks(plain) / 2);
    CHECK(back >= kicks(plain) * 3 / 4);
    // A trim never makes the level bigger once the reference has adapted: the levels are loudness-free (ADR-1510).
    cfg.gainDb = 0.0f;
    const analysis::StemTrack ref = analysis::StemTrack::analyze(*drums, cfg);
    cfg.gainDb = 12.0f;
    const analysis::StemTrack hot = analysis::StemTrack::analyze(*drums, cfg);
    double sumRef = 0.0, sumHot = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < ref.frames().size(); ++i) {
        if (ref.frames()[i].timeSeconds > 12.0) {
            sumRef += static_cast<double>(ref.frames()[i].level);
            sumHot += static_cast<double>(hot.frames()[i].level);
            ++n;
        }
    }
    REQUIRE(n > 0);
    CHECK(sumHot / n == Catch::Approx(sumRef / n).margin(0.05));
    CHECK(analysis::trimGain(0.0f) == 1.0f);
    CHECK(analysis::trimGain(100.0f) == Catch::Approx(std::pow(10.0f, 24.0f / 20.0f)));
}

TEST_CASE("feeds: a live source runs the beat tracker and queues its feed; its trim moves without a reset",
          "[feeds][multichannel][threading]") {
    constexpr std::uint32_t kRate = 48000;
    audio::AudioInput input;
    std::vector<audio::InputSource> sources{{.key = "kick", .label = "Kick", .channel = 0},
                                            {.key = "other", .label = "Other", .channel = 3}};
    input.setSources(sources);
    audio::SyntheticCaptureConfig cfg; // a click every 0.5 s on each channel: 120 bpm
    cfg.realtime = false;
    auto backend = std::make_unique<audio::SyntheticCapture>(cfg);
    audio::SyntheticCapture* device = backend.get();
    REQUIRE(input.openBackend(std::move(backend)).has_value());
    analysis::ChannelAnalysis channels({{.stream = input.sourceStream(0), .hits = true, .beat = true, .feed = true},
                                        {.stream = input.sourceStream(1)}},
                                       kRate);
    channels.start();
    analysis::FeedRouting routing;
    routing.hit[static_cast<std::size_t>(analysis::HitClass::Onset)] = 0;
    analysis::LiveFeedMixer mixer(channels, {0}, routing);
    std::vector<double> fedOnsets;
    analysis::BeatState beat;
    const int hops = 12 * kRate / 512; // 12 s
    for (int h = 0; h < hops; ++h) {
        device->pump(4);
        const auto want = static_cast<std::uint64_t>(h + 1) * 2;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (channels.hopsProduced() + 3 * 2 < want && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        if (channels.acquire(0)) {
            const analysis::LiveStemFrame& f = channels.latest(0);
            REQUIRE(f.tracked);
            beat = f.beat;
        }
        if (channels.acquire(1)) {
            CHECK_FALSE(channels.latest(1).tracked);
        }
        // The mix hop at this time (the mix's own onsets are empty: everything fed comes from the source).
        const double t = static_cast<double>(h * 512 + 1024) / kRate - 0.03;
        analysis::ResponseFeed out;
        REQUIRE(mixer.feedAt(t, analysis::CausalOnsets{}, out));
        if (out.hit[static_cast<std::size_t>(analysis::HitClass::Onset)]) {
            fedOnsets.push_back(t);
        }
        if (h == hops / 2) {
            channels.setGainDb(0, -12.0f); // applied at the next read; nothing resets
        }
    }
    channels.stop();
    INFO("tempo " << beat.tempoBpm << ", beats " << beat.beatCount << ", fed onsets " << fedOnsets.size());
    CHECK(beat.tempoBpm == Catch::Approx(120.0f).margin(2.0f));
    CHECK(beat.beatCount >= 15);
    // Each fed onset is one of channel 1's clicks (every 0.5 s from frame 0): within the analyser's 35 ms of window-centre
    // timing plus a hop, the stem fixture's tolerance. (Channel 4's clicks, 94 ms later, never reach it.)
    CHECK(fedOnsets.size() >= 18);
    for (double o : fedOnsets) {
        const double phase = std::fmod(o, 0.5);
        INFO("fed onset at " << o);
        CHECK(std::min(phase, 0.5 - phase) <= 0.035 + 512.0 / kRate);
    }
    CHECK(channels.feedsDropped() == 0);
}

namespace {

struct LiveRun {
    std::uint64_t onsetHits = 0;
    double tempo = 0.0;
    audio::TempoProvenance provenance = audio::TempoProvenance::None;
};

// The live engine on the synthetic 8-channel device (a click every 0.5 s per channel, channel c 31.25 ms x c late),
// live Sonic on, `feeds` set, for `seconds` of audio.
LiveRun runLive(const audio::Feeds& feeds, double seconds) {
    app::Engine engine(app::EngineMode::Live);
    engine.setLiveControl(false);
    engine.setInputSources({{.key = "click", .label = "Click", .channel = 4}, {.key = "other", .label = "Other", .channel = 6}});
    engine.setFeeds(feeds);
    audio::SyntheticCaptureConfig cfg;
    cfg.realtime = false;
    auto backend = std::make_unique<audio::SyntheticCapture>(cfg);
    audio::SyntheticCapture* device = backend.get();
    REQUIRE(engine.useAudioInputBackend(std::move(backend)).has_value());
    REQUIRE(engine.setLiveSonic(true).has_value());
    double t = 0.0;
    const int frames = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < frames; ++i) {
        device->pump(6); // 768 frames, 16 ms
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.update(FrameTime{t, 1.0 / 60.0, static_cast<std::uint64_t>(i)});
        t += 1.0 / 60.0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.update(FrameTime{t, 1.0 / 60.0, static_cast<std::uint64_t>(frames)});
    LiveRun out;
    out.onsetHits = engine.sonicRuntime().response().hits(sonic::ResponseHit::Onset);
    out.tempo = engine.tempo().bpm;
    out.provenance = engine.tempo().source;
    engine.stopAudioInput();
    return out;
}

} // namespace

TEST_CASE("feeds: live, the beat clock and response.onset come from the input source the feeds name",
          "[feeds][multichannel][threading][engine]") {
    audio::Feeds fed;
    fed.beat = "click";
    fed.hit[static_cast<std::size_t>(analysis::HitClass::Onset)] = "click";
    const LiveRun a = runLive(fed, 10.0);
    const LiveRun b = runLive({}, 10.0);
    INFO("fed: " << a.onsetHits << " onsets, " << a.tempo << " bpm; mix: " << b.onsetHits << " onsets, " << b.tempo << " bpm");
    // One click every 0.5 s on the fed channel: about 20 in 10 s (the first ones while the detector warms up).
    CHECK(a.onsetHits >= 14);
    CHECK(a.onsetHits <= 22);
    // The mix averages eight staggered clicks: it hears more attacks than the one channel has.
    CHECK(b.onsetHits > a.onsetHits);
    CHECK(a.provenance == audio::TempoProvenance::Detected);
    CHECK(a.tempo == Catch::Approx(120.0).margin(2.0));
}
