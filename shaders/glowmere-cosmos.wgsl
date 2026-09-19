/*{
  "NAME": "Glowmere cosmos",
  "DESCRIPTION": "The ethereal deep behind the Tree of Life: a blue-black base, broad indigo and violet nebular formations, a soft cyan halo to stand the island against, and two very distant luminous regions. Everything here is low-amplitude and low-frequency on purpose -- it is a stage, not a subject.",
  "CREDIT": "avgen -- Tree of Life floating island",
  "CATEGORIES": ["Generator", "Background"],
  "INPUTS": [
    {"NAME": "baseTint", "TYPE": "color", "DEFAULT": [0.030, 0.042, 0.115, 1.0], "LABEL": "Deep space base"},
    {"NAME": "baseLevel", "TYPE": "float", "DEFAULT": 0.105, "MIN": 0.0, "MAX": 0.5, "LABEL": "Base level"},
    {"NAME": "hazeAmount", "TYPE": "float", "DEFAULT": 0.052, "MIN": 0.0, "MAX": 0.6, "LABEL": "Nebular haze"},
    {"NAME": "hazeScale", "TYPE": "float", "DEFAULT": 3.4, "MIN": 0.2, "MAX": 6.0, "LABEL": "Haze scale"},
    {"NAME": "violetAmount", "TYPE": "float", "DEFAULT": 0.030, "MIN": 0.0, "MAX": 0.5, "LABEL": "Violet filament"},
    {"NAME": "haloAmount", "TYPE": "float", "DEFAULT": 0.048, "MIN": 0.0, "MAX": 0.5, "LABEL": "Island halo"},
    {"NAME": "haloRadius", "TYPE": "float", "DEFAULT": 0.42, "MIN": 0.05, "MAX": 1.5, "LABEL": "Halo radius"},
    {"NAME": "haloCenter", "TYPE": "point2D", "DEFAULT": [0.5, 0.47], "LABEL": "Halo centre"},
    {"NAME": "distantAmount", "TYPE": "float", "DEFAULT": 0.028, "MIN": 0.0, "MAX": 0.4, "LABEL": "Distant regions"},
    {"NAME": "drift", "TYPE": "float", "DEFAULT": 0.004, "MIN": 0.0, "MAX": 0.15, "LABEL": "Drift"},
    {"NAME": "vignette", "TYPE": "float", "DEFAULT": 0.38, "MIN": 0.0, "MAX": 1.0, "LABEL": "Edge falloff"},
    {"NAME": "nebulaIntensity", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0, "LABEL": "Nebula intensity"},
    {"NAME": "backgroundSaturation", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 2.0, "LABEL": "Background saturation"}
  ]
}*/

// Output is scene-linear HDR, drawn before geometry with depth write off. Everything is scaled to
// live in the 0.002 .. 0.08 range: the tree's lit leaves sit near 0.5 and its emissive specks
// above 1.0, so the background has to stay two orders of magnitude below them or it stops being a
// background. The brief's §21 list of failure modes -- giant purple galaxy, rainbow nebula, dense
// starfield -- is mostly a list of amplitude mistakes, so the amplitudes are the art direction.
//
// Stars are NOT drawn here. They are point geometry in the scene, at three depths, so that they
// are occluded by the island and so that their size and brightness vary per instance. A star
// drawn in a background shader is a star that shines through the rock.

// The classic sin-hash. Note the order: the dot product comes first and `fract` last. Taking
// `fract` of the scaled lattice coordinate first -- which is what the first draft of this file
// did -- leaves a hash whose input is only the fractional part of 127.1*n, i.e. almost no
// entropy, and the fbm built on it comes out nearly constant. The frame looked like a plain
// gradient and the fault was two lines up from where it showed.
fn hash21(p: vec2<f32>) -> f32 {
    return fract(sin(dot(p, vec2<f32>(127.1, 311.7))) * 43758.5453123);
}

fn valueNoise(p: vec2<f32>) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    let a = hash21(i);
    let b = hash21(i + vec2<f32>(1.0, 0.0));
    let c = hash21(i + vec2<f32>(0.0, 1.0));
    let d = hash21(i + vec2<f32>(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Five octaves, each rotated so the lobes do not line up into a visible grid.
fn fbm(p0: vec2<f32>) -> f32 {
    var p = p0;
    var sum = 0.0;
    var amp = 0.5;
    let rot = mat2x2<f32>(0.8, 0.6, -0.6, 0.8);
    for (var i = 0; i < 5; i = i + 1) {
        sum = sum + amp * valueNoise(p);
        p = rot * p * 2.03 + vec2<f32>(11.3, 7.7);
        amp = amp * 0.5;
    }
    return sum;
}

fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    let aspect = sys.passSize.x / max(sys.passSize.y, 1.0);
    let p = (uv - vec2<f32>(0.5, 0.5)) * vec2<f32>(aspect, 1.0);
    let t = sys.time * inputs.drift;

    // --- Layer 1: deep space. Not black: a blue-black that lifts very slightly toward the
    // bottom of the frame, so the void under the island reads as depth rather than as a hole.
    let vertical = 0.82 + 0.35 * uv.y;
    var colour = inputs.baseTint.rgb * inputs.baseLevel * vertical;

    // --- Layer 2: distant nebular haze. Two fbm fields at different scales and offsets, one
    // indigo-cyan and one violet, warped by a third so the formations are not blobs. Raised to a
    // power to keep most of the frame empty -- the haze should be somewhere, not everywhere.
    //
    // The thresholds below straddle the fbm's actual distribution rather than its nominal 0..1
    // range. Five octaves of value noise at amplitude 0.5, 0.25, ... sum to a field centred near
    // 0.48 with a standard deviation around 0.16, so a smoothstep(0.42, 0.92) -- which looks
    // reasonable written down -- selects the top 0.3% of it and renders a flat frame. That is
    // exactly what the first version of this shader did.
    let warp = vec2<f32>(fbm(p * 0.8 + vec2<f32>(t, -t)), fbm(p * 0.8 + vec2<f32>(4.1 - t, 2.7)));
    let q = p * inputs.hazeScale + (warp - 0.5) * 0.9;

    let cloud = fbm(q + vec2<f32>(0.0, t * 0.5));
    let cyanMask = pow(smoothstep(0.44, 0.70, cloud), 1.3);
    colour = colour + vec3<f32>(0.055, 0.215, 0.290) * cyanMask * inputs.hazeAmount;

    let indigo = fbm(q * 0.62 + vec2<f32>(9.4, 3.1 - t * 0.4));
    let indigoMask = pow(smoothstep(0.40, 0.66, indigo), 1.2);
    colour = colour + vec3<f32>(0.085, 0.095, 0.300) * indigoMask * inputs.hazeAmount * 1.35;

    // --- Layer 4 (placed here so the violet sits under the halo): a violet filament, stretched
    // along one axis so it reads as a structure rather than a cloud, and kept to one region.
    let fil = fbm(vec2<f32>(q.x * 0.45, q.y * 1.9) + vec2<f32>(21.0, 5.0 + t * 0.3));
    let filMask = pow(smoothstep(0.49, 0.72, fil), 1.7)
                * smoothstep(1.05, 0.25, length(p - vec2<f32>(-0.42, 0.22)));
    colour = colour + vec3<f32>(0.230, 0.070, 0.300) * filMask * inputs.violetAmount;

    // Two very distant diffuse luminous regions. Soft, wide, barely there: they exist to stop the
    // corners of the frame from being identical to each other.
    let d0 = length((p - vec2<f32>(0.74, 0.30)) * vec2<f32>(0.7, 1.25));
    let d1 = length((p - vec2<f32>(-0.66, -0.34)) * vec2<f32>(1.15, 0.8));
    let far = exp(-d0 * d0 * 5.0) * 0.65 + exp(-d1 * d1 * 4.0) * 0.45;
    let farNoise = 0.55 + 0.9 * fbm(q * 1.7 + vec2<f32>(31.0, 13.0));
    colour = colour + vec3<f32>(0.060, 0.170, 0.235) * far * farNoise * inputs.distantAmount;

    // --- Layer 3: the stage. A broad, very soft cyan-indigo halo centred on the island, so the
    // silhouette has something to be a silhouette against. Two lobes: a tight one that hugs the
    // rock and a wide one that fills the middle of the frame.
    let hc = (inputs.haloCenter - vec2<f32>(0.5, 0.5)) * vec2<f32>(aspect, 1.0);
    let hr = length(p - hc) / max(inputs.haloRadius, 0.01);
    let halo = exp(-hr * hr * 1.35) * 0.75 + exp(-hr * hr * 0.22) * 0.45;
    colour = colour + vec3<f32>(0.075, 0.190, 0.265) * halo * inputs.haloAmount;

    // Edge falloff: the frame gets quieter toward its corners so the eye stays on the tree.
    let edge = 1.0 - inputs.vignette * smoothstep(0.35, 1.05, length(p));
    colour = colour * edge;

    // ADR-360, the key-light brief's section 18. "Background brightness" and "background
    // saturation" are asked for as environment controls, and for this scene the background IS this
    // layer: the clear colour behind it is 0.0045, 0.0062, 0.0185 and scaling that alone would be
    // invisible. One multiply and one desaturate at the end is the honest place for them, because
    // the composition rule they serve is section 23's -- the vortex, the nebula and the stars must
    // stay below the tree in luminance, and this is the single number that decides it for the
    // largest of the three. Both default to 1.0, so a project written before them is unchanged.
    colour = colour * inputs.nebulaIntensity;
    let grey = dot(colour, vec3<f32>(0.2126, 0.7152, 0.0722));
    colour = mix(vec3<f32>(grey), colour, inputs.backgroundSaturation);

    return vec4<f32>(max(colour, vec3<f32>(0.0)), 1.0);
}
