#include "rendering/transform_history.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace avgen::rendering {

namespace {

bool finite(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool finite(const glm::mat4& m) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(m[c][r])) {
                return false;
            }
        }
    }
    return true;
}

// The scale is the length of each basis column and the basis is those columns normalised. A column
// of zero length is left as the identity's: a degenerate basis is not a rotation, and inventing one
// would make a collapsed object read as merely rotated.
void decompose(const glm::mat4& m, glm::vec3& scale, glm::mat3& basis) {
    basis = glm::mat3(1.0f);
    for (int c = 0; c < 3; ++c) {
        const glm::vec3 column(m[c]);
        const float length = glm::length(column);
        scale[c] = length;
        if (length > 1e-8f && std::isfinite(length)) {
            basis[c] = column / length;
        }
    }
}

float maxComponentDifference(const glm::mat4& a, const glm::mat4& b) {
    float worst = 0.0f;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            worst = std::max(worst, std::abs(a[c][r] - b[c][r]));
        }
    }
    return worst;
}

} // namespace

TransformHistory::TransformHistory(std::size_t capacity) : capacity_(std::max<std::size_t>(2, capacity)) {}

void TransformHistory::setSubject(std::string name) {
    if (name == subject_) {
        return;
    }
    subject_ = std::move(name);
    samples_.clear();
}

void TransformHistory::clear() {
    samples_.clear();
}

void TransformHistory::record(const RendererDiagnosticFrame& frame) {
    if (subject_.empty()) {
        return;
    }
    const auto it = std::find_if(frame.objects.begin(), frame.objects.end(),
                                 [&](const RenderObjectDiagnostic& o) { return o.name == subject_; });
    if (it == frame.objects.end()) {
        return;
    }

    TransformSample sample;
    sample.frameIndex = frame.frameIndex;
    sample.worldPosition = it->worldPosition;
    sample.worldMatrix = it->worldMatrix;
    decompose(it->worldMatrix, sample.worldScale, sample.worldBasis);
    sample.cameraPosition = frame.cameraPosition;
    sample.view = frame.view;
    sample.viewProjection = frame.viewProjection;
    sample.visible = it->visible;
    sample.cameraCulled = it->cameraCulled;
    sample.submitted = it->submitted;
    sample.cullReason = it->cullReason;
    sample.finite = it->finite && finite(it->worldPosition) && finite(it->worldMatrix) && finite(frame.viewProjection);

    // The projection is done here rather than taken from the renderer because the renderer does not
    // compute it: this is the screen-space history Phase 9.3 asks for, and it has to be the *same*
    // view-projection the frame drew with or it answers a different question.
    if (sample.finite) {
        const glm::vec4 clip = frame.viewProjection * glm::vec4(it->worldPosition, 1.0f);
        if (std::abs(clip.w) > 1e-8f) {
            sample.ndc = glm::vec3(clip) / clip.w;
            sample.onScreen = clip.w > 0.0f && std::abs(sample.ndc.x) <= 1.0f && std::abs(sample.ndc.y) <= 1.0f &&
                              sample.ndc.z >= 0.0f && sample.ndc.z <= 1.0f;
        }
    }

    samples_.push_back(sample);
    while (samples_.size() > capacity_) {
        samples_.pop_front();
    }
}

std::vector<glm::vec3> TransformHistory::path() const {
    std::vector<glm::vec3> points;
    points.reserve(samples_.size());
    for (const TransformSample& s : samples_) {
        if (finite(s.worldPosition)) {
            points.push_back(s.worldPosition);
        }
    }
    return points;
}

TransformVerdict TransformHistory::explain(float epsilon) const {
    TransformVerdict verdict;
    if (samples_.size() < 2) {
        verdict.sentence = samples_.empty() ? "nothing has been recorded for '" + subject_ + "' yet"
                                            : "only one frame of '" + subject_ + "' has been recorded";
        return verdict;
    }
    verdict.haveHistory = true;

    const TransformSample& first = samples_.front();
    bool sawSubmitted = false;
    for (std::size_t i = 0; i < samples_.size(); ++i) {
        const TransformSample& s = samples_[i];
        if (!s.finite) {
            verdict.wentNonFinite = true;
        }
        if (s.submitted) {
            sawSubmitted = true;
        } else if (sawSubmitted) {
            verdict.stoppedDrawing = true;
        }
        if (maxComponentDifference(s.worldMatrix, first.worldMatrix) > epsilon) {
            verdict.worldMoved = true;
        }
        if (maxComponentDifference(s.view, first.view) > epsilon) {
            verdict.cameraMoved = true;
        }
        if (glm::length(s.ndc - first.ndc) > epsilon) {
            verdict.screenMoved = true;
        }
        if (i > 0) {
            const TransformSample& prev = samples_[i - 1];
            if (finite(s.worldPosition) && finite(prev.worldPosition)) {
                verdict.worldDistance += glm::length(s.worldPosition - prev.worldPosition);
            }
            verdict.screenDistance += glm::length(s.ndc - prev.ndc);
        }
    }

    const std::string who = "'" + subject_ + "'";
    const std::string over = " over " + std::to_string(samples_.size()) + " frames";
    if (verdict.wentNonFinite) {
        verdict.sentence = who + " went non-finite" + over + ": the history is not evidence about motion";
    } else if (verdict.worldMoved && verdict.cameraMoved) {
        verdict.sentence = who + " moved and so did the camera" + over +
                           "; the screen motion is both, and neither is isolated by this window";
    } else if (verdict.worldMoved) {
        verdict.sentence = who + " moved" + over + " with a still camera: the scene moved it";
    } else if (verdict.cameraMoved && verdict.screenMoved) {
        verdict.sentence = who + " held its transform" + over +
                           " while the camera moved, and its screen position moved with it: that is parallax";
    } else if (verdict.cameraMoved) {
        verdict.sentence = who + " held its transform" + over +
                           " and did not move on screen although the camera did, which is only correct if it is "
                           "at the camera's own position or off screen";
    } else if (verdict.screenMoved) {
        verdict.sentence = who + " moved on screen" + over +
                           " with an unchanged transform and an unchanged camera: nothing in the recorded state "
                           "explains it";
    } else {
        verdict.sentence = who + " held still" + over + ", and so did the camera";
    }
    if (verdict.stoppedDrawing) {
        verdict.sentence += "; it stopped being submitted partway through";
    }
    return verdict;
}

} // namespace avgen::rendering
