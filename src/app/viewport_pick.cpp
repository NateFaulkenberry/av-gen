#include "app/viewport_pick.hpp"

#include "gpu/readback.hpp"

#include <algorithm>
#include <cstring>
#include <cmath>

namespace avgen::app {
namespace {

// NDC of the centre of a pixel. The half-texel matters: without it every pick is biased half a
// pixel up and left, which is invisible at a glance and consistently wrong.
glm::vec2 pixelCentreNdc(const PickView& view, glm::uvec2 pixel) {
    const float w = static_cast<float>(std::max(view.size.x, 1u));
    const float h = static_cast<float>(std::max(view.size.y, 1u));
    const float u = (static_cast<float>(pixel.x) + 0.5f) / w;
    const float v = (static_cast<float>(pixel.y) + 0.5f) / h;
    // Y is flipped: pixel rows run down the screen, NDC runs up it. This is the same convention
    // linear_depth.wgsl uses, and the two have to agree or reconstruction lands on the wrong side
    // of the frame.
    return glm::vec2(u * 2.0f - 1.0f, 1.0f - v * 2.0f);
}
} // namespace

glm::vec3 rayThroughPixel(const PickView& view, glm::uvec2 pixel) {
    const glm::vec2 ndc = pixelCentreNdc(view, pixel);
    // Two points on the ray, un-projected and de-homogenised. Un-projecting a single point and
    // subtracting the camera position works for a perspective camera and silently fails for an
    // orthographic one, where every ray starts somewhere different.
    const glm::vec4 nearH = view.invViewProj * glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
    const glm::vec4 farH = view.invViewProj * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);
    if (std::abs(nearH.w) < 1e-9f || std::abs(farH.w) < 1e-9f) {
        return glm::normalize(view.cameraForward);
    }
    const glm::vec3 near3 = glm::vec3(nearH) / nearH.w;
    const glm::vec3 far3 = glm::vec3(farH) / farH.w;
    const glm::vec3 direction = far3 - near3;
    const float length = glm::length(direction);
    return length > 1e-9f ? direction / length : glm::normalize(view.cameraForward);
}

glm::vec3 worldPositionAt(const PickView& view, glm::uvec2 pixel, float linearDepth) {
    const glm::vec3 ray = rayThroughPixel(view, pixel);
    const glm::vec3 forward = glm::normalize(view.cameraForward);
    // The target stores distance along the forward axis. Travelling `linearDepth` along the ray
    // instead lands short by exactly this cosine, and the error is zero at the frame's centre and
    // largest at its corners -- so it looks like a small inaccuracy rather than a missing division.
    const float cosine = glm::dot(ray, forward);
    if (cosine < 1e-4f) {
        return view.cameraPosition + ray * linearDepth;
    }
    return view.cameraPosition + ray * (linearDepth / cosine);
}

Result<PickResult> pickAt(gpu::Context& context, const wgpu::Texture& ids,
                          const wgpu::Texture& linearDepth, const PickView& view, glm::uvec2 pixel) {
    if (view.size.x == 0 || view.size.y == 0) {
        return fail("pick: the render target has no size");
    }
    if (pixel.x >= view.size.x || pixel.y >= view.size.y) {
        return fail("pick: ({}, {}) is outside the {}x{} target", pixel.x, pixel.y, view.size.x,
                    view.size.y);
    }
    if (ids == nullptr || linearDepth == nullptr) {
        return fail("pick: the scene has not been rendered yet");
    }

    // Both texels in one submission and one wait.
    //
    // This used to read the depth, block for the GPU, and then read the identifier and block again
    // -- two full CPU stalls on the main thread, on a click, to move eight bytes. The cost of a
    // readback here is the round trip and not the payload, so the second wait was the entire
    // second half of the price. It is item 4 of docs/application-performance.md section 14.
    //
    // The identifier is now fetched even for a click that turns out to be sky, where the old code
    // returned before asking for it. That is one more 256-byte copy inside a submission that was
    // already happening, against a whole round trip saved on every click that hits something --
    // which is most of them, and the only ones anybody is waiting on.
    const gpu::TexelRequest requests[] = {
        {.texture = &linearDepth, .x = pixel.x, .y = pixel.y},
        {.texture = &ids, .x = pixel.x, .y = pixel.y},
    };
    auto texels = gpu::readTexelsR32(context, requests);
    if (!texels) {
        return std::unexpected(texels.error());
    }
    float depth = 0.0f;
    std::memcpy(&depth, &(*texels)[0], sizeof(depth));
    const std::uint32_t identifier = (*texels)[1];

    PickResult out;
    out.distance = depth;
    // Sky. Reported as a miss with a valid distance rather than as an error: clicking the sky is a
    // perfectly ordinary thing to do and it means "deselect", not "something went wrong".
    if (!(depth < kPickFarDistance)) {
        return out;
    }
    out.hit = true;
    out.objectId = identifier & 0xffffu;
    out.materialId = (identifier >> 16) & 0xffffu;
    out.position = worldPositionAt(view, pixel, depth);
    return out;
}


Result<glm::vec3> pickNormalAt(gpu::Context& context, const wgpu::Texture& linearDepth,
                               const PickView& view, glm::uvec2 pixel, std::uint32_t step) {
    if (linearDepth == nullptr) {
        return fail("pick normal: the scene has not been rendered yet");
    }
    step = std::max(step, 1u);
    // Sampled toward the middle of the frame, so a click near an edge still has both neighbours
    // inside the target rather than failing or clamping onto itself.
    const std::uint32_t maxX = view.size.x > 0 ? view.size.x - 1 : 0;
    const std::uint32_t maxY = view.size.y > 0 ? view.size.y - 1 : 0;
    const std::uint32_t rightX = pixel.x + step <= maxX ? pixel.x + step : (pixel.x >= step ? pixel.x - step : pixel.x);
    const std::uint32_t downY = pixel.y + step <= maxY ? pixel.y + step : (pixel.y >= step ? pixel.y - step : pixel.y);
    if (rightX == pixel.x || downY == pixel.y) {
        return fail("pick normal: no room for neighbours at ({}, {})", pixel.x, pixel.y);
    }

    const glm::uvec2 centre = pixel;
    const glm::uvec2 right(rightX, pixel.y);
    const glm::uvec2 down(pixel.x, downY);
    const glm::uvec2 taps[3] = {centre, right, down};
    // Three taps, one submission, one wait -- as above, and for the same reason.
    const gpu::TexelRequest requests[] = {
        {.texture = &linearDepth, .x = taps[0].x, .y = taps[0].y},
        {.texture = &linearDepth, .x = taps[1].x, .y = taps[1].y},
        {.texture = &linearDepth, .x = taps[2].x, .y = taps[2].y},
    };
    auto texels = gpu::readTexelsR32(context, requests);
    if (!texels) {
        return std::unexpected(texels.error());
    }
    glm::vec3 points[3];
    for (int i = 0; i < 3; ++i) {
        float depth = 0.0f;
        std::memcpy(&depth, &(*texels)[static_cast<std::size_t>(i)], sizeof(depth));
        if (!(depth < kPickFarDistance)) {
            return fail("pick normal: a neighbour is sky");
        }
        points[i] = worldPositionAt(view, taps[i], depth);
    }

    // The winding is chosen so the result points back toward the camera for a surface facing it.
    const glm::vec3 a = points[1] - points[0];
    const glm::vec3 b = points[2] - points[0];
    const glm::vec3 cross = glm::cross(b, a);
    const float length = glm::length(cross);
    if (length < 1e-9f) {
        return fail("pick normal: the neighbourhood is degenerate");
    }
    glm::vec3 normal = cross / length;
    if (glm::dot(normal, view.cameraPosition - points[0]) < 0.0f) {
        normal = -normal;
    }
    return normal;
}

} // namespace avgen::app
