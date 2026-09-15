// The cinematic director (ADR-062). What is worth guarding: a shot kind actually produces the move
// its name promises, overlapping shots are refused rather than silently resolved, a shot without an
// explicit start follows the previous one, and the result is ordinary timeline keys.

#include "app/cinematic.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <map>
#include <set>

using namespace avgen;

namespace {
app::Shot shotOf(app::ShotKind kind, float radius = 10.0f) {
    const auto doc = nlohmann::json::parse(R"({"name":"s","shots":[{"name":"a","duration":5.0,
        "subject":{"name":"hero","position":[0,0,0],"radius":10.0}}]})");
    auto seq = app::Sequence::fromJson(doc);
    REQUIRE(seq.has_value());
    app::Shot s = seq->shots[0];
    s.kind = kind;
    s.subject.radius = radius;
    return s;
}
float distanceAt(const app::Shot& s, float t) {
    return glm::length(s.cameraAt(t) - s.subject.position);
}
} // namespace

TEST_CASE("Each shot kind produces the move its name promises", "[app][cinematic]") {
    const auto doc = nlohmann::json::parse(R"({"name":"film","shots":[
      {"name":"wide","kind":"establish","duration":6.0,"subject":{"position":[0,0,0],"radius":10}},
      {"name":"in","kind":"approach","duration":6.0,"subject":{"position":[0,0,0],"radius":10}},
      {"name":"out","kind":"reveal","duration":6.0,"subject":{"position":[0,0,0],"radius":10}},
      {"name":"down","kind":"descent","duration":6.0,"subject":{"position":[0,0,0],"radius":10}},
      {"name":"up","kind":"ascent","duration":6.0,"subject":{"position":[0,0,0],"radius":10}},
      {"name":"round","kind":"orbit","duration":6.0,"subject":{"position":[0,0,0],"radius":10}}]})");
    auto seq = app::Sequence::fromJson(doc);
    INFO((seq ? std::string() : seq.error().message));
    REQUIRE(seq.has_value());
    const auto& s = seq->shots;

    // Establish barely moves: the world is the subject, not the thing at the centre.
    CHECK_THAT(static_cast<double>(distanceAt(s[0], 1.0f) / distanceAt(s[0], 0.0f)),
               Catch::Matchers::WithinAbs(1.0, 0.15));
    // Approach ends much closer than it began.
    CHECK(distanceAt(s[1], 1.0f) < distanceAt(s[1], 0.0f) * 0.4f);
    // Reveal is the opposite: it starts too close to read and pulls back.
    CHECK(distanceAt(s[2], 1.0f) > distanceAt(s[2], 0.0f) * 4.0f);
    // Descent ends below where it began; ascent ends above.
    CHECK(s[3].cameraAt(1.0f).y < s[3].cameraAt(0.0f).y);
    CHECK(s[4].cameraAt(1.0f).y > s[4].cameraAt(0.0f).y);
    // Orbit travels around without closing in.
    CHECK(glm::length(s[5].cameraAt(1.0f) - s[5].cameraAt(0.0f)) > s[5].subject.radius);
    CHECK_THAT(static_cast<double>(distanceAt(s[5], 1.0f) / distanceAt(s[5], 0.0f)),
               Catch::Matchers::WithinAbs(1.0, 0.05));
}

TEST_CASE("Distances are in subject radii, so a shot works at any scale", "[app][cinematic]") {
    const auto small = shotOf(app::ShotKind::Approach, 2.0f);
    const auto large = shotOf(app::ShotKind::Approach, 60.0f);
    // The same shot against a subject thirty times bigger sits thirty times further out.
    CHECK_THAT(static_cast<double>(distanceAt(large, 0.0f) / distanceAt(small, 0.0f)),
               Catch::Matchers::WithinRel(30.0, 1e-4));
}


// Where `p` lands on screen when the shot's camera aims where `targetAt` says, in normalised offsets
// from centre. This replaces a family of `targetAt(t) == subject.position` checks that asserted the
// aim was dead centre -- which was true only because `CompositionProfile::framing` and `headroom`
// were read by nothing. The projection is the stronger claim: it says the subject is *where the
// composition asked for it*, which is what those fields were authored to mean.
glm::vec2 screenOffsetOf(const app::Shot& shot, float t, glm::vec3 p) {
    const glm::vec3 eye = shot.cameraAt(t);
    const glm::vec3 aim = shot.targetAt(t);
    glm::vec3 forward = aim - eye;
    const float d = glm::length(forward);
    if (d < 1e-5f) {
        return glm::vec2(0.0f);
    }
    forward /= d;
    glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
    right = glm::dot(right, right) < 1e-8f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::normalize(right);
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const glm::vec3 v = p - eye;
    const float z = glm::dot(v, forward);
    if (z < 1e-5f) {
        return glm::vec2(1e9f);
    }
    const float f = std::max(shot.composition.focalLength, 1e-3f);
    return glm::vec2(glm::dot(v, right) / z * (f / 18.0f), glm::dot(v, up) / z * (f / 12.0f));
}

// The offset the shot asked for: framing, with headroom sitting the subject lower.
glm::vec2 requestedOffset(const app::Shot& shot) {
    return glm::vec2(shot.composition.framing.x, shot.composition.framing.y - shot.composition.headroom);
}

TEST_CASE("Every kind aims at its subject, except the one that is going somewhere",
          "[app][cinematic]") {
    for (const auto kind : {app::ShotKind::Establish, app::ShotKind::Approach, app::ShotKind::Reveal,
                            app::ShotKind::Orbit, app::ShotKind::Track, app::ShotKind::Descent}) {
        const auto s = shotOf(kind);
        INFO(app::shotKindName(kind));
        // The subject lands where the composition asked, which since `framing` and `headroom` were
        // wired is not dead centre. Asserting the aim equalled the subject asserted that those two
        // fields did nothing.
        const glm::vec2 where = screenOffsetOf(s, 0.5f, s.subject.position);
        CHECK(glm::length(where - requestedOffset(s)) < 1e-3f);
    }
    // Passage looks where it is going: aiming back at what you are flying through reads as an error.
    const auto passage = shotOf(app::ShotKind::Passage);
    CHECK(glm::length(passage.targetAt(0.5f) - passage.subject.position) > 1.0f);
}

TEST_CASE("A shot with no explicit start follows the previous one", "[app][cinematic]") {
    const auto doc = nlohmann::json::parse(R"({"name":"cut","shots":[
      {"name":"a","kind":"establish","duration":4.0,"subject":{"radius":5}},
      {"name":"b","kind":"approach","duration":6.0,"subject":{"radius":5}},
      {"name":"c","kind":"entry","duration":3.5,"subject":{"radius":5}}]})");
    auto seq = app::Sequence::fromJson(doc);
    REQUIRE(seq.has_value());
    CHECK_THAT(seq->shots[1].startSeconds, Catch::Matchers::WithinAbs(4.0, 1e-9));
    CHECK_THAT(seq->shots[2].startSeconds, Catch::Matchers::WithinAbs(10.0, 1e-9));
    CHECK_THAT(seq->durationSeconds(), Catch::Matchers::WithinAbs(13.5, 1e-9));

    REQUIRE(seq->shotAt(5.0) != nullptr);
    CHECK(seq->shotAt(5.0)->name == "b");
    CHECK(seq->shotAt(99.0) == nullptr);
}

TEST_CASE("Overlapping shots are refused rather than silently resolved", "[app][cinematic]") {
    const auto doc = nlohmann::json::parse(R"({"name":"bad","shots":[
      {"name":"a","duration":10.0,"start":0.0,"subject":{"radius":5}},
      {"name":"b","duration":5.0,"start":4.0,"subject":{"radius":5}}]})");
    auto seq = app::Sequence::fromJson(doc);
    REQUIRE(!seq.has_value());
    CHECK(seq.error().message.find("previous shot") != std::string::npos);
}

TEST_CASE("A subject with no size has no shot", "[app][cinematic]") {
    const auto doc = nlohmann::json::parse(R"({"name":"bad","shots":[
      {"name":"a","duration":4.0,"subject":{"radius":0.0}}]})");
    auto seq = app::Sequence::fromJson(doc);
    REQUIRE(!seq.has_value());
    CHECK(seq.error().message.find("radii") != std::string::npos);
}

TEST_CASE("A sequence bakes into ordinary timeline tracks", "[app][cinematic]") {
    const auto doc = nlohmann::json::parse(R"({"name":"film","shots":[
      {"name":"a","kind":"approach","duration":6.0,"subject":{"position":[3,1,-4],"radius":8},
       "composition":{"focalLength":28.0,"aperture":2.0}},
      {"name":"b","kind":"orbit","duration":8.0,"subject":{"position":[3,1,-4],"radius":8}}]})");
    auto seq = app::Sequence::fromJson(doc);
    REQUIRE(seq.has_value());
    const auto tracks = seq->toTimelineTracks(6);
    REQUIRE(tracks.is_array());
    REQUIRE(tracks.size() == 7);

    // The targets are the parameter paths the engine's timeline already drives; nothing new had to
    // be taught to the camera.
    CHECK(tracks[0]["target"] == "camera/position");
    CHECK(tracks[1]["target"] == "camera/target");
    CHECK(tracks[2]["target"] == "camera/lens/focalLength");
    CHECK(tracks[3]["target"] == "camera/lens/aperture");
    CHECK(tracks[4]["target"] == "camera/lens/focusDistance");
    CHECK(tracks[5]["target"] == "camera/focus/emphasis");
    // ...and the mode that makes the first two readable at all: a composition ignores
    // `camera/position` and `camera/target` unless it is in free mode, and defaults to orbit.
    CHECK(tracks[6]["target"] == "camera/mode");
    REQUIRE(tracks[6]["keys"].size() == 1);
    CHECK(tracks[6]["keys"][0]["interp"] == "step");
    CHECK_THAT(tracks[6]["keys"][0]["value"].get<double>(), Catch::Matchers::WithinAbs(1.0, 1e-9));

    REQUIRE(tracks[0]["keys"].size() == 12); // two shots x six samples
    CHECK_THAT(tracks[0]["keys"][0]["time"].get<double>(), Catch::Matchers::WithinAbs(0.0, 1e-9));
    CHECK_THAT(tracks[0]["keys"][11]["time"].get<double>(), Catch::Matchers::WithinAbs(14.0, 1e-9));
    // Keys carry already-eased positions, so they interpolate linearly; easing them twice would
    // flatten every move's ends.
    CHECK(tracks[0]["keys"][3]["interp"] == "linear");
    // The lens gets one key per shot: a focal length that slides through a shot is a zoom, and a
    // zoom should be asked for.
    REQUIRE(tracks[2]["keys"].size() == 2);
    CHECK_THAT(tracks[2]["keys"][0]["value"].get<double>(), Catch::Matchers::WithinAbs(28.0, 1e-6));
}

TEST_CASE("A sequence round-trips through JSON", "[app][cinematic]") {
    const auto doc = nlohmann::json::parse(R"({"name":"film","shots":[
      {"name":"a","kind":"reveal","duration":7.5,"subject":{"name":"bloom","position":[1,2,3],"radius":9},
       "distance":[0.8,14.0],"elevation":[0.05,0.4],"azimuth":[0.0,1.2],
       "composition":{"focalLength":21.0,"aperture":1.8,"headroom":0.2,"focusOnSubject":false},
       "easeIn":true,"easeOut":false}]})");
    auto seq = app::Sequence::fromJson(doc);
    REQUIRE(seq.has_value());
    auto again = app::Sequence::fromJson(seq->toJson());
    REQUIRE(again.has_value());
    const auto& a = seq->shots[0];
    const auto& b = again->shots[0];
    CHECK(a.name == b.name);
    CHECK(a.kind == b.kind);
    CHECK(a.subject.name == b.subject.name);
    CHECK_THAT(static_cast<double>(a.startDistance), Catch::Matchers::WithinAbs(static_cast<double>(b.startDistance), 1e-6));
    CHECK_THAT(static_cast<double>(a.endAzimuth), Catch::Matchers::WithinAbs(static_cast<double>(b.endAzimuth), 1e-6));
    CHECK_THAT(static_cast<double>(a.composition.focalLength), Catch::Matchers::WithinAbs(static_cast<double>(b.composition.focalLength), 1e-6));
    CHECK(a.composition.focusOnSubject == b.composition.focusOnSubject);
    CHECK(a.easeOut == b.easeOut);
}

// ---- milestone 9: a vocabulary rather than "zoom in, zoom out, rotate" -------------------------

namespace {
// The horizontal angle the camera travels around its subject, which is what "goes round it" means
// and what distinguishes a reveal that turns from one that merely backs off.
float sweptAngle(const app::Shot& s) {
    const glm::vec3 a = s.cameraAt(0.0f) - s.subject.position;
    const glm::vec3 b = s.cameraAt(1.0f) - s.subject.position;
    const glm::vec2 p(a.x, a.z);
    const glm::vec2 q(b.x, b.z);
    if (glm::length(p) < 1e-5f || glm::length(q) < 1e-5f) {
        return 0.0f;
    }
    return std::acos(std::clamp(glm::dot(glm::normalize(p), glm::normalize(q)), -1.0f, 1.0f));
}
// How far the midpoint of the move departs from the straight line between its ends.
float bowFraction(const app::Shot& s) {
    const glm::vec3 a = s.cameraAt(0.0f);
    const glm::vec3 b = s.cameraAt(1.0f);
    const float chord = glm::length(b - a);
    if (chord < 1e-5f) {
        return 0.0f;
    }
    const glm::vec3 mid = s.cameraAt(0.5f);
    const glm::vec3 dir = (b - a) / chord;
    const glm::vec3 onLine = a + dir * glm::dot(mid - a, dir);
    return glm::length(mid - onLine) / chord;
}
float closestApproach(const app::Shot& s) {
    float nearest = 1e9f;
    for (int i = 0; i <= 32; ++i) {
        nearest = std::min(nearest, distanceAt(s, static_cast<float>(i) / 32.0f));
    }
    return nearest;
}
app::Shot parseOne(const std::string& shotJson) {
    const auto doc = nlohmann::json::parse("{\"name\":\"s\",\"shots\":[" + shotJson + "]}");
    auto seq = app::Sequence::fromJson(doc);
    REQUIRE(seq.has_value());
    return seq->shots[0];
}
} // namespace

TEST_CASE("Discovery arrives from the side rather than straight down the barrel",
          "[app][cinematic]") {
    const auto s = parseOne(R"({"name":"d","kind":"discovery","duration":8.0,
        "subject":{"position":[0,0,0],"radius":10}})");
    // It closes in, like an approach...
    CHECK(distanceAt(s, 1.0f) < distanceAt(s, 0.0f) * 0.5f);
    // ...but it does not close in along one line, which is the whole difference: coming straight at
    // something that is behind a tree only makes the tree bigger.
    CHECK(sweptAngle(s) > 0.4f);
    CHECK(bowFraction(s) > 0.08f);
}

TEST_CASE("A hero reveal both opens out and goes round", "[app][cinematic]") {
    const auto hero = parseOne(R"({"name":"h","kind":"heroReveal","duration":8.0,
        "subject":{"position":[0,0,0],"radius":10}})");
    const auto plain = parseOne(R"({"name":"r","kind":"reveal","duration":8.0,
        "subject":{"position":[0,0,0],"radius":10}})");
    CHECK(distanceAt(hero, 1.0f) > distanceAt(hero, 0.0f) * 3.0f);
    // Distance alone gives scale and sweep alone gives silhouette; the hero moment needs both, and
    // that is exactly what separates it from the pull-back that was already here.
    CHECK(sweptAngle(hero) > 0.9f);
    CHECK(sweptAngle(hero) > sweptAngle(plain) * 4.0f);
    CHECK(hero.cameraAt(1.0f).y > hero.cameraAt(0.0f).y);
}

TEST_CASE("A flyby passes the subject and comes out the far side, without going through it",
          "[app][cinematic]") {
    const auto s = parseOne(R"({"name":"f","kind":"flyby","duration":4.0,
        "subject":{"position":[0,0,0],"radius":10}})");
    const glm::vec3 a = s.cameraAt(0.0f) - s.subject.position;
    const glm::vec3 b = s.cameraAt(1.0f) - s.subject.position;
    CHECK(glm::dot(glm::vec2(a.x, a.z), glm::vec2(b.x, b.z)) < 0.0f); // out the other side
    // Interpolating a distance from positive to negative puts the midpoint exactly on the subject.
    // The bow is not decoration: it is what keeps the camera outside the thing it is flying past.
    CHECK(closestApproach(s) > s.subject.radius);
    // ...and unlike a passage, a flyby holds the subject in frame. That is the whole distinction.
    CHECK(glm::length(screenOffsetOf(s, 0.5f, s.subject.position) - requestedOffset(s)) < 1e-3f);
    CHECK(glm::length(parseOne(R"({"name":"p","kind":"passage","duration":4.0,
        "subject":{"position":[0,0,0],"radius":10}})").targetAt(0.5f)) > 1.0f);
}

TEST_CASE("A drift holds its aim while the world slides past it", "[app][cinematic]") {
    const auto s = parseOne(R"({"name":"g","kind":"drift","duration":10.0,
        "subject":{"position":[0,0,0],"radius":10}})");
    const glm::vec3 d0 = glm::normalize(s.targetAt(0.0f) - s.cameraAt(0.0f));
    const glm::vec3 d1 = glm::normalize(s.targetAt(1.0f) - s.cameraAt(1.0f));
    // The camera travels...
    CHECK(glm::length(s.cameraAt(1.0f) - s.cameraAt(0.0f)) > s.subject.radius);
    // ...and the aim does not move at all. Panning to hold something during a lateral move cancels
    // exactly the parallax that was the reason for making the move.
    CHECK(glm::dot(d0, d1) > 0.99999f);
    // Contrast with a track, which is the same travel with the aim following.
    const auto tracked = parseOne(R"({"name":"t","kind":"track","duration":10.0,
        "subject":{"position":[0,0,0],"radius":10}})");
    const glm::vec3 t0 = glm::normalize(tracked.targetAt(0.0f) - tracked.cameraAt(0.0f));
    const glm::vec3 t1 = glm::normalize(tracked.targetAt(1.0f) - tracked.cameraAt(1.0f));
    CHECK(glm::dot(t0, t1) < 0.999f);
}

TEST_CASE("A transition leaves one subject and finds another", "[app][cinematic]") {
    const auto s = parseOne(R"({"name":"x","kind":"transition","duration":6.0,
        "subject":{"name":"first","position":[0,0,0],"radius":6},
        "handoff":{"name":"second","position":[80,0,-40],"radius":6}})");
    // It holds the first subject at the start rather than drifting off it from frame one: a target
    // already moving on the opening frame means the first subject is never actually held.
    CHECK(glm::length(screenOffsetOf(s, 0.0f, s.subject.position) - requestedOffset(s)) < 1e-3f);
    CHECK(glm::length(screenOffsetOf(s, 0.15f, s.subject.position) - requestedOffset(s)) < 1e-3f);
    REQUIRE(s.handoff.has_value());
    CHECK(glm::length(screenOffsetOf(s, 1.0f, s.handoff->position) - requestedOffset(s)) < 1e-3f);
    CHECK(glm::length(s.targetAt(0.5f) - s.subject.position) > 1.0f);
    // And it ends near the thing it went to, not the thing it left.
    CHECK(glm::length(s.cameraAt(1.0f) - s.handoff->position) <
          glm::length(s.cameraAt(1.0f) - s.subject.position));
}

TEST_CASE("A transition with nothing to transition to is refused", "[app][cinematic]") {
    const auto doc = nlohmann::json::parse(R"({"name":"bad","shots":[
      {"name":"x","kind":"transition","duration":6.0,"subject":{"radius":6}}]})");
    auto seq = app::Sequence::fromJson(doc);
    REQUIRE(!seq.has_value());
    CHECK(seq.error().message.find("handoff") != std::string::npos);
}

TEST_CASE("A shot may be authored as two points, for a move about a place",
          "[app][cinematic]") {
    // The reference move this project is measured against (audit 1.4): forward through the valley
    // over ninety seconds, rising twelve metres, never orbiting and never zooming. Expressing it as
    // a distance in radii would mean dividing a valley by a radius it does not have.
    const auto s = parseOne(R"({"name":"valley","kind":"establish","duration":90.0,
        "subject":{"name":"valley","position":[0,0,-60],"radius":40},
        "startPosition":[-14,-2.4,6],"endPosition":[-30,10,-118]})");
    CHECK(glm::length(s.cameraAt(0.0f) - glm::vec3(-14.0f, -2.4f, 6.0f)) < 1e-4f);
    CHECK(glm::length(s.cameraAt(1.0f) - glm::vec3(-30.0f, 10.0f, -118.0f)) < 1e-4f);
    CHECK_THAT(static_cast<double>(s.cameraAt(1.0f).y - s.cameraAt(0.0f).y),
               Catch::Matchers::WithinAbs(12.4, 1e-4));
    // Restraint, as a property rather than as a promise: it only ever goes forward and only ever
    // goes up. A shot that orbited or backed off would break monotonicity on one of the two.
    float lastZ = 1e9f;
    float lastY = -1e9f;
    for (int i = 0; i <= 40; ++i) {
        const glm::vec3 p = s.cameraAt(static_cast<float>(i) / 40.0f);
        INFO("sample " << i);
        CHECK(p.z <= lastZ + 1e-3f);
        CHECK(p.y >= lastY - 1e-3f);
        lastZ = p.z;
        lastY = p.y;
    }
}

TEST_CASE("A height range replaces the elevation the kind would have used", "[app][cinematic]") {
    const auto plain = parseOne(R"({"name":"a","kind":"orbit","duration":8.0,
        "subject":{"position":[0,0,0],"radius":10}})");
    const auto pinned = parseOne(R"({"name":"b","kind":"orbit","duration":8.0,
        "subject":{"position":[0,0,0],"radius":10},"height":[2.0,14.0]})");
    CHECK(plain.cameraAt(0.0f).y > 1.5f); // the kind's own elevation, in radii
    CHECK_THAT(static_cast<double>(pinned.cameraAt(0.0f).y), Catch::Matchers::WithinAbs(2.0, 1e-4));
    CHECK_THAT(static_cast<double>(pinned.cameraAt(1.0f).y), Catch::Matchers::WithinAbs(14.0, 1e-4));
    // The horizontal shape of the orbit is untouched; only the height was overridden.
    CHECK_THAT(static_cast<double>(glm::length(glm::vec2(pinned.cameraAt(0.5f).x, pinned.cameraAt(0.5f).z))),
               Catch::Matchers::WithinRel(
                   static_cast<double>(glm::length(glm::vec2(plain.cameraAt(0.5f).x, plain.cameraAt(0.5f).z))),
                   1e-4));
}

TEST_CASE("A shot may be paced rather than timed", "[app][cinematic]") {
    const auto doc = nlohmann::json::parse(R"({"name":"film","shots":[
      {"name":"a","kind":"establish","duration":6.0,"subject":{"position":[0,0,0],"radius":10}},
      {"name":"b","kind":"flyby","duration":99.0,"speed":30.0,
       "subject":{"position":[0,0,-200],"radius":12}},
      {"name":"c","kind":"track","duration":7.0,"subject":{"position":[0,0,-200],"radius":12}}]})");
    auto seq = app::Sequence::fromJson(doc);
    REQUIRE(seq.has_value());
    REQUIRE(seq->retime().has_value());

    const auto& fly = seq->shots[1];
    // The duration is now whatever travelling that path at thirty metres a second takes.
    CHECK_THAT(static_cast<double>(fly.pathLength() / fly.speed),
               Catch::Matchers::WithinRel(fly.durationSeconds, 1e-3));
    CHECK(fly.durationSeconds < 90.0);
    // Easing means the middle is faster than the average, which is the number that matters when
    // asking whether a move reads as a teleport.
    CHECK(fly.peakSpeed() > fly.speed);
    // The untouched shots keep their durations, and the whole list is repacked back-to-back.
    CHECK_THAT(seq->shots[0].durationSeconds, Catch::Matchers::WithinAbs(6.0, 1e-9));
    CHECK_THAT(seq->shots[1].startSeconds, Catch::Matchers::WithinAbs(6.0, 1e-9));
    CHECK_THAT(seq->shots[2].startSeconds,
               Catch::Matchers::WithinAbs(6.0 + fly.durationSeconds, 1e-9));
    CHECK(seq->validate().has_value());
}

TEST_CASE("A shot that names a speed but does not move is refused", "[app][cinematic]") {
    const auto doc = nlohmann::json::parse(R"({"name":"film","shots":[
      {"name":"a","kind":"establish","duration":6.0,"speed":10.0,
       "subject":{"position":[0,0,0],"radius":10},
       "distance":[6,6],"azimuth":[0,0],"elevation":[0.2,0.2]}]})");
    auto seq = app::Sequence::fromJson(doc);
    REQUIRE(seq.has_value());
    auto retimed = seq->retime();
    REQUIRE(!retimed.has_value());
    CHECK(retimed.error().message.find("does not move") != std::string::npos);
}

// ---- hero spotlighting -------------------------------------------------------------------------

TEST_CASE("A hero shot in which the hero cannot be seen is refused", "[app][cinematic]") {
    // The failure this exists to catch is a shot that calls itself a hero shot with the hero forty
    // pixels tall, which otherwise only shows up in a render.
    const auto tooFar = nlohmann::json::parse(R"({"name":"bad","shots":[
      {"name":"h","kind":"establish","duration":6.0,"spotlight":{"emphasis":0.9},
       "subject":{"name":"elder","position":[0,0,0],"radius":8},
       "distance":[400,400],"composition":{"focalLength":24.0}}]})");
    auto bad = app::Sequence::fromJson(tooFar);
    REQUIRE(!bad.has_value());
    CHECK(bad.error().message.find("frame") != std::string::npos);

    const auto readable = nlohmann::json::parse(R"({"name":"good","shots":[
      {"name":"h","kind":"establish","duration":6.0,"spotlight":{"emphasis":0.9},
       "subject":{"name":"elder","position":[0,0,0],"radius":8},
       "distance":[9,9],"composition":{"focalLength":50.0}}]})");
    auto good = app::Sequence::fromJson(readable);
    INFO((good ? std::string() : good.error().message));
    REQUIRE(good.has_value());
    CHECK(good->shots[0].subjectCoverageAt(0.5f) > 0.06f);
    // A shot that never claimed to be a hero shot is not held to it.
    const auto quiet = nlohmann::json::parse(R"({"name":"ok","shots":[
      {"name":"h","kind":"establish","duration":6.0,
       "subject":{"name":"elder","position":[0,0,0],"radius":8},
       "distance":[400,400],"composition":{"focalLength":24.0}}]})");
    CHECK(app::Sequence::fromJson(quiet).has_value());
}

TEST_CASE("The spotlight is a span the rest of the engine can read", "[app][cinematic]") {
    const auto doc = nlohmann::json::parse(R"({"name":"film","shots":[
      {"name":"a","kind":"establish","duration":6.0,"subject":{"name":"valley","radius":30}},
      {"name":"b","kind":"heroReveal","duration":8.0,"spotlight":{"emphasis":0.8},
       "subject":{"name":"elder","position":[0,0,0],"radius":8}},
      {"name":"c","kind":"orbit","duration":6.0,"spotlight":{"emphasis":0.6},
       "subject":{"name":"elder","position":[0,0,0],"radius":8}},
      {"name":"d","kind":"drift","duration":6.0,"subject":{"name":"ferns","radius":2}}]})");
    auto seq = app::Sequence::fromJson(doc);
    INFO((seq ? std::string() : seq.error().message));
    REQUIRE(seq.has_value());

    CHECK(seq->spotlightAt(3.0) == nullptr);
    REQUIRE(seq->spotlightAt(10.0) != nullptr);
    CHECK(seq->spotlightAt(10.0)->name == "elder");
    CHECK(seq->spotlightAt(23.0) == nullptr);

    // Two consecutive shots of the same hero are one span. A rig that dimmed and re-lit the elder
    // across a cut it is still the subject of is worse than one that never touched it.
    const auto spans = seq->spotlightSpans();
    REQUIRE(spans.size() == 1);
    CHECK(spans[0]["subject"] == "elder");
    CHECK_THAT(spans[0]["start"].get<double>(), Catch::Matchers::WithinAbs(6.0, 1e-9));
    CHECK_THAT(spans[0]["end"].get<double>(), Catch::Matchers::WithinAbs(20.0, 1e-9));
    CHECK_THAT(spans[0]["emphasis"].get<double>(), Catch::Matchers::WithinAbs(0.8, 1e-6));

    // On the timeline, emphasis is a state rather than a curve: it holds flat across each shot and
    // changes at the cut, instead of ramping the hero's importance through the shot before it.
    const auto tracks = seq->toTimelineTracks(4);
    const auto& keys = tracks[5]["keys"];
    REQUIRE(keys.size() == 8); // two per shot
    CHECK_THAT(keys[0]["value"].get<double>(), Catch::Matchers::WithinAbs(0.0, 1e-6));
    CHECK_THAT(keys[1]["value"].get<double>(), Catch::Matchers::WithinAbs(0.0, 1e-6));
    CHECK_THAT(keys[2]["value"].get<double>(), Catch::Matchers::WithinAbs(0.8, 1e-6));
    CHECK_THAT(keys[3]["time"].get<double>(), Catch::Matchers::WithinAbs(13.999, 1e-6));
}

TEST_CASE("The lens focuses on what the shot is about", "[app][cinematic]") {
    // ADR-062 recorded focusOnSubject and left it unwired; this is the wire.
    const auto onSubject = parseOne(R"({"name":"a","kind":"approach","duration":6.0,
        "subject":{"position":[0,0,0],"radius":10}})");
    for (const float t : {0.0f, 0.5f, 1.0f}) {
        CHECK_THAT(static_cast<double>(onSubject.focusDistanceAt(t)),
                   Catch::Matchers::WithinRel(static_cast<double>(distanceAt(onSubject, t)), 1e-4));
    }
    // With the subject released, focus follows the aim instead, which for a passage is well ahead
    // of the thing it is flying past.
    const auto ahead = parseOne(R"({"name":"b","kind":"passage","duration":6.0,
        "subject":{"position":[0,0,0],"radius":10},"composition":{"focusOnSubject":false}})");
    CHECK(std::abs(ahead.focusDistanceAt(0.5f) - distanceAt(ahead, 0.5f)) > 1.0f);

    const auto seq = app::Sequence{"f", {onSubject}};
    const auto tracks = seq.toTimelineTracks(5);
    CHECK(tracks[4]["target"] == "camera/lens/focusDistance");
    // Per sample, unlike focal length: a rack focus that follows the subject is not a zoom, it is
    // the lens doing the one thing it must to keep the subject sharp.
    REQUIRE(tracks[4]["keys"].size() == 5);
    CHECK(tracks[4]["keys"][0]["value"].get<double>() >
          tracks[4]["keys"][4]["value"].get<double>());
}

TEST_CASE("Shot vocabulary names round-trip, and a director's words are understood",
          "[app][cinematic]") {
    for (const auto k : {app::ShotKind::Establish, app::ShotKind::Approach, app::ShotKind::Reveal,
                         app::ShotKind::Entry, app::ShotKind::Passage, app::ShotKind::Descent,
                         app::ShotKind::Ascent, app::ShotKind::Orbit, app::ShotKind::Track,
                         app::ShotKind::Discovery, app::ShotKind::HeroReveal, app::ShotKind::Flyby,
                         app::ShotKind::Drift, app::ShotKind::Transition}) {
        const auto again = app::shotKindFromName(app::shotKindName(k));
        REQUIRE(again.has_value());
        CHECK(*again == k);
    }
    // The aliases are one-way, so a sequence does not change spelling on its second round trip.
    CHECK(app::shotKindFromName("follow") == app::ShotKind::Track);
    CHECK(app::shotKindFromName("establishing") == app::ShotKind::Establish);
    CHECK(app::shotKindFromName("hero-reveal") == app::ShotKind::HeroReveal);
    CHECK(std::string(app::shotKindName(app::ShotKind::Track)) == "track");
    CHECK(!app::shotKindFromName("zoom").has_value());
    CHECK(app::lookModeFromName(app::lookModeName(app::LookMode::Parallel)) ==
          app::LookMode::Parallel);
    CHECK(app::movementCurveFromName(app::movementCurveName(app::MovementCurve::Dip)) ==
          app::MovementCurve::Dip);
}

TEST_CASE("The added fields survive a round trip, and unset ones stay unset",
          "[app][cinematic]") {
    const auto doc = nlohmann::json::parse(R"({"name":"film","shots":[
      {"name":"a","kind":"transition","duration":7.0,"look":"handoff","curve":"rise","bow":0.22,
       "subject":{"name":"one","position":[1,2,3],"radius":9},
       "handoff":{"name":"two","position":[40,0,-10],"radius":5},
       "startPosition":[-4,3,20],"height":[2.0,9.5],"speed":6.0,
       "spotlight":{"emphasis":0.55}},
      {"name":"b","kind":"orbit","duration":5.0,"subject":{"radius":4}}]})");
    auto seq = app::Sequence::fromJson(doc);
    INFO((seq ? std::string() : seq.error().message));
    REQUIRE(seq.has_value());
    auto again = app::Sequence::fromJson(seq->toJson());
    REQUIRE(again.has_value());

    const auto& a = seq->shots[0];
    const auto& b = again->shots[0];
    REQUIRE(b.handoff.has_value());
    CHECK(b.handoff->name == "two");
    REQUIRE(b.startPosition.has_value());
    CHECK(glm::length(*a.startPosition - *b.startPosition) < 1e-5f);
    REQUIRE(b.heightRange.has_value());
    CHECK_THAT(static_cast<double>(b.heightRange->y), Catch::Matchers::WithinAbs(9.5, 1e-5));
    CHECK(b.look == app::LookMode::Handoff);
    CHECK(b.curve == app::MovementCurve::Rise);
    CHECK_THAT(static_cast<double>(b.speed), Catch::Matchers::WithinAbs(6.0, 1e-5));
    CHECK(b.spotlight.active);
    CHECK_THAT(static_cast<double>(b.spotlight.emphasis), Catch::Matchers::WithinAbs(0.55, 1e-5));
    CHECK(glm::length(a.cameraAt(0.4f) - b.cameraAt(0.4f)) < 1e-4f);

    // The shot that asked for nothing keeps asking for nothing: writing a resolved default back out
    // would freeze it, and the shot would stop following its kind the first time it round-tripped.
    CHECK(!again->shots[1].look.has_value());
    CHECK(!again->shots[1].curve.has_value());
    CHECK(!again->shots[1].startPosition.has_value());
    CHECK(!again->shots[1].spotlight.active);
}

// ---- milestone 10: a sequence built from the music ---------------------------------------------

namespace {
using avgen::signals::MusicalEvent;
using avgen::signals::MusicalMoment;
using avgen::signals::MusicalSection;

// A piece with the shape a producer would recognise: an intro, a build into a drop, a passage, a
// breakdown, a final build and the drop it was for.
avgen::signals::MusicalStructure referenceStructure(double totalSeconds = 180.0) {
    std::vector<MusicalMoment> m{
        MusicalMoment{MusicalEvent::Build, 24.0, 0.6f},
        MusicalMoment{MusicalEvent::Drop, 40.0, 0.8f},
        MusicalMoment{MusicalEvent::SectionChange, 72.0, 0.5f},
        MusicalMoment{MusicalEvent::Break, 96.0, 0.7f},
        MusicalMoment{MusicalEvent::Build, 120.0, 0.9f},
        MusicalMoment{MusicalEvent::Drop, 136.0, 1.0f},
    };
    return avgen::signals::MusicalStructure::fromMoments(m, totalSeconds);
}

app::DirectionBrief referenceBrief() {
    app::DirectionBrief brief;
    brief.hero = app::FocalTarget{glm::vec3(0.0f, 6.0f, -40.0f), 9.0f, "elder"};
    brief.supporting = {app::FocalTarget{glm::vec3(-30.0f, 0.0f, -10.0f), 3.0f, "fungi-cluster"},
                        app::FocalTarget{glm::vec3(50.0f, 2.0f, -90.0f), 12.0f, "rim-rock"},
                        app::FocalTarget{glm::vec3(10.0f, 1.0f, 20.0f), 2.0f, "ferns"}};
    return brief;
}
} // namespace

TEST_CASE("The drop lands on a reveal, exactly where the drop is", "[app][cinematic][director]") {
    const auto structure = referenceStructure();
    auto seq = app::directFromStructure(structure, referenceBrief());
    INFO((seq ? std::string() : seq.error().message));
    REQUIRE(seq.has_value());

    int drops = 0;
    for (const auto& section : structure.sections) {
        if (section.kind != MusicalSection::Drop && section.kind != MusicalSection::FinalDrop) {
            continue;
        }
        ++drops;
        // Not "a shot near the drop" -- a shot that starts on it. Smoothing the cut away from the
        // moment the music lands is smoothing away the entire reason for reading the structure.
        const app::Shot* landing = nullptr;
        for (const auto& s : seq->shots) {
            if (std::abs(s.startSeconds - section.startSeconds) < 1e-6) {
                landing = &s;
            }
        }
        INFO("drop at " << section.startSeconds);
        REQUIRE(landing != nullptr);
        CHECK((landing->kind == app::ShotKind::HeroReveal || landing->kind == app::ShotKind::Reveal));
        CHECK(landing->spotlight.active);
        // Not *which* subject. ADR-202: the drop lands on a reveal at the moment the music drops,
        // and who it reveals is decided by importance alone -- there is no longer a hero that owns
        // every drop. This used to assert "elder", which was the contract that made raising another
        // subject's importance unable to win it one.
        // A reveal opens out. If the drop landed on a shot that closed in, it is not a reveal.
        CHECK(landing->endDistance > landing->startDistance);
    }
    CHECK(drops == 2);
}

TEST_CASE("A drop that arrives before the shot before it has settled still gets its own shot",
          "[app][cinematic][director]") {
    // The hard case: a three-second build into a drop. Every other rule in the grouping says do not
    // cut that soon, and the drop overrides all of them -- landing the reveal on the moment the
    // music lands is the entire reason for reading the structure at all.
    std::vector<MusicalMoment> m{MusicalMoment{MusicalEvent::SectionChange, 20.0, 0.5f},
                                 MusicalMoment{MusicalEvent::Build, 60.0, 0.8f},
                                 MusicalMoment{MusicalEvent::Drop, 63.0, 1.0f}};
    const auto structure = avgen::signals::MusicalStructure::fromMoments(m, 120.0);
    REQUIRE(structure.at(61.0) != nullptr);
    REQUIRE(structure.at(61.0)->kind == MusicalSection::Build);

    auto seq = app::directFromStructure(structure, referenceBrief());
    INFO((seq ? std::string() : seq.error().message));
    REQUIRE(seq.has_value());
    const app::Shot* landing = seq->shotAt(63.5);
    REQUIRE(landing != nullptr);
    CHECK_THAT(landing->startSeconds, Catch::Matchers::WithinAbs(63.0, 1e-6));
    CHECK((landing->kind == app::ShotKind::HeroReveal || landing->kind == app::ShotKind::Reveal));
    CHECK(landing->spotlight.active);
    // The three-second build in front of it survives too, because a build exists to end.
    const app::Shot* build = seq->shotAt(61.0);
    REQUIRE(build != nullptr);
    CHECK_THAT(build->startSeconds, Catch::Matchers::WithinAbs(60.0, 1e-6));

    // ...but a one-second build is a flash, not a shot. Handed a structure that says so -- which a
    // caller may build directly, not only through the fold -- the director gives that second back to
    // the shot before it rather than leaving a cut nobody can read. The drop still lands on the drop.
    avgen::signals::MusicalStructure byHand;
    byHand.sections = {{MusicalSection::Intro, 0.0, 40.0, 0.3f},
                       {MusicalSection::Build, 40.0, 1.0, 0.9f},
                       {MusicalSection::Drop, 41.0, 79.0, 1.0f}};
    auto seq2 = app::directFromStructure(byHand, referenceBrief());
    INFO((seq2 ? std::string() : seq2.error().message));
    REQUIRE(seq2.has_value());
    REQUIRE(seq2->shotAt(50.0) != nullptr);
    CHECK_THAT(seq2->shotAt(50.0)->startSeconds, Catch::Matchers::WithinAbs(41.0, 1e-6));
    // The drop is one shot from 41 s to the end, and nothing in the film is a flash. The intro
    // ahead of it is forty seconds of passage, which is several shots rather than one hold.
    CHECK(seq2->shots.back().startSeconds == 41.0);
    CHECK(seq2->shots.size() >= 2);
    int dropShots = 0;
    for (const auto& shot : seq2->shots) {
        INFO(shot.name);
        CHECK(shot.durationSeconds > 2.0);
        dropShots += shot.startSeconds >= 41.0 ? 1 : 0;
    }
    CHECK(dropShots == 1);
}

TEST_CASE("A build sets the camera going and is not cut into", "[app][cinematic][director]") {
    const auto structure = referenceStructure();
    auto seq = app::directFromStructure(structure, referenceBrief());
    REQUIRE(seq.has_value());

    int builds = 0;
    for (const auto& section : structure.sections) {
        if (section.kind != MusicalSection::Build && section.kind != MusicalSection::FinalBuild) {
            continue;
        }
        ++builds;
        int opening = 0;
        int inside = 0;
        for (const auto& s : seq->shots) {
            if (std::abs(s.startSeconds - section.startSeconds) < 1e-6) {
                ++opening;
            } else if (s.startSeconds > section.startSeconds + 1e-6 &&
                       s.startSeconds < section.endSeconds() - 1e-6) {
                ++inside;
            }
        }
        INFO("build at " << section.startSeconds);
        CHECK(opening == 1);
        CHECK(inside == 0); // one continuous move, not a montage
    }
    CHECK(builds == 2);

    // ...and the camera actually goes somewhere during it, which is the difference between a build
    // and the establishing shot before it.
    const app::Shot* build = seq->shotAt(30.0);
    const app::Shot* intro = seq->shotAt(5.0);
    REQUIRE(build != nullptr);
    REQUIRE(intro != nullptr);
    CHECK(build->pathLength() / static_cast<float>(build->durationSeconds) >
          intro->pathLength() / static_cast<float>(intro->durationSeconds) * 3.0f);
}

TEST_CASE("The director does not cut constantly", "[app][cinematic][director]") {
    // The same three-minute piece, but with the analyser twitching: fourteen extra section changes,
    // fifty energy trends and a beat every half second. None of it may become a cut.
    std::vector<MusicalMoment> m{
        MusicalMoment{MusicalEvent::Build, 24.0, 0.6f}, MusicalMoment{MusicalEvent::Drop, 40.0, 0.8f},
        MusicalMoment{MusicalEvent::Break, 96.0, 0.7f}, MusicalMoment{MusicalEvent::Build, 120.0, 0.9f},
        MusicalMoment{MusicalEvent::Drop, 136.0, 1.0f}};
    for (double t = 0.0; t < 180.0; t += 0.5) {
        m.push_back(MusicalMoment{MusicalEvent::Beat, t, 0.5f});
    }
    for (double t = 46.0; t < 94.0; t += 3.5) {
        m.push_back(MusicalMoment{MusicalEvent::SectionChange, t, 0.5f});
        m.push_back(MusicalMoment{MusicalEvent::EnergyRise, t + 1.0, 0.6f});
    }
    std::stable_sort(m.begin(), m.end(), [](const MusicalMoment& a, const MusicalMoment& b) {
        return a.timeSeconds < b.timeSeconds;
    });
    const auto structure = avgen::signals::MusicalStructure::fromMoments(m, 180.0);
    auto seq = app::directFromStructure(structure, referenceBrief());
    INFO((seq ? std::string() : seq.error().message));
    REQUIRE(seq.has_value());

    // Three hundred and sixty beats and a dozen section changes come out as a handful of shots.
    // Not one per section: a passage longer than a shot anybody would hold becomes several, so the
    // count is bounded by the cadence below rather than by the number of sections.
    CHECK(seq->shots.size() <= 10);
    const auto cadence = seq->validateCadence(4.0, 8.0);
    INFO((cadence ? std::string() : cadence.error().message));
    CHECK(cadence.has_value());
    CHECK(seq->cutsPerMinute() < 8.0);
    CHECK(seq->validate().has_value());
    // The film covers the piece: no gaps, and it does not stop halfway.
    CHECK_THAT(seq->durationSeconds(), Catch::Matchers::WithinAbs(180.0, 1e-6));
}

TEST_CASE("The directed camera is one unbroken move by default", "[app][cinematic][director]") {
    // "It never orbits, never zooms, and holds its final pose" (audit 1.4), and section 8 lists that
    // restraint among the things not to change. Continuity is therefore the default.
    const auto structure = referenceStructure();
    auto continuous = app::directFromStructure(structure, referenceBrief());
    REQUIRE(continuous.has_value());
    REQUIRE(continuous->shots.size() > 2);

    // Every boundary is joined except the one into the breakdown, which is the single deliberate
    // cut: the music has stopped there, so the cut is invisible, and a quiet close shot cannot be
    // reached at a sprint from wherever the last loud one finished.
    int joins = 0;
    int cuts = 0;
    for (std::size_t i = 1; i < continuous->shots.size(); ++i) {
        const bool joined = glm::length(continuous->shots[i].cameraAt(0.0f) -
                                        continuous->shots[i - 1].cameraAt(1.0f)) < 1e-3f;
        const auto* section = structure.at(continuous->shots[i].startSeconds + 1e-6);
        REQUIRE(section != nullptr);
        INFO("boundary " << i << " into " << avgen::signals::musicalSectionName(section->kind));
        if (section->kind == MusicalSection::Breakdown) {
            CHECK(!joined);
            ++cuts;
        } else {
            CHECK(joined);
            ++joins;
        }
    }
    CHECK(cuts == 1);
    CHECK(joins >= 3);

    auto brief = referenceBrief();
    brief.mode = app::DirectorMode::EditedSequence;
    auto cut = app::directFromStructure(structure, brief);
    REQUIRE(cut.has_value());
    REQUIRE(cut->shots.size() == continuous->shots.size());
    float largestJump = 0.0f;
    for (std::size_t i = 1; i < cut->shots.size(); ++i) {
        largestJump = std::max(largestJump, glm::length(cut->shots[i].cameraAt(0.0f) -
                                                        cut->shots[i - 1].cameraAt(1.0f)));
    }
    CHECK(largestJump > 1.0f); // turning it off gives actual cuts
}

TEST_CASE("A breakdown gets a slow, close shot", "[app][cinematic][director]") {
    // A breakdown and a drop with the same amount of time on screen *and the same subject*, so the
    // comparison is about the shot and nothing else. Both controls are load-bearing. Duration was
    // always controlled here; the subject became a confound with ADR-202, which rotates the cast by
    // importance instead of giving the hero every build and drop -- a slow shot onto a far subject
    // covers more ground than a fast one onto a near subject, and this assertion started failing on
    // geometry (25.4 against a 25.1 budget) while every other claim in the case still held. A cast
    // of one removes the confound at the source rather than widening the margin.
    std::vector<MusicalMoment> m{MusicalMoment{MusicalEvent::Break, 30.0, 0.8f},
                                 MusicalMoment{MusicalEvent::Build, 60.0, 0.7f},
                                 MusicalMoment{MusicalEvent::Drop, 90.0, 1.0f}};
    const auto structure = avgen::signals::MusicalStructure::fromMoments(m, 120.0);
    auto brief = referenceBrief();
    brief.supporting.clear();
    auto seq = app::directFromStructure(structure, brief);
    INFO((seq ? std::string() : seq.error().message));
    REQUIRE(seq.has_value());

    const app::Shot* breakdown = seq->shotAt(45.0);
    const app::Shot* drop = seq->shotAt(100.0);
    REQUIRE(breakdown != nullptr);
    REQUIRE(drop != nullptr);
    CHECK(breakdown->kind == app::ShotKind::Approach);
    // Distance covered is what makes a shot feel fast, not the music under it. Both shots run for
    // thirty seconds; the quiet one has to actually travel less ground.
    CHECK_THAT(breakdown->durationSeconds, Catch::Matchers::WithinAbs(drop->durationSeconds, 1e-6));
    CHECK(breakdown->pathLength() < drop->pathLength() * 0.5f);
    CHECK(breakdown->peakSpeed() < drop->peakSpeed());
    // ...and it ends close enough for the subject to fill the frame.
    CHECK(breakdown->endDistance < 3.0f);
    CHECK(breakdown->subjectCoverageAt(1.0f) > 0.4f);
}

TEST_CASE("The same music and the same brief give the same film", "[app][cinematic][director]") {
    const auto structure = referenceStructure();
    auto a = app::directFromStructure(structure, referenceBrief());
    auto b = app::directFromStructure(structure, referenceBrief());
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->toJson() == b->toJson());

    // The seed only chooses which supporting subject a section gets; it never invents a boundary,
    // so the cut list is identical whatever it is.
    auto seeded = referenceBrief();
    seeded.seed = 99;
    auto c = app::directFromStructure(structure, seeded);
    REQUIRE(c.has_value());
    REQUIRE(c->shots.size() == a->shots.size());
    for (std::size_t i = 0; i < a->shots.size(); ++i) {
        CHECK_THAT(c->shots[i].startSeconds,
                   Catch::Matchers::WithinAbs(a->shots[i].startSeconds, 1e-9));
        CHECK(c->shots[i].kind == a->shots[i].kind);
    }
}

TEST_CASE("The director refuses what it cannot direct", "[app][cinematic][director]") {
    CHECK(!app::directFromStructure({}, referenceBrief()).has_value());
    auto brief = referenceBrief();
    brief.hero.radius = 0.0f;
    auto bad = app::directFromStructure(referenceStructure(), brief);
    REQUIRE(!bad.has_value());
    CHECK(bad.error().message.find("radii") != std::string::npos);
}

TEST_CASE("A film with no supporting cast is still a film", "[app][cinematic][director]") {
    // A transition with nothing to transition to would otherwise fail validation, which is the
    // right refusal for a hand-authored shot and the wrong one for a generated sequence.
    auto brief = referenceBrief();
    brief.supporting.clear();
    auto seq = app::directFromStructure(referenceStructure(), brief);
    INFO((seq ? std::string() : seq.error().message));
    REQUIRE(seq.has_value());
    for (const auto& s : seq->shots) {
        CHECK(s.subject.name == "elder");
        CHECK(s.kind != app::ShotKind::Transition);
    }
}

// ---- the Auto-director's shot modes (section 9) ------------------------------------------------

namespace {
// Camera speed in world units per second, from finite differences of the shot's own evaluator.
float speedAt(const app::Shot& shot, float t) {
    constexpr float kDt = 1.0e-3f;
    const float a = std::clamp(t - kDt, 0.0f, 1.0f);
    const float b = std::clamp(t + kDt, 0.0f, 1.0f);
    const float dt = static_cast<float>(shot.durationSeconds) * (b - a);
    if (dt <= 0.0f) {
        return 0.0f;
    }
    return glm::distance(shot.cameraAt(b), shot.cameraAt(a)) / dt;
}
} // namespace

TEST_CASE("a continuous shot does not stop at every section boundary", "[app][cinematic][autodirector]") {
    const auto structure = referenceStructure();

    app::DirectionBrief continuous = referenceBrief();
    continuous.mode = app::DirectorMode::ContinuousShot;
    const auto take = app::directFromStructure(structure, continuous);
    REQUIRE(take.has_value());
    REQUIRE(take->shots.size() >= 3);

    SECTION("the camera is still moving at the joins it carries through") {
        // The defect this mode existed to fix and did not: `ease` smoothsteps whichever ends ask for
        // it and both ends asked, so every shot arrived at a boundary at zero velocity and left the
        // next from zero. Pinning the *position* made that look continuous in a still and read as a
        // cut in motion.
        int carried = 0;
        for (std::size_t i = 1; i < take->shots.size(); ++i) {
            if (!take->shots[i].startPosition.has_value()) {
                continue; // a deliberate cut -- a breakdown never carries through
            }
            ++carried;
            const float arriving = speedAt(take->shots[i - 1], 1.0f);
            const float leaving = speedAt(take->shots[i], 0.0f);
            INFO("join " << i << ": arriving " << arriving << " leaving " << leaving);
            REQUIRE(arriving > 0.05f);
            REQUIRE(leaving > 0.05f);
        }
        REQUIRE(carried >= 2);
    }

    SECTION("the film still starts and ends at rest") {
        // Only the *interior* joins lose their easing. A take that begins mid-move and ends mid-move
        // is a clip, not a film.
        REQUIRE(take->shots.front().easeIn);
        REQUIRE(take->shots.back().easeOut);
    }

    SECTION("position is still continuous across those joins") {
        for (std::size_t i = 1; i < take->shots.size(); ++i) {
            if (!take->shots[i].startPosition.has_value()) {
                continue;
            }
            REQUIRE(glm::distance(take->shots[i - 1].cameraAt(1.0f), take->shots[i].cameraAt(0.0f)) < 0.01f);
        }
    }
}

TEST_CASE("an edited sequence cuts, and that is the difference", "[app][cinematic][autodirector]") {
    const auto structure = referenceStructure();
    app::DirectionBrief edited = referenceBrief();
    edited.mode = app::DirectorMode::EditedSequence;
    const auto cutList = app::directFromStructure(structure, edited);
    REQUIRE(cutList.has_value());

    SECTION("no shot is pinned to the one before it") {
        for (const app::Shot& s : cutList->shots) {
            REQUIRE_FALSE(s.startPosition.has_value());
        }
    }

    SECTION("every shot eases at both ends, because every shot is its own move") {
        for (const app::Shot& s : cutList->shots) {
            REQUIRE(s.easeIn);
            REQUIRE(s.easeOut);
        }
    }

    SECTION("the two modes are genuinely different films from one structure") {
        app::DirectionBrief continuous = referenceBrief();
        continuous.mode = app::DirectorMode::ContinuousShot;
        const auto take = app::directFromStructure(structure, continuous);
        REQUIRE(take.has_value());
        // Same cuts -- the music decides those -- and different camera paths.
        REQUIRE(take->shots.size() == cutList->shots.size());
        bool anyDifferent = false;
        for (std::size_t i = 0; i < take->shots.size(); ++i) {
            if (glm::distance(take->shots[i].cameraAt(0.0f), cutList->shots[i].cameraAt(0.0f)) > 0.01f) {
                anyDifferent = true;
            }
        }
        REQUIRE(anyDifferent);
    }
}

TEST_CASE("the director mode round-trips by name", "[app][cinematic][autodirector]") {
    REQUIRE(std::string(app::directorModeName(app::DirectorMode::ContinuousShot)) == "continuous");
    REQUIRE(std::string(app::directorModeName(app::DirectorMode::EditedSequence)) == "edited");
    REQUIRE(app::directorModeFromName("continuous") == app::DirectorMode::ContinuousShot);
    REQUIRE(app::directorModeFromName("edited-sequence") == app::DirectorMode::EditedSequence);
    REQUIRE_FALSE(app::directorModeFromName("cinematic").has_value());
}

TEST_CASE("a subject's preferred elevation biases the shot without flattening it",
          "[app][cinematic][autodirector]") {
    // The third dead authored property, wired. `HeroPoint::preferredCameraElevationDegrees` was
    // serialised per hero and read by nothing; "look up at this one, down into that one" is a real
    // opinion a subject has, so it is honoured as a bias rather than dropped from the format.
    const auto structure = referenceStructure();
    app::DirectionBrief low = referenceBrief();
    app::DirectionBrief high = low;
    high.hero.preferredElevationDegrees = 26.0f;
    for (app::FocalTarget& t : high.supporting) {
        t.preferredElevationDegrees = 26.0f;
    }

    const auto flat = app::directFromStructure(structure, low);
    const auto lifted = app::directFromStructure(structure, high);
    REQUIRE(flat.has_value());
    REQUIRE(lifted.has_value());
    REQUIRE(flat->shots.size() == lifted->shots.size());

    SECTION("every shot is raised") {
        for (std::size_t i = 0; i < flat->shots.size(); ++i) {
            INFO("shot " << i);
            REQUIRE(lifted->shots[i].startElevation > flat->shots[i].startElevation);
        }
    }

    SECTION("the kind's own sweep survives, because both ends shift together") {
        for (std::size_t i = 0; i < flat->shots.size(); ++i) {
            const float a = flat->shots[i].endElevation - flat->shots[i].startElevation;
            const float b = lifted->shots[i].endElevation - lifted->shots[i].startElevation;
            INFO("shot " << i << " sweep " << a << " vs " << b);
            REQUIRE(std::fabs(a - b) < 1e-3f);
        }
    }

    SECTION("a subject with no opinion changes nothing") {
        const auto again = app::directFromStructure(structure, low);
        REQUIRE(again.has_value());
        for (std::size_t i = 0; i < flat->shots.size(); ++i) {
            REQUIRE(again->shots[i].startElevation == flat->shots[i].startElevation);
        }
    }
}

TEST_CASE("a subject's approach bearing moves the whole film around it",
          "[app][cinematic][autodirector]") {
    // The part of per-hero cinematic regions that has to exist: a camera offset that comes from where
    // the hero actually stands rather than from a global default that suits one of them.
    const auto structure = referenceStructure();
    app::DirectionBrief north = referenceBrief();
    app::DirectionBrief east = north;
    east.hero.preferredAzimuth = 1.5708f; // a quarter turn
    for (app::FocalTarget& t : east.supporting) {
        t.preferredAzimuth = 1.5708f;
    }

    const auto a = app::directFromStructure(structure, north);
    const auto b = app::directFromStructure(structure, east);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->shots.size() == b->shots.size());

    SECTION("every shot is rotated by the bearing") {
        for (std::size_t i = 0; i < a->shots.size(); ++i) {
            INFO("shot " << i);
            REQUIRE_THAT(static_cast<double>(b->shots[i].startAzimuth - a->shots[i].startAzimuth),
                         Catch::Matchers::WithinAbs(1.5708, 1e-4));
        }
    }

    SECTION("the golden-angle spread between shots survives, because it is applied around it") {
        // The spread is what stops a film being nine views down one axis; the bearing decides which
        // axis they are spread around, and must not collapse them onto it.
        for (std::size_t i = 1; i < a->shots.size(); ++i) {
            const float spreadA = a->shots[i].startAzimuth - a->shots[i - 1].startAzimuth;
            const float spreadB = b->shots[i].startAzimuth - b->shots[i - 1].startAzimuth;
            REQUIRE_THAT(static_cast<double>(spreadA - spreadB), Catch::Matchers::WithinAbs(0.0, 1e-4));
        }
    }
}

// Reported against Glowmere Valley 2: "the auto director seems to get stuck focusing only on one
// hero during the middle of any song... around the 45 second mark, it will continue to spin around
// a single hero for about a minute before moving on", seen across several tracks of different
// lengths, always at about the same point.
//
// Measured on the real project, it was worse than the report: **25 of 26 shots were on
// `elder-2-cap`** in a world declaring eleven heroes. Two faults compound.
//
// The structure of this test is the structure of the complaint: a long run of sections the hero
// owns, then a run of sections it does not. Both halves failed, for different reasons.
TEST_CASE("The film does not settle on one hero and stay there", "[app][cinematic][director][cast]") {
    avgen::signals::MusicalStructure structure;
    const auto add = [&](MusicalSection kind, double start, double end) {
        avgen::signals::StructureSection section;
        section.kind = kind;
        section.startSeconds = start;
        section.durationSeconds = end - start;
        section.intensity = 0.7f;
        structure.sections.push_back(section);
    };
    // The analyser really does produce this: 183 s of Glowmere folded into a run of nineteen
    // consecutive drops. Whether that is a good reading of the music is the analyser's business --
    // the director has to stay watchable when it gets one.
    add(MusicalSection::Intro, 0.0, 4.0);
    double t = 4.0;
    for (int i = 0; i < 16; ++i) {
        add(MusicalSection::Drop, t, t + 5.0);
        t += 5.0;
    }
    // ...and then a run of verses, which are transitions.
    for (int i = 0; i < 6; ++i) {
        add(MusicalSection::Verse, t, t + 10.0);
        t += 10.0;
    }

    auto seq = app::directFromStructure(structure, referenceBrief());
    INFO((seq ? std::string() : seq.error().message));
    REQUIRE(seq.has_value());
    REQUIRE(seq->shots.size() > 10);

    // Fault 1: a run of transitions never advanced. A transition takes the previous shot's subject
    // and puts the intended new one in `handoff` -- so the *next* transition read the subject the
    // last one had left, not the one it had arrived at, and the chain never moved off the first
    // object. Six verses in a row all came out on the hero.
    //
    // Asserted as "the transitions are not all about the same thing", which is the property, rather
    // than as a specific expected cast order, which is a policy that may reasonably change.
    std::set<std::string> transitionSubjects;
    int transitions = 0;
    for (const app::Shot& s : seq->shots) {
        if (s.kind == app::ShotKind::Transition) {
            ++transitions;
            transitionSubjects.insert(s.subject.name);
        }
    }
    INFO("transition shots: " << transitions << ", distinct subjects: " << transitionSubjects.size());
    REQUIRE(transitions >= 4);            // the state the measurement assumes
    CHECK(transitionSubjects.size() > 1); // and they are not all the same object

    // Fault 2: the hero owns every build and drop, which is right when a structure has a few of
    // them and ruinous when it has nineteen. Nothing bounded how long the film could stay on one
    // subject, so the whole middle was one object.
    std::size_t longestRun = 0;
    std::size_t run = 0;
    std::string previous;
    for (const app::Shot& s : seq->shots) {
        run = s.subject.name == previous ? run + 1 : 1;
        previous = s.subject.name;
        longestRun = std::max(longestRun, run);
    }
    INFO("longest run of consecutive shots on one subject: " << longestRun << " of " << seq->shots.size());
    CHECK(longestRun <= 6);

    // And the film as a whole is about more than one thing. The hero should still dominate -- it is
    // the hero -- but a world with four declared targets that shows one is not a film.
    std::map<std::string, double> screenTime;
    double total = 0.0;
    for (const app::Shot& s : seq->shots) {
        screenTime[s.subject.name] += s.durationSeconds;
        total += s.durationSeconds;
    }
    REQUIRE(total > 0.0);
    const double heroShare = screenTime["elder"] / total;
    INFO("hero screen time " << 100.0 * heroShare << "%, subjects " << screenTime.size());
    CHECK(screenTime.size() >= 3);
    CHECK(heroShare < 0.85);
}

// ADR-200: "sometimes it's just moving about way too fast."
//
// The properties that matter are what the cap must NOT break, not that speeds come down. A cut in
// this director lands on the music, so the timing is untouchable; and a continuous cut's shots have
// to still join afterwards, or the fix for one complaint creates the one ADR-185 removed.
namespace {
float peakViewRateOf(const app::Shot& shot, int samples = 64) {
    if (!(shot.durationSeconds > 0.0)) {
        return 0.0f;
    }
    const auto dt = static_cast<float>(shot.durationSeconds) / static_cast<float>(samples - 1);
    float peak = 0.0f;
    glm::vec3 pc = shot.cameraAt(0.0f);
    glm::vec3 pa = shot.targetAt(0.0f);
    for (int i = 1; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples - 1);
        const glm::vec3 c = shot.cameraAt(t);
        const glm::vec3 a = shot.targetAt(t);
        if (glm::length(pa - pc) > 1e-4f && glm::length(a - c) > 1e-4f) {
            const float cosine =
                std::clamp(glm::dot(glm::normalize(pa - pc), glm::normalize(a - c)), -1.0f, 1.0f);
            peak = std::max(peak, glm::degrees(std::acos(cosine)) / dt);
        }
        pc = c;
        pa = a;
    }
    return peak;
}
} // namespace

TEST_CASE("a camera speed cap shortens the move and leaves the cut alone",
          "[app][cinematic][director][pace]") {
    const auto structure = referenceStructure();
    auto seq = app::directFromStructure(structure, referenceBrief());
    REQUIRE(seq.has_value());
    REQUIRE(seq->shots.size() > 3);

    // What the film was before, so the cap can be shown to have changed only what it should.
    std::vector<double> starts, durations;
    std::vector<std::string> subjects;
    float fastest = 0.0f;
    for (const app::Shot& s : seq->shots) {
        starts.push_back(s.startSeconds);
        durations.push_back(s.durationSeconds);
        subjects.push_back(s.subject.name);
        fastest = std::max(fastest, s.peakSpeed());
    }
    INFO("fastest shot before the cap: " << fastest << " m/s");
    REQUIRE(fastest > 1.0f); // the state the measurement assumes: there is something to slow down

    const float cap = fastest * 0.4f;
    const std::size_t shortened = seq->limitCameraSpeed(cap);
    INFO("shots shortened: " << shortened << " of " << seq->shots.size());
    CHECK(shortened > 0);

    // It did what it says. A little over the cap is the iteration's tolerance, not a miss.
    for (const app::Shot& s : seq->shots) {
        INFO(s.name << " peak " << s.peakSpeed());
        CHECK(s.peakSpeed() <= cap * 1.05f);
    }

    // And nothing else moved. This is the assertion that matters: the cuts are still on the music
    // and the film is still about the same things in the same order.
    REQUIRE(seq->shots.size() == starts.size());
    for (std::size_t i = 0; i < seq->shots.size(); ++i) {
        INFO(seq->shots[i].name);
        CHECK(seq->shots[i].startSeconds == starts[i]);
        CHECK(seq->shots[i].durationSeconds == durations[i]);
        CHECK(seq->shots[i].subject.name == subjects[i]);
    }

    // A continuous cut still joins. A shot whose start was chained to the previous end must have
    // followed that end when it moved -- otherwise this fix reintroduces exactly the teleport
    // ADR-185 was written to remove.
    for (std::size_t i = 1; i < seq->shots.size(); ++i) {
        if (!seq->shots[i].startPosition) {
            continue;
        }
        const glm::vec3 previousEnd = seq->shots[i - 1].cameraAt(1.0f);
        const glm::vec3 here = seq->shots[i].cameraAt(0.0f);
        INFO(seq->shots[i].name << " joins at " << glm::length(here - previousEnd) << " m");
        CHECK(glm::length(here - previousEnd) < 0.01f);
    }

    // ---- the swing cap, which is the one that addresses what a viewer calls "too fast" ----
    //
    // Measured before it existed: capping the camera to 1 m/s took travel from 23.2 to 1.0 and left
    // the view rotating at 63.7 deg/s, *faster* than the 52.0 it started at. The fastest view in
    // this cut is a Subject shot, where the aim is a fixed point and every degree comes from the
    // camera swinging around it -- which no metres-per-second cap can reach.
    {
        auto swung = app::directFromStructure(structure, referenceBrief());
        REQUIRE(swung.has_value());
        float before = 0.0f;
        for (const app::Shot& s : swung->shots) {
            before = std::max(before, peakViewRateOf(s));
        }
        REQUIRE(before > 40.0f); // the state the measurement assumes
        CHECK(swung->limitViewRate(30.0f) > 0);
        float after = 0.0f;
        for (const app::Shot& s : swung->shots) {
            after = std::max(after, peakViewRateOf(s));
        }
        INFO("view rate " << before << " -> " << after << " deg/s");
        // Substantially slower -- and *not* asserted to reach the cap, because it cannot always.
        //
        // A handoff has a floor: the aim must travel from one subject to the other inside the shot,
        // and both are fixed points. Widening the swing spreads that turn over the whole shot and
        // shrinking the camera's move does not reduce it at all, so the irreducible rate is the
        // angle between the two subjects over the shot's duration. Reaching an arbitrary cap would
        // mean either not completing the handoff or moving the cut, and the cut belongs to the
        // music.
        //
        // So the contract is "as slow as this cut allows", asserted as at least a third off. A cap
        // met by making every shot static would also be "under the cap", which is why the relative
        // assertion is the one that means something.
        CHECK(after < before * 0.65f);

        // The cuts did not move. Same assertion as the travel cap, for the same reason: the music
        // decides when, and nothing here may change that.
        REQUIRE(swung->shots.size() == starts.size());
        for (std::size_t i = 0; i < swung->shots.size(); ++i) {
            CHECK(swung->shots[i].startSeconds == starts[i]);
            CHECK(swung->shots[i].durationSeconds == durations[i]);
        }
    }

    // Off is off: a cap of zero is not a cap of nothing-may-move.
    auto untouched = app::directFromStructure(structure, referenceBrief());
    REQUIRE(untouched.has_value());
    CHECK(untouched->limitCameraSpeed(0.0f) == 0);
    float stillFastest = 0.0f;
    for (const app::Shot& s : untouched->shots) {
        stillFastest = std::max(stillFastest, s.peakSpeed());
    }
    CHECK_THAT(stillFastest, Catch::Matchers::WithinAbs(fastest, 1e-4));
}

// "even at its smallest value it's still moving blazing fast". Is the cap not working, or is the
// thing that feels fast not the thing it caps? Measured rather than guessed: the camera's own speed
// and, separately, how fast the point it is LOOKING at sweeps -- because a camera that barely moves
// while its aim whips across a valley feels very fast indeed, and nothing caps the aim.
TEST_CASE("what is actually fast in a directed cut", "[.probe][app][cinematic][director][pace]") {
    const auto structure = referenceStructure();
    const auto report = [](const char* label, const app::Sequence& seq) {
        float camPeak = 0.0f, aimPeak = 0.0f, angPeak = 0.0f;
        std::string worstName = "-";
        app::LookMode worstMode = app::LookMode::Subject;
        for (const app::Shot& s : seq.shots) {
            if (!(s.durationSeconds > 0.0)) {
                continue;
            }
            constexpr int kN = 64;
            const auto dt = static_cast<float>(s.durationSeconds) / (kN - 1);
            glm::vec3 pc = s.cameraAt(0.0f);
            glm::vec3 pa = s.targetAt(0.0f);
            for (int i = 1; i < kN; ++i) {
                const float t = static_cast<float>(i) / (kN - 1);
                const glm::vec3 c = s.cameraAt(t);
                const glm::vec3 a = s.targetAt(t);
                camPeak = std::max(camPeak, glm::length(c - pc) / dt);
                aimPeak = std::max(aimPeak, glm::length(a - pa) / dt);
                // What the viewer actually experiences: how fast the view direction rotates.
                const glm::vec3 d0 = glm::normalize(pa - pc);
                const glm::vec3 d1 = glm::normalize(a - c);
                const float cosine = std::clamp(glm::dot(d0, d1), -1.0f, 1.0f);
                const float rate = glm::degrees(std::acos(cosine)) / dt;
                if (rate > angPeak) {
                    angPeak = rate;
                    worstName = s.name;
                    worstMode = s.lookMode();
                }
                pc = c;
                pa = a;
            }
        }
        UNSCOPED_INFO(fmt::format("{:14} camera {:7.1f} m/s | aim point {:8.1f} m/s | view {:7.1f} deg/s"
                                  " | fastest view: {} ({})",
                                  label, camPeak, aimPeak, angPeak, worstName,
                                  app::lookModeName(worstMode)));
    };

    auto plain = app::directFromStructure(structure, referenceBrief());
    REQUIRE(plain.has_value());
    report("uncapped", *plain);

    for (const float cap : {20.0f, 5.0f, 1.0f}) {
        auto capped = app::directFromStructure(structure, referenceBrief());
        REQUIRE(capped.has_value());
        capped->limitCameraSpeed(cap);
        report(fmt::format("travel {:.0f} m/s", cap).c_str(), *capped);
    }
    // And the control that actually addresses what a viewer calls "too fast".
    for (const float rate : {30.0f, 15.0f}) {
        auto capped = app::directFromStructure(structure, referenceBrief());
        REQUIRE(capped.has_value());
        capped->limitViewRate(rate);
        report(fmt::format("swing {:.0f} deg/s", rate).c_str(), *capped);
    }
    CHECK(true);
}
