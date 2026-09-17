#include "labs/visibility_reason.hpp"

#include <array>

namespace avgen::labs {
namespace {

constexpr std::array<ReasonInfo, 13> kReasons{{
    {VisibilityReason::Visible, "VISIBLE", ReasonStatus::Reported,
     "src/rendering/scene_renderer.cpp:makeItem", "submitted"},
    {VisibilityReason::Eligible, "ELIGIBLE", ReasonStatus::Reported,
     "src/rendering/scene_renderer.cpp", "eligible"},
    {VisibilityReason::Disabled, "DISABLED", ReasonStatus::Reported,
     "src/scene/scene_types.hpp:Entity::visible", "hidden"},
    {VisibilityReason::InvalidMesh, "INVALID_MESH", ReasonStatus::Reported,
     "src/rendering/scene_renderer.cpp", "invalid-mesh"},
    {VisibilityReason::InvalidBounds, "INVALID_BOUNDS", ReasonStatus::Unreported,
     "src/scene/scene.cpp:entityCullBounds", ""},
    {VisibilityReason::FrustumCulled, "FRUSTUM_CULLED", ReasonStatus::Reported,
     "src/scene/composition.cpp:cullEntityNodes", "camera-frustum"},
    {VisibilityReason::DistanceCulled, "DISTANCE_CULLED", ReasonStatus::Unreported,
     "shaders/cull.wgsl:cs_cull_classify", ""},
    {VisibilityReason::ScreenSizeCulled, "SCREEN_SIZE_CULLED", ReasonStatus::Unreported,
     "shaders/cull.wgsl:cs_cull_classify", ""},
    {VisibilityReason::DepthBandThinned, "DEPTH_BAND_THINNED", ReasonStatus::Unreported,
     "shaders/cull.wgsl:depthBand", ""},
    {VisibilityReason::LodRejected, "LOD_REJECTED", ReasonStatus::Unreported,
     "shaders/cull.wgsl:cs_cull_classify", ""},
    {VisibilityReason::NotAShadowCaster, "NOT_A_SHADOW_CASTER", ReasonStatus::Unreported,
     "src/rendering/scene_renderer.cpp:shadowEligible", ""},
    {VisibilityReason::ShadowFrustumCulled, "SHADOW_FRUSTUM_CULLED", ReasonStatus::Unreported,
     "src/rendering/scene_renderer.cpp:anyCascadeSees", ""},
    {VisibilityReason::PassDisabled, "PASS_DISABLED", ReasonStatus::Unreported,
     "src/rendering/scene_renderer.hpp:PassToggles", ""},
}};

} // namespace

std::span<const ReasonInfo> visibilityReasons() { return kReasons; }

const ReasonInfo& reasonInfo(VisibilityReason reason) {
    return kReasons[static_cast<std::size_t>(reason)];
}

std::string_view reasonCode(VisibilityReason reason) { return reasonInfo(reason).code; }

std::optional<VisibilityReason> reasonFromCode(std::string_view code) {
    for (const ReasonInfo& info : kReasons) {
        if (info.code == code) {
            return info.reason;
        }
    }
    return std::nullopt;
}

std::optional<VisibilityReason> fromCullReason(std::string_view cullReason) {
    if (cullReason.empty()) {
        return std::nullopt;
    }
    for (const ReasonInfo& info : kReasons) {
        if (!info.cullReason.empty() && info.cullReason == cullReason) {
            return info.reason;
        }
    }
    return std::nullopt;
}

} // namespace avgen::labs
