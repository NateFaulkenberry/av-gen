// Selected-object transform history (renderer forensics 4.3/9.3).
//
// The history exists to separate two things that look identical in one frame: an object moving, and
// the camera moving past it. So every case here is built as a pair -- the motion, and the control
// that produces the same *screen* motion for a different reason -- and the assertion is that the
// verdict distinguishes them. A history that reported "it moved" for both would be exactly as
// useful as looking at the screen, which is the thing that already failed.

#include "rendering/transform_history.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>

using namespace avgen;
using Catch::Matchers::ContainsSubstring;

namespace {

// A frame diagnosis carrying one object, built the way the renderer builds one: the view-projection
// is the product, not an independent number, because a test that let them disagree would be testing
// arithmetic nobody performs.
rendering::RendererDiagnosticFrame frameWith(std::uint64_t index, const glm::vec3& cameraPosition,
                                             const glm::vec3& cameraTarget, const glm::mat4& model,
                                             const char* name = "subject") {
    rendering::RendererDiagnosticFrame frame;
    frame.frameIndex = index;
    frame.cameraPosition = cameraPosition;
    frame.view = glm::lookAt(cameraPosition, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    frame.projection = glm::perspective(0.87f, 16.0f / 9.0f, 0.1f, 200.0f);
    frame.projection[1][1] *= -1.0f; // the engine's depth-0..1 convention; only the sign of y moves
    frame.viewProjection = frame.projection * frame.view;
    frame.aspect = 16.0f / 9.0f;
    frame.fovY = 0.87f;
    frame.nearPlane = 0.1f;
    frame.farPlane = 200.0f;

    rendering::RenderObjectDiagnostic object;
    object.name = name;
    object.worldMatrix = model;
    object.worldPosition = glm::vec3(model[3]);
    object.visible = true;
    object.submitted = true;
    frame.objects.push_back(object);
    return frame;
}

glm::mat4 at(const glm::vec3& p) { return glm::translate(glm::mat4(1.0f), p); }

} // namespace

TEST_CASE("A history without two frames refuses to have an opinion", "[forensics][history]") {
    rendering::TransformHistory history;
    CHECK_FALSE(history.explain().haveHistory);
    CHECK_THAT(history.explain().sentence, ContainsSubstring("nothing has been recorded"));

    history.setSubject("subject");
    history.record(frameWith(0, {0, 0, 5}, {0, 0, 0}, at({0, 0, 0})));
    CHECK(history.size() == 1);
    CHECK_FALSE(history.explain().haveHistory);
    CHECK_THAT(history.explain().sentence, ContainsSubstring("only one frame"));
}

TEST_CASE("A frame that does not contain the subject is skipped, not interpolated", "[forensics][history]") {
    rendering::TransformHistory history;
    history.setSubject("subject");
    history.record(frameWith(0, {0, 0, 5}, {0, 0, 0}, at({0, 0, 0})));
    history.record(frameWith(1, {0, 0, 5}, {0, 0, 0}, at({9, 9, 9}), "somebody-else"));
    history.record(frameWith(2, {0, 0, 5}, {0, 0, 0}, at({0, 0, 0})));
    CHECK(history.size() == 2);
    // The gap is visible in the record rather than smoothed over.
    CHECK(history.samples().front().frameIndex == 0);
    CHECK(history.samples().back().frameIndex == 2);
}

TEST_CASE("Changing the subject clears the trail", "[forensics][history]") {
    rendering::TransformHistory history;
    history.setSubject("subject");
    history.record(frameWith(0, {0, 0, 5}, {0, 0, 0}, at({0, 0, 0})));
    history.record(frameWith(1, {0, 0, 5}, {0, 0, 0}, at({1, 0, 0})));
    REQUIRE(history.size() == 2);
    history.setSubject("subject"); // the same name is not a change
    CHECK(history.size() == 2);
    history.setSubject("other");
    CHECK(history.empty());
}

TEST_CASE("The history separates parallax from an object that actually moved", "[forensics][history]") {
    // The pair: in both, the object's projected position travels across the screen.
    SECTION("the camera moved and the object did not: parallax") {
        rendering::TransformHistory history;
        history.setSubject("subject");
        for (int i = 0; i < 8; ++i) {
            const float t = static_cast<float>(i) * 0.5f;
            history.record(frameWith(static_cast<std::uint64_t>(i), {t, 1.0f, 5.0f}, {t, 0.0f, 0.0f},
                                     at({0, 0, 0})));
        }
        const rendering::TransformVerdict verdict = history.explain();
        REQUIRE(verdict.haveHistory);
        CHECK_FALSE(verdict.worldMoved);
        CHECK(verdict.cameraMoved);
        CHECK(verdict.screenMoved);
        CHECK(verdict.worldDistance == 0.0f);
        CHECK(verdict.screenDistance > 0.0f);
        CHECK_THAT(verdict.sentence, ContainsSubstring("that is parallax"));
    }

    SECTION("the object moved and the camera did not") {
        rendering::TransformHistory history;
        history.setSubject("subject");
        for (int i = 0; i < 8; ++i) {
            const float t = static_cast<float>(i) * 0.5f;
            history.record(frameWith(static_cast<std::uint64_t>(i), {0, 1, 5}, {0, 0, 0}, at({t, 0, 0})));
        }
        const rendering::TransformVerdict verdict = history.explain();
        CHECK(verdict.worldMoved);
        CHECK_FALSE(verdict.cameraMoved);
        CHECK(verdict.screenMoved);
        CHECK(verdict.worldDistance > 3.0f);
        CHECK_THAT(verdict.sentence, ContainsSubstring("the scene moved it"));
    }

    SECTION("both moved: the window isolates nothing, and says so") {
        rendering::TransformHistory history;
        history.setSubject("subject");
        for (int i = 0; i < 8; ++i) {
            const float t = static_cast<float>(i) * 0.5f;
            history.record(frameWith(static_cast<std::uint64_t>(i), {t, 1, 5}, {t, 0, 0}, at({t, 0, 0})));
        }
        const rendering::TransformVerdict verdict = history.explain();
        CHECK(verdict.worldMoved);
        CHECK(verdict.cameraMoved);
        CHECK_THAT(verdict.sentence, ContainsSubstring("neither is isolated"));
    }

    SECTION("nothing moved") {
        rendering::TransformHistory history;
        history.setSubject("subject");
        for (int i = 0; i < 8; ++i) {
            history.record(frameWith(static_cast<std::uint64_t>(i), {0, 1, 5}, {0, 0, 0}, at({0, 0, 0})));
        }
        const rendering::TransformVerdict verdict = history.explain();
        CHECK_FALSE(verdict.worldMoved);
        CHECK_FALSE(verdict.cameraMoved);
        CHECK_FALSE(verdict.screenMoved);
        CHECK_THAT(verdict.sentence, ContainsSubstring("held still"));
    }
}

TEST_CASE("Screen motion with a still camera and a still transform is reported as unexplained",
          "[forensics][history]") {
    // The shape of the defect this whole phase is looking for: the recorded state says the object
    // and the camera both held, and the pixel moved anyway. Built by moving the projection alone --
    // a zoom -- which is a camera input that is not the view matrix.
    rendering::TransformHistory history;
    history.setSubject("subject");
    for (int i = 0; i < 6; ++i) {
        rendering::RendererDiagnosticFrame frame = frameWith(static_cast<std::uint64_t>(i), {0, 0, 5}, {0, 0, 0},
                                                             at({1.0f, 0.0f, 0.0f}));
        frame.fovY = 0.87f - static_cast<float>(i) * 0.05f;
        frame.projection = glm::perspective(frame.fovY, 16.0f / 9.0f, 0.1f, 200.0f);
        frame.projection[1][1] *= -1.0f;
        frame.viewProjection = frame.projection * frame.view;
        history.record(frame);
    }
    const rendering::TransformVerdict verdict = history.explain();
    CHECK_FALSE(verdict.worldMoved);
    CHECK_FALSE(verdict.cameraMoved);
    CHECK(verdict.screenMoved);
    CHECK_THAT(verdict.sentence, ContainsSubstring("nothing in the recorded state explains it"));
}

TEST_CASE("A non-finite sample disqualifies the window rather than reporting motion",
          "[forensics][history]") {
    rendering::TransformHistory history;
    history.setSubject("subject");
    history.record(frameWith(0, {0, 0, 5}, {0, 0, 0}, at({0, 0, 0})));
    glm::mat4 broken = at({1, 0, 0});
    broken[3][0] = std::numeric_limits<float>::quiet_NaN();
    history.record(frameWith(1, {0, 0, 5}, {0, 0, 0}, broken));
    const rendering::TransformVerdict verdict = history.explain();
    CHECK(verdict.wentNonFinite);
    CHECK_THAT(verdict.sentence, ContainsSubstring("not evidence about motion"));
    // And the trail skips it rather than drawing a line to infinity.
    CHECK(history.path().size() == 1);
}

TEST_CASE("An object that stops being submitted says so", "[forensics][history]") {
    rendering::TransformHistory history;
    history.setSubject("subject");
    for (int i = 0; i < 4; ++i) {
        rendering::RendererDiagnosticFrame frame =
            frameWith(static_cast<std::uint64_t>(i), {0, 0, 5}, {0, 0, 0}, at({0, 0, 0}));
        if (i >= 2) {
            frame.objects[0].submitted = false;
            frame.objects[0].cameraCulled = true;
            frame.objects[0].cullReason = "outside the frustum";
        }
        history.record(frame);
    }
    const rendering::TransformVerdict verdict = history.explain();
    CHECK(verdict.stoppedDrawing);
    CHECK_THAT(verdict.sentence, ContainsSubstring("stopped being submitted"));
    CHECK(history.samples().back().cullReason == "outside the frustum");
}

TEST_CASE("The history is bounded and keeps the newest frames", "[forensics][history]") {
    rendering::TransformHistory history(4);
    history.setSubject("subject");
    for (int i = 0; i < 20; ++i) {
        history.record(frameWith(static_cast<std::uint64_t>(i), {0, 0, 5}, {0, 0, 0},
                                 at({static_cast<float>(i), 0, 0})));
    }
    CHECK(history.size() == 4);
    CHECK(history.samples().front().frameIndex == 16);
    CHECK(history.samples().back().frameIndex == 19);
    CHECK(history.path().size() == 4);
}

TEST_CASE("A sample decomposes the matrix the GPU was given", "[forensics][history]") {
    rendering::TransformHistory history;
    history.setSubject("subject");
    glm::mat4 model = glm::translate(glm::mat4(1.0f), {2.0f, 3.0f, -1.0f});
    model = glm::rotate(model, 1.0f, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, {2.0f, 0.5f, 4.0f});
    history.record(frameWith(0, {0, 0, 12}, {0, 0, 0}, model));
    REQUIRE(history.size() == 1);
    const rendering::TransformSample& sample = history.samples().front();
    CHECK(std::abs(sample.worldScale.x - 2.0f) < 1e-4f);
    CHECK(std::abs(sample.worldScale.y - 0.5f) < 1e-4f);
    CHECK(std::abs(sample.worldScale.z - 4.0f) < 1e-4f);
    for (int c = 0; c < 3; ++c) {
        CHECK(std::abs(glm::length(sample.worldBasis[c]) - 1.0f) < 1e-4f);
    }
    CHECK(sample.worldPosition == glm::vec3(2.0f, 3.0f, -1.0f));
    // A scale change alone is motion the position cannot see, and the verdict must catch it.
    history.record(frameWith(1, {0, 0, 12}, {0, 0, 0}, glm::scale(model, {1.5f, 1.0f, 1.0f})));
    CHECK(history.explain().worldMoved);
    CHECK(history.explain().worldDistance == 0.0f);
}

TEST_CASE("A collapsed transform keeps the identity basis rather than inventing a rotation",
          "[forensics][history]") {
    rendering::TransformHistory history;
    history.setSubject("subject");
    history.record(frameWith(0, {0, 0, 5}, {0, 0, 0}, glm::scale(glm::mat4(1.0f), glm::vec3(0.0f))));
    REQUIRE(history.size() == 1);
    const rendering::TransformSample& sample = history.samples().front();
    CHECK(sample.worldScale == glm::vec3(0.0f));
    CHECK(sample.worldBasis == glm::mat3(1.0f));
}

TEST_CASE("On-screen is the clip test, not a guess from the position", "[forensics][history]") {
    rendering::TransformHistory history;
    history.setSubject("subject");
    history.record(frameWith(0, {0, 0, 5}, {0, 0, 0}, at({0, 0, 0})));
    CHECK(history.samples().front().onScreen);
    // Behind the camera: w goes negative, and a naive divide would place it back on screen.
    history.record(frameWith(1, {0, 0, 5}, {0, 0, 0}, at({0, 0, 40})));
    CHECK_FALSE(history.samples().back().onScreen);
}
