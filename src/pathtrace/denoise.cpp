#include "pathtrace/denoise.hpp"

#include "core/log.hpp"

#include <fmt/format.h>

#if AVGEN_HAVE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

namespace avgen::pathtrace {

#if AVGEN_HAVE_OIDN

bool denoiseAvailable() { return true; }

std::string denoiseVersion() {
    return fmt::format("Open Image Denoise {}.{}.{}", OIDN_VERSION_MAJOR, OIDN_VERSION_MINOR,
                       OIDN_VERSION_PATCH);
}

Result<void> denoise(const DenoiseInput& in, std::vector<glm::vec3>& out) {
    if (in.color == nullptr) return fail("pathtrace: denoise needs a colour buffer");
    const std::size_t pixels = static_cast<std::size_t>(in.width) * in.height;
    if (pixels == 0) return fail("pathtrace: denoise needs a non-empty image");
    if (in.color->size() != pixels) {
        return fail("pathtrace: denoise colour buffer is {} pixels but the image is {}x{}",
                    in.color->size(), in.width, in.height);
    }
    if (in.albedo != nullptr && in.albedo->size() != pixels) {
        return fail("pathtrace: denoise albedo buffer size {} does not match the image", in.albedo->size());
    }
    if (in.normal != nullptr && in.normal->size() != pixels) {
        return fail("pathtrace: denoise normal buffer size {} does not match the image", in.normal->size());
    }
    if (in.normal != nullptr && in.albedo == nullptr) {
        // OIDN's RT filter ignores a normal buffer without an albedo buffer. Say so rather than
        // letting the caller believe a feature they passed is being used.
        log::warn("pathtrace: a normal AOV was given to the denoiser without an albedo AOV; the RT "
                  "filter ignores it, so it is not being used");
    }

    oidn::DeviceRef device = oidn::newDevice(oidn::DeviceType::CPU);
    device.commit();
    const char* err = nullptr;
    if (device.getError(err) != oidn::Error::None) {
        return fail("pathtrace: OIDN device error: {}", err != nullptr ? err : "unknown");
    }

    out.assign(pixels, glm::vec3(0.0f));

    oidn::FilterRef filter = device.newFilter("RT");
    // glm::vec3 is three tightly-packed floats, which is exactly oidn::Format::Float3.
    filter.setImage("color", const_cast<glm::vec3*>(in.color->data()), oidn::Format::Float3, in.width,
                    in.height);
    if (in.albedo != nullptr) {
        filter.setImage("albedo", const_cast<glm::vec3*>(in.albedo->data()), oidn::Format::Float3,
                        in.width, in.height);
    }
    if (in.normal != nullptr && in.albedo != nullptr) {
        filter.setImage("normal", const_cast<glm::vec3*>(in.normal->data()), oidn::Format::Float3,
                        in.width, in.height);
    }
    filter.setImage("output", out.data(), oidn::Format::Float3, in.width, in.height);
    // The input is scene-linear HDR with values well above 1 (an emissive surface, a sun). Telling
    // OIDN otherwise makes it assume a 0..1 range and crush the highlights.
    filter.set("hdr", true);
    if (in.cleanAux) filter.set("cleanAux", true);
    filter.commit();
    if (device.getError(err) != oidn::Error::None) {
        return fail("pathtrace: OIDN filter error: {}", err != nullptr ? err : "unknown");
    }

    filter.execute();
    if (device.getError(err) != oidn::Error::None) {
        return fail("pathtrace: OIDN execute error: {}", err != nullptr ? err : "unknown");
    }
    return {};
}

#else

bool denoiseAvailable() { return false; }

std::string denoiseVersion() { return "unavailable"; }

Result<void> denoise(const DenoiseInput& /*in*/, std::vector<glm::vec3>& /*out*/) {
    // Section 54: fail explicitly. Returning the input unchanged would look like a denoise that ran
    // and did very little, which is a far worse thing to debug than a refusal.
    return fail("pathtrace: this build has no denoiser. Configure with -DAVGEN_PATHTRACE_DENOISE=ON "
                "to fetch Open Image Denoise (ADR-346)");
}

#endif

} // namespace avgen::pathtrace
