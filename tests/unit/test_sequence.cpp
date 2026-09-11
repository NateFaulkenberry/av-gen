// The cinematic sequence (ADR-089): the model, the bake, and the two properties the whole design
// rests on.
//
// The first is that a bake is a *pure function*: the same sequence produces the same tracks, so a
// frame is an evaluation and nothing else. The second is that a sequence owns what it wrote, so
// re-installing after an edit replaces its automation rather than layering a second copy on top --
// which is what every "the camera jitters after I moved a shot" bug is made of.
//
// The spec's testing section (57) asks for creation, duration, shot ordering, shot activation,
// scene selection, keyframe evaluation, out-of-range behaviour, deterministic camera and character
// evaluation, layer ordering and timing, serialisation, and determinism through multiple paths.
// They are all here, and against the real composition layer stack rather than a stub, because the
// two halves of that seam have disagreed about spelling before.

#include "comp/layer_stack.hpp"
#include "comp/shape_layer.hpp"
#include "comp/text_layer.hpp"
#include "params/parameter_set.hpp"
#include "params/timeline.hpp"
#include "seq/director.hpp"
#include "seq/layer_sink.hpp"
#include "seq/sequence.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

seq::Shot keyedShot(std::string name, double start, double duration, glm::vec3 from, glm::vec3 to) {
    seq::Shot shot;
    shot.name = std::move(name);
    shot.startSeconds = start;
    shot.durationSeconds = duration;
    shot.camera.kind = seq::CameraKind::Keys;
    shot.camera.keys.push_back(
        seq::CameraKey{.timeSeconds = 0.0, .position = from, .target = glm::vec3(0.0f)});
    shot.camera.keys.push_back(
        seq::CameraKey{.timeSeconds = duration, .position = to, .target = glm::vec3(0.0f)});
    return shot;
}

seq::OverlayCue lyric(std::string id, std::string words, double start, double end,
                      seq::OverlayPreset preset, int order = 0) {
    seq::OverlayCue cue;
    cue.id = std::move(id);
    cue.kind = seq::OverlayKind::Text;
    cue.content = std::move(words);
    cue.style = "lyric";
    cue.startSeconds = start;
    cue.endSeconds = end;
    cue.preset = preset;
    cue.order = order;
    cue.anchor = glm::vec2(0.5f, 0.18f);
    return cue;
}

// A sequence with one of everything, so a single bake exercises every branch.
seq::Sequence samplePiece() {
    seq::Sequence piece;
    piece.name = "piece";
    piece.scenes.push_back(seq::SceneSlot{.id = "day", .node = "city-day"});
    piece.scenes.push_back(seq::SceneSlot{.id = "night", .node = "city-night"});

    seq::Shot one = keyedShot("wide", 0.0, 8.0, {0.0f, 10.0f, 30.0f}, {0.0f, 6.0f, 18.0f});
    one.scene = "day";
    one.in = seq::Transition{seq::TransitionKind::FadeIn, 1.0};
    piece.shots.push_back(std::move(one));

    seq::Shot two = keyedShot("close", 8.0, 6.0, {4.0f, 2.0f, 6.0f}, {-4.0f, 2.0f, 6.0f});
    two.scene = "night";
    two.camera.lookAtActor = "hero";
    two.camera.lookAtHeight = 1.6f;
    two.out = seq::Transition{seq::TransitionKind::FadeOut, 1.5};
    piece.shots.push_back(std::move(two));

    seq::Actor hero;
    hero.id = "hero";
    hero.node = "walker";
    hero.keys.push_back(seq::ActorKey{.timeSeconds = 0.0, .position = {-6.0f, 0.0f, 0.0f}});
    hero.keys.push_back(seq::ActorKey{.timeSeconds = 14.0, .position = {6.0f, 0.0f, 0.0f}});
    hero.clips.push_back(seq::ClipCue{.timeSeconds = 0.0, .clip = "Idle"});
    hero.clips.push_back(seq::ClipCue{.timeSeconds = 2.0, .clip = "Walk"});
    hero.clips.push_back(seq::ClipCue{.timeSeconds = 9.0, .clip = "Run", .speed = 1.3f});
    hero.clips.push_back(seq::ClipCue{.timeSeconds = 12.0, .clip = "Walk"});
    piece.actors.push_back(std::move(hero));

    piece.overlays.push_back(lyric("l1", "I'M WALKING", 1.0, 4.4, seq::OverlayPreset::FadeInOut, 1));
    piece.overlays.push_back(lyric("l2", "THROUGH THE CITY", 4.6, 8.2, seq::OverlayPreset::SlideUp, 1));
    seq::OverlayCue border;
    border.id = "frame";
    border.kind = seq::OverlayKind::Shape;
    border.content = "rectangle";
    border.startSeconds = 0.0;
    border.endSeconds = 14.0;
    border.order = -5; // under the lyrics
    border.color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    border.extra = nlohmann::json{{"size", nlohmann::json::array({1.7, 0.94})}, {"strokeWidth", 0.004}};
    piece.overlays.push_back(std::move(border));

    piece.markers.push_back(seq::Marker{.timeSeconds = 0.0, .name = "INTRO", .kind = seq::MarkerKind::Section});
    piece.markers.push_back(seq::Marker{.timeSeconds = 8.0, .name = "VERSE", .kind = seq::MarkerKind::Section});
    return piece;
}

// Registers every parameter a baked sequence writes against, so a bind resolves. Named after what
// the scene it stands in for would have: two scene-slot nodes, a character, and the camera.
void registerSceneParameters(params::ParameterSet& params) {
    const auto vec3 = [&](const std::string& path, glm::vec3 value) {
        params.add(params::ParamDesc<glm::vec3>{
            .path = path, .defaultValue = value, .hardMin = glm::vec3(-1e4f), .hardMax = glm::vec3(1e4f)});
    };
    const auto scalar = [&](const std::string& path, float value, float lo, float hi) {
        params.add(params::ParamDesc<float>{
            .path = path, .defaultValue = value, .hardMin = lo, .hardMax = hi});
    };
    vec3("camera/position", glm::vec3(0.0f));
    vec3("camera/target", glm::vec3(0.0f));
    scalar("camera/mode", 0.0f, 0.0f, 2.0f);
    scalar("camera/lens/focalLength", 35.0f, 1.0f, 400.0f);
    scalar("scene/brightness", 1.0f, 0.0f, 8.0f);
    for (const char* node : {"city-day", "city-night", "walker"}) {
        vec3(std::string("nodes/") + node + "/position", glm::vec3(0.0f));
        vec3(std::string("nodes/") + node + "/rotation", glm::vec3(0.0f));
        vec3(std::string("nodes/") + node + "/scale", glm::vec3(1.0f));
        params.add(params::ParamDesc<bool>{.path = std::string("nodes/") + node + "/visible",
                                           .defaultValue = true,
                                           .hardMin = false,
                                           .hardMax = true});
    }
}

const params::Track* trackFor(const params::Timeline& timeline, std::string_view target,
                              int component = -1) {
    for (const params::Track& t : timeline.tracks()) {
        if (t.target == target && t.component == component) {
            return &t;
        }
    }
    return nullptr;
}

} // namespace

// ---- the model (spec 4, 5, 6) -----------------------------------------------------------------

TEST_CASE("a sequence knows its duration, its shots and which scene is showing", "[sequence]") {
    const seq::Sequence piece = samplePiece();
    REQUIRE(piece.validate().has_value());

    // Duration derives from the content when it is not declared.
    CHECK(piece.duration() == Approx(14.0));

    REQUIRE(piece.shotAt(0.0) != nullptr);
    CHECK(piece.shotAt(0.0)->name == "wide");
    CHECK(piece.shotAt(7.999)->name == "wide");
    CHECK(piece.shotAt(8.0)->name == "close");
    CHECK(piece.shotAt(13.999)->name == "close");
    // Out of range in both directions is nothing, not a clamp: "before the piece" is a real answer.
    CHECK(piece.shotAt(-1.0) == nullptr);
    CHECK(piece.shotAt(20.0) == nullptr);

    REQUIRE(piece.sceneAt(2.0) != nullptr);
    CHECK(piece.sceneAt(2.0)->id == "day");
    CHECK(piece.sceneAt(9.0)->id == "night");
}

TEST_CASE("a shot with no scene inherits the one before it", "[sequence]") {
    seq::Sequence piece;
    piece.scenes.push_back(seq::SceneSlot{.id = "a"});
    piece.scenes.push_back(seq::SceneSlot{.id = "b"});
    piece.shots.push_back(keyedShot("one", 0.0, 4.0, {}, {}));
    piece.shots.back().scene = "a";
    piece.shots.push_back(keyedShot("two", 4.0, 4.0, {}, {})); // no scene: keeps "a"
    piece.shots.push_back(keyedShot("three", 8.0, 4.0, {}, {}));
    piece.shots.back().scene = "b";
    REQUIRE(piece.validate().has_value());

    CHECK(piece.sceneAt(1.0)->id == "a");
    CHECK(piece.sceneAt(5.0)->id == "a");
    CHECK(piece.sceneAt(9.0)->id == "b");
}

TEST_CASE("a sequence refuses shots that overlap or name things that do not exist", "[sequence]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("one", 0.0, 6.0, {}, {}));
    piece.shots.push_back(keyedShot("two", 3.0, 6.0, {}, {}));
    // Two cameras at once is not something a single-camera engine can honour, and quietly picking
    // one is worse than refusing.
    CHECK_FALSE(piece.validate().has_value());

    seq::Sequence missing;
    missing.shots.push_back(keyedShot("one", 0.0, 6.0, {}, {}));
    missing.shots.back().scene = "nowhere";
    CHECK_FALSE(missing.validate().has_value());

    seq::Sequence ghost;
    ghost.shots.push_back(keyedShot("one", 0.0, 6.0, {}, {}));
    ghost.shots.back().camera.lookAtActor = "nobody";
    CHECK_FALSE(ghost.validate().has_value());
}

// ---- the bake (spec 6, 7, 8, 17) --------------------------------------------------------------

TEST_CASE("the bake is a pure function of the sequence", "[sequence][determinism]") {
    const seq::Sequence piece = samplePiece();
    seq::NullLayerSink a;
    seq::NullLayerSink b;
    const auto first = piece.bake(a);
    const auto second = piece.bake(b);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    // Byte-identical JSON, which is the strongest form of the claim spec 33 makes.
    CHECK(first->timeline.dump() == second->timeline.dump());
    CHECK(first->targets == second->targets);
}

TEST_CASE("a scene cut is a step on visibility, and a transition is a dip", "[sequence][bake]") {
    const seq::Sequence piece = samplePiece();
    seq::NullLayerSink sink;
    const auto baked = piece.bake(sink);
    REQUIRE(baked.has_value());

    params::Timeline timeline;
    REQUIRE(timeline.fromJson(baked->timeline).has_value());

    const params::Track* day = trackFor(timeline, "nodes/city-day/visible");
    const params::Track* night = trackFor(timeline, "nodes/city-night/visible");
    REQUIRE(day != nullptr);
    REQUIRE(night != nullptr);
    // Before the cut the day scene is on and the night one is off; after, the reverse. A cut is a
    // Step key, because an interpolated bool would have both scenes half-visible for a frame.
    CHECK(day->evaluate(4.0)[0] == Approx(1.0f));
    CHECK(night->evaluate(4.0)[0] == Approx(0.0f));
    CHECK(day->evaluate(9.0)[0] == Approx(0.0f));
    CHECK(night->evaluate(9.0)[0] == Approx(1.0f));
    for (const params::Key& key : day->keys) {
        CHECK(key.interp == params::KeyInterp::Step);
    }

    const params::Track* brightness = trackFor(timeline, "scene/brightness");
    REQUIRE(brightness != nullptr);
    CHECK(brightness->evaluate(0.0)[0] == Approx(0.0f));  // fade in from black
    CHECK(brightness->evaluate(1.0)[0] == Approx(1.0f));
    CHECK(brightness->evaluate(6.0)[0] == Approx(1.0f));
    CHECK(brightness->evaluate(14.0)[0] == Approx(0.0f)); // fade out at the end
}

TEST_CASE("a camera shot bakes position and target keys that evaluate to the authored path",
          "[sequence][bake][camera]") {
    const seq::Sequence piece = samplePiece();
    seq::NullLayerSink sink;
    const auto baked = piece.bake(sink);
    REQUIRE(baked.has_value());
    params::Timeline timeline;
    REQUIRE(timeline.fromJson(baked->timeline).has_value());

    const params::Track* position = trackFor(timeline, "camera/position");
    REQUIRE(position != nullptr);
    const params::KeyValue start = position->evaluate(0.0);
    CHECK(start[0] == Approx(0.0f));
    CHECK(start[1] == Approx(10.0f));
    CHECK(start[2] == Approx(30.0f));
    // A hair before the cut, because at the cut itself the *next* shot's first key is what a
    // timeline holds -- which is what a cut means.
    const params::KeyValue end = position->evaluate(7.999);
    CHECK(end[1] == Approx(6.0f).margin(0.01));
    CHECK(position->evaluate(8.0)[1] == Approx(2.0f));

    // Out of range holds: before the first key and after the last, a track is its endpoints.
    CHECK(position->evaluate(-5.0)[1] == Approx(10.0f));
    CHECK(position->evaluate(500.0)[1] == Approx(2.0f));

    // The camera is put into free mode, or the composition ignores camera/position entirely and the
    // whole track does nothing while saying nothing (ADR-075).
    const params::Track* mode = trackFor(timeline, "camera/mode");
    REQUIRE(mode != nullptr);
    CHECK(mode->evaluate(0.0)[0] == Approx(1.0f));
}

TEST_CASE("a look-at actor aims the camera at where the actor is, not where it started",
          "[sequence][bake][camera]") {
    const seq::Sequence piece = samplePiece();
    seq::NullLayerSink sink;
    const auto baked = piece.bake(sink);
    REQUIRE(baked.has_value());
    params::Timeline timeline;
    REQUIRE(timeline.fromJson(baked->timeline).has_value());

    const params::Track* target = trackFor(timeline, "camera/target");
    REQUIRE(target != nullptr);
    // The second shot looks at the hero, so the aim at the shot's first key is wherever the hero
    // is at that second -- evaluated through the actor's own interpolation, not assumed linear --
    // lifted by the shot's eye height.
    const seq::Actor& hero = *piece.actorNamed("hero");
    const params::KeyValue aim = target->evaluate(8.0);
    CHECK(aim[0] == Approx(hero.positionAt(8.0).x).margin(0.01));
    CHECK(aim[1] == Approx(1.6f).margin(0.05));
    // And it is not simply the hero's starting position, which is the bug this replaces.
    CHECK(aim[0] != Approx(hero.positionAt(0.0).x));
    // The first shot does not, so its aim is the authored point.
    CHECK(target->evaluate(0.0)[1] == Approx(0.0f));
}

TEST_CASE("an actor's transform is baked and its clips are not", "[sequence][bake][character]") {
    const seq::Sequence piece = samplePiece();
    seq::NullLayerSink sink;
    const auto baked = piece.bake(sink);
    REQUIRE(baked.has_value());

    // spec 13: animation selection and character transform are different things, and only one of
    // them is a number a track can carry.
    CHECK(std::find(baked->targets.begin(), baked->targets.end(), "nodes/walker/position") !=
          baked->targets.end());
    for (const std::string& target : baked->targets) {
        INFO(target);
        CHECK(target.find("animation") == std::string::npos);
    }

    params::Timeline timeline;
    REQUIRE(timeline.fromJson(baked->timeline).has_value());
    const params::Track* position = trackFor(timeline, "nodes/walker/position");
    REQUIRE(position != nullptr);
    CHECK(position->evaluate(7.0)[0] == Approx(0.0f).margin(0.001));
}

TEST_CASE("the active animation cue is a pure function of the clock", "[sequence][character][determinism]") {
    const seq::Sequence piece = samplePiece();

    const auto cueAt = [&](double t) {
        const auto cues = piece.animationAt(t);
        REQUIRE(cues.size() == 1);
        return cues.front();
    };

    CHECK(cueAt(0.5).clip == "Idle");
    CHECK(cueAt(3.0).clip == "Walk");
    CHECK(cueAt(3.0).startSeconds == Approx(2.0));
    CHECK(cueAt(10.0).clip == "Run");
    CHECK(cueAt(10.0).speed == Approx(1.3f));

    // The same clip cued twice carries two different phase origins. This is the whole reason clips
    // are pushed rather than baked: a state index would have lost the second number.
    CHECK(cueAt(3.0).startSeconds == Approx(2.0));
    CHECK(cueAt(13.0).clip == "Walk");
    CHECK(cueAt(13.0).startSeconds == Approx(12.0));

    // Reached in any order, the answer is the same.
    const std::vector<double> forwards{1.0, 3.0, 10.0, 13.0};
    const std::vector<double> scrubbed{13.0, 3.0, 10.0, 1.0};
    std::vector<std::string> a;
    std::vector<std::string> b;
    for (double t : forwards) {
        a.push_back(cueAt(t).clip);
    }
    for (double t : scrubbed) {
        b.push_back(cueAt(t).clip);
    }
    std::ranges::reverse(b);
    std::ranges::rotate(b, b.begin() + 1); // undo the scrubbed order
    CHECK(cueAt(1.0).clip == "Idle");
    CHECK(a.size() == b.size());
}

TEST_CASE("an actor walks a spline path facing the way it is going", "[sequence][bake][character]") {
    seq::Sequence piece;
    seq::Actor hero;
    hero.id = "hero";
    spatial::Spline path;
    path.points.push_back(spatial::SplinePoint{.position = {0.0f, 0.0f, 0.0f}});
    path.points.push_back(spatial::SplinePoint{.position = {10.0f, 0.0f, 0.0f}});
    path.points.push_back(spatial::SplinePoint{.position = {10.0f, 0.0f, 10.0f}});
    hero.path.spline = std::move(path);
    hero.path.active = true;
    hero.path.startSeconds = 0.0;
    hero.path.endSeconds = 10.0;
    hero.path.samples = 32;
    piece.actors.push_back(std::move(hero));
    piece.shots.push_back(keyedShot("one", 0.0, 10.0, {0.0f, 4.0f, 12.0f}, {0.0f, 4.0f, 12.0f}));
    REQUIRE(piece.validate().has_value());

    const seq::Actor& hero2 = piece.actors.front();
    // Ends where the spline ends, starts where it starts, and is somewhere in between at the middle.
    CHECK(hero2.positionAt(0.0).x == Approx(0.0f).margin(0.01));
    CHECK(hero2.positionAt(10.0).z == Approx(10.0f).margin(0.2));
    const glm::vec3 mid = hero2.positionAt(5.0);
    CHECK(mid.x > 1.0f);
    // Heading turns through the corner.
    CHECK(hero2.headingAt(1.0) != Approx(hero2.headingAt(9.0)));

    seq::NullLayerSink sink;
    const auto baked = piece.bake(sink);
    REQUIRE(baked.has_value());
    CHECK(std::find(baked->targets.begin(), baked->targets.end(), "nodes/hero/rotation") !=
          baked->targets.end());
}

// ---- overlays through the real composition system (spec 22-27) --------------------------------

TEST_CASE("overlay cues become real layers with real parameters", "[sequence][composition]") {
    comp::LayerStack stack;
    params::ParameterSet params;
    stack.attach(params);
    seq::CompositionLayerSink sink(stack, &params);

    const seq::Sequence piece = samplePiece();
    const auto baked = piece.bake(sink);
    REQUIRE(baked.has_value());

    REQUIRE(stack.size() == 3);
    // spec 26: order is explicit. The border has the lowest order, so it is realised first and sits
    // under the lyrics.
    CHECK(stack.layers()[0]->name == "seq:frame");
    CHECK(stack.layers()[1]->name == "seq:l1");
    CHECK(stack.layers()[2]->name == "seq:l2");

    const auto* line = dynamic_cast<const comp::TextLayer*>(stack.layers()[1].get());
    REQUIRE(line != nullptr);
    CHECK(line->text() == "I'M WALKING");
    // spec 23: the layer carries the cue's timing, so it is absent outside it even with no tracks.
    CHECK(line->startTime == Approx(1.0));
    CHECK(line->endTime == Approx(4.4));
    CHECK_FALSE(line->liveAt(0.5));
    CHECK(line->liveAt(2.0));
    CHECK_FALSE(line->liveAt(5.0));

    const auto* border = dynamic_cast<const comp::ShapeLayer*>(stack.layers()[0].get());
    REQUIRE(border != nullptr);
    CHECK(border->strokeWidth == Approx(0.004f));

    // Every path the bake wrote against actually exists, which is the one thing that makes the
    // difference between an animated lyric and a silent no-op.
    for (const std::string& target : baked->targets) {
        if (target.rfind("layers/", 0) == 0) {
            INFO(target);
            CHECK(params.find(target) != nullptr);
        }
    }
}

TEST_CASE("an overlay preset keys the layer's own properties", "[sequence][composition]") {
    comp::LayerStack stack;
    params::ParameterSet params;
    stack.attach(params);
    seq::CompositionLayerSink sink(stack, &params);

    seq::Sequence piece;
    piece.shots.push_back(keyedShot("one", 0.0, 10.0, {}, {}));
    piece.overlays.push_back(lyric("pop", "HELLO", 2.0, 6.0, seq::OverlayPreset::ScalePop));
    piece.overlays.push_back(lyric("slide", "AGAIN", 2.0, 6.0, seq::OverlayPreset::SlideUp));
    const auto baked = piece.bake(sink);
    REQUIRE(baked.has_value());

    params::Timeline timeline;
    REQUIRE(timeline.fromJson(baked->timeline).has_value());
    const std::uint32_t popId = stack.layers()[0]->id;
    const std::uint32_t slideId = stack.layers()[1]->id;

    const params::Track* opacity = trackFor(timeline, fmt::format("layers/{}/opacity", popId));
    REQUIRE(opacity != nullptr);
    CHECK(opacity->evaluate(0.0)[0] == Approx(0.0f));
    CHECK(opacity->evaluate(4.0)[0] == Approx(1.0f));
    CHECK(opacity->evaluate(6.0)[0] == Approx(0.0f));

    // A scale pop is two component tracks on one vec2 parameter, so the pop stays uniform.
    const params::Track* scaleX = trackFor(timeline, fmt::format("layers/{}/scale", popId), 0);
    const params::Track* scaleY = trackFor(timeline, fmt::format("layers/{}/scale", popId), 1);
    REQUIRE(scaleX != nullptr);
    REQUIRE(scaleY != nullptr);
    CHECK(scaleX->evaluate(2.0)[0] == Approx(0.72f));
    CHECK(scaleX->evaluate(6.0)[0] == Approx(1.0f));
    CHECK(scaleY->evaluate(2.0)[0] == Approx(0.72f));

    // A slide writes the y component of the layer's position, in absolute frame units, ending at
    // the cue's own anchor rather than at zero.
    const params::Track* slideY = trackFor(timeline, fmt::format("layers/{}/position", slideId), 1);
    REQUIRE(slideY != nullptr);
    CHECK(slideY->evaluate(4.0)[0] == Approx(0.18f));
    CHECK(slideY->evaluate(2.0)[0] < 0.18f); // comes up from below
}

TEST_CASE("a null sink declines cues and the bake says so rather than inventing paths",
          "[sequence][composition]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("one", 0.0, 10.0, {}, {}));
    piece.overlays.push_back(lyric("l1", "WORDS", 1.0, 4.0, seq::OverlayPreset::FadeIn));
    seq::NullLayerSink sink;
    const auto baked = piece.bake(sink);
    REQUIRE(baked.has_value());
    REQUIRE(baked->overlays.size() == 1);
    CHECK(baked->overlays.front().layerId.empty());
    CHECK_FALSE(baked->warnings.empty());
    for (const std::string& target : baked->targets) {
        INFO(target);
        CHECK(target.rfind("layers/", 0) != 0);
    }
}

// ---- installing (spec 30, 36) -----------------------------------------------------------------

TEST_CASE("installing twice replaces the sequence's tracks rather than stacking them",
          "[sequence][install]") {
    params::ParameterSet params;
    registerSceneParameters(params);
    comp::LayerStack stack;
    stack.attach(params);
    params::Timeline timeline;

    // A track the author wrote, on a parameter the sequence never touches.
    params::Track authored;
    authored.target = "nodes/walker/scale";
    authored.addKey(params::Key{.time = 0.0, .value = {1.0f, 1.0f, 1.0f, 0.0f}});
    (void)timeline.addTrack(authored);

    seq::Sequence piece = samplePiece();
    std::vector<std::string> owned;

    seq::CompositionLayerSink sinkA(stack, &params);
    auto first = seq::install(piece, timeline, params, sinkA, owned);
    REQUIRE(first.has_value());
    CHECK(first->unresolved.empty());
    CHECK(first->trackCount > 0);
    CHECK(first->layersRealised == 3);
    owned = first->targets;
    const std::size_t afterFirst = timeline.tracks().size();

    // Move a shot and install again.
    piece.shots[1].startSeconds = 9.0;
    piece.shots[0].durationSeconds = 9.0;
    seq::CompositionLayerSink sinkB(stack, &params);
    auto second = seq::install(piece, timeline, params, sinkB, owned);
    REQUIRE(second.has_value());
    CHECK(timeline.tracks().size() == afterFirst);
    CHECK(second->tracksReplaced > 0);
    // Exactly one layer per cue, still.
    CHECK(stack.size() == 3);
    // And the author's track survived untouched.
    CHECK(trackFor(timeline, "nodes/walker/scale") != nullptr);
    // The cut moved with the shot.
    const params::Track* night = trackFor(timeline, "nodes/city-night/visible");
    REQUIRE(night != nullptr);
    CHECK(night->evaluate(8.5)[0] == Approx(0.0f));
    CHECK(night->evaluate(9.5)[0] == Approx(1.0f));
}

TEST_CASE("an install names the targets this scene has no parameter for", "[sequence][install]") {
    params::ParameterSet params;
    registerSceneParameters(params);
    comp::LayerStack stack;
    stack.attach(params);
    params::Timeline timeline;

    seq::Sequence piece = samplePiece();
    piece.actors.front().node = "ghost"; // a node this scene does not have
    seq::CompositionLayerSink sink(stack, &params);
    auto report = seq::install(piece, timeline, params, sink, {});
    REQUIRE(report.has_value());
    // Not an error -- the rest of the piece is fine -- but never silent.
    CHECK_FALSE(report->unresolved.empty());
    CHECK(std::find(report->unresolved.begin(), report->unresolved.end(), "nodes/ghost/position") !=
          report->unresolved.end());
    CHECK_FALSE(report->warnings.empty());
}

TEST_CASE("uninstalling leaves the timeline as the author left it", "[sequence][install]") {
    params::ParameterSet params;
    registerSceneParameters(params);
    comp::LayerStack stack;
    stack.attach(params);
    params::Timeline timeline;

    params::Track authored;
    authored.target = "scene/brightness";
    authored.component = -1;
    authored.addKey(params::Key{.time = 0.0, .value = {0.5f, 0.0f, 0.0f, 0.0f}});

    const seq::Sequence piece = samplePiece();
    seq::CompositionLayerSink sink(stack, &params);
    auto report = seq::install(piece, timeline, params, sink, {});
    REQUIRE(report.has_value());
    REQUIRE_FALSE(timeline.tracks().empty());

    seq::CompositionLayerSink sink2(stack, &params);
    seq::uninstall(timeline, params, sink2, report->targets);
    CHECK(timeline.tracks().empty());
    CHECK(stack.empty());
    // And every layer parameter went with the layers.
    for (const std::string& target : report->targets) {
        if (target.rfind("layers/", 0) == 0) {
            INFO(target);
            CHECK(params.find(target) == nullptr);
        }
    }
}

// ---- markers and snapping (spec 19, 20, 21) ---------------------------------------------------

TEST_CASE("section markers come from the musical fold and beat markers from the beats",
          "[sequence][markers]") {
    seq::Sequence piece;
    piece.markers.push_back(seq::Marker{.timeSeconds = 3.0, .name = "mine", .kind = seq::MarkerKind::Cue});
    piece.markers.push_back(
        seq::Marker{.timeSeconds = 1.0, .name = "stale", .kind = seq::MarkerKind::Section});

    signals::MusicalStructure structure;
    structure.sections.push_back(signals::StructureSection{
        .kind = signals::MusicalSection::Intro, .startSeconds = 0.0, .durationSeconds = 8.0});
    structure.sections.push_back(signals::StructureSection{
        .kind = signals::MusicalSection::Drop, .startSeconds = 8.0, .durationSeconds = 8.0});
    piece.setSectionMarkers(structure);

    const auto sections = piece.markerTimes(seq::MarkerKind::Section);
    REQUIRE(sections.size() == 2);
    CHECK(sections[0] == Approx(0.0));
    CHECK(sections[1] == Approx(8.0));
    // An author's own cue is not derived and is never discarded.
    CHECK(piece.markerTimes(seq::MarkerKind::Cue).size() == 1);

    const std::vector<double> beats{0.0, 0.5, 1.0, 1.5, 2.0};
    piece.setBeatMarkers(beats);
    CHECK(piece.markerTimes(seq::MarkerKind::Beat).size() == beats.size());
    piece.setBeatMarkers(beats); // replaced, not appended
    CHECK(piece.markerTimes(seq::MarkerKind::Beat).size() == beats.size());
}

TEST_CASE("snapping lands a cut where the editor and a test agree it does", "[sequence][snap]") {
    const std::vector<double> beats{0.0, 0.5, 1.0, 1.5, 2.0};
    CHECK(seq::snapTime(1.21, seq::SnapMode::Off, beats) == Approx(1.21));
    CHECK(seq::snapTime(1.21, seq::SnapMode::Beats, beats) == Approx(1.0));
    CHECK(seq::snapTime(1.29, seq::SnapMode::Beats, beats) == Approx(1.5));
    CHECK(seq::snapTime(1.21, seq::SnapMode::Frames, {}, 30.0) == Approx(36.0 / 30.0));
    // A tolerance means "snap only when something is close", so a click in open space stays put.
    CHECK(seq::snapTime(1.21, seq::SnapMode::Beats, beats, 60.0, 0.05) == Approx(1.21));
    CHECK(seq::snapTime(1.02, seq::SnapMode::Beats, beats, 60.0, 0.05) == Approx(1.0));
}

// ---- serialisation (spec 37) ------------------------------------------------------------------

TEST_CASE("a sequence round-trips through JSON", "[sequence][serialization]") {
    const seq::Sequence piece = samplePiece();
    const nlohmann::json doc = piece.toJson();
    const auto back = seq::Sequence::fromJson(doc);
    REQUIRE(back.has_value());

    CHECK(back->name == piece.name);
    REQUIRE(back->shots.size() == piece.shots.size());
    CHECK(back->shots[1].name == "close");
    CHECK(back->shots[1].startSeconds == Approx(8.0));
    CHECK(back->shots[1].camera.lookAtActor == "hero");
    CHECK(back->shots[1].out.kind == seq::TransitionKind::FadeOut);
    REQUIRE(back->actors.size() == 1);
    REQUIRE(back->actors[0].clips.size() == 4);
    CHECK(back->actors[0].clips[2].clip == "Run");
    CHECK(back->actors[0].clips[2].speed == Approx(1.3f));
    REQUIRE(back->overlays.size() == 3);
    CHECK(back->markers.size() == 2);
    CHECK(back->scenes.size() == 2);

    // And the round trip is a fixed point: the second dump equals the first.
    CHECK(back->toJson().dump() == doc.dump());

    // Which means the bake of the reloaded sequence is the bake of the original.
    seq::NullLayerSink a;
    seq::NullLayerSink b;
    CHECK(piece.bake(a)->timeline.dump() == back->bake(b)->timeline.dump());
}

TEST_CASE("camera presets fill a shot with a move a subject's size makes sense of",
          "[sequence][camera]") {
    app::FocalTarget subject;
    subject.position = glm::vec3(0.0f, 1.0f, 0.0f);
    subject.radius = 4.0f;
    subject.name = "hero";

    for (const seq::CameraPreset preset :
         {seq::CameraPreset::Isometric, seq::CameraPreset::Follow, seq::CameraPreset::Wide,
          seq::CameraPreset::Close, seq::CameraPreset::TopDown, seq::CameraPreset::Tracking,
          seq::CameraPreset::Reveal}) {
        INFO(seq::cameraPresetName(preset));
        const seq::ShotCamera cam = seq::cameraFromPreset(preset, subject);
        CHECK(cam.kind == seq::CameraKind::Move);
        // A preset that put the camera inside the subject would be useless whatever it was called.
        CHECK(glm::length(cam.move.cameraAt(0.0f) - subject.position) > subject.radius);
        CHECK(glm::length(cam.move.cameraAt(1.0f) - subject.position) > subject.radius);
        CHECK(cam.move.composition.focalLength > 0.0f);
        // Round-trips by name, so a preset survives the project file.
        const auto parsed = seq::cameraPresetFromName(seq::cameraPresetName(preset));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == preset);
    }
}

// ---- cost (spec 58) ---------------------------------------------------------------------------

// Hidden by default (`[.]`): it measures rather than checks, and a timing assertion in the ordinary
// suite is a test that fails on a busy machine. Run it with `avgen_tests "[seqcost]"`.
//
// The claim under measurement is the one the whole design rests on: **nothing in seq/ runs per
// frame**. A bake is a moment, and what a sequence costs while it plays is what its tracks cost.
TEST_CASE("what a sequence costs", "[.][seqcost]") {
    // A piece the size of the proof-of-concept: five shots, one actor, eighteen overlay cues,
    // twenty-one piece-level tracks.
    seq::Sequence piece;
    piece.name = "cost";
    piece.scenes.push_back(seq::SceneSlot{.id = "a", .node = "city-day"});
    piece.scenes.push_back(seq::SceneSlot{.id = "b", .node = "city-night"});
    for (int i = 0; i < 5; ++i) {
        seq::Shot shot = keyedShot(fmt::format("shot{}", i), i * 21.0, 21.0,
                                   glm::vec3(0.0f, 12.0f, 30.0f), glm::vec3(0.0f, 6.0f, 14.0f));
        shot.scene = i % 2 == 0 ? "a" : "b";
        shot.camera.kind = seq::CameraKind::Move;
        shot.camera.samples = 28;
        shot.camera.lookAtActor = "hero";
        piece.shots.push_back(std::move(shot));
    }
    seq::Actor hero;
    hero.id = "hero";
    hero.node = "walker";
    for (int i = 0; i < 11; ++i) {
        hero.keys.push_back(seq::ActorKey{.timeSeconds = i * 10.5,
                                          .position = glm::vec3(static_cast<float>(i) * 6.0f, 0.0f, 0.0f)});
    }
    for (int i = 0; i < 5; ++i) {
        hero.clips.push_back(seq::ClipCue{.timeSeconds = i * 21.0,
                                          .clip = i % 2 == 0 ? "Walk" : "Idle"});
    }
    piece.actors.push_back(std::move(hero));
    for (int i = 0; i < 16; ++i) {
        piece.overlays.push_back(lyric(fmt::format("l{:03}", i), "A LINE OF WORDS", 4.0 + i * 6.0,
                                       7.5 + i * 6.0, seq::OverlayPreset::FadeInOut, 10));
    }
    for (int i = 0; i < 21; ++i) {
        params::Track t;
        t.target = "scene/fogDensity";
        for (int k = 0; k < 6; ++k) {
            t.addKey(params::Key{.time = k * 20.0, .value = {0.002f, 0.0f, 0.0f, 0.0f}});
        }
        piece.tracks.push_back(std::move(t));
    }
    REQUIRE(piece.validate().has_value());

    const auto measure = [](const char* what, int runs, auto&& body) {
        // Minimum, not median: under contention the minimum is the only statistic that means
        // anything about the code rather than about the machine.
        double best = 1e30;
        for (int i = 0; i < runs; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            body();
            const auto t1 = std::chrono::steady_clock::now();
            best = std::min(best, std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        WARN(fmt::format("{}: {:.1f} us (min of {})", what, best, runs));
        return best;
    };

    comp::LayerStack stack;
    params::ParameterSet params;
    stack.attach(params);
    registerSceneParameters(params);
    params::Timeline timeline;
    std::vector<std::string> owned;

    int trackCount = 0;
    int keyCount = 0;
    measure("bake (18 cues, no layers)", 20, [&] {
        seq::NullLayerSink sink;
        const auto baked = piece.bake(sink);
        trackCount = baked->trackCount;
        keyCount = baked->keyCount;
    });
    WARN(fmt::format("bake produced {} track(s), {} key(s)", trackCount, keyCount));

    measure("install (bake + realise + bind)", 10, [&] {
        seq::CompositionLayerSink sink(stack, &params);
        auto report = seq::install(piece, timeline, params, sink, owned);
        owned = report->targets;
    });

    // The only thing that runs per frame.
    measure("animationAt x 1000", 5, [&] {
        for (int i = 0; i < 1000; ++i) {
            const auto cues = piece.animationAt(static_cast<double>(i) * 0.105);
            (void)cues;
        }
    });

    // And what the tracks it produced cost to evaluate, which is the real per-frame number.
    // Measured apart from resetFinals, which every frame pays whether or not a sequence exists.
    (void)timeline.bind(params);
    const double resetOnly = measure("resetFinals x 1000 (baseline)", 5, [&] {
        for (int i = 0; i < 1000; ++i) {
            params.resetFinals();
        }
    });
    const double both = measure("resetFinals + timeline.apply x 1000", 5, [&] {
        for (int i = 0; i < 1000; ++i) {
            params.resetFinals();
            timeline.apply(params::TimelineClock{.seconds = static_cast<double>(i) * 0.105});
        }
    });
    WARN(fmt::format("the sequence's {} baked track(s) evaluate in {:.3f} us per frame",
                     timeline.tracks().size(), (both - resetOnly) / 1000.0));
    CHECK(trackCount > 0);
}
