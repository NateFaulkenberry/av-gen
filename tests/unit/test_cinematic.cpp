// The cinematic director (ADR-062). What is worth guarding: a shot kind actually produces the move
// its name promises, overlapping shots are refused rather than silently resolved, a shot without an
// explicit start follows the previous one, and the result is ordinary timeline keys.

#include "app/cinematic.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

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

TEST_CASE("Every kind aims at its subject, except the one that is going somewhere",
          "[app][cinematic]") {
    for (const auto kind : {app::ShotKind::Establish, app::ShotKind::Approach, app::ShotKind::Reveal,
                            app::ShotKind::Orbit, app::ShotKind::Track, app::ShotKind::Descent}) {
        const auto s = shotOf(kind);
        INFO(app::shotKindName(kind));
        CHECK(glm::length(s.targetAt(0.5f) - s.subject.position) < 1e-4f);
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
    REQUIRE(tracks.size() == 4);

    // The targets are the parameter paths the engine's timeline already drives; nothing new had to
    // be taught to the camera.
    CHECK(tracks[0]["target"] == "camera/position");
    CHECK(tracks[1]["target"] == "camera/target");
    CHECK(tracks[2]["target"] == "camera/lens/focalLength");
    CHECK(tracks[3]["target"] == "camera/lens/aperture");

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
