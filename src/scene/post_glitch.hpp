#pragma once

// Post effects as instruments, and the glitch vocabulary (ADR-1065, brief §7-8; research 4).
//
// Two passes, each skipped entirely while every one of its amounts is 0 (the defaults), so no existing picture
// changes and an effect left at 0 costs nothing:
//
//   post/glitch   HDR, beside the lens pass and before bloom (so the glow follows the damage):
//     shock     a screen-space shockwave ring with a chromatic fringe     post/shock/{amount, radius, width, chroma,
//                                                                          centerX, centerY}
//     glitch    block displacement, line tears and channel swaps, chosen  post/glitch/{amount, block, rate, seed,
//               by hashes of the block and floor(time x rate) + seed       tear, tearShift, swap, drift}
//     split     a directional (or spectral) RGB split                      post/split/{amount, angle, spectral}
//     sort      a stateless "pixel sort": inside a brightness mask, the   post/sort/{amount, threshold, length,
//               brightest sample along a direction smeared down the span   angle, invert}
//     radial    a radial (zoom) blur toward a centre                       post/radial/{amount, centerX, centerY}
//   post/display  after the composite and the look, before FXAA:
//     display   scanlines, mosaic, posterise with ordered dither           post/display/{scanlines, lines, pixelate,
//                                                                          posterize, dither}
//
// Every one is a pure function of the frame and timeline time: no history, so seek equals play at once. Pixel
// sizes are authored at 1080 lines and scale with the output; the scanline count is in lines of the frame, so a 4K
// render matches its 1080p preview.

#include "params/parameter.hpp"

#include <nlohmann/json_fwd.hpp>

#include <vector>

namespace avgen::params {
class ParameterSet;
}

namespace avgen::scene {

struct PostGlitchSettings {
    // shock
    float shockAmount = 0.0f;  // displacement at the ring's crest, pixels at 1080 lines
    float shockRadius = 0.5f;  // the ring's radius, in half-diagonals (0 = the centre, 1 = the corners)
    float shockWidth = 0.08f;  // the ring's half-thickness, in half-diagonals
    float shockChroma = 0.3f;  // 0..1: how far the channels part across the crest
    float shockCenterX = 0.5f; // uv
    float shockCenterY = 0.5f;
    // glitch
    float glitchAmount = 0.0f; // 0..1: the share of blocks displaced
    float glitchBlock = 48.0f; // block edge, pixels at 1080 lines
    float glitchRate = 12.0f;  // re-chosen this many times a second
    float glitchSeed = 0.0f;   // added to the epoch: key it for a different pattern
    float glitchTear = 0.0f;   // 0..1: the share of row bands torn sideways
    float glitchTearShift = 60.0f; // how far a torn band moves, pixels at 1080 lines
    float glitchSwap = 0.0f;   // 0..1: the chance a displaced block swaps its channels
    float glitchDrift = 40.0f; // how far a displaced block moves, pixels at 1080 lines
    // split
    float splitAmount = 0.0f;  // red and blue apart by this many pixels at 1080 lines
    float splitAngle = 0.0f;   // degrees (0 = horizontal)
    float splitSpectral = 0.0f; // 0..1: three copies -> a rainbow fringe of eight taps
    // sort
    float sortAmount = 0.0f;   // 0..1: mix of the sorted image
    float sortThreshold = 0.8f; // the mask: pixels brighter than this (scene-linear luminance) sort
    float sortLength = 120.0f; // longest span, pixels at 1080 lines
    float sortAngle = 90.0f;   // degrees: 90 pours down, 0 runs right
    float sortInvert = 0.0f;   // 1: the dark pixels sort instead
    // radial
    float radialAmount = 0.0f; // 0..1: the blur's length as a fraction of the distance to the centre
    float radialCenterX = 0.5f;
    float radialCenterY = 0.5f;
    // display
    float displayScanlines = 0.0f; // 0..1: depth of the scanline darkening
    float displayLines = 270.0f;   // scanlines over the frame's height
    float displayPixelate = 0.0f;  // mosaic cell, pixels at 1080 lines (< 1 off)
    float displayPosterize = 0.0f; // levels per channel (< 2 off)
    float displayDither = 0.0f;    // 0..1: ordered (Bayer 4x4) dither across the levels
    float displayLetterbox = 0.0f; // ADR-1075: black bars to this aspect (width / height, e.g. 2.39); 0 off
    float displayLetterboxAmount = 1.0f; // ADR-1075: 0..1, how far the bars have slid in

    [[nodiscard]] bool glitchPassActive() const {
        return shockAmount > 0.0f || glitchAmount > 0.0f || glitchTear > 0.0f || splitAmount > 0.0f ||
               sortAmount > 0.0f || radialAmount > 0.0f;
    }
    [[nodiscard]] bool displayPassActive() const {
        return displayScanlines > 0.0f || displayPixelate >= 1.0f || displayPosterize >= 2.0f ||
               (displayLetterbox > 0.0f && displayLetterboxAmount > 0.0f);
    }
};

struct PostGlitchParameters {
    struct Row {
        params::Parameter<float>* param = nullptr;
        float PostGlitchSettings::*field = nullptr;
        const char* key = nullptr; // the scene file's `post` key ("shockAmount")
    };
    std::vector<Row> rows;
};

[[nodiscard]] PostGlitchParameters registerPostGlitchParameters(params::ParameterSet& params,
                                                                const PostGlitchSettings& defaults);
void applyPostGlitchParameters(const PostGlitchParameters& p, PostGlitchSettings& settings);
// A scene's `post` block key: true when it is one of these (and was applied).
bool applyPostGlitchJsonKey(const PostGlitchParameters& p, const std::string& key, const nlohmann::json& value);

} // namespace avgen::scene
