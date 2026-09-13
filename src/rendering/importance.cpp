#include "rendering/importance.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::rendering {

namespace {

// Half the triangles of a closed mesh face the camera, and the other half are back-face culled, so
// the silhouette area is shared among N/2 triangles rather than N. Dividing by the fraction is the
// same arithmetic as multiplying the area by two; written this way because "over the front-facing
// half" is what the number means.
constexpr float kFrontFacingFraction = 2.0f;

constexpr float kPi = 3.14159265358979323846f;

} // namespace

ViewContext ViewContext::fromCamera(const scene::Camera& camera, std::uint32_t width,
                                    std::uint32_t height) {
    ViewContext view;
    view.cameraPosition = camera.position;
    view.viewportWidth = static_cast<float>(width);
    view.viewportHeight = static_cast<float>(height);
    view.nearPlane = camera.nearPlane;
    if (height == 0 || width == 0) {
        return view; // a zero viewport measures nothing; pixelsPerUnit stays 0 and every record is 0
    }
    const float aspect = view.viewportWidth / view.viewportHeight;
    view.viewProjection = camera.projection(aspect) * camera.view();
    const float halfFov = std::max(camera.effectiveFovY() * 0.5f, 1e-4f);
    view.pixelsPerUnit = view.viewportHeight / (2.0f * std::tan(halfFov));
    return view;
}

float ViewContext::pixelsPerUnitAt(float distance) const {
    return pixelsPerUnit / std::max(distance, std::max(nearPlane, 1e-4f));
}

bool ViewContext::projectToScreen(glm::vec3 world, glm::vec2& outPixels) const {
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0f) {
        outPixels = glm::vec2(0.0f);
        return false;
    }
    const glm::vec2 ndc(clip.x / clip.w, clip.y / clip.w);
    outPixels = glm::vec2((ndc.x * 0.5f + 0.5f) * viewportWidth,
                          (0.5f - ndc.y * 0.5f) * viewportHeight);
    return true;
}

ImportanceRecord ImportanceEvaluator::evaluate(const ViewContext& view, const ImportanceInput& in,
                                               std::uint32_t index) {
    ImportanceRecord r;
    r.index = index;
    r.hero = in.hero;
    r.triangles = in.triangles;
    r.distance = glm::length(in.center - view.cameraPosition);
    r.pixelsPerUnit = view.pixelsPerUnitAt(r.distance);
    r.projectedRadius = in.radius * r.pixelsPerUnit;

    // The Cauchy estimate when the mesh's area is known, the bounding disc when it is not.
    //
    // Both divide by the *front-facing* triangle count, so the two are the same statement about the
    // same thing and differ only in where the silhouette area comes from. For a sphere they agree
    // exactly -- Cauchy's A/4 of 4(pi)r^2 is the disc -- and for anything that does not fill its
    // bounding sphere the disc is larger. That direction is the one to have: over-stating how many
    // pixels a triangle gets keeps triangles rather than dropping them, so a mesh whose area was
    // never measured is drawn at more detail than it needs and never at less.
    const float ppu2 = r.pixelsPerUnit * r.pixelsPerUnit;
    if (in.surfaceArea > 0.0f) {
        r.projectedArea = in.surfaceArea * ppu2 * 0.25f;
    } else {
        r.projectedArea = kPi * r.projectedRadius * r.projectedRadius;
    }
    r.pixelsPerTriangle =
        in.triangles > 0
            ? r.projectedArea * kFrontFacingFraction / static_cast<float>(in.triangles)
            : 0.0f;

    glm::vec2 here{0.0f};
    glm::vec2 there{0.0f};
    const bool front = view.projectToScreen(in.center, here);
    r.inFront = front;
    if (in.hasPrevious && front && view.projectToScreen(in.previousCenter, there)) {
        r.screenVelocity = glm::length(here - there);
    }
    return r;
}

void ImportanceEvaluator::evaluate(const ViewContext& view, std::span<const ImportanceInput> inputs,
                                   std::span<ImportanceRecord> out) {
    const std::size_t n = std::min(inputs.size(), out.size());
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = evaluate(view, inputs[i], static_cast<std::uint32_t>(i));
    }
}

float ImportanceEvaluator::pixelsPerTriangleFor(const ImportanceRecord& record, float surfaceArea,
                                                std::uint32_t triangles) {
    if (triangles == 0) {
        return 0.0f;
    }
    const float ppu2 = record.pixelsPerUnit * record.pixelsPerUnit;
    // No area for the candidate: reuse the record's own projected area, which is the best statement
    // available about how much of the screen this object covers.
    const float area = surfaceArea > 0.0f ? surfaceArea * ppu2 * 0.25f : record.projectedArea;
    return area * kFrontFacingFraction / static_cast<float>(triangles);
}

} // namespace avgen::rendering
