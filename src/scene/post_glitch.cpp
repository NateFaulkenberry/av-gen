#include "scene/post_glitch.hpp"

#include "params/parameter_set.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace avgen::scene {

namespace {

struct Spec {
    const char* path;
    const char* key;
    float PostGlitchSettings::*field;
    float lo, hi, softLo, softHi;
};

// One row per parameter: its path, its scene-file key, its field and its ranges.
const Spec kSpecs[] = {
    {"post/shock/amount", "shockAmount", &PostGlitchSettings::shockAmount, 0.0f, 1000.0f, 0.0f, 120.0f},
    {"post/shock/radius", "shockRadius", &PostGlitchSettings::shockRadius, -1.0f, 4.0f, 0.0f, 1.5f},
    {"post/shock/width", "shockWidth", &PostGlitchSettings::shockWidth, 0.001f, 2.0f, 0.01f, 0.4f},
    {"post/shock/chroma", "shockChroma", &PostGlitchSettings::shockChroma, 0.0f, 1.0f, 0.0f, 1.0f},
    {"post/shock/centerX", "shockCenterX", &PostGlitchSettings::shockCenterX, -2.0f, 3.0f, 0.0f, 1.0f},
    {"post/shock/centerY", "shockCenterY", &PostGlitchSettings::shockCenterY, -2.0f, 3.0f, 0.0f, 1.0f},
    {"post/glitch/amount", "glitchAmount", &PostGlitchSettings::glitchAmount, 0.0f, 1.0f, 0.0f, 1.0f},
    {"post/glitch/block", "glitchBlock", &PostGlitchSettings::glitchBlock, 2.0f, 1024.0f, 8.0f, 256.0f},
    {"post/glitch/rate", "glitchRate", &PostGlitchSettings::glitchRate, 0.0f, 240.0f, 0.0f, 60.0f},
    {"post/glitch/seed", "glitchSeed", &PostGlitchSettings::glitchSeed, -1.0e6f, 1.0e6f, 0.0f, 100.0f},
    {"post/glitch/tear", "glitchTear", &PostGlitchSettings::glitchTear, 0.0f, 1.0f, 0.0f, 1.0f},
    {"post/glitch/tearShift", "glitchTearShift", &PostGlitchSettings::glitchTearShift, 0.0f, 2000.0f, 0.0f, 400.0f},
    {"post/glitch/swap", "glitchSwap", &PostGlitchSettings::glitchSwap, 0.0f, 1.0f, 0.0f, 1.0f},
    {"post/glitch/drift", "glitchDrift", &PostGlitchSettings::glitchDrift, 0.0f, 2000.0f, 0.0f, 400.0f},
    {"post/split/amount", "splitAmount", &PostGlitchSettings::splitAmount, 0.0f, 400.0f, 0.0f, 60.0f},
    {"post/split/angle", "splitAngle", &PostGlitchSettings::splitAngle, -360.0f, 360.0f, -180.0f, 180.0f},
    {"post/split/spectral", "splitSpectral", &PostGlitchSettings::splitSpectral, 0.0f, 1.0f, 0.0f, 1.0f},
    {"post/sort/amount", "sortAmount", &PostGlitchSettings::sortAmount, 0.0f, 1.0f, 0.0f, 1.0f},
    {"post/sort/threshold", "sortThreshold", &PostGlitchSettings::sortThreshold, 0.0f, 100.0f, 0.0f, 4.0f},
    {"post/sort/length", "sortLength", &PostGlitchSettings::sortLength, 0.0f, 2000.0f, 0.0f, 400.0f},
    {"post/sort/angle", "sortAngle", &PostGlitchSettings::sortAngle, -360.0f, 360.0f, -180.0f, 180.0f},
    {"post/sort/invert", "sortInvert", &PostGlitchSettings::sortInvert, 0.0f, 1.0f, 0.0f, 1.0f},
    {"post/radial/amount", "radialAmount", &PostGlitchSettings::radialAmount, 0.0f, 1.0f, 0.0f, 0.5f},
    {"post/radial/centerX", "radialCenterX", &PostGlitchSettings::radialCenterX, -2.0f, 3.0f, 0.0f, 1.0f},
    {"post/radial/centerY", "radialCenterY", &PostGlitchSettings::radialCenterY, -2.0f, 3.0f, 0.0f, 1.0f},
    {"post/display/scanlines", "displayScanlines", &PostGlitchSettings::displayScanlines, 0.0f, 1.0f, 0.0f, 1.0f},
    {"post/display/lines", "displayLines", &PostGlitchSettings::displayLines, 8.0f, 4320.0f, 120.0f, 1080.0f},
    {"post/display/pixelate", "displayPixelate", &PostGlitchSettings::displayPixelate, 0.0f, 512.0f, 0.0f, 64.0f},
    {"post/display/posterize", "displayPosterize", &PostGlitchSettings::displayPosterize, 0.0f, 256.0f, 0.0f, 16.0f},
    {"post/display/dither", "displayDither", &PostGlitchSettings::displayDither, 0.0f, 1.0f, 0.0f, 1.0f},
    // ADR-1075: the letterbox.
    {"post/display/letterbox", "displayLetterbox", &PostGlitchSettings::displayLetterbox, 0.0f, 10.0f, 0.0f, 3.0f},
    {"post/display/letterboxAmount", "displayLetterboxAmount", &PostGlitchSettings::displayLetterboxAmount, 0.0f, 1.0f,
     0.0f, 1.0f},
};

} // namespace

PostGlitchParameters registerPostGlitchParameters(params::ParameterSet& params, const PostGlitchSettings& defaults) {
    PostGlitchParameters out;
    for (const Spec& s : kSpecs) {
        params::ParamDesc<float> d;
        d.path = s.path;
        d.defaultValue = defaults.*(s.field);
        d.hardMin = s.lo;
        d.hardMax = s.hi;
        d.softMin = s.softLo;
        d.softMax = s.softHi;
        out.rows.push_back({&params.add(std::move(d)), s.field, s.key});
    }
    return out;
}

void applyPostGlitchParameters(const PostGlitchParameters& p, PostGlitchSettings& settings) {
    for (const auto& row : p.rows) {
        if (row.param != nullptr) {
            settings.*(row.field) = row.param->value();
        }
    }
}

bool applyPostGlitchJsonKey(const PostGlitchParameters& p, const std::string& key, const nlohmann::json& value) {
    for (const auto& row : p.rows) {
        if (key == row.key && row.param != nullptr) {
            if (value.is_number()) {
                row.param->setBase(value.get<float>());
            }
            return true;
        }
    }
    return false;
}

} // namespace avgen::scene
