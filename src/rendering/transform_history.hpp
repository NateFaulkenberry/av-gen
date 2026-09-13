#pragma once

// Selected-object transform history (renderer forensics, Phases 4.3 and 9.3).
//
// The question this exists to answer is the one that is hardest to answer by looking: **an object
// moved across the screen -- did it move, or did the camera?** Both look identical in a single
// frame, and the investigation has already lost time to the confusion in both directions (a
// "drifting" entity that was a clock restarting per frame, and a static-object test that proved
// nothing because the camera was still too). Parallax and corruption are only distinguishable over
// *time*, against the camera that was in force for each sample, so a history is the smallest tool
// that can tell them apart.
//
// What a sample carries is deliberately all three layers at once: the world transform the scene
// holds, the matrix the GPU was actually handed, and the camera state that projected it, plus the
// resulting screen position. A history that carried only the first could not distinguish "the scene
// moved it" from "the upload moved it"; one that carried only the last could not say why.
//
// No device: this reads `RendererDiagnosticFrame`, which is names and numbers.

#include "rendering/renderer_diagnostics.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace avgen::rendering {

// One frame of one object's life.
struct TransformSample {
    std::uint64_t frameIndex = 0;
    // The world transform, decomposed, because a matrix diff says "something changed" and a
    // decomposition says which of the three things did. Rotation is kept as the matrix's upper 3x3
    // columns normalised, not a quaternion: a quaternion and its negation are the same rotation and
    // would report a difference that is not one.
    glm::vec3 worldPosition{0.0f};
    glm::vec3 worldScale{1.0f};
    glm::mat3 worldBasis{1.0f};
    glm::mat4 worldMatrix{1.0f};  // exactly what the renderer put in the object's uniform
    glm::vec3 cameraPosition{0.0f};
    glm::mat4 view{1.0f};
    glm::mat4 viewProjection{1.0f};
    // The object's origin through this frame's own view-projection. `onScreen` is the clip test, so
    // an object that left the frustum is distinguishable from one that stopped being drawn.
    glm::vec3 ndc{0.0f};
    bool onScreen = false;
    bool visible = false;
    bool cameraCulled = false;
    bool submitted = false;
    bool finite = true;
    std::string cullReason;
};

// What a stretch of history says happened. Every field is a claim about the whole window, not the
// last step, because a single step is where the confusion lives.
struct TransformVerdict {
    bool haveHistory = false;   // false when fewer than two samples were recorded
    bool worldMoved = false;    // the scene's transform for this object changed
    bool cameraMoved = false;   // the view matrix changed
    bool screenMoved = false;   // the projected origin changed
    bool wentNonFinite = false; // a sample arrived with a non-finite matrix
    bool stoppedDrawing = false;// submitted in one sample and not in a later one
    float worldDistance = 0.0f; // total world-space travel of the origin
    float screenDistance = 0.0f;// total NDC travel of the projected origin
    // The reading, in one sentence, for a person: which of the three moved and what that means.
    std::string sentence;
};

class TransformHistory {
public:
    explicit TransformHistory(std::size_t capacity = 240);

    // Whose history this is. Changing the name clears what was recorded: a trail that mixes two
    // objects is a trail that lies about both.
    void setSubject(std::string name);
    [[nodiscard]] const std::string& subject() const { return subject_; }

    // Appends this frame if the frame contains the subject. A frame without the subject is not a
    // gap to interpolate over and not an error -- the object may simply not be in the scene yet --
    // so it is skipped, and `frameIndex` is what keeps the gap visible in the record.
    void record(const RendererDiagnosticFrame& frame);
    void clear();

    [[nodiscard]] const std::deque<TransformSample>& samples() const { return samples_; }
    [[nodiscard]] std::size_t size() const { return samples_.size(); }
    [[nodiscard]] bool empty() const { return samples_.empty(); }
    [[nodiscard]] std::size_t capacity() const { return capacity_; }

    // Reads the recorded window. `epsilon` is in world units for the transform, NDC for the screen.
    [[nodiscard]] TransformVerdict explain(float epsilon = 1e-4f) const;

    // The world-space path of the object's origin, oldest first -- what a trail overlay draws.
    [[nodiscard]] std::vector<glm::vec3> path() const;

private:
    std::string subject_;
    std::size_t capacity_;
    std::deque<TransformSample> samples_;
};

} // namespace avgen::rendering
