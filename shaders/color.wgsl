// Colour utilities on the GPU (ADR-030): the WGSL transliteration of src/core/color.cpp. Every
// function evaluates the same expressions, in the same order, as its CPU twin, so material
// programs shade identically on both sides (tests/rendering/test_material_gpu.cpp compares them
// within 1e-4). The exact formulas are in docs/procedural-materials.md ("Colour utilities").
//
// All RGB values are linear. Hue is in turns, wrapped to [0, 1) with h - floor(h).
//
// Names avoid the WGSL predeclared identifiers: the OKLab chroma scale is `colorSaturate`
// (`saturate` stays the builtin fields.wgsl and pbr_shade.wgsl use). Included by material.wgsl;
// include it only once per module.

const COLOR_TWO_PI: f32 = 6.283185307179586;

fn colorFract(x: f32) -> f32 {
    return x - floor(x);
}

// Sign-preserving cube root (cbrt(x) = sign(x) |x|^(1/3)); sign(0) = 0 gives cbrt(0) = 0.
fn signedCbrt(x: f32) -> f32 {
    return sign(x) * pow(abs(x), 1.0 / 3.0);
}

fn clampPositive(v: vec3<f32>) -> vec3<f32> {
    return max(v, vec3<f32>(0.0));
}

// Hue sector (0..6) from the max channel, shared by HSV and HSL.
fn hueSector(rgb: vec3<f32>, maxc: f32, chroma: f32) -> f32 {
    if (chroma <= 0.0) {
        return 0.0;
    }
    if (maxc == rgb.r) {
        var h6 = (rgb.g - rgb.b) / chroma;
        if (h6 < 0.0) {
            h6 = h6 + 6.0;
        }
        return h6;
    }
    if (maxc == rgb.g) {
        return (rgb.b - rgb.r) / chroma + 2.0;
    }
    return (rgb.r - rgb.g) / chroma + 4.0;
}

// Chroma / intermediate / minimum -> RGB (the common tail of hsvToRgb and hslToRgb).
fn fromChroma(hueTurns: f32, chroma: f32, m: f32) -> vec3<f32> {
    let h6 = colorFract(hueTurns) * 6.0;
    let x = chroma * (1.0 - abs(h6 % 2.0 - 1.0));
    var rgb = vec3<f32>(0.0);
    if (h6 < 1.0) {
        rgb = vec3<f32>(chroma, x, 0.0);
    } else if (h6 < 2.0) {
        rgb = vec3<f32>(x, chroma, 0.0);
    } else if (h6 < 3.0) {
        rgb = vec3<f32>(0.0, chroma, x);
    } else if (h6 < 4.0) {
        rgb = vec3<f32>(0.0, x, chroma);
    } else if (h6 < 5.0) {
        rgb = vec3<f32>(x, 0.0, chroma);
    } else {
        rgb = vec3<f32>(chroma, 0.0, x);
    }
    return rgb + vec3<f32>(m);
}

// ---- conversions --------------------------------------------------------------------------------

fn rgbToHsv(rgb: vec3<f32>) -> vec3<f32> { // h in turns [0,1), s, v
    let maxc = max(rgb.r, max(rgb.g, rgb.b));
    let minc = min(rgb.r, min(rgb.g, rgb.b));
    let chroma = maxc - minc;
    let h = colorFract(hueSector(rgb, maxc, chroma) / 6.0);
    var s = 0.0;
    if (maxc > 0.0) {
        s = chroma / maxc;
    }
    return vec3<f32>(h, s, maxc);
}

fn hsvToRgb(hsv: vec3<f32>) -> vec3<f32> {
    let chroma = hsv.z * hsv.y;
    return fromChroma(hsv.x, chroma, hsv.z - chroma);
}

fn rgbToHsl(rgb: vec3<f32>) -> vec3<f32> {
    let maxc = max(rgb.r, max(rgb.g, rgb.b));
    let minc = min(rgb.r, min(rgb.g, rgb.b));
    let chroma = maxc - minc;
    let l = 0.5 * (maxc + minc);
    let h = colorFract(hueSector(rgb, maxc, chroma) / 6.0);
    let denom = 1.0 - abs(2.0 * l - 1.0);
    var s = 0.0;
    if (chroma > 0.0 && denom > 0.0) {
        s = chroma / denom;
    }
    return vec3<f32>(h, s, l);
}

fn hslToRgb(hsl: vec3<f32>) -> vec3<f32> {
    let chroma = (1.0 - abs(2.0 * hsl.z - 1.0)) * hsl.y;
    return fromChroma(hsl.x, chroma, hsl.z - 0.5 * chroma);
}

// Björn Ottosson's OKLab, linear sRGB in and out.
fn rgbToOklab(c: vec3<f32>) -> vec3<f32> {
    let l = 0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b;
    let m = 0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b;
    let s = 0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b;
    let l_ = signedCbrt(l);
    let m_ = signedCbrt(m);
    let s_ = signedCbrt(s);
    return vec3<f32>(0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
                     1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
                     0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_);
}

fn oklabToRgb(lab: vec3<f32>) -> vec3<f32> {
    let l_ = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;
    let m_ = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;
    let s_ = lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z;
    let l = l_ * l_ * l_;
    let m = m_ * m_ * m_;
    let s = s_ * s_ * s_;
    return vec3<f32>(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                     -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                     -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}

// (L, C, h) with h in turns; |a| and |b| at or below 1e-8 give h = 0.
fn oklabToOklch(lab: vec3<f32>) -> vec3<f32> {
    let chroma = sqrt(lab.y * lab.y + lab.z * lab.z);
    var h = 0.0;
    if (abs(lab.y) > 1e-8 || abs(lab.z) > 1e-8) {
        h = colorFract(atan2(lab.z, lab.y) / COLOR_TWO_PI);
    }
    return vec3<f32>(lab.x, chroma, h);
}

fn oklchToOklab(lch: vec3<f32>) -> vec3<f32> {
    let angle = lch.z * COLOR_TWO_PI;
    return vec3<f32>(lch.x, lch.y * cos(angle), lch.y * sin(angle));
}

fn srgbToLinear1(c: f32) -> f32 {
    if (c <= 0.04045) {
        return c / 12.92;
    }
    return pow((c + 0.055) / 1.055, 2.4);
}

fn linearToSrgb1(c: f32) -> f32 {
    if (c <= 0.0031308) {
        return c * 12.92;
    }
    return 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

fn srgbToLinear(srgb: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(srgbToLinear1(srgb.r), srgbToLinear1(srgb.g), srgbToLinear1(srgb.b));
}

fn linearToSrgb(linear: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(linearToSrgb1(linear.r), linearToSrgb1(linear.g), linearToSrgb1(linear.b));
}

fn luminance(rgb: vec3<f32>) -> f32 { // Rec. 709
    return 0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b;
}

// ---- manipulation (results clamped to >= 0 component-wise) ---------------------------------------

fn hueShift(rgb: vec3<f32>, turns: f32) -> vec3<f32> { // OKLCH hue rotation
    var lch = oklabToOklch(rgbToOklab(rgb));
    lch.z = colorFract(lch.z + turns);
    return clampPositive(oklabToRgb(oklchToOklab(lch)));
}

fn hueShiftHsv(rgb: vec3<f32>, turns: f32) -> vec3<f32> { // HSV hue rotation (legacy look)
    var hsv = rgbToHsv(rgb);
    hsv.x = colorFract(hsv.x + turns);
    return clampPositive(hsvToRgb(hsv));
}

fn colorSaturate(rgb: vec3<f32>, factor: f32) -> vec3<f32> { // OKLab chroma x factor
    var lab = rgbToOklab(rgb);
    lab.y = lab.y * factor;
    lab.z = lab.z * factor;
    return clampPositive(oklabToRgb(lab));
}

fn lighten(rgb: vec3<f32>, amount: f32) -> vec3<f32> { // OKLab L += amount
    var lab = rgbToOklab(rgb);
    lab.x = lab.x + amount;
    return clampPositive(oklabToRgb(lab));
}

fn contrast(rgb: vec3<f32>, factor: f32, pivot: f32) -> vec3<f32> {
    return clampPositive((rgb - vec3<f32>(pivot)) * factor + vec3<f32>(pivot));
}

fn mixOklab(a: vec3<f32>, b: vec3<f32>, t: f32) -> vec3<f32> { // perceptual mix
    let la = rgbToOklab(a);
    let lb = rgbToOklab(b);
    return clampPositive(oklabToRgb(la * (1.0 - t) + lb * t));
}

// ---- palettes and ramps -------------------------------------------------------------------------

// Quilez cosine palette: a + b * cos(2pi (c t + d)) per channel, not clamped.
fn cosinePalette(a: vec3<f32>, b: vec3<f32>, c: vec3<f32>, d: vec3<f32>, t: f32) -> vec3<f32> {
    let phase = (c * t + d) * COLOR_TWO_PI;
    return a + b * vec3<f32>(cos(phase.x), cos(phase.y), cos(phase.z));
}

// The three-stop linear ramp the Ramp material op uses: stops at t = 0, 0.5 and 1, linear (not
// perceptual) blending of all four components, t clamped to [0, 1]. This is core::Ramp with
// three evenly spaced stops and `perceptual = false`.
fn ramp3(c0: vec4<f32>, c1: vec4<f32>, c2: vec4<f32>, tIn: f32) -> vec4<f32> {
    let t = saturate(tIn);
    if (t < 0.5) {
        let u = t * 2.0;
        return c0 * (1.0 - u) + c1 * u;
    }
    let u = (t - 0.5) * 2.0;
    return c1 * (1.0 - u) + c2 * u;
}
